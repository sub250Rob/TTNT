// src/pwm.h
// PWM output to the payload PCB's dim input, on Timer1 / OC1A / Arduino pin 9.
//
// Timer1 rather than Timer2 because it is 16-bit. Running it in Fast PWM mode
// 14 with ICR1 as TOP gives a freely chosen carrier frequency AND a duty
// resolution fine enough that the 1% floor is exactly 1%, not the 1.18% that
// 8-bit Timer2 would round it to. On a safety floor, that matters.
//
// Mode 14 (ICR1 as TOP) rather than mode 15 (OCR1A as TOP): mode 15 would
// consume OCR1A for the period and cost us pin 9 as an output.
#ifndef PWM_H
#define PWM_H

#include <stdint.h>

// Configures Timer1 and drives pin 9 at 0% duty.
//
// Deliberately starts OFF rather than at the normal running duty: if the pack
// is already below threshold at power-up, the driver must never see full duty,
// not even for the few milliseconds before the first reading lands.
void pwm_init(void);

// Sets the duty the output should ramp towards. 0..100, clamped.
// Nothing moves until pwm_ramp_step() is called.
void pwm_set_target_pct(uint8_t pct);

// Sets the duty NOW, bypassing the ramp, and pins the target there so the ramp
// cannot walk it back. This is the latch's path: dropping to the floor is a
// safety action and must not be gradual.
void pwm_force_pct(uint8_t pct);

// Moves the output one step towards the target. Call once per sample.
//
// Increases are limited to PWM_RAMP_STEP_PCT per call so the boost converter's
// inrush is spread across many small transients, each of which settles inside
// one sample interval. Decreases are applied immediately -- the rate limit
// exists to protect the pack from a load step, not to slow down shedding load.
void pwm_ramp_step(void);

// Current output duty, and the value it is heading towards.
uint8_t pwm_get_duty_pct(void);
uint8_t pwm_get_target_pct(void);

// Reads OCR1A back from the timer and reports whether the hardware is really
// sitting at PWM_DUTY_FLOOR_PCT. This checks silicon, not our own bookkeeping,
// which is the point of the flowchart's "is the duty actually at 1%?" step.
uint8_t pwm_is_at_floor(void);

#endif
