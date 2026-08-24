// src/main.c
// TTNT stage 3: sample VBAT on a Timer2 cadence, run the 7-of-10 vote from the
// flowchart, and drop the Timer1 PWM on pin 9 to its floor when the pack is
// spent. Orchestration only -- ADC in adc.c, timing in timebase.c, PWM in
// pwm.c, maths in vbat.c, every tunable in config.h.
#include <Arduino.h>
#include <stdio.h>
#include <avr/boot.h>
#include <util/atomic.h>

#include "serial_shim.h"
#include "config.h"
#include "adc.h"
#include "vbat.h"
#include "timebase.h"
#include "pwm.h"

// bg_sum is printed as a uint16_t to avoid depending on whether the linked
// avr-libc printf supports %lu. 1023 * 64 = 65472 still fits; 65 samples do not.
#if (ADC_AVG_SAMPLES > 64)
#error "ADC_AVG_SAMPLES > 64 overflows the uint16_t bandgap sum printed below"
#endif

// ---- Detection state -------------------------------------------------------
//
// A direct transcription of the hand-drawn flowchart. NORMAL is the "sample
// again" loop; VERIFY is the count=0 / 10-sample / count>=7 block; LATCHED is
// "wait for reset".
typedef enum {
  STATE_NORMAL = 0,
  STATE_VERIFY,
  STATE_LATCHED
} state_t;

static state_t   state;
static uint8_t   low_flag;        // the flowchart's "Set Flag" / "Clear Flag"
static uint8_t   votes_taken;
static uint8_t   votes_low;
static uint8_t   have_first_reading;

// Cached because the bandgap is only re-read every BANDGAP_EVERY_N_SAMPLES.
static uint16_t  avcc_mv;
static uint16_t  bg_sum_last;

static uint16_t  sample_counter;

static const char *state_name(state_t s)
{
  switch (s) {
    case STATE_NORMAL:  return "NORMAL";
    case STATE_VERIFY:  return "VERIFY";
    case STATE_LATCHED: return "LATCHED";
    default:            return "?";
  }
}

// ---- Boot diagnostics ------------------------------------------------------
//
// Reads the chip's own fuse bytes and decodes the brown-out threshold -- the
// rail voltage at which the ATmega resets itself and, with it, forgets the
// latch. Read only: we never write fuses.
//
// Stage 4 adds the MCUSR reset-cause report alongside this.
static void report_fuses(void)
{
  uint8_t low, high, ext;

  // boot_lock_fuse_bits_get() sets BLBSET+SPMEN in SPMCSR and must execute the
  // following LPM within three clock cycles, or the hardware clears those bits
  // and the read returns junk. An interrupt in that ~190 ns window would break
  // it, so keep the whole sequence uninterruptible.
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    low  = boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS);
    high = boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS);
    ext  = boot_lock_fuse_bits_get(GET_EXTENDED_FUSE_BITS);
  }

  printf("fuses: low=0x%02X high=0x%02X ext=0x%02X\n", low, high, ext);

  // BODLEVEL is the low three bits of the extended fuse. Unprogrammed bits read
  // as 1, so "all ones" means the brown-out detector is switched off entirely.
  printf("brown-out reset at: ");
  switch (ext & 0x07u) {
    case 0x07u: printf("DISABLED\n");  break;
    case 0x06u: printf("1.8 V\n");     break;
    case 0x05u: printf("2.7 V\n");     break;
    case 0x04u: printf("4.3 V\n");     break;
    default:    printf("reserved\n");  break;
  }
}

// ---- Sampling --------------------------------------------------------------

// One VBAT reading in millivolts at the pack. Re-reads the bandgap only
// occasionally: it drifts thermally, over seconds, and each visit costs two
// mux settles that a VBAT-only read avoids.
static uint16_t sample_vbat_mv(uint16_t *counts_out, uint8_t refresh_bandgap)
{
  if (refresh_bandgap) {
    uint32_t bg_sum = adc_read_bandgap_sum();
    bg_sum_last = (uint16_t)bg_sum;
    avcc_mv     = adc_avcc_mv_from_bandgap_sum(bg_sum, ADC_AVG_SAMPLES);
  }

  uint16_t counts = adc_read_counts_avg(VBAT_ADC_MUX, ADC_AVG_SAMPLES);
  uint16_t adc_mv = vbat_counts_to_adc_mv(counts, avcc_mv);

  *counts_out = counts;
  return vbat_adc_mv_to_pack_mv(adc_mv);
}

// ---- Setup / loop ----------------------------------------------------------

void setup(void)
{
  serial_begin(9600);

  // Pin 9 goes to 0% here, not to the running duty. If the pack is already flat
  // at power-up, the driver must never see full duty -- not even briefly.
  pwm_init();

  // adc_init() also discards the slow first conversion after ADEN.
  adc_init();

  printf("\nTTNT stage 3 - threshold detection and PWM floor\n");
  report_fuses();
  printf("threshold %u mV, vote %u of %u over %u ms\n",
         VBAT_THRESHOLD_MV, VOTE_TRIP_COUNT, VOTE_SAMPLES,
         VOTE_SAMPLES * SAMPLE_INTERVAL_MS);
  // %u not %lu: this firmware never relies on the linked avr-libc printf
  // supporting longs. 16 MHz / 16000 = 1000 Hz fits a uint16_t comfortably.
  printf("pwm pin 9: %u Hz, %u steps, normal %u%%, floor %u%%\n",
         (uint16_t)(F_CPU / (PWM_TOP + 1UL)), (uint16_t)(PWM_TOP + 1u),
         PWM_DUTY_NORMAL_PCT, PWM_DUTY_FLOOR_PCT);
  printf("sampling every %u ms\n\n", SAMPLE_INTERVAL_MS);
  printf(" raw   bg_sum   avcc_mv   vbat_mv   duty   state\n");

  // Seed AVCC before the state machine needs it -- sample 0 must not divide by
  // a zero that has never been measured.
  uint32_t bg_sum = adc_read_bandgap_sum();
  bg_sum_last = (uint16_t)bg_sum;
  avcc_mv     = adc_avcc_mv_from_bandgap_sum(bg_sum, ADC_AVG_SAMPLES);

  state              = STATE_NORMAL;
  low_flag           = 0;
  votes_taken        = 0;
  votes_low          = 0;
  have_first_reading = 0;
  sample_counter     = 0;

  // Started last, after the serial header has drained, so the first interval
  // is a full one.
  timebase_init();
}

void loop(void)
{
  if (!timebase_sample_due()) {
    return;
  }

  sample_counter++;

  uint8_t  refresh = (sample_counter % BANDGAP_EVERY_N_SAMPLES) == 0u;
  uint16_t counts;
  uint16_t vbat_mv = sample_vbat_mv(&counts, refresh);

  uint8_t  below   = (vbat_mv < VBAT_THRESHOLD_MV) ? 1u : 0u;
  state_t  entered = state;

  switch (state) {

    case STATE_NORMAL:
      // The flowchart's top loop: "is VBAT < threshold?" -- no, sample again.
      if (below) {
        low_flag = 1;             // "Set Flag"
        votes_taken = 0;          // "Count = 0"
        votes_low   = 0;
        state = STATE_VERIFY;
      } else {
        // Healthy. Aim for the running duty; pwm_ramp_step() below walks the
        // output there over PWM_RAMP_MS rather than stepping in one go. Covers
        // the first reading after boot, where pwm_init() left the pin at 0%.
        if (!have_first_reading) {
          printf("  -> healthy, ramping to %u%% over %u ms\n",
                 PWM_DUTY_NORMAL_PCT, (uint16_t)PWM_RAMP_MS);
          have_first_reading = 1;
        }
        pwm_set_target_pct(PWM_DUTY_NORMAL_PCT);
      }
      break;

    case STATE_VERIFY:
      // "Take 10 samples; count the ones below threshold."
      votes_taken++;
      if (below) {
        votes_low++;
      }

      if (votes_taken >= VOTE_SAMPLES) {
        if (votes_low >= VOTE_TRIP_COUNT) {
          // force, not target: the drop to the floor is immediate and pins the
          // target so no later ramp step can raise it again.
          pwm_force_pct(PWM_DUTY_FLOOR_PCT);

          // The flowchart's last decision: flag set AND duty actually at the
          // floor? Reading OCR1A back checks the silicon rather than our own
          // bookkeeping, so a failed register write cannot be mistaken for a
          // successful latch.
          if (low_flag && pwm_is_at_floor()) {
            state = STATE_LATCHED;
          } else {
            // Should be unreachable. If the hardware did not take the floor
            // value, say so loudly rather than pretending we are latched.
            printf("  ** PWM DID NOT REACH FLOOR - latch not armed **\n");
            pwm_set_target_pct(PWM_DUTY_NORMAL_PCT);
            low_flag = 0;
            state = STATE_NORMAL;
          }
        } else {
          low_flag = 0;           // "Clear Flag"
          state = STATE_NORMAL;
        }
      }
      break;

    case STATE_LATCHED:
      // "Wait for reset." One-way by construction: nothing in this case can
      // move the duty or the state, however far the pack recovers.
      // Stage 4 hardens this against resets that are not power cycles.
      break;

    default:
      break;
  }

  // Advance the ramp, but only in NORMAL. Freezing it during VERIFY means the
  // 10-sample vote judges one steady operating point instead of a load that is
  // still climbing underneath it. In LATCHED the target is pinned at the floor,
  // so this would do nothing anyway -- not calling it is belt and braces.
  uint8_t duty_before = pwm_get_duty_pct();

  if (state == STATE_NORMAL) {
    pwm_ramp_step();
  }

  uint8_t duty_now = pwm_get_duty_pct();

  if (duty_now != duty_before && duty_now == pwm_get_target_pct()) {
    printf("  -> ramp complete at %u%%\n", duty_now);
  }

  // Print on every state change, and otherwise only occasionally -- one line
  // takes 59 ms to transmit, which is longer than the 20 ms sample interval.
  uint8_t changed = (state != entered);

  if (changed || (sample_counter % PRINT_EVERY_N_SAMPLES) == 0u) {
    printf("%4u  %6u  %8u  %8u  %4u%%   %s\n",
           counts, bg_sum_last, avcc_mv, vbat_mv,
           pwm_get_duty_pct(), state_name(state));
  }

  if (changed && state == STATE_VERIFY) {
    printf("  -> below %u mV, verifying\n", VBAT_THRESHOLD_MV);
  }
  if (changed && state == STATE_LATCHED) {
    printf("  ** LATCHED: %u of %u samples low. PWM held at %u%%. **\n",
           votes_low, VOTE_SAMPLES, PWM_DUTY_FLOOR_PCT);
  }

  // Should stay quiet. If it fires, the work above is outrunning the interval.
  if (timebase_overrun()) {
    printf("  ** OVERRUN **\n");
  }
}
