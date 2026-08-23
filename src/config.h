// src/config.h
// Every tunable constant for the TTNT payload firmware lives here, so
// calibration is a one-file edit and no magic numbers leak into logic.
#ifndef CONFIG_H
#define CONFIG_H

// ---- ADC channels ---------------------------------------------------------

// VBAT sense: A0 = ADC0 = PC0, fed by the 47k/10k divider.
#define VBAT_ADC_MUX          0x00u

// ADMUX MUX[3:0] = 1110 selects the internal 1.1 V bandgap reference as the
// ADC *input*. Reading it against AVCC is how we recover the real AVCC.
#define ADC_MUX_BANDGAP       0x0Eu

// ---- Calibration ----------------------------------------------------------
// Both values below are NOMINAL. They are wrong until measured on this board.

// VBAT / V_A0, scaled by 1000. Nominal 47k/10k -> (47+10)/10 = 5.700.
// CALIBRATE (task 10): set the bench supply near 21 V, measure Vin and V_A0
// with the DMM at the same instant, then use round(1000 * Vin / V_A0).
#define DIVIDER_RATIO_X1000   5700u

// The internal bandgap in millivolts. Datasheet spread is 1.0-1.2 V, so this
// is a per-board constant and it will NOT be exactly 1100.
// CALIBRATE (task 9): DMM the 5 V pin for AVCC_true, take the bandgap count
// this firmware prints, then use round(counts * AVCC_true_mV / 1024).
#define BANDGAP_MV            1108u

// ---- Sampling -------------------------------------------------------------

// Samples averaged per reported value. See the note in adc.c: averaging only
// buys resolution when there is at least 1 LSB of noise to dither against.
#define ADC_AVG_SAMPLES       16u

// Settle delay after an ADMUX channel change, before the discarded sample.
// The bandgap needs the longest settle of any channel. UNVERIFIED starting
// value: confirm the first post-switch reading matches the second.
#define ADC_MUX_SETTLE_US     100u

// ---- Reporting ------------------------------------------------------------

// Stage 2 deletes this: the delay() in loop() is replaced by a timer ISR.
#define PRINT_INTERVAL_MS     500u

#endif
