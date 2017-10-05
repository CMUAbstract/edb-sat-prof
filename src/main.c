#include <msp430.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <libmsp/periph.h>
#include <libmsp/clock.h>
#include <libmsp/watchdog.h>
#include <libcapybara/capybara.h>
#include <libio/console.h>
#include <libmspuartlink/uartlink.h>

#include "payload.h"
#include "flash.h"
#include "random.h"

#define CONFIG_WDT_BITS WATCHDOG_BITS(WATCHDOG_CLOCK, WATCHDOG_INTERVAL)

typedef enum {
    TASK_BEACON = 0,
#ifdef CONFIG_COLLECT_ENERGY_PROFILE
    TASK_ENERGY_PROFILE,
#endif // CONFIG_COLLECT_ENERGY_PROFILE
    NUM_TASKS
} task_t;

// Data generated by application that came over the radio port for transmission
#define MAX_APP_DATA_LEN 16
uint8_t app_data[MAX_APP_DATA_LEN];
unsigned app_data_len = 0;

static void handle_flash_op_outcome(unsigned rc) {
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
            LOG("saved pkt and desc to flash, shutting down\r\n");
            break;
    }
}

int main(void)
{
#ifdef CONFIG_WATCHDOG
	msp_watchdog_enable(CONFIG_WDT_BITS);
#else // !CONFIG_WATCHDOG
	msp_watchdog_disable();
#endif // !CONFIG_WATCHDOG

    P3OUT &= ~(BIT0 | BIT2 | BIT3);
    P3DIR |= BIT0 | BIT2 | BIT3;

    // Pin config
    GPIO(PORT_APP_SW, OUT) &= ~BIT(PIN_APP_SW);
    GPIO(PORT_APP_SW, DIR) |= BIT(PIN_APP_SW);

    GPIO(PORT_ISOL_EN, OUT) &= ~BIT(PIN_ISOL_EN);
    GPIO(PORT_ISOL_EN, DIR) |= BIT(PIN_ISOL_EN);

    capybara_config_pins();

    __enable_interrupt();

    capybara_wait_for_supply();

    msp_clock_setup(); // set up unified clock system
    INIT_CONSOLE();

    LOG("EDBsat v1.2 - EDB MCU\r\n");

#ifdef CONFIG_SEED_RNG_FROM_VCAP
    seed_random_from_adc();
#endif // CONFIG_SEED_RNG_FROM_VCAP

    // Randomly choose which action to perform: P=1/4
    task_t task = ((rand() & 0x3) == 0) ? TASK_BEACON : TASK_ENERGY_PROFILE;
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

            LOG("collect profile: isolate and turn on app supply\r\n");

            flash_loc_t loc;
            unsigned free_space = flash_find_space(PROFILE_SIZE + PAYLOAD_DESC_SIZE, &loc);
            LOG("free space in flash: %u (need %u)\r\n", free_space,
                PROFILE_SIZE + PAYLOAD_DESC_SIZE + MAX_APP_DATA_LEN + PAYLOAD_DESC_SIZE);
            if (free_space < PROFILE_SIZE + PAYLOAD_DESC_SIZE + MAX_APP_DATA_LEN + PAYLOAD_DESC_SIZE) {
                LOG("insufficient flash space for profile and app data\r\n");
                flash_erase();
                capybara_shutdown();
            }

            uartlink_open_rx();

            GPIO(PORT_ISOL_EN, OUT) |= BIT(PIN_ISOL_EN);
            GPIO(PORT_APP_SW, OUT) |= BIT(PIN_APP_SW);

            app_data_len = 0;

            start_profiling();

            while (continue_profiling()) {

                // sleep, will wake up on watchpoint, or on incoming byte on radio port
                __bis_SR_register(LPM0_bits);

                if (!app_data_len) { // if haven't received a data pkt yet, try receiving
                    app_data_len = uartlink_receive(&app_data[0]);
                    if (app_data_len) { // if we got something this time
                        LOG("received app data pkt (len %u): ", app_data_len);
                        for (int i = 0; i < app_data_len && i < MAX_APP_DATA_LEN; ++i) {
                            LOG("%02x ", app_data[i]);
                        }
                        LOG("\r\n");

                        uartlink_close(); // we only allow one app data pkt per profiling run

                        // Received app data will be saved to flash after profiling is done.
                        // Don't want to do it now, to avoid halting CPU for the flash writes,
                        // while the CPU is needed to process incoming watchpoints.
                    }
                }
            }

            stop_profiling();

            LOG("turn off app supply and reconnect harvester\r\n");
            GPIO(PORT_APP_SW, OUT) &= ~BIT(PIN_APP_SW);
            GPIO(PORT_ISOL_EN, OUT) &= ~BIT(PIN_ISOL_EN);

            uartlink_close();

            LOG("saving profile to flash\r\n");
            flash_status_t rc = save_payload(&loc, PKT_TYPE_ENERGY_PROFILE, (uint8_t *)&profile, PROFILE_SIZE);
            handle_flash_op_outcome(rc);

            if (app_data_len) { // we know there is space, because we checked above; loc was updated
                LOG("saving app data to flash\r\n");
                flash_status_t rc = save_payload(&loc, PKT_TYPE_APP_OUTPUT, (uint8_t *)&app_data[0], app_data_len);
                handle_flash_op_outcome(rc);
            } else {
                LOG("no app data was received\r\n");
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
