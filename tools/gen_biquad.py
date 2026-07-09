#!/usr/bin/env python3
"""Emit Q15 biquad coefficients (RBJ cookbook) as a C initializer.

Example
-------
    python tools/gen_biquad.py --type lowpass --f0 1000 --fs 48000 --q 0.707
"""
import argparse
import math

Q15 = 32767

def rbj(kind, f0, fs, q, gain_db=0.0):
    w0 = 2 * math.pi * f0 / fs
    cw = math.cos(w0)
    sw = math.sin(w0)
    alpha = sw / (2 * q)
    A = 10 ** (gain_db / 40.0)

    if kind == "lowpass":
        b0 = (1 - cw) / 2;  b1 = 1 - cw;        b2 = (1 - cw) / 2
        a0 = 1 + alpha;     a1 = -2 * cw;       a2 = 1 - alpha
    elif kind == "highpass":
        b0 = (1 + cw) / 2;  b1 = -(1 + cw);     b2 = (1 + cw) / 2
        a0 = 1 + alpha;     a1 = -2 * cw;       a2 = 1 - alpha
    elif kind == "bandpass":
        b0 = alpha;         b1 = 0.0;           b2 = -alpha
        a0 = 1 + alpha;     a1 = -2 * cw;       a2 = 1 - alpha
    elif kind == "notch":
        b0 = 1.0;           b1 = -2 * cw;       b2 = 1.0
        a0 = 1 + alpha;     a1 = -2 * cw;       a2 = 1 - alpha
    else:
        raise SystemExit(f"unknown type {kind}")

    return [b0/a0, b1/a0, b2/a0, a1/a0, a2/a0]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--type", required=True, choices=["lowpass","highpass","bandpass","notch"])
    ap.add_argument("--f0", type=float, required=True)
    ap.add_argument("--fs", type=float, default=48000.0)
    ap.add_argument("--q", type=float, default=0.707)
    ap.add_argument("--gain", type=float, default=0.0, help="gain dB (peaking/shelf)")
    a = ap.parse_args()

    coeffs = rbj(a.type, a.f0, a.fs, a.q, a.gain)
    names = ["b0","b1","b2","a1","a2"]
    print(f"/* {a.type}: f0={a.f0} fs={a.fs} Q={a.q} */")
    print("qdsp_biquad_t filter = {")
    for n, c in zip(names, coeffs):
        qv = int(round(c * Q15))
        qv = max(-32768, min(32767, qv))
        print(f"    .{n} = ({qv}),  /* {c:+.6f} */")
    print("    .w1 = 0, .w2 = 0,")
    print("};")

if __name__ == "__main__":
    main()
