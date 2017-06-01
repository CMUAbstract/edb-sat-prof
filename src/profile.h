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

void start_profiling();
bool continue_profiling();
void stop_profiling();

// Process the event, and returns whether to wake up the MCU or not afterwards
bool profile_event(unsigned index, uint16_t vcap);

#endif // PROFILE_H
