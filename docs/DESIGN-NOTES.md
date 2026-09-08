# Design notes

Decisions and findings that are not obvious from reading the code, and traps that cost time
to diagnose.

---

## Timer allocation

| Timer | Owner | Notes |
|---|---|---|
| Timer0 | Arduino core | Backs `millis()`, `micros()`, `delay()`, and PWM on pins 5 and 6. Reconfiguring it breaks all of those. |
| Timer1 | PWM output | 16-bit. Fast PWM mode 14 with `ICR1` as TOP, prescaler 1. |
| Timer2 | Sample cadence | 8-bit, CTC, /128 prescaler. Drives no pin. |

**The cadence went on Timer2 specifically to keep Timer1 free for the PWM.** With `ICR1` as
TOP, `ICR1 = 15999` gives exactly 1000 Hz over 16000 steps, which makes the 1% floor
exactly `OCR1A = 160` — a true 1.0000%. On 8-bit Timer2 the nearest step to 1% is 3/255 =
1.18%, and the carrier frequency is fixed by the prescaler ladder. On a safety floor, that
difference matters.

Mode 14 rather than mode 15: mode 15 uses `OCR1A` as the period, which would consume pin 9
as an output.

The cost is PWM on pins 3 and 11, which nothing uses.

### Timer2 cannot auto-trigger the ADC

`ADCSRB.ADTS` offers free-running, the analog comparator, INT0, and Timer0/Timer1 events —
Timer2 is not among them. Hardware auto-trigger would therefore force the cadence onto
Timer1 and cost the PWM timer. The design instead has the Timer2 ISR set a flag that the
main loop consumes and starts conversions in software.

### The tick chain

Timer2 is 8-bit and cannot span 500 ms; even at its slowest prescaler and a full count it
tops out at 16.4 ms. Two stages of division get there:

```
16 MHz --/128--> 125 kHz --OCR2A=249--> 2 ms ISR --software count--> sample interval
```

The /1024 prescaler cannot produce exact intervals here: 15625 Hz × 0.5 s = 7812.5 counts,
and there is no half count. /128 gives 125 000 Hz, where 250 counts is exactly 2 ms.
Timer2 is the only timer on this part that offers /32 and /128.

**2 ms is the ceiling for the /128 prescaler.** `OCR2A = 125 × tick_ms − 1`, and `OCR2A`
holds one byte, so a 3 ms tick would need 374 and will not fit.

### Assign the timer registers, do not OR into them

The Arduino core's `init()` sets `TCCR2A.WGM20` and `TCCR2B.CS22` to configure Timer2 for
default PWM before `setup()` runs, and does the same for Timer1. Writing
`TCCR2A |= (1 << WGM21)` leaves WGM20 set and selects Fast PWM instead of CTC — the timer
runs, the ISR fires, and the period is silently wrong. Use `=`.

`init()` also calls `sei()` before `setup()`, so interrupts are already enabled.

---

## ADC reference and the bandgap correction

The ADC is ratiometric: `count = 1024 × Vin / AVCC`. AVCC is set by whatever regulator is
feeding the board, differs between USB and the buck, and is not 5.000 V in either case. A
5% AVCC error would be a 5% VBAT error — ±1.05 V at a 21 V threshold.

The firmware recovers the real AVCC by measuring the internal 1.1 V bandgap *as an input*
against AVCC (`ADMUX` MUX = 1110):

```
count = 1024 × BANDGAP_MV / AVCC   =>   AVCC = 1024 × BANDGAP_MV / count
```

Note the inversion — a rising rail makes the bandgap read *lower*.

The useful consequence, and the reason the whole scheme exists:

```
a0_mv = raw × BANDGAP_stored / bg_counts
      = V_A0 × (BANDGAP_stored / BANDGAP_true)      ← AVCC cancels exactly
```

The reading is therefore **immune to supply rail drift**, and any residual bandgap error is
a fixed scale factor that `DIVIDER_RATIO_X1000` absorbs during calibration. That is why
calibration performed on USB remains valid on the buck regulator.

Do **not** use the 1.1 V bandgap as the ADC *reference*. With this divider it would cap
measurable VBAT at 6.27 V. Clipping at the top of the range is harmless — only the low
threshold matters.

### Oversampling only helps with dither

Averaging N conversions and rounding to whole counts discards the sub-count information the
oversampling produced. The bandgap path therefore keeps the accumulated **sum** and divides
at full precision:

```
AVCC = 1024 × BANDGAP_MV × n / sum
```

This matters because the bandgap sits near 219 counts, not 1023, so a single count is worth
about 0.44% of AVCC — roughly 114 mV of reported pack voltage. Oversampling recovers that
only when at least one LSB of noise is present to dither against.

---

## ADC multiplexer settling

**Symptom:** `bg_sum` climbed 3418 → 3486 (+2.0%) as the supply went 18 V → 25 V.

The bandgap is a fixed internal reference with no electrical path to the supply. It
*cannot* depend on the input voltage. About 1.7% of the previous channel's voltage was
still on the 14 pF sample-and-hold when the bandgap converted, because a 100 µs settle and
a single discarded conversion were not enough.

**Why it was hard to see:** AVCC is computed *inversely* from the bandgap count, so
contamination pushed AVCC low and VBAT low, and the effect grew with input voltage. The
residuals read +0.13% / −0.59% / −1.72% across 18 → 25 V.

**The reusable rule:**

> A **tilt** in the residuals means the reference is being corrupted.
> A **flat offset** means a scale constant is wrong.

**First diagnostic to reach for:** `bg_sum` must not change when the input voltage changes.
Sweep the supply (15 - 25V) and watch it.

Current settings are deliberately generous: `ADC_MUX_SETTLE_US = 2000` and
`ADC_MUX_DISCARD_SAMPLES = 4`. The residual decays across several conversions rather than
vanishing after one, so a single discard leaves the early samples of an average biased
toward wherever the multiplexer used to point.

Two dead ends, recorded so they are not re-derived: a 10 MΩ DMM does not meaningfully load
the A0 node — it agrees with the firmware to 0.01% once the bandgap is honest — and a bench
supply's front-panel display is not accurate enough for calibration.

---

## Latch persistence

The requirement is precise: recharging and power-cycling the pack must clear the latch, and
nothing else may. A reset is **not** a power cycle — the reset button, the serial monitor's
DTR pulse, a brown-out and a watchdog all wipe RAM and would otherwise restart the firmware
unlatched, commanding the driver straight back to full duty.

### `MCUSR` is unusable on this board

`MCUSR` records the reset cause, and the obvious approach is to read it and boot latched
after any reset that was not a power-on. It does not work here.

Measured after a reset button press, with the capture done in a naked `.init3` function —
the earliest point application code can run, before the C runtime and before `init()`:

```
MCUSR = 0x00
r2    = 0x52
```

Optiboot reads and clears `MCUSR` first. (`high = 0xDE` in the fuses gives `BOOTSZ = 11`, a
512-byte boot section, which identifies Optiboot.) The `r2` fallback is also dead: `0x52`
cannot be an `MCUSR` copy, because bits 4 and 6 are reserved in that register and always
read 0.

### SRAM retention instead

A reset does not clear RAM. Only the C startup code does, by zeroing `.bss` and copying
`.data` — and `.noinit` escapes both. Removing power *does* clear RAM. So a 32-bit magic
value parked in `.noinit` answers the question directly:

- **magic intact** → RAM survived → this was a reset. Restore the latch.
- **magic garbage** → RAM was lost → genuine power-on. Start clean.

The odds of uninitialised SRAM matching the magic on a real power-on are about 1 in 4×10⁹,
and that failure lands in the safe direction anyway — booting latched when it was not
necessary leaves the driver dimmed.

**This is deliberately not EEPROM.** EEPROM would survive a power cycle and defeat the
requirement that recharging clears the latch. `.noinit` RAM has the opposite property: it
dies with the power, which is exactly the behaviour required.

### Verification requirement

**After any toolchain or linker change, confirm that `.noinit` still escapes the `.bss`
clear:**

```sh
avr-objdump -d .pio/build/uno/firmware.elf | sed -n '/<__do_clear_bss>:/,/<__do_copy_data>:/p'
avr-objdump -t .pio/build/uno/firmware.elf | grep '\.noinit'
```

The `cpi` comparison that terminates the clear loop must equal the first `.noinit` address.
If `.noinit` ever falls inside the cleared range, the latch silently stops surviving resets
with no other symptom.

Related: a misspelled ISR vector name compiles cleanly and simply never fires. Confirm the
handler is installed rather than assuming:

```sh
avr-objdump -d .pio/build/uno/firmware.elf | sed -n '/^ *1c:/p'   # vector 7, TIMER2_COMPA
```

It must jump to `__vector_7`, not to `__bad_interrupt`.

---

## Power and grounding

The aircraft and the payload run from **separate battery packs**. There is no isolation
problem, because the Arduino sits entirely in the payload's ground domain: the payload PCB
exposes a ground pin alongside its PWM dim input, and that ground is the pack's ground —
the same node the divider returns to.

The Arduino should be powered from the payload pack through **the pcb's regulated pad**, only.

**Do not feed 25.2 V into VIN or the barrel jack.** The documented Uno input range is
6–20 V and the onboard regulator is rated at or below 20 V, so a 6S pack exceeds absolute
maximum. Either buck to 7–9 V into VIN, or buck to 5 V into the 5 V pin (which bypasses the
onboard regulator and the polyfuse, and makes AVCC exactly the pack's output).

### Brown-out

Fuses on board #1 read `low=0xFF high=0xDE ext=0xFD`, giving **BODLEVEL = 101, brown-out
reset at 2.7 V**. They can be read at runtime with `boot_lock_fuse_bits_get()` from
`<avr/boot.h>`, wrapped in an `ATOMIC_BLOCK` — the BLBSET+SPMEN sequence must reach its
`LPM` within three clock cycles.

**Do not write fuses.** A wrong `CKSEL` or a cleared `SPIEN` bricks the board with no
recovery over USB.

Two consequences worth knowing:

- At 16 MHz the part is only in specification down to about 3.8 V (interpolated from the
  datasheet speed grades: 10 MHz at 2.7 V, 20 MHz at 4.5 V). With BOD at 2.7 V there is a
  2.7–3.8 V window in which the chip keeps executing out of specification instead of
  resetting.
- That window is acceptable here because the pcb regulator holds 5 V down to roughly 7 V of pack input, while the latch trips  near 21 V. A sag deep enough to brown out the MCU is about
  14 V beyond the sag that trips the latch, so the two events are not meaningfully
  correlated. This is the structural argument that makes the latch robust; the SRAM
  mechanism above is defence in depth.

---

## Harness IR drop

The divider measures at the payload's power input, which sits below the pack terminals by
whatever the harness drops under load. This is not an error in the measurement — it is a
real voltage — but it means the threshold judges the payload input rather than the pack
unless it is compensated.

The drop is `I × R`, and the driver is roughly a constant-power load, so **as the pack sags
it draws more current and the drop grows** — precisely where the threshold decision is made.
A fixed offset tuned at full charge is therefore wrong at the threshold, and a scale factor
is wrong by roughly twice as much. `vbat_compensate_harness()` models the current instead.

Practical notes:

- Connector and contact resistance dominates, not wire gauge. A bench rig using alligator
  clips measured 546 mΩ; 1 m of 18 AWG round trip is 42 mΩ. This is why calibration is important, further,
  18 AWG resistance losses are negligable, nowhere near 1 m of wire is used for the entire payload system.
- With a proper XT60 harness the drop is on the order of 40 mV, about 1.4 ADC counts. At
  that level, setting `HARNESS_MILLIOHMS` to 0 is a reasonable choice.
- The 1.5 A load path never passes through an Arduino header pin. The board taps the pack
  node through a 57 kΩ divider and draws 442 µA, so its own wiring can stay thin.

---

## Miscellaneous

- **Do not `printf()` from an ISR.** It is slow and not reentrant. Set a flag and print from
  the main loop.
- **State shared with an ISR must be `volatile`, and `volatile` is not the same as atomic.**
  `volatile` stops the compiler caching a value in a register; it does nothing about an
  interrupt landing between two separate accesses. A read-then-clear pair needs
  `ATOMIC_BLOCK` as well. Prefer `ATOMIC_RESTORESTATE` over `ATOMIC_FORCEON`, which would
  unconditionally enable interrupts on exit.
- **16-bit timer registers are written as two bytes through a shared temporary register.**
  An interrupt doing its own 16-bit timer access in between would corrupt the write.
- **Serial transmission is the slowest thing in the system.** One 57-character line takes
  59 ms at 9600 baud, longer than the 20 ms sample interval, so sampling and printing must
  be decoupled. Above roughly 23 lines per second the transmit buffer backs up and blocks.
- **An open sense path fails safe.** A floating A0 reads well below threshold and latches.
  The dangerous failure would be A0 stuck high, which would never latch; there is currently
  no protection against that. Ensure proper calibration and verification that all in-use mcu
  are functioning properly before deployment.
