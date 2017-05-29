#include <msp430.h>

#include <libio/log.h>

#include "power.h"

// Read VDD_AP through a divider
uint16_t sense_vdd_ap()
{
    // Set pin to ADC mode
    P2MAP4 = 31;
    P2SEL |= BIT4;

    ADC12CTL0 |= ADC12ON;

    //Reset the ENC bit to set the starting memory address and conversion mode sequence
    ADC12CTL0 &= ~(ADC12ENC);

    REFCTL0 |= REFMSTR | REFTCOFF; // use reference control bits in REF, disable temp sensor
    REFCTL0 |= REFON; // turn on reference (TODO: is this needed? or will ADC turn it on on demand?)

    ADC12CTL1 |= ADC12SHP;
    ADC12CTL0 |= ADC12SHT0_15 | ADC12SHT1_15;
    ADC12MCTL0 |= 0x4 | ADC12SREF_1 | ADC12EOS; // channel A4

    //Reset the bits about to be set
    ADC12CTL1 &= ~(ADC12CONSEQ_3);

    ADC12CTL0 |= ADC12ENC + ADC12SC;

    // TODO: replace with sleep with interrupt wakeup
    for (int i = 0; i < 0xff; ++i) {
        __delay_cycles(0xffff);
    }

    uint16_t vdd_ap = ADC12MEM0;
    LOG("V=%u B=%u I=%x\r\n", vdd_ap, (ADC12CTL1 & ADC12BUSY), ADC12IFG);

    return vdd_ap;
}
