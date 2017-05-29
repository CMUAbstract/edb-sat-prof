#include <msp430.h>

#include <stdlib.h>

#include <libio/log.h>

// Seed random generator by reading an analog voltage from ADC (Vbank)
void seed_random_from_adc()
{
    // Set pin to ADC mode
    P2MAP4 = 31;
    P2SEL |= BIT4;

    ADC12CTL0 |= ADC12ON;

    //Reset the ENC bit to set the starting memory address and conversion mode sequence
    ADC12CTL0 &= ~(ADC12ENC);

    ADC12CTL1 |= ADC12SHP;
    ADC12CTL0 |= ADC12SHT0_0 | ADC12SHT1_0;
    ADC12MCTL0 |= 0x4 | ADC12SREF_0 | ADC12EOS; // channel A4

    //Reset the bits about to be set
    ADC12CTL1 &= ~(ADC12CONSEQ_3);

    ADC12CTL0 |= ADC12ENC + ADC12SC;

    while (ADC12CTL1 & ADC12BUSY); // wait for conversion to complete
    uint16_t seed = ADC12MEM0;

    LOG("rnd seed=%u\r\n", seed);
    srand(seed);
}
