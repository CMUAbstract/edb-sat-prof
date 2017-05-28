#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t word_idx;
    uint8_t bit_idx;
} flash_loc_t;

typedef enum {
    FLASH_STATUS_OK = 0,
    FLASH_STATUS_ALLOC_FAILED = 1,
    FLASH_STATUS_WRITE_FAILED = 2,
} flash_status_t;

bool flash_find_space(unsigned len, flash_loc_t *loc);
uint8_t *flash_alloc(flash_loc_t *loc, unsigned len);
uint8_t *flash_find_last_byte();

bool flash_write_byte(uint8_t *addr, uint8_t byte);
bool flash_write_word(uint16_t *addr, uint16_t word);
bool flash_write_long(uint16_t *addr, uint16_t hi, uint16_t lo);
bool flash_write(uint8_t *dest, uint8_t *data, unsigned len);

bool flash_erase();

#endif // FLASH_H
