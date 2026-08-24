// src/pwm.c
#include <avr/io.h>
#include <util/atomic.h>

#include "pwm.h"
#include "config.h"

// OCR1A value for a given percentage. Note the (TOP + 1): at 100% this gives
// OCR1A = TOP + 1, which the counter never reaches, so the compare never fires
// and the output stays high for the whole period -- a true 100%. Using TOP
// itself would top out at 15999/16000 = 99.99%.
#define PWM_OCR_FOR_PCT(pct)  ((uint16_t)(((uint32_t)(PWM_TOP + 1UL) * (pct)) / 100UL))

static uint8_t duty_pct;

void pwm_init(void)
{
  // Pin 9 = PB1 = OC1A. The timer only reaches the pin if the pin is an output.
  DDRB |= (1 << DDB1);

  // Stop the timer before reconfiguring. As with Timer2, the Arduino core's
  // init() has already set Timer1 up for its own default PWM, so these must be
  // ASSIGNED, not OR-ed -- leaving a stale WGM bit set would silently select a
  // different waveform mode and a different frequency.
  TCCR1B = 0;

  // Mode 14 = Fast PWM, TOP = ICR1. The mode bits are split across both
  // registers: WGM11 here, WGM13 and WGM12 below, WGM10 left clear.
  // COM1A1 alone = non-inverting: OC1A is set at BOTTOM and cleared on match,
  // so a larger OCR1A means a longer high time.
  TCCR1A = (1 << COM1A1) | (1 << WGM11);

  ICR1 = PWM_TOP;

  // Start at zero duty. The state machine raises it only after a reading
  // confirms the pack is healthy.
  duty_pct = 0;
  OCR1A = 0;

  // CS12:CS10 = 001 -> no prescaling. 16 MHz / 16000 = 1000 Hz.
  TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS10);
}

void pwm_set_duty_pct(uint8_t pct)
{
  if (pct > 100u) {
    pct = 100u;
  }

  duty_pct = pct;

  // OCR1A is 16-bit, and an 8-bit core writes it as two bytes through a shared
  // temporary register. If an interrupt landed between the two halves and did
  // its own 16-bit timer access, that temporary would be clobbered and the
  // duty would come out wrong. Nothing in this firmware does that today, but
  // the cost of being sure is a handful of cycles.
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    OCR1A = PWM_OCR_FOR_PCT(pct);
  }
}

uint8_t pwm_get_duty_pct(void)
{
  return duty_pct;
}

uint8_t pwm_is_at_floor(void)
{
  uint16_t actual;

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    actual = OCR1A;
  }

  return (actual == PWM_OCR_FOR_PCT(PWM_DUTY_FLOOR_PCT)) ? 1u : 0u;
}
