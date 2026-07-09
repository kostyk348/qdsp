#!/usr/bin/env python3
"""Reference implementation of the qdsp demo in floating point.

Validates that the fixed-point pipeline (band-pass + Goertzel) detects the
1000 Hz tone and rejects the 5000 Hz tone. Run with:  python examples/reference.py
"""
import math

FS = 48000
N = 256

def rbj_bandpass(f0, fs, q):
    w0 = 2 * math.pi * f0 / fs
    cw, sw = math.cos(w0), math.sin(w0)
    alpha = sw / (2 * q)
    a0 = 1 + alpha
    b0, b1, b2 = alpha / a0, 0.0, -alpha / a0
    a1, a2 = -2 * cw / a0, (1 - alpha) / a0
    return b0, b1, b2, a1, a2

def biquad(b, x0, s):
    w = x0 - b[3] * s[0] - b[4] * s[1]
    y = b[0] * w + b[1] * s[0] + b[2] * s[1]
    s[1] = s[0]; s[0] = w
    return y

def goertzel(samples, freq, fs, N):
    k = round(freq / fs * N)
    coeff = 2 * math.cos(2 * math.pi * k / N)
    s_prev = s_prev2 = 0.0
    for x in samples:
        s = x + coeff * s_prev - s_prev2
        s_prev2 = s_prev; s_prev = s
    return s_prev ** 2 + s_prev2 ** 2 - coeff * s_prev * s_prev2

b = rbj_bandpass(1000, FS, 0.707)
s_state = [0.0, 0.0]
samples = []
for i in range(N):
    t = i / FS
    x = 0.5 * math.sin(2 * math.pi * 1000 * t) + 0.5 * math.sin(2 * math.pi * 5000 * t)
    samples.append(biquad(b, x, s_state))

p = goertzel(samples, 1000, FS, N)
print(f"reference Goertzel power = {p:.4f}")
print(f"Tone 1000 Hz {'DETECTED' if p > 0.25 else 'not detected'}")
