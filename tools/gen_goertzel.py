#!/usr/bin/env python3
"""Emit Q15 Goertzel coefficient 2*cos(2*pi*k/N)."""
import argparse
import math

Q15 = 32767

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--freq", type=float, required=True, help="target frequency Hz")
    ap.add_argument("--fs", type=float, default=48000.0)
    ap.add_argument("--n", type=int, required=True, help="block length")
    a = ap.parse_args()
    k = int(round(a.freq / a.fs * a.n))
    coeff = 2.0 * math.cos(2 * math.pi * k / a.n)
    qv = int(round(coeff * Q15))
    qv = max(-65536, min(65535, qv))
    print(f"/* detect {a.freq} Hz in block N={a.n} (k={k}) */")
    print(f"qdsp_goertzel_t g = {{ .coeff = {qv}, .s_prev = 0, .s_prev2 = 0, .n = 0, .N = {a.n} }};")

if __name__ == "__main__":
    main()
