#include <msp430.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <libmsp/periph.h>
#include <libmsp/clock.h>
#include <libmsp/watchdog.h>

#include <libio/log.h>

#include <libedbserver/edb.h>
#include <libedbserver/pin_assign.h>
#include <libedbserver/host_comm.h>
#include <libedbserver/target_comm_impl.h>
#include <libedbserver/adc.h>
#include <libedbserver/sched.h>
#include <libedbserver/codepoint.h>
#include <libedbserver/tether.h>

#include "payload.h"

#define CONFIG_MAIN_LOOP_SLEEP_STATE LPM0_bits

typedef enum {
    TASK_BEACON = 0,
#ifdef CONFIG_COLLECT_ENERGY_PROFILE
    TASK_ENERGY_PROFILE,
#endif // CONFIG_COLLECT_ENERGY_PROFILE
#ifdef CONFIG_COLLECT_APP_OUTPUT
    TASK_APP_OUTPUT,
#endif // CONFIG_COLLECT_APP_OUTPUT
    NUM_TASKS,
} task_t;


typedef enum {
    FLAG_ENERGY_PROFILE_READY   = 0x0001, //!< send payload packet to ground
    FLAG_COLLECT_WATCHPOINTS    = 0x0002, //!< start collecting energy profile
    FLAG_APP_OUTPUT             = 0x0004, //!< interrupt target and get app data packet
    FLAG_SEND_BEACON            = 0x0008, //!< transmit a beacon to ground
} task_flag_t;

static uint16_t task_flags = 0;

#ifdef CONFIG_COLLECT_APP_OUTPUT // TODO: a timeout should be applied to all target comms
/* Whether timed out while communicating with target over UART */
static bool target_comm_timeout;
#endif // CONFIG_COLLECT_APP_OUTPUT

static sched_cmd_t on_watchpoint_collection_complete()
{
    disable_watchpoints();
    task_flags |= FLAG_ENERGY_PROFILE_READY;
    return SCHED_CMD_WAKEUP;
}

#ifdef CONFIG_COLLECT_APP_OUTPUT
static sched_cmd_t on_target_comm_timeout()
{
    target_comm_timeout = true;
    return SCHED_CMD_WAKEUP;
}
#endif // CONFIG_COLLECT_APP_OUTPUT

#ifdef CONFIG_COLLECT_APP_OUTPUT
static void get_app_output()
{
    LOG("interrupting target\r\n");
    enter_debug_mode(INTERRUPT_TYPE_DEBUGGER_REQ, DEBUG_MODE_WITH_UART);

    // TODO: sleep
    while (state != STATE_DEBUG && state != STATE_IDLE);

    if (state == STATE_IDLE) {
        LOG("timed out while interrupting target\r\n");
        return;
    }

    LOG("requesting data from target\r\n");
    target_comm_send_get_app_output();

    LOG("waiting for reply\r\n");
    target_comm_timeout = false;
    schedule_action(on_target_comm_timeout, CONFIG_TARGET_COMM_TIMEOUT);

    // TODO: sleep
    while(!target_comm_timeout &&
          ((UART_buildRxPkt(UART_INTERFACE_WISP, &wispRxPkt) != 0) ||
           (wispRxPkt.descriptor != WISP_RSP_APP_OUTPUT)));

    if (target_comm_timeout) {
        LOG("timed out while waiting for target reply\r\n");
        return;
    } else {
        abort_action(on_target_comm_timeout);
    }

    LOG("received reply: msg %02x len %u data %02x...\r\n",
        wispRxPkt.descriptor, wispRxPkt.length, wispRxPkt.data[0]);

    payload_record_app_output(wispRxPkt.data, wispRxPkt.length);
    wispRxPkt.processed = 1;

    LOG("exiting debug mode\r\n");
    exit_debug_mode();
}
#endif // CONFIG_COLLECT_APP_OUTPUT


int main(void)
{
#ifdef CONFIG_WATCHDOG
	msp_watchdog_enable(CONFIG_WDT_BITS);
#else // !CONFIG_WATCHDOG
	msp_watchdog_disable();
#endif // !CONFIG_WATCHDOG

    // TODO: merge into edb_server_init?
    edb_pin_setup();

#ifdef CONFIG_BOOT_LED
    GPIO(PORT_LED_BOOT, OUT) |= BIT(PIN_LED_BOOT);
#endif // CONFIG_BOOT_LED

    msp_clock_setup(); // set up unified clock system

#ifdef CONFIG_DEV_CONSOLE
    INIT_CONSOLE();
#endif // CONFIG_DEV_CONSOLE

    __enable_interrupt();                   // enable all interrupts

    LOG("\r\nEDB\r\n");

#ifdef CONFIG_SEED_RNG_FROM_VCAP
    // Seed the random number generator
    uint16_t seed = ADC_read(ADC_CHAN_INDEX_VCAP);
    srand(seed);
    LOG("seed: %u\r\n", seed);
#endif // CONFIG_SEED_RNG_FROM_VCAP

    payload_init();

#ifdef CONFIG_BOOT_LED
    GPIO(PORT_LED_BOOT, OUT) &= ~BIT(PIN_LED_BOOT);
#endif // CONFIG_BOOT_LED

    LOG("init done\r\n");

#if CONFIG_TARGET_POWER_SWITCH
    // Setup target's "power switch" pin
    GPIO(PORT_TARGET_PWR_SWITCH, OUT) &= ~BIT(PIN_TARGET_PWR_SWITCH);
    GPIO(PORT_TARGET_PWR_SWITCH, DIR) |= BIT(PIN_TARGET_PWR_SWITCH);

    // turn on target's "power switch"
    GPIO(PORT_TARGET_PWR_SWITCH, OUT) |= BIT(PIN_TARGET_PWR_SWITCH);
#endif // CONFIG_TARGET_POWER_SWITCH

    // Randomly choose which action to perform (EDB does not keep state across reboots)
    // NOTE: this is outside the loop, because within the loop we manually chain the tasks.
    task_t task = rand() % NUM_TASKS;
    LOG("task: %u\r\n", task);

    switch (task) {
#ifdef CONFIG_COLLECT_ENERGY_PROFILE
        case TASK_ENERGY_PROFILE:
            task_flags |= FLAG_COLLECT_WATCHPOINTS;
            break;
#endif // CONFIG_COLLECT_ENERGY_PROFILE
#ifdef CONFIG_COLLECT_APP_OUTPUT
        case TASK_APP_OUTPUT:
            task_flags |= FLAG_APP_OUTPUT;
            break;
#endif // CONFIG_COLLECT_APP_OUTPUT
        case TASK_BEACON:
        default:
            task_flags |= FLAG_SEND_BEACON;
            break;
    }

    LOG("main loop\r\n");

    unsigned main_loop_count = 0;

    while(1) {

#ifdef CONFIG_WATCHDOG
        msp_watchdog_kick();
#endif // !CONFIG_WATCHDOG

        if (task_flags & FLAG_SEND_BEACON) {
            LOG("sb\r\n");
            payload_send_beacon();
            task_flags &= ~FLAG_SEND_BEACON;

            // next action
            task_flags |= FLAG_COLLECT_WATCHPOINTS;
            //task_flags |= FLAG_APP_OUTPUT;
            //task_flags |= FLAG_SEND_BEACON;
            continue;
        }

        if (task_flags & FLAG_COLLECT_WATCHPOINTS) {
            LOG("cw\r\n");
            schedule_action(on_watchpoint_collection_complete, CONFIG_WATCHPOINT_COLLECTION_TIME);
            enable_watchpoints();
            task_flags &= ~FLAG_COLLECT_WATCHPOINTS;
        }

#ifdef CONFIG_COLLECT_APP_OUTPUT
        if (task_flags & FLAG_APP_OUTPUT) {
            LOG("ao\r\n");
            get_app_output();
            payload_send_app_output();
            task_flags &= ~FLAG_APP_OUTPUT;

            // next action
            //task_flags |= FLAG_COLLECT_WATCHPOINTS;
            task_flags |= FLAG_SEND_BEACON;
            continue;
        }
#endif

#ifdef CONFIG_COLLECT_ENERGY_PROFILE
        if (task_flags & FLAG_ENERGY_PROFILE_READY) {
            LOG("sw\r\n");
            payload_send_profile();
            task_flags &= ~FLAG_ENERGY_PROFILE_READY;

            // next action
            task_flags |= FLAG_APP_OUTPUT;
            //task_flags |= FLAG_SEND_BEACON;
            continue;
        }
#endif

        edb_service();

#ifdef CONFIG_MAIN_LOOP_LED
        // This LED toggle is unnecessary, and probably a huge waste of processing time.
        // The LED blinking will slow down when the monitor is performing more tasks.
        if (state == STATE_IDLE) {
            if (main_loop_count++ == ~0) {
                GPIO(PORT_LED_MAIN_LOOP, OUT) ^= BIT(PIN_LED_MAIN_LOOP);
            }
        }
#endif

#ifdef CONFIG_SLEEP_IN_MAIN_LOOP
        LOG("sleep\r\n");
        // sleep, wait for event flag to be set, then handle it in loop
        __bis_SR_register(CONFIG_MAIN_LOOP_SLEEP_STATE + GIE);
        LOG("woke up\r\n");
#endif // CONFIG_SLEEP_IN_MAIN_LOOP
    }
}
