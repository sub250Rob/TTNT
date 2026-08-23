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

// VBAT / V_A0, scaled by 1000. Nominal 47k/10k would be 5.700; the real parts
// measure 5.589, i.e. a bottom leg near 10.26k (+2.6%, inside 5% tolerance).
//
// CALIBRATED 2026-08-22, least squares through the origin over three
// DMM-verified points (18.99 / 20.99 / 24.96 V). Residuals all under 0.07%,
// well inside the 28.4 mV per-count resolution:
//
//     DMM 18990 -> 18985   DMM 20990 -> 21003   DMM 24960 -> 24949
//
// Calibrated end-to-end against the firmware's own a0_mv rather than a DMM
// reading at A0, because a0_mv = V_A0 * (BANDGAP_stored / BANDGAP_true) with
// AVCC cancelling exactly. This one constant therefore absorbs any bandgap
// error as a fixed scale factor, and the correction stays valid when AVCC
// changes from USB to the buck regulator.
//
// It agrees with the divider measured directly by DMM (21.000 V in -> 3757 mV
// at A0) to 0.01%, so the two methods independently confirm each other.
#define DIVIDER_RATIO_X1000   5589u

// The internal bandgap in millivolts. Datasheet spread is 1.0-1.2 V, so this
// is a per-board constant and it is not exactly 1100.
//
// CALIBRATED 2026-08-22: DMM read 5.20 V at the 5 V pin with bg_sum settled at
// 3408, so round((3408/16) * 5200 / 1024) = 1082. Firmware then prints
// avcc_mv = 5201, agreeing with the DMM.
//
// An earlier value of 1108 came from bg_sum = 3490, which was inflated by the
// mux settling bug -- see ADC_MUX_SETTLE_US below.
//
// Any residual error here is a pure scale factor on a0_mv and is absorbed by
// DIVIDER_RATIO_X1000 above, so this value is not load-bearing for VBAT
// accuracy. It does decide whether the printed avcc_mv can be trusted, which
// matters when the board moves to the buck and we want to compare the printed
// rail against a DMM without chasing phantoms.
#define BANDGAP_MV            1082u

// ---- Sampling -------------------------------------------------------------

// Samples averaged per reported value. See the note in adc.c: averaging only
// buys resolution when there is at least 1 LSB of noise to dither against.
#define ADC_AVG_SAMPLES       16u

// Settle delay after an ADMUX channel change, before the discarded samples.
//
// 100 us was NOT enough. Measured 2026-08-22: bg_sum climbed 3418 -> 3486 as
// the bench supply went 18 V -> 25 V, i.e. ~1.7% of the previous channel's
// voltage was still sitting on the sample-and-hold when the bandgap was read.
// The bandgap is a fixed 1.1 V reference and must not move with Vin at all.
//
// The bandgap has the highest source impedance of any channel and needs the
// longest settle. Raised generously: at a 500 ms cadence the cost is nothing.
#define ADC_MUX_SETTLE_US     2000u

// Conversions thrown away after a channel change, before the reading counts.
// One was not enough -- the residual decays across several conversions, so the
// early samples of a 16-sample average were still pulling the mean off.
#define ADC_MUX_DISCARD_SAMPLES  4u

// ---- Reporting ------------------------------------------------------------

// Stage 2 deletes this: the delay() in loop() is replaced by a timer ISR.
#define PRINT_INTERVAL_MS     500u

#endif
