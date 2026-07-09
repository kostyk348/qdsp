# qdsp

**Branchless, fixed-point (Q15) DSP primitives for microcontrollers without a
floating-point unit.**

`qdsp` is a header-only C99 library of digital-signal-processing building
blocks that run on FPU-less MCUs (Cortex-M0/M3, ESP32 in soft-float,
RISC-V rv32i). Every quantity is Q15 (`int16`, value = raw / 32768, range
`[-1, 1)`). All routines are `static inline`, have no global state, perform
no dynamic allocation, and stay branch-light at `-O2`.

It complements [MPTC](https://github.com/kostyk348/MPTC)'s sliding-DFT with
the classic IIR/FIR primitives that real audio and sensor pipelines need.

## Modules

| Primitive | What it does |
|-----------|--------------|
| `qdsp_biquad_t` | 2nd-order IIR (low/high/band-pass, notch) — Direct Form II Transposed |
| `qdsp_onepole_t` | 1st-order one-pole low/high pass |
| `qdsp_goertzel_t` | Single-frequency detection (DTMF, tone signalling) |
| `qdsp_envelope_t` | Attack/release envelope follower |
| `qdsp_fir_t` | Symmetric FIR convolution (caller-owned delay line) |
| metering | running peak + block RMS (integer sqrt) |

## Quick start

```c
#include "qdsp.h"

qdsp_biquad_t bp = { /* coeffs from tools/gen_biquad.py */ };
qdsp_goertzel_t gz = { /* coeffs from tools/gen_goertzel.py */ };

for (int i = 0; i < N; i++) {
    q15_t y = qdsp_biquad_process(&bp, sample[i]);
    qdsp_goertzel_process(&gz, y);
}
q15_t power = qdsp_goertzel_power(&gz);   /* > 0.25*Q15 => tone present */
```

Generate coefficients:

```bash
python tools/gen_biquad.py  --type bandpass --f0 1000 --fs 48000 --q 0.707
python tools/gen_goertzel.py --freq 1000 --fs 48000 --n 256
```

## Demo

`examples/demo.c` synthesises a 1000 Hz + 5000 Hz mixture, band-passes it,
runs the envelope follower and Goertzel detector, and prints the result.

```bash
cc -O2 -o demo examples/demo.c && ./demo
# Goertzel power (Q15) = 18551  (threshold 8191)
# Tone 1000 Hz DETECTED
```

`examples/reference.py` is the floating-point reference used to validate the
fixed-point pipeline.

## Determinism / WCET

All primitives are straight-line (no loops with data-dependent bounds except
the fixed-tap FIR, whose cost is `n` multiplies regardless of input). Worst-
case execution time is therefore constant and computable from the tap count
— suitable for the time-triggered, lock-free style of
[DSO-TRON](https://github.com/kostyk348/DSO-TRON).

## License

Apache 2.0
