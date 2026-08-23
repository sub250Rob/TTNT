// src/adc.c
#include <avr/io.h>
#include <util/delay.h>

#include "adc.h"
#include "config.h"

// Which channel ADMUX is currently pointed at, so we only pay the settle
// delay when the channel actually changes.
static uint8_t current_mux;

// ADMUX with REFS[1:0] = 01 (AVCC as reference) and ADLAR = 0 (right
// adjusted). Every write to ADMUX has to preserve these, so build it here
// rather than repeating the bit fiddling at each call site.
static inline uint8_t admux_for(uint8_t mux)
{
  return (uint8_t)((1 << REFS0) | (mux & 0x0F));
}

// _delay_us() wants a compile-time constant, and its upper bound depends on
// F_CPU, so walk to the target in fixed 10 us chunks rather than handing it a
// large runtime value. Loop overhead makes this slightly longer than asked
// for, which for a settling delay is free.
static void settle_delay(void)
{
  for (uint16_t i = 0; i < (ADC_MUX_SETTLE_US / 10u); i++) {
    _delay_us(10);
  }
}

// Start a conversion and spin until the hardware clears ADSC.
static inline void convert_blocking(void)
{
  ADCSRA |= (1 << ADSC);
  while (ADCSRA & (1 << ADSC)) {
    // ~104 us at a 125 kHz ADC clock. Stage 2 turns this into an ISR.
  }
}

void adc_init(void)
{
  current_mux = VBAT_ADC_MUX;
  ADMUX = admux_for(VBAT_ADC_MUX);

  // ADEN enables the ADC. ADPS[2:0] = 111 divides the 16 MHz system clock by
  // 128, giving a 125 kHz ADC clock -- inside the 50-200 kHz window the
  // datasheet wants for full 10-bit accuracy.
  ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);

  // No auto-trigger in stage 1. Stage 2 sets ADATE here and picks a trigger
  // source in ADCSRB.ADTS.
  ADCSRB = 0;

  // Disconnect PC0's digital input buffer. It has no use on an analog pin and
  // switching it costs power and couples digital noise into the reading.
  DIDR0 |= (1 << ADC0D);

  // The first conversion after ADEN takes 25 ADC clocks instead of 13, because
  // the extra cycles initialise the analog core. Discard it.
  convert_blocking();
  (void)ADC;
}

uint16_t adc_read_counts(uint8_t mux)
{
  mux &= 0x0F;

  if (mux != current_mux) {
    ADMUX = admux_for(mux);
    current_mux = mux;

    // Let the new source settle, then throw away several conversions so the
    // sample-and-hold is charged from the new channel and not the old. The
    // residual from the previous channel decays over successive conversions
    // rather than vanishing after one, so a single discard leaves the early
    // samples of an average biased toward wherever the mux used to point.
    settle_delay();

    for (uint8_t d = 0; d < ADC_MUX_DISCARD_SAMPLES; d++) {
      convert_blocking();
      (void)ADC;
    }
  }

  convert_blocking();

  // Read through the ADC macro, not ADCH/ADCL by hand: the low byte must be
  // read first, and reading ADCH first returns a stale low byte.
  return ADC;
}

uint32_t adc_read_counts_sum(uint8_t mux, uint8_t n)
{
  uint32_t sum = 0;

  for (uint8_t i = 0; i < n; i++) {
    sum += adc_read_counts(mux);
  }

  return sum;
}

uint16_t adc_read_counts_avg(uint8_t mux, uint8_t n)
{
  if (n == 0) {
    return 0;
  }

  // Round rather than truncate. Integer division alone biases every reading
  // low by half a count -- systematic, not noise, so averaging never cancels it.
  return (uint16_t)((adc_read_counts_sum(mux, n) + (n / 2)) / n);
}

uint32_t adc_read_bandgap_sum(void)
{
  return adc_read_counts_sum(ADC_MUX_BANDGAP, ADC_AVG_SAMPLES);
}

uint16_t adc_avcc_mv_from_bandgap_sum(uint32_t sum, uint8_t n)
{
  // The ADC always computes counts = 1024 * Vin / AVCC. On a normal channel
  // Vin is the unknown. On the bandgap channel Vin is known, so the same
  // relation solves for AVCC instead:
  //
  //     counts = 1024 * BANDGAP_MV / AVCC   =>   AVCC = 1024 * BANDGAP_MV / counts
  //
  // Note the inversion: a rising rail makes the bandgap read *lower*.
  //
  // Dividing by the accumulated sum rather than a rounded average keeps the
  // sub-count resolution the oversampling earned:
  //
  //     counts = sum / n   =>   AVCC = 1024 * BANDGAP_MV * n / sum
  //
  // The bandgap lands near 219 counts, not 1023, so a single count is worth
  // ~22 mV of AVCC (~0.44%) and ~114 mV of reported pack voltage. Oversampling
  // recovers that only when there is at least 1 LSB of noise to dither
  // against; the observed 218/219 flicker confirms there is.
  //
  // Overflow bound: 1024 * BANDGAP_MV * n stays inside uint32_t while
  // BANDGAP_MV <= 1200 and n <= 255 (worst case 3.1e8 against a 4.29e9 ceiling).
  if (sum == 0) {
    return 0;
  }

  return (uint16_t)((1024UL * BANDGAP_MV * n) / sum);
}

uint16_t adc_measure_avcc_mv(void)
{
  return adc_avcc_mv_from_bandgap_sum(adc_read_bandgap_sum(), ADC_AVG_SAMPLES);
}
