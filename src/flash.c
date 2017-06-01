#include <msp430.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <libmsp/watchdog.h>
#include <libio/log.h>

#include "flash.h"
#include "bits.h"

// Number of words reserved for the free-block bitmask
#define FREE_MASK_WORDS 7
#if (FREE_MASK_WORDS * 2) + (FREE_MASK_WORDS * 16) > FLASH_STORAGE_SEGMENT_SIZE
#error Invalid size of free block bitmask: FREE_MASK_WORDS
#endif

#define FREE_MASK_ADDR  ((uint8_t *)FLASH_STORAGE_SEGMENT) /* don't use A, since it's locked by default */
#define STORE_ADDR (FREE_MASK_ADDR + FREE_MASK_WORDS * 2)

static void print_mask()
{
    LOG("FM: mask: ");
    for (int i = 0; i < FREE_MASK_WORDS * 2; ++i) {
        LOG("%02x ", *(FREE_MASK_ADDR + i));
    }
    LOG("\r\n");
}

bool flash_addr_in_range(uint8_t *addr)
{
    return FREE_MASK_ADDR <= addr && addr < FREE_MASK_ADDR + FLASH_STORAGE_SEGMENT_SIZE;
}

static unsigned find_first_set_word_in_mask()
{
    uint16_t *addr = (uint16_t *)FREE_MASK_ADDR;
    unsigned idx = 0;
    while (idx < FREE_MASK_WORDS && *addr++ == 0x0)
        ++idx;
    return idx;
}

// Returns the number of free bytes (including len)
unsigned flash_find_space(unsigned len, flash_loc_t *loc)
{
    LOG("FM: find space: len %u\r\n", len);
    print_mask();

    loc->word_idx = find_first_set_word_in_mask();
    if (loc->word_idx == FREE_MASK_WORDS) {
        LOG("FM: no free words\r\n");
        return 0; // no more free bytes left
    }

    loc->bit_idx = find_first_set_bit_in_word(*(((uint16_t *)FREE_MASK_ADDR) + loc->word_idx));

    LOG("FM: free loc: word %u bit %u\r\n", loc->word_idx, loc->bit_idx);

    unsigned free_bits_in_word = 16 - loc->bit_idx;
    if (free_bits_in_word < len && loc->word_idx == FREE_MASK_WORDS - 1) {
        LOG("FM: insufficient free bits\r\n");
        return 0; // not enough free bytes left
    }

    return ((FREE_MASK_WORDS - loc->word_idx) << 4) - loc->bit_idx; // free bytes
}

uint8_t *flash_find_last_byte()
{
    LOG("FM: find last byte\r\n");
    print_mask();

    flash_loc_t loc = {0};
    flash_find_space(0, &loc);
    if (loc.word_idx == 0 && loc.bit_idx == 0) {
        LOG("FM: mem empty\r\n");
        return NULL;
    }

    uint8_t *p = STORE_ADDR + (loc.word_idx * 16) + loc.bit_idx - 1;
    LOG("FM: last byte: 0x%04x [0x%02x]\r\n", (uint16_t)p, *p);
    return p;
}

// Sets bits in free mask and updates loc to point to the next free bit
uint8_t *flash_alloc(flash_loc_t *loc, unsigned len)
    // precondition: len < 16 ( so that we only need to worry about current and next word )
    // precondition: loc is a result of flash_find_space, executed after last flash_alloc
{
    uint8_t *p = NULL;

    LOG("FM: alloc: len %u, word %u bit %u\r\n", len, loc->word_idx, loc->bit_idx);
    print_mask();

    unsigned free_bits_in_word = 16 - loc->bit_idx;
    unsigned in_first_word, in_second_word;
    if (len < free_bits_in_word) {
       in_first_word = len;
       in_second_word = 0;
    } else {
       in_first_word = free_bits_in_word;
       in_second_word = len - free_bits_in_word;
    }

    uint16_t first_mask_word = 0xffff >> (loc->bit_idx + in_first_word);
    uint16_t second_mask_word = 0xffff >> in_second_word; // if len == 0, stay at 0xffff, and no write

    uint16_t *mask_word_addr = ((uint16_t *)FREE_MASK_ADDR) + loc->word_idx;

    LOG("FM: update mask: addr 0x%04x: %04x %04x\r\n",
        (uint16_t)mask_word_addr, first_mask_word, second_mask_word);

    if (second_mask_word == 0xffff) { // no change to second word
        if (!flash_write_word(mask_word_addr, first_mask_word))
            goto exit;
    } else {
        if (!((uint16_t)mask_word_addr & 0x3)) {
            if (!flash_write_long(mask_word_addr, first_mask_word, second_mask_word))
                goto exit;
        } else {
            if (!flash_write_word(mask_word_addr, first_mask_word))
                goto exit;
            if (!flash_write_word(mask_word_addr + 1, second_mask_word))
                goto exit;
        }
    }

    p = STORE_ADDR + (loc->word_idx << 4) + loc->bit_idx;

    // update loc to point to next free bit
    if (in_second_word) {
        ++loc->word_idx;
        loc->bit_idx = in_second_word;
    } else {
        loc->bit_idx += in_first_word;
    }

exit:
    LOG("FM: alloced: 0x%04x\r\n", (uint16_t)p);
    print_mask();
    return p; 
}

bool flash_write_byte(uint8_t *addr, uint8_t byte)
{
    LOG("FM: write byte: 0x%04x <- 0x%02x\r\n", (uint16_t)addr, byte);

    __disable_interrupt();
    msp_watchdog_hold();

    FCTL3 = FWPW; // clear LOCK (and LOCKA)
    FCTL1 = FWPW | WRT; // word/byte write

    *addr = byte;

    FCTL1 = FWPW; // clear write
    FCTL3 = FWPW | LOCK; // lock

    msp_watchdog_release();
    __enable_interrupt();

    if (FCTL3 & ACCVIFG) {
        LOG("FM: write error\r\n");
        return false;
    }

    LOG("FM: write completed\r\n");
    return true;
}

bool flash_write_word(uint16_t *addr, uint16_t word)
{
    LOG("FM: write word: 0x%04x <- 0x%04x\r\n", (uint16_t)addr, word);

    __disable_interrupt();
    msp_watchdog_hold();

    FCTL3 = FWPW; // clear LOCK (and LOCKA)
    FCTL1 = FWPW | WRT; // word/byte write

    *addr = word;

    FCTL1 = FWPW; // clear write
    FCTL3 = FWPW | LOCK; // lock

    msp_watchdog_release();
    __enable_interrupt();

    if (FCTL3 & ACCVIFG) {
        LOG("FM: write error\r\n");
        return false;
    }

    LOG("FM: write completed\r\n");
    return true;
}

bool flash_write_long(uint16_t *addr, uint16_t hi, uint16_t lo)
{
    LOG("FM: write long: 0x%04x <- 0x%04x%04x\r\n", (uint16_t)addr, hi, lo);

    __disable_interrupt();
    msp_watchdog_hold();

    FCTL3 = FWPW; // clear LOCK (and LOCKA)
    FCTL1 = FWPW | BLKWRT; // long write

    // *addr = longword, both of uint32_t, does not work, even though it does
    // compile to two separate moves
    
    *addr++ = hi;
    *addr = lo;

    FCTL1 = FWPW; // clear write
    FCTL3 = FWPW | LOCK; // lock

    msp_watchdog_release();
    __enable_interrupt();

    if (FCTL3 & ACCVIFG) {
        LOG("FM: write error\r\n");
        return false;
    }

    LOG("FM: write completed\r\n");
    return true;
}

bool flash_write(uint8_t *dest, uint8_t *data, unsigned len)
    // precondition: assumes dest was allocated
{
    LOG("FM: write: 0x%04x <- @0x%04x len %u\r\n", (uint16_t)dest, (uint16_t)data, len);

    LOG("FM: data: ");
    for (int i = 0; i < len; ++i)
        LOG("%02x ", *(data + i));
    LOG("\r\n");

    bool success = true;

    __disable_interrupt();
    msp_watchdog_hold();


    // Write the first byte to align onto word boundary
    if (len > 0 && (uint16_t)dest & 0x1) {
        uint8_t b = *data;

        FCTL3 = FWPW; // clear LOCK (and LOCKA)
        FCTL1 = FWPW | WRT; // byte write
        *dest = b;
        FCTL1 = FWPW; // clear write
        FCTL3 = FWPW | LOCK; // lock

        if (FCTL3 & ACCVIFG) {
            success = false;
            goto exit;
        }

        ++dest;
        ++data;

        --len;
    }

    uint16_t *dest_w = (uint16_t *)dest;
    uint16_t *data_w = (uint16_t *)data;
    while (len >= 2) {
        uint16_t w = *data_w;

        FCTL3 = FWPW; // clear LOCK (and LOCKA)
        FCTL1 = FWPW | WRT; // word write
        *dest_w = w;
        FCTL1 = FWPW; // clear write
        FCTL3 = FWPW | LOCK; // lock

        if (FCTL3 & ACCVIFG) {
            success = false;
            goto exit;
        }

        ++dest_w;
        ++data_w;
        len -= 2;
    }

    if (len) { // last byte if len is odd
        dest = (uint8_t *)dest_w;
        data = (uint8_t *)data_w;

        uint8_t b = *data;

        FCTL3 = FWPW; // clear LOCK (and LOCKA)
        FCTL1 = FWPW | WRT; // byte write
        *dest = b;
        FCTL1 = FWPW; // clear write
        FCTL3 = FWPW | LOCK; // lock

        if (FCTL3 & ACCVIFG) {
            success = false;
            goto exit;
        }
    }

exit:

    msp_watchdog_release();
    __enable_interrupt();

    if (success)
        LOG("FM: write completed\r\n");
    else
        LOG("FM: write failed\r\n");

    return success;
}

bool flash_erase()
{
    LOG("FM: erasing seg at 0x%04x\r\n", (uint16_t)FREE_MASK_ADDR);

    __disable_interrupt();
    msp_watchdog_hold();

    FCTL3 = FWPW; // clear LOCK (and LOCKA)
    FCTL1 = FWPW | ERASE; // segment erase mode
    *FREE_MASK_ADDR = 0; // dummy write to trigger erase
    while (FCTL3 & BUSY);
    FCTL3 = FWPW | LOCK;

    msp_watchdog_release();
    __enable_interrupt();

    if (FCTL3 & ACCVIFG) {
        LOG("FM: erase failed\r\n");
        return false;
    }

    LOG("FM: erase completed\r\n");
    return true;
}
