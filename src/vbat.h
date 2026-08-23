// src/vbat.h
// Conversion from ADC counts to pack millivolts. Pure functions: no hardware
// access, no globals, no side effects. Kept apart from adc.c so the maths can
// be reasoned about (and tested on the host) independently of the ADC, which
// matters once stage 3 runs this logic near an ISR.
#ifndef VBAT_H
#define VBAT_H

#include <stdint.h>

// ADC counts -> millivolts at the A0 pin, given the measured AVCC.
uint16_t vbat_counts_to_adc_mv(uint16_t counts, uint16_t avcc_mv);

// Millivolts at A0 -> millivolts at the battery, undoing the divider.
uint16_t vbat_adc_mv_to_pack_mv(uint16_t adc_mv);

#endif
