#ifndef PROFILE_H
#define PROFILE_H

#include <stdint.h>
#include <stdbool.h>

#define NUM_EVENTS             4    // num watchpoints
#define PROFILE_EHIST_BIN_MASK 0x0F // must match the bitfield length in event_t
#define PROFILE_COUNT_MASK     0xFF // must match the bitfield length in event_t

typedef struct __attribute__((packed)) {
    uint8_t count;
    uint8_t ehist_bin0:4;
    uint8_t ehist_bin1:4;
} event_t;

typedef struct __attribute__((packed)) {
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
