#include <string.h>

#include <libio/console.h>
#include <libmsp/periph.h>
#include <libmsp/sleep.h>
#include <libedbserver/codepoint.h>
#include <libedbserver/pin_assign.h>
#include <libedbserver/edb.h>

#include <libedbserver/uart.h>

#include "profile.h"

// Shorthand
#define COMP_VBANK(...)  COMP(COMP_TYPE_VBANK, __VA_ARGS__)
#define COMP2_VBANK(...) COMP2(COMP_TYPE_VBANK, __VA_ARGS__)

// We write from here to flash, and we keep flash addresses aligned, so that we
// can write words, instead of bytes. This source buffer must also be aligned.
__attribute__((aligned(2)))
profile_t profile;

// Flag indicating when Vcap drops below threshold to end profiling
static volatile bool profiling_vcap_ok = false;

// A counter in the profile data has reached its max value
static volatile bool profiling_overflow = false;

static volatile bool profiling_timeout = false;

static bool arm_vcap_comparator()
{
    // Configure comparator to interrupt when Vcap drops below a threshold
    COMP_VBANK(CTL3) |= COMP2_VBANK(PD, COMP_CHAN_VBANK);
    COMP_VBANK(CTL0) = COMP_VBANK(IMEN) | COMP2_VBANK(IMSEL_, COMP_CHAN_VBANK);
    // VDD applied to resistor ladder, ladder tap applied to V+ terminal
    COMP_VBANK(CTL2) = COMP_VBANK(RS_1) | COMP2_VBANK(REF0_, PROFILING_VBANK_MIN_DOWN) |
                                          COMP2_VBANK(REF1_, PROFILING_VBANK_MIN_UP);
    // Turn comparator on in ultra-low power mode
    COMP_VBANK(CTL1) |= COMP_VBANK(PWRMD_2) | COMP_VBANK(ON);

    // Let the comparator output settle before checking or setting up interrupt
    msp_sleep(PERIOD_VBANK_COMP_SETTLE);

    if (COMP_VBANK(CTL1) & COMP_VBANK(OUT)) {
        // Vcap already below threshold
        return false;
    }

    // Clear int flag and enable int
    COMP_VBANK(INT) &= ~(COMP_VBANK(IFG) | COMP_VBANK(IIFG));
    COMP_VBANK(INT) |= COMP_VBANK(IE);

    return true;
}

static void toggle_watchpoints(bool enable)
{
    for (unsigned i = 0; i < NUM_EVENTS; ++i)
        toggle_watchpoint(i, /* enable */ enable, /* vcap snapshot */ true);

    // actually configure the pins
    if (enable)
        enable_watchpoints();
    else
        disable_watchpoints();
}

static msp_alarm_action_t on_profiling_timeout()
{
    profiling_timeout = true;
    return MSP_ALARM_ACTION_WAKEUP;
}

void start_profiling()
{
    LOG("start profiling\r\n");

    memset(&profile, 0, sizeof(profile_t));

    profiling_vcap_ok = arm_vcap_comparator();
    profiling_timeout = false;
    msp_alarm(PERIOD_PROFILING_TIMEOUT, on_profiling_timeout);

    LOG("EDB server init\r\n");
    edb_server_init();
    LOG("EDB server done\r\n");

    edb_set_watchpoint_callback(profile_event);
    toggle_watchpoints(true);

}

bool continue_profiling()
{
    return profiling_vcap_ok && !profiling_timeout;
}

void stop_profiling() {
    toggle_watchpoints(false);

    __delay_cycles(256); // avoid corruption in softuart output on wakeup
    LOG("profiling stopped: vcap %u ovrflw %u timeout %u\r\n",
        profiling_vcap_ok, profiling_overflow, profiling_timeout);

    LOG("profile: %u %u:%u | %u %u:%u | %u %u:%u | %u %u:%u\r\n",
        profile.events[0].count,
        profile.events[0].ehist_bin0,
        profile.events[0].ehist_bin1,
        profile.events[1].count,
        profile.events[1].ehist_bin0,
        profile.events[1].ehist_bin1,
        profile.events[2].count,
        profile.events[2].ehist_bin0,
        profile.events[2].ehist_bin1,
        profile.events[3].count,
        profile.events[3].ehist_bin0,
        profile.events[3].ehist_bin1);
}

// Returns true if overflowed
static inline bool inc_with_overflow(uint8_t *addr, uint8_t max)
{
    if (*addr == max)
        return true;
    *addr = *addr + 1;
    return false;
}

bool profile_event(unsigned index, uint16_t vcap)
{
    uint8_t cnt;

    cnt = profile.events[index].count;
    if (inc_with_overflow(&cnt, PROFILE_COUNT_MASK))
        goto overflow;
    profile.events[index].count = cnt;

    if (vcap > PROFILING_EHIST_BIN_EDGE_0) {
        cnt = profile.events[index].ehist_bin1;
        if (inc_with_overflow(&cnt, PROFILE_EHIST_BIN_MASK))
            goto overflow;
        profile.events[index].ehist_bin1 = cnt;
    } else {
        cnt = profile.events[index].ehist_bin0;
        if (inc_with_overflow(&cnt, PROFILE_EHIST_BIN_MASK))
            goto overflow;
        profile.events[index].ehist_bin0 = cnt;
    }

    return false; // don't wakeup the MCU

overflow:
    // Disable watchpoints to avoid counting any other watchpoints, as soon as possible
    toggle_watchpoints(false);

    profiling_overflow = true;
    return true; // wake up the MCU
}

__attribute__ ((interrupt(COMP_VECTOR(COMP_TYPE_VBANK))))
void COMP_VBANK_ISR (void)
{
    switch (__even_in_range(COMP_VBANK(IV), 0x4)) {
        case COMP_VBANK(IV_IIFG):
            break;
        case COMP_VBANK(IV_IFG):
            COMP_VBANK(INT) &= ~COMP_VBANK(IE);
            COMP_VBANK(CTL1) &= ~COMP_VBANK(ON);
            profiling_vcap_ok = false; // tell main to stop profiling
            break;
    }
    __bic_SR_register_on_exit(LPM4_bits); // Exit active CPU
}
