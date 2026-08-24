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

// 0..100. Values above 100 are clamped.
void pwm_set_duty_pct(uint8_t pct);

// Last value handed to pwm_set_duty_pct().
uint8_t pwm_get_duty_pct(void);

// Reads OCR1A back from the timer and reports whether the hardware is really
// sitting at PWM_DUTY_FLOOR_PCT. This checks silicon, not our own bookkeeping,
// which is the point of the flowchart's "is the duty actually at 1%?" step.
uint8_t pwm_is_at_floor(void);

#endif
