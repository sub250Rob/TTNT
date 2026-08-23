// src/main.c
// TTNT stage 1: read VBAT through the 47k/10k divider on A0 and report raw
// counts alongside millivolts. Orchestration only -- the hardware lives in
// adc.c, the maths in vbat.c, and every tunable in config.h.
#include <Arduino.h>
#include <stdio.h>

#include "serial_shim.h"
#include "config.h"
#include "adc.h"
#include "vbat.h"

// bg_sum is printed as a uint16_t to avoid depending on whether the linked
// avr-libc printf supports %lu. 1023 * 64 = 65472 still fits; 65 samples do not.
#if (ADC_AVG_SAMPLES > 64)
#error "ADC_AVG_SAMPLES > 64 overflows the uint16_t bandgap sum printed below"
#endif

void setup(void)
{
  serial_begin(9600);

  // adc_init() also discards the slow first conversion after ADEN.
  adc_init();

  printf("\nTTNT stage 1 - VBAT readout\n");
  printf("divider x1000 = %u, bandgap = %u mV (both uncalibrated until measured)\n\n",
         DIVIDER_RATIO_X1000, BANDGAP_MV);
  printf("averaging %u samples; bg_sum / %u = bandgap counts\n\n",
         ADC_AVG_SAMPLES, ADC_AVG_SAMPLES);
  printf(" raw   bg_sum   avcc_mv   a0_mv   vbat_mv\n");
}

void loop(void)
{
  // Re-measured every pass rather than cached at boot, so the reading tracks
  // rail drift as the supply warms or the pack sags. One extra averaged
  // conversion per cycle is irrelevant at this cadence.
  uint32_t bg_sum  = adc_read_bandgap_sum();
  uint16_t avcc_mv = adc_avcc_mv_from_bandgap_sum(bg_sum, ADC_AVG_SAMPLES);

  uint16_t counts  = adc_read_counts_avg(VBAT_ADC_MUX, ADC_AVG_SAMPLES);
  uint16_t adc_mv  = vbat_counts_to_adc_mv(counts, avcc_mv);
  uint16_t vbat_mv = vbat_adc_mv_to_pack_mv(adc_mv);

  // bg_sum and a0_mv are printed for calibration, not for their own sake:
  // bg_sum feeds task 9 at full precision, and a0_mv is what gets compared
  // against the DMM in task 10.
  printf("%4u  %6u  %8u  %6u  %8u\n",
         counts, (uint16_t)bg_sum, avcc_mv, adc_mv, vbat_mv);

  // The one line stage 2 deletes.
  delay(PRINT_INTERVAL_MS);
}
