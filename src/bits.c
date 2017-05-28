#include "bits.h"

unsigned find_first_set_bit_in_word(uint16_t word)
{
    unsigned b = 0;
    while (b < 16 && !(word & (1 << (15 - b))))
        ++b;
    return b;
}

