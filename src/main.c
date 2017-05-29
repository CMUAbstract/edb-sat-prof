#include <msp430.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <libmsp/periph.h>
#include <libmsp/clock.h>
#include <libmsp/watchdog.h>
#include <libcapybara/capybara.h>
#include <libio/log.h>

#ifdef CONFIG_COLLECT_APP_OUTPUT
#include <libedbserver/edb.h>
#include <libedbserver/pin_assign.h>
#include <libedbserver/host_comm.h>
#include <libedbserver/target_comm_impl.h>
#include <libedbserver/adc.h>
#include <libedbserver/sched.h>
#include <libedbserver/codepoint.h>
#include <libedbserver/tether.h>
#endif // CONFIG_COLLECT_APP_OUTPUT

#include "payload.h"
#include "flash.h"

#define CONFIG_MAIN_LOOP_SLEEP_STATE LPM0_bits

#define CONFIG_WDT_BITS WATCHDOG_BITS(WATCHDOG_CLOCK, WATCHDOG_INTERVAL)

typedef enum {
    TASK_BEACON = 0,
#ifdef CONFIG_COLLECT_ENERGY_PROFILE
    TASK_ENERGY_PROFILE,
#endif // CONFIG_COLLECT_ENERGY_PROFILE
#ifdef CONFIG_COLLECT_APP_OUTPUT
    TASK_APP_OUTPUT,
#endif // CONFIG_COLLECT_APP_OUTPUT
    NUM_TASKS
} task_t;

#ifdef CONFIG_COLLECT_APP_OUTPUT // TODO: a timeout should be applied to all target comms
/* Whether timed out while communicating with target over UART */
static bool target_comm_timeout;

static sched_cmd_t on_target_comm_timeout()
{
    target_comm_timeout = true;
    return SCHED_CMD_WAKEUP;
}

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

    P3OUT &= ~(BIT0 | BIT2 | BIT3);
    P3DIR |= BIT0 | BIT2 | BIT3;

    capybara_config_pins();
    capybara_wait_for_supply();

    msp_clock_setup(); // set up unified clock system
    INIT_CONSOLE();
    __enable_interrupt();
    LOG("EDBsat v1.2 - EDB MCU\r\n");

#ifdef CONFIG_SEED_RNG_FROM_VCAP
    // Seed the random number generator
    uint16_t seed = ADC_read(ADC_CHAN_INDEX_VCAP);
    srand(seed);
    LOG("seed: %u\r\n", seed);
#endif // CONFIG_SEED_RNG_FROM_VCAP

    // Randomly choose which action to perform
    task_t task = rand() % NUM_TASKS;
    LOG("task: %u\r\n", task);

    switch (task) {
        case TASK_BEACON:
            payload_send_beacon();
            break;
#ifdef CONFIG_COLLECT_ENERGY_PROFILE
        case TASK_ENERGY_PROFILE:

            if (transmit_saved_payload()) {
                LOG("saved pkt transmitted: shutting down\r\n");
                capybara_shutdown(); // we're out of energy after any transmission
            } else {
                LOG("no saved payload found in flash\r\n");
                // move on, collect some data
            }

            LOG("collect profile: turn on app supply\r\n");

            GPIO(PORT_APP_SW, OUT) |= BIT(PIN_APP_SW);
            GPIO(PORT_APP_SW, DIR) |= BIT(PIN_APP_SW);

            LOG("check space in flash\r\n");
            flash_loc_t loc;
            if (!flash_find_space(PROFILE_SIZE, &loc)) {
                LOG("out of space in flash\r\n");
                flash_erase();
                capybara_shutdown();
            }

            LOG("start profiling\r\n");
            collect_profile();

            LOG("profiling stopped: turn off app supply\r\n");
            GPIO(PORT_APP_SW, OUT) &= ~BIT(PIN_APP_SW);

            flash_status_t rc = save_payload(&loc, PKT_TYPE_ENERGY_PROFILE, (uint8_t *)&profile, PROFILE_SIZE);
            switch (rc) {
                case FLASH_STATUS_ALLOC_FAILED:
                    LOG("pkt not saved: flash alloc failed: erasing and rebooting\r\n");
                    flash_erase(); // can't trust the state of the free bitmask in flash
                    capybara_shutdown();
                    break;
                case FLASH_STATUS_WRITE_FAILED:
                    LOG("pkt not saved: flash write failed: rebooting\r\n");
                    capybara_shutdown();  // free bitmask not affected, so no need to panic-erase
                    break;
                default:
                    LOG("saved pkt and desc to flash, shutting down\r\b");
                    break;
            }
            break;
#endif // CONFIG_COLLECT_ENERGY_PROFILE

        default:
            LOG("unknown task: %u\r\n", task); // should not happen
    }

    // One-shot design
    LOG("task completed: shutting down\r\n");
    capybara_shutdown();
}

#define _THIS_PORT 2
__attribute__ ((interrupt(GPIO_VECTOR(_THIS_PORT))))
void  GPIO_ISR(_THIS_PORT) (void)
{
    switch (__even_in_range(INTVEC(_THIS_PORT), INTVEC_RANGE(_THIS_PORT))) {
#if LIBCAPYBARA_PORT_VBOOST_OK == _THIS_PORT
        case INTFLAG(LIBCAPYBARA_PORT_VBOOST_OK, LIBCAPYBARA_PIN_VBOOST_OK):
            capybara_vboost_ok_isr();
            break;
#else
#error Handler in wrong ISR: capybara_vboost_ok_isr
#endif // LIBCAPYBARA_PORT_VBOOST_OK
    }
}
#undef _THIS_PORT
