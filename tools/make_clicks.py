#!/usr/bin/env python3
"""Deterministically synthesise the embedded kick-click set (design section 8).

Writes 48 kHz mono 16-bit WAVs into resources/clicks/. No licensing issues:
everything is generated from filtered impulses, FM and noise bursts.
"""
import math
import os
import struct
import wave

FS = 48000
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "resources", "clicks")


class Lcg:
    def __init__(self, seed):
        self.s = seed

    def next(self):
        self.s = (self.s * 1664525 + 1013904223) & 0xFFFFFFFF
        return self.s / 2147483648.0 - 1.0


def env(t, tau):
    return math.exp(-t / tau)


def make(name, ms, fn):
    n = int(FS * ms / 1000)
    x = [fn(i / FS) for i in range(n)]
    # 0.5 ms fade out at the end, then normalise to 0.9 peak
    fade = int(FS * 0.0005)
    for i in range(fade):
        x[n - 1 - i] *= i / fade
    peak = max(abs(v) for v in x) or 1.0
    x = [v / peak * 0.9 for v in x]
    path = os.path.join(OUT, name + ".wav")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(FS)
        w.writeframes(b"".join(struct.pack("<h", int(round(v * 32767))) for v in x))
    print("wrote", path, n, "samples")


def main():
    os.makedirs(OUT, exist_ok=True)
    two_pi = 2 * math.pi
    make("00_tick", 12, lambda t: math.sin(two_pi * 3500 * t) * env(t, 0.0015))
    make("01_tok", 18, lambda t: math.sin(two_pi * 1800 * t) * env(t, 0.004))
    r = Lcg(1)
    make("02_snap", 10, lambda t: (r.next() * 0.7 + math.sin(two_pi * 4200 * t) * 0.3) * env(t, 0.0025))
    make("03_tap", 25, lambda t: math.sin(two_pi * 1200 * t) * env(t, 0.006))
    make("04_fmblip", 20,
         lambda t: math.sin(two_pi * 2000 * t + 4.0 * env(t, 0.004) * math.sin(two_pi * 600 * t)) * env(t, 0.005))
    r2 = Lcg(7)
    make("05_noise", 8, lambda t: r2.next() * env(t, 0.0012))
    r3 = Lcg(11)
    make("06_nineish", 30,
         lambda t: (math.sin(two_pi * (3000 * env(t, 0.004) + 600) * t) * 0.7 + r3.next() * 0.3) * env(t, 0.007))
    make("07_hitech", 28,
         lambda t: math.sin(two_pi * 5000 * t) * env(t, 0.0012) + 0.6 * math.sin(two_pi * 2500 * t) * env(t, 0.008))


if __name__ == "__main__":
    main()
