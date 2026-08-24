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
static uint8_t target_pct;

// The only place OCR1A is written. Everything else goes through the ramp.
static void apply_duty(uint8_t pct)
{
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

  // Start at zero duty, and target zero. The state machine raises the target
  // only after a reading confirms the pack is healthy.
  target_pct = 0;
  apply_duty(0);

  // CS12:CS10 = 001 -> no prescaling. 16 MHz / 16000 = 1000 Hz.
  TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS10);
}

void pwm_set_target_pct(uint8_t pct)
{
  target_pct = (pct > 100u) ? 100u : pct;
}

void pwm_force_pct(uint8_t pct)
{
  pct = (pct > 100u) ? 100u : pct;

  // Pin the target too, so a later pwm_ramp_step() cannot walk the output back
  // up. The latch depends on this.
  target_pct = pct;
  apply_duty(pct);
}

void pwm_ramp_step(void)
{
  if (duty_pct == target_pct) {
    return;
  }

  if (target_pct > duty_pct) {
    // Rate limited on the way up. Each small step's inrush settles well inside
    // one sample interval, so the next reading measures a stable operating
    // point rather than a transient.
    uint8_t room = (uint8_t)(target_pct - duty_pct);
    uint8_t step = (room < PWM_RAMP_STEP_PCT) ? room : (uint8_t)PWM_RAMP_STEP_PCT;

    apply_duty((uint8_t)(duty_pct + step));
  } else {
    // Immediate on the way down. Shedding load is never the dangerous
    // direction, and a gradual latch would defeat the point of latching.
    apply_duty(target_pct);
  }
}

uint8_t pwm_get_duty_pct(void)
{
  return duty_pct;
}

uint8_t pwm_get_target_pct(void)
{
  return target_pct;
}

uint8_t pwm_is_at_floor(void)
{
  uint16_t actual;

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    actual = OCR1A;
  }

  return (actual == PWM_OCR_FOR_PCT(PWM_DUTY_FLOOR_PCT)) ? 1u : 0u;
}
