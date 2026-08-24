// src/main.c
// TTNT stage 2: read VBAT through the 47k/10k divider on A0 and report raw
// counts alongside millivolts, on a cadence driven by Timer2 rather than
// delay(). Orchestration only -- the hardware lives in adc.c and timebase.c,
// the maths in vbat.c, and every tunable in config.h.
#include <Arduino.h>
#include <stdio.h>

#include "serial_shim.h"
#include "config.h"
#include "adc.h"
#include "vbat.h"
#include "timebase.h"

// bg_sum is printed as a uint16_t to avoid depending on whether the linked
// avr-libc printf supports %lu. 1023 * 64 = 65472 still fits; 65 samples do not.
#if (ADC_AVG_SAMPLES > 64)
#error "ADC_AVG_SAMPLES > 64 overflows the uint16_t bandgap sum printed below"
#endif

// Stage 2 verification only (task 5). Timer0 backs millis() and Timer2 backs
// the cadence, but both divide the same 16 MHz crystal, so the gap between
// samples must read SAMPLE_INTERVAL_MS on every line. Delete before stage 3.
static uint32_t last_sample_ms;

void setup(void)
{
  serial_begin(9600);

  // adc_init() also discards the slow first conversion after ADEN.
  adc_init();

  printf("\nTTNT stage 2 - timer-driven VBAT readout\n");
  printf("divider x1000 = %u, bandgap = %u mV\n",
         DIVIDER_RATIO_X1000, BANDGAP_MV);
  printf("averaging %u samples; bg_sum / %u = bandgap counts\n",
         ADC_AVG_SAMPLES, ADC_AVG_SAMPLES);
  printf("timer2 tick %u ms, sample every %u ticks = %u ms\n\n",
         TIMEBASE_TICK_MS, SAMPLE_INTERVAL_TICKS, SAMPLE_INTERVAL_MS);
  printf(" raw   bg_sum   avcc_mv   a0_mv   vbat_mv   dt_ms\n");

  // Started last, after the serial header has drained, so the first interval
  // is a full one and the first dt_ms is meaningful rather than short.
  timebase_init();
  last_sample_ms = millis();
}

void loop(void)
{
  // The whole of stage 2. loop() now spins freely and does nothing until
  // Timer2 says so, instead of blocking inside delay(). Reading the flag
  // consumes it, so this is called exactly once per pass.
  if (!timebase_sample_due()) {
    return;
  }

  // millis() reads a 4-byte value updated by the Timer0 ISR; the Arduino core
  // already guards that read internally, so no ATOMIC_BLOCK is needed here.
  uint32_t now_ms  = millis();
  uint16_t dt_ms   = (uint16_t)(now_ms - last_sample_ms);
  last_sample_ms   = now_ms;

  // Re-measured every pass rather than cached at boot, so the reading tracks
  // rail drift as the supply warms or the pack sags. One extra averaged
  // conversion per cycle is irrelevant at this cadence.
  uint32_t bg_sum  = adc_read_bandgap_sum();
  uint16_t avcc_mv = adc_avcc_mv_from_bandgap_sum(bg_sum, ADC_AVG_SAMPLES);

  uint16_t counts  = adc_read_counts_avg(VBAT_ADC_MUX, ADC_AVG_SAMPLES);
  uint16_t adc_mv  = vbat_counts_to_adc_mv(counts, avcc_mv);
  uint16_t vbat_mv = vbat_adc_mv_to_pack_mv(adc_mv);

  // bg_sum and a0_mv are printed for calibration, not for their own sake:
  // bg_sum feeds the bandgap calibration at full precision, and a0_mv is what
  // gets compared against the DMM when calibrating the divider.
  printf("%4u  %6u  %8u  %6u  %8u  %7u\n",
         counts, (uint16_t)bg_sum, avcc_mv, adc_mv, vbat_mv, dt_ms);

  // Should never fire at 500 ms. If it does, the work in this function is
  // taking longer than the interval and the cadence is no longer what it says.
  if (timebase_overrun()) {
    printf("  ** OVERRUN: loop took longer than %u ms **\n", SAMPLE_INTERVAL_MS);
  }
}
