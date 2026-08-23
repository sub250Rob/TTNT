// src/adc.h
// Register-level ADC driver for the ATmega328P. Deliberately no analogRead():
// Stage 2 replaces the polling in here with a timer-triggered ISR, and keeping
// the API "counts in, counts out" means nothing above this layer changes.
#ifndef ADC_H
#define ADC_H

#include <stdint.h>

// Configures ADMUX/ADCSRA/DIDR0 and throws away the first conversion.
// Call once from setup(), before any read.
void adc_init(void);

// One blocking conversion on the given ADMUX MUX[3:0] value.
// Handles the settle delay and discarded sample when the channel changes.
uint16_t adc_read_counts(uint8_t mux);

// Accumulated total of n blocking conversions, kept at full precision.
//
// This is the primitive rather than the mean, because collapsing the sum into
// a whole-count average discards exactly the sub-LSB information oversampling
// buys. That is tolerable on the VBAT channel (1 LSB = 27.8 mV at the pack)
// and not on the bandgap channel, where 1 LSB is ~0.44% of AVCC and therefore
// ~114 mV of reported pack voltage.
uint32_t adc_read_counts_sum(uint8_t mux, uint8_t n);

// Rounded mean, for values that are genuinely wanted in whole counts.
// Returns 0 if n is 0.
uint16_t adc_read_counts_avg(uint8_t mux, uint8_t n);

// Accumulated bandgap total over ADC_AVG_SAMPLES conversions. Exposed because
// task 9 calibrates BANDGAP_MV against it, and wants the full precision.
uint32_t adc_read_bandgap_sum(void);

// AVCC implied by an accumulated bandgap sum of n samples, at sub-count
// resolution. Split from the read so a caller can report the sum as well
// without re-reading the channel or restating the formula.
// Returns 0 for a sum of 0.
uint16_t adc_avcc_mv_from_bandgap_sum(uint32_t sum, uint8_t n);

// Convenience: read the bandgap and convert in one call.
uint16_t adc_measure_avcc_mv(void);

#endif
