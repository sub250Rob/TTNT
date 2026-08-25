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
// measure 5.594, i.e. a bottom leg near 10.23k (+2.3%, inside 5% tolerance).
//
// RECALIBRATED for the permanent payload divider, least squares through the
// origin over three DMM-verified points (17.99 / 20.99 / 24.00 V). Residuals
// all under 0.04%, well inside the 28.4 mV per-count resolution:
//
//     DMM 17990 -> 17984   DMM 20990 -> 20994   DMM 24000 -> 24003
//
// The breadboard build measured 5589 -- only 0.09% away, under one count. That
// closeness is a property of these particular resistors, not a general result:
// recalibrate whenever the divider is rebuilt.
//
// Calibrated end-to-end against the firmware's own a0_mv rather than a DMM
// reading at A0, because a0_mv = V_A0 * (BANDGAP_stored / BANDGAP_true) with
// AVCC cancelling exactly. This one constant therefore absorbs any bandgap
// error as a fixed scale factor, and the correction stays valid when AVCC
// changes from USB to the buck regulator.
//
// It agrees with the divider measured directly by DMM (21.000 V in -> 3757 mV
// at A0) to 0.01%, so the two methods independently confirm each other.
#define DIVIDER_RATIO_X1000   5594u

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

// ---- Timing ---------------------------------------------------------------
//
// Replaces the stage 1 delay() in loop(). Two stages of division get us from a
// 16 MHz crystal to a 500 ms cadence, because Timer2 cannot span that alone:
// even at its slowest prescaler and a full 8-bit count it tops out at 16.4 ms.
//
//   16 MHz --/128--> 125 kHz --OCR2A=249--> 2 ms ISR --count 250--> 500 ms

// How often the Timer2 ISR fires. timebase.c derives OCR2A from this.
//
// 2 ms is the CEILING for the /128 prescaler: OCR2A = 125 * TIMEBASE_TICK_MS - 1,
// and OCR2A only holds 255, so 3 ms would need 374 and will not fit. A slower
// tick means moving to the /256 or /1024 prescaler.
#define TIMEBASE_TICK_MS        2u

// How often a full VBAT sample is taken. The ISR runs many times more often
// than this and counts down to it, doing nothing on the intervening ticks.
//
// Stage 3 dropped this from 500 ms to 20 ms so the 10-sample vote resolves in
// 200 ms instead of 5 seconds. Budget at 20 ms: a VBAT-only read is 1.66 ms
// (8%), and the one sample in 25 that also reads the bandgap is 8.16 ms (41%).
#define SAMPLE_INTERVAL_MS      20u

// Ticks the ISR counts before raising the sample flag. 500 / 2 = 250.
#define SAMPLE_INTERVAL_TICKS   (SAMPLE_INTERVAL_MS / TIMEBASE_TICK_MS)

// Integer division would silently truncate and leave the real period short.
#if (SAMPLE_INTERVAL_MS % TIMEBASE_TICK_MS) != 0
#error "SAMPLE_INTERVAL_MS must be a whole number of TIMEBASE_TICK_MS ticks"
#endif

// The bandgap only drifts thermally, over seconds, so re-reading it every
// sample is waste: it costs two mux settles (4 ms) that a VBAT-only read
// avoids entirely. Every 25th sample = every 500 ms.
#define BANDGAP_EVERY_N_SAMPLES  25u

// One 57-character line takes 59 ms to transmit at 9600 baud -- longer than
// the 20 ms sample interval. Sampling and printing therefore have to be
// decoupled. Every 25th sample = every 500 ms = about 12% serial duty.
#define PRINT_EVERY_N_SAMPLES    25u

// ---- PWM output (Timer1, OC1A, Arduino pin 9) ------------------------------
//
// Fast PWM mode 14 with ICR1 as TOP. Prescaler 1, so:
//   frequency = 16 MHz / (PWM_TOP + 1),  resolution = PWM_TOP + 1 steps
//
// 15999 gives exactly 1000 Hz and 16000 steps, which makes the 1% floor
// exactly 160 counts -- an exact 1.0000%, not a rounded approximation.
//
// CONFIRM AGAINST THE DRIVER'S DATASHEET: 1 kHz is a safe general choice for
// LED dimming, but the payload PCB may want a specific range. Changing it is a
// one-line edit; any PWM_TOP from 244 Hz (65535) upward works at prescaler 1.
#define PWM_TOP                 15999u

// Duty commanded during healthy operation.
// CONFIRM: 100% assumed. Set to whatever the study actually calls for.
#define PWM_DUTY_NORMAL_PCT     100u

// The floor the latch drops to. 1% per the hand-drawn flowchart -- deliberately
// not 0%, so the driver is left in a defined dimmed state rather than dark.
#define PWM_DUTY_FLOOR_PCT      1u

// Soft start. Duty rises by this much per sample instead of stepping straight
// to full, so the boost converter's inrush is spread over many small
// transients rather than one large one. 5% every 20 ms reaches 100% in 400 ms.
//
// Ramping is deliberately ONE-WAY: increases are rate limited, decreases are
// immediate. Dropping to the floor is a safety action and must never be
// gradual. See pwm_ramp_step().
//
// This also makes the turn-on decision consistent with the latch decision.
// Without it, full duty is authorised from an unloaded reading and then judged
// against a loaded one -- the threshold means something different either side
// of a single register write.
#define PWM_RAMP_STEP_PCT       5u

#define PWM_RAMP_MS             ((PWM_DUTY_NORMAL_PCT / PWM_RAMP_STEP_PCT) * SAMPLE_INTERVAL_MS)

#if (PWM_RAMP_STEP_PCT == 0)
#error "PWM_RAMP_STEP_PCT of 0 would never reach the running duty"
#endif

// ---- Detection ------------------------------------------------------------
//
// CONFIRM BEFORE FLIGHT: 21.0 V is 3.50 V/cell on a 6S pack -- conservative,
// well above the 3.0 V/cell floor, and leaves capacity for RTH. This is the
// single most safety-relevant number in the firmware and it is Robby's call.
//
// Resolution context: one ADC count is 28.4 mV at the pack, so the threshold
// lands between counts 754 and 755. Hysteresis is irrelevant -- the latch is
// one-way and never re-crosses.
#define VBAT_THRESHOLD_MV       21000u

// The 7-of-10 vote from the flowchart. Samples are paced at
// SAMPLE_INTERVAL_MS, so the vote spans 10 x 20 ms = 200 ms -- long enough to
// ride out any LED load transient, short enough to react promptly to a pack
// that is genuinely finished.
#define VOTE_SAMPLES            10u
#define VOTE_TRIP_COUNT         7u

#if (VOTE_TRIP_COUNT > VOTE_SAMPLES)
#error "VOTE_TRIP_COUNT cannot exceed VOTE_SAMPLES -- the latch could never trip"
#endif

// ---- Latch indicator (pin 13 / PB5, the Uno's onboard LED) -----------------
//
// The flight build has no serial output, so this is the only way to tell, after
// recovering the payload, whether the latch fired. PB5 is free: Timer1 drives
// PB1 for the PWM and nothing else uses the SPI pins.
#define LATCH_LED_DDR           DDRB
#define LATCH_LED_PORT          PORTB
#define LATCH_LED_PIN           PINB
#define LATCH_LED_BIT           PB5

// Toggle every N samples. 25 x 20 ms = 500 ms per half cycle, so a 1 Hz blink.
// Blinking rather than sitting solid is deliberate: a blink proves the firmware
// is still running AND latched, where a steady pin could equally mean it hung.
#define LATCH_LED_BLINK_SAMPLES 25u

// How often the latched state prints a heartbeat, in samples. 100 x 20 ms = 2 s.
// Bench diagnostics only -- the flight build has no serial output at all.
#define LATCHED_HEARTBEAT_SAMPLES  100u

#endif
