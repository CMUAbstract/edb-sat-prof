#include <string.h>

#include <libio/log.h>
#include <libmsp/periph.h>
#include <libmsp/sleep.h>
#include <libedbserver/codepoint.h>
#include <libedbserver/pin_assign.h>

#include "profile.h"

// Shorthand
#define COMP_VBANK(...)  COMP(COMP_TYPE_VBANK, __VA_ARGS__)
#define COMP2_VBANK(...) COMP2(COMP_TYPE_VBANK, __VA_ARGS__)

profile_t profile;

volatile bool profiling_vcap_ok = false;
volatile bool profiling_overflow = false;

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
    msp_sleep(VBANK_COMP_SETTLE);

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
    for (unsigned i = 0; i < NUM_CODEPOINT_PINS; ++i)
        toggle_watchpoint(i, /* enable */ enable, /* vcap snapshot */ true);
    enable_watchpoints(); // actually enable the pins
}

void collect_profile()
{
    memset(&profile, 0, sizeof(profile_t));

    profiling_vcap_ok = arm_vcap_comparator();

    edb_set_watchpoint_callback(profile_event);
    toggle_watchpoints(true);

    while (profiling_vcap_ok && !profiling_overflow) {
        // will wake up on watchpoint
        __bis_SR_register(LPM0_bits);
    }

    toggle_watchpoints(false);
    __enable_interrupt(); // re-enable, in case of overflow

    __delay_cycles(256); // avoid corruption in softuart output on wakeup
    LOG("profile: %u %u %u %u\r\n",
        profile.events[0].count,
        profile.events[1].count,
        profile.events[2].count,
        profile.events[3].count);
}

bool profile_event(unsigned index, uint16_t vcap)
{
    profile.events[index].count++;
    if (profile.events[index].count == 0) { // overflow
        __disable_interrupt(); // to avoid counting any other watchpoints (re-enable after loop)
        profile.events[index].count -= 1; // revert increment
        profiling_overflow = true;
        return true; // wake up the MCU
    }
#if 0
#ifdef CONFIG_PROFILE_SUB_BYTE_BUCKET_SIZES
    unsigned quantum_idx = vcap / NUM_ENERGY_QUANTA; // this is wrong (see byte-based version below)
    unsigned byte_idx = quantum_idx / NUM_ENERGY_QUANTA_PER_BYTE;
    unsigned slot_idx = quantum_idx % NUM_ENERGY_QUANTA_PER_BYTE;

    unsigned e_byte = profile.events[index].energy[byte_idx];
    unsigned shift = slot_idx * NUM_ENERGY_BITS_PER_QUANTUM;
    unsigned slot_mask = ENERGY_QUANTUM_MASK << shift;
    unsigned e_slot = (e_byte & slot_mask) >> shift;
    e_slot++;
    e_byte = (e_byte & ~slot_mask) | (e_slot << shift);

    profile.events[index].energy[byte_idx] = e_byte;
#else
    // Split the range [MIN_VOLTAGE, VREF (2.5v)] into NUM_ENERGY_BYTES buckets
    // (one-byte per bucket), and count the values in each bucket.

    unsigned byte_idx = (((float)vcap - CONFIG_ENERGY_PROFILE_MIN_VOLTAGE) /
                        ((1 << 12) - CONFIG_ENERGY_PROFILE_MIN_VOLTAGE)) * NUM_ENERGY_BYTES;
    if (byte_idx == NUM_ENERGY_BYTES)
        --byte_idx;

    uint8_t e = profile.events[index].energy[byte_idx];

    e++;

    if (e > 0) {
        profile.events[index].energy[byte_idx] = e;
    } else { // bucket overflowed, reset all buckets, to keep histogram consistent
        for (int i = 0; i < NUM_ENERGY_BYTES; ++i)
            profile.events[index].energy[i] = 0;
        profile.events[index].count = 0;
    }

#endif // CONFIG_PROFILE_SUB_BYTE_BUCKET_SIZES
#endif

    return false; // don't wakeup the MCU
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
