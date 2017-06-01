#ifndef PROFILE_H
#define PROFILE_H

#include <stdint.h>
#include <stdbool.h>

#define NUM_EVENTS                      5
#define NUM_ENERGY_QUANTA               4
#define NUM_ENERGY_BITS_PER_QUANTUM     4
#define ENERGY_QUANTUM_MASK             0x0F
#define NUM_ENERGY_QUANTA_PER_BYTE      (8 / NUM_ENERGY_BITS_PER_QUANTUM)
#define NUM_ENERGY_BYTES                (NUM_ENERGY_QUANTA / NUM_ENERGY_QUANTA_PER_BYTE)

typedef struct {
    uint8_t count;
    uint8_t energy[NUM_ENERGY_BYTES]; // buckets
} event_t;

typedef struct {
    event_t events[NUM_EVENTS];
} profile_t;
#define PROFILE_SIZE sizeof(profile_t) // TODO: specify explicitly, to avoid padding

extern profile_t profile;

// Flag indicating when Vcap drops below threshold to end profiling
extern volatile bool profiling_vcap_ok;

// A counter in the profile data has reached its max value
extern volatile bool profiling_overflow;

void collect_profile();

// Process the event, and returns whether to wake up the MCU or not afterwards
bool profile_event(unsigned index, uint16_t vcap);

#endif // PROFILE_H
