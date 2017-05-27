#include <msp430.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <libmsp/periph.h>
#include <libmsp/clock.h>
#include <libmsp/watchdog.h>
#include <libcapybara/capybara.h>

#ifdef CONFIG_DEV_CONSOLE
#include <libio/log.h>
#endif // CONFIG_DEV_CONSOLE

#include <libedbserver/edb.h>
#include <libedbserver/pin_assign.h>
#include <libedbserver/host_comm.h>
#include <libedbserver/target_comm_impl.h>
#include <libedbserver/adc.h>
#include <libedbserver/sched.h>
#include <libedbserver/codepoint.h>
#include <libedbserver/tether.h>

#include "payload.h"
#include "flash.h"

#define CONFIG_MAIN_LOOP_SLEEP_STATE LPM0_bits

/** @brief Watchdog configuration */
#define CONFIG_WDT_BITS (WDTSSEL__ACLK | WDTIS__8192K) // 4 minutes

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

#if 1 // TODO: temporary -- turn on APP MCU
    GPIO(PORT_APP_SW, OUT) |= BIT(PIN_APP_SW);
    GPIO(PORT_APP_SW, DIR) |= BIT(PIN_APP_SW);
#endif

#if 0 // NOP main: sleep forever
    __disable_interrupt();
    while (1) {
        __bis_SR_register(LPM4_bits);
    }
#endif

    P3OUT &= ~(BIT0 | BIT2 | BIT3);
    P3DIR |= BIT0 | BIT2 | BIT3;

    capybara_config_pins();

#if 0
#if 0
    // wait for voltage supervisor to indicate target voltage reached
    // i.e. supercap is charged to 3.3v
    P2IES &= ~(BIT1 | BIT2);
    P2IFG &= ~(BIT1 | BIT2);
    P2IE |= BIT1 | BIT2;
    while ((P2IN & (BIT1 | BIT2)) != (BIT1 | BIT2) ) {
        __enable_interrupt();
        __bis_SR_register(LPM4_bits);
    }
    P2IE &= ~(BIT1 | BIT2);
    P2IFG &= ~(BIT1 | BIT2);
#else // wait for VBOOST_OK only, ignore VCAP state
    __enable_interrupt();
    P2IES &= ~BIT2;
    P2IFG &= ~BIT2;
    P2IE |= BIT2;
    while (!(P2IN & BIT2)) {
        __bis_SR_register(LPM4_bits);
    }
    P2IE &= ~BIT2;
    P2IFG &= ~BIT2;
#endif
#endif

    P3OUT &= ~BIT0;

    msp_clock_setup(); // set up unified clock system

#ifdef CONFIG_DEV_CONSOLE
    INIT_CONSOLE();
#endif // CONFIG_DEV_CONSOLE

    __enable_interrupt();

    LOG("EDBsat v1.1 - EDB MCU\r\n");


#if 1
    // TODO: merge into edb_server_init?
    LOG("EDB pin setup\r\n");
    edb_pin_setup();
    LOG("EDB server init\r\n");
    edb_server_init();
    LOG("EDB server done\r\n");
#endif

#if 0 // testing reading VDD_AP through a divider
    // Set pin to ADC mode
    P2MAP4 = 31;
    P2SEL |= BIT4;

    ADC12CTL0 |= ADC12ON;

    while (1) {
        //Reset the ENC bit to set the starting memory address and conversion mode sequence
        ADC12CTL0 &= ~(ADC12ENC);

        REFCTL0 |= REFMSTR | REFTCOFF; // use reference control bits in REF, disable temp sensor
        REFCTL0 |= REFON; // turn on reference (TODO: is this needed? or will ADC turn it on on demand?)

        ADC12CTL1 |= ADC12SHP;
        ADC12CTL0 |= ADC12SHT0_15 | ADC12SHT1_15;
        ADC12MCTL0 |= 0x4 | ADC12SREF_1 | ADC12EOS; // channel A4

        //Reset the bits about to be set
        ADC12CTL1 &= ~(ADC12CONSEQ_3);

        ADC12CTL0 |= ADC12ENC + ADC12SC;

        for (int i = 0; i < 0xff; ++i) {
            __delay_cycles(0xffff);
        }

        uint16_t vdd_ap = ADC12MEM0;
        PRINTF("V=%u B=%u I=%x\r\n", vdd_ap, (ADC12CTL1 & ADC12BUSY), ADC12IFG);
    }
#endif


#if 0
    LOG("radio init\r\n");
    SpriteRadio_SpriteRadio();

    while (1) {

#if 0
        if (P2IN & BIT2) {
#endif

            LOG("tx start\r\n");
            P3OUT |= BIT2;

            static char pkt[1] = { 0xED };

            SpriteRadio_txInit();
            SpriteRadio_transmit(&pkt[0], 1);
            SpriteRadio_sleep();

            P3OUT &= ~BIT2;
            LOG("tx end\r\n");

#if 1 // disable booster, go to sleep to charge
            LOG("shutdown\r\n");
            GPIO(PORT_BOOST_SW, OUT) |= BIT(PIN_BOOST_SW);
            __disable_interrupt();
            while (1) { // wait for power to be cut
                __bis_SR_register(LPM4_bits);
            }
#else
            while(1); // discharge
#endif
#if 0
        }
#else
        // Could be a loop, but let's just go ahead to avoid the scenario of
        // getting stuck in this loop, if supervisor comparator goes low after
        // the interrupt, but before we reach this condition. By going ahead
        // without re-checking the condition, we're simply being optimistic.
        if (!(P2IN & BIT2)) {
            P3OUT |= BIT1;
            P2IES &= ~BIT2;
            P2IFG &= ~BIT2;
            P2IE |= BIT2;
            __bis_SR_register(LPM4_bits);
            P3OUT &= ~BIT1;
        }
#endif
    }
#endif

#if 0
    while (1) {
        LOG("getting app output\r\n");
        get_app_output();
        LOG("got app output\r\n");

        __delay_cycles(0xffff);
    }
#endif

#ifdef CONFIG_SEED_RNG_FROM_VCAP
    // Seed the random number generator
    uint16_t seed = ADC_read(ADC_CHAN_INDEX_VCAP);
    srand(seed);
    LOG("seed: %u\r\n", seed);
#endif // CONFIG_SEED_RNG_FROM_VCAP

    payload_init();

#if 1

    LOG("look for unsent pkt in flash\r\n");
    uint8_t *saved_pkt_desc_addr = flash_find_last_byte();
    LOG("pkt descriptor @0x%04x\r\n", (uint16_t)saved_pkt_desc_addr);
    if (saved_pkt_desc_addr) {
        pkt_desc_union_t saved_pkt_desc_union;
        saved_pkt_desc_union.raw = *saved_pkt_desc_addr; // read from flash
        pkt_desc_t saved_pkt_desc = saved_pkt_desc_union.typed;

        LOG("pkt descriptor: 0x%02x: type %u flags 0x%x size %u\r\n",
            *(uint8_t *)&saved_pkt_desc, saved_pkt_desc.type, saved_pkt_desc.flags, saved_pkt_desc.size);

        if (saved_pkt_desc.flags & PKT_FLAG_NOT_SENT) {
            // TODO: transmit buffer at (pkt_desc - pkt_desc->size)
            uint8_t *saved_pkt_addr = ((uint8_t*)saved_pkt_desc_addr) - saved_pkt_desc.size;

            LOG("transmit pkt (addr 0x%04x len %u): ", (uint16_t)saved_pkt_addr, saved_pkt_desc.size);
            for(int i = 0; i < saved_pkt_desc.size; ++i) {
                LOG("%02x ", *(saved_pkt_addr + i));
            }
            LOG("\r\n");

            pkt_desc_t sent_desc = { saved_pkt_desc.type, saved_pkt_desc.flags & ~PKT_FLAG_NOT_SENT, saved_pkt_desc.size };
            LOG("markig pkt at 0x%04x as sent: 0x%02x\r\n", (uint16_t)saved_pkt_desc_addr, *(uint8_t *)&sent_desc);
            flash_write_byte((uint8_t *)saved_pkt_desc_addr, *(uint8_t *)&sent_desc);
            LOG("pkt desc: @0x%04x [0x%02x]\r\n", (uint16_t)saved_pkt_desc_addr, *(uint8_t *)saved_pkt_desc_addr);
        }
    }

    LOG("collect profile\r\n");

    LOG("check space in flash\r\n");
    flash_loc_t loc;
    if (!flash_find_space(PROFILE_SIZE, &loc)) {
        LOG("out of space in flash\r\n");
        flash_erase();
        capybara_shutdown();
    }

    LOG("start profiling\r\n");
    collect_profile();

    pkt_desc_t pkt_desc = { PKT_TYPE_ENERGY_PROFILE, PKT_FLAG_NOT_SENT, PROFILE_SIZE };
    LOG("saving profile to flash: ");
    for (int i = 0; i < PROFILE_SIZE; ++i) {
        LOG("%02x ", *(uint8_t *)&profile);
    }
    LOG("(%02x)\r\n", *(uint8_t *)&pkt_desc);

    uint8_t *profile_saved = flash_alloc(&loc, PROFILE_SIZE + PAYLOAD_DESC_SIZE);
    if (!flash_write(profile_saved, (uint8_t *)&profile, PROFILE_SIZE)) {
        LOG("failed to write to flash\r\n");
        capybara_shutdown();
    }
    if (!flash_write(profile_saved + PROFILE_SIZE, (uint8_t *)&pkt_desc, PAYLOAD_DESC_SIZE)) {
        LOG("failed to write to flash\r\n");
        capybara_shutdown();
    }
    LOG("saved profile and desc to flash\r\n");

    capybara_shutdown();
    // should not get here
#endif

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
#if 0
    task_t task = rand() % NUM_TASKS;
#else
    task_t task = TASK_BEACON;
#endif
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

#ifdef CONFIG_MAIN_LOOP_LED
    unsigned main_loop_count = 0;
#endif // CONFIG_MAIN_LOOP_LED

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
#endif // CONFIG_COLLECT_APP_OUTPUT

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
#endif // CONFIG_COLLECT_ENERGY_PROFILE

        edb_service();

#ifdef CONFIG_MAIN_LOOP_LED
        // This LED toggle is unnecessary, and probably a huge waste of processing time.
        // The LED blinking will slow down when the monitor is performing more tasks.
        if (state == STATE_IDLE) {
            if (main_loop_count++ == ~0) {
                GPIO(PORT_LED_MAIN_LOOP, OUT) ^= BIT(PIN_LED_MAIN_LOOP);
            }
        }
#endif // CONFIG_MAIN_LOOP_LED

#ifdef CONFIG_SLEEP_IN_MAIN_LOOP
        LOG("sleep\r\n");
        // sleep, wait for event flag to be set, then handle it in loop
        __bis_SR_register(CONFIG_MAIN_LOOP_SLEEP_STATE + GIE);
        LOG("woke up\r\n");
#endif // CONFIG_SLEEP_IN_MAIN_LOOP
    }
}

void __attribute__ ((interrupt(PORT2_VECTOR))) P2_ISR (void)
{
#if 0
    P2IFG &= ~(BIT2);
    P2IE &= ~(BIT2);
#else
    unsigned flags = P2IFG;
    if (flags & BIT1)
        P2IFG &= ~BIT1;
    if (flags & BIT2)
        P2IFG &= ~BIT2;
#endif

    // We were sleeping waiting for interrupt, so exit sleep
    __bic_SR_register_on_exit(LPM4_bits);
}
