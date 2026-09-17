#!/usr/bin/env python3
"""Regenerate ng/tests/data/ens.dat (checked-in GrADS binary fixture).

Layout matches ens.ctl: nx=2, ny=2, nz=1, nt=2, ne=2; var hgt (surface).
Record order: ensemble members outermost (member 0's full time run, then
member 1), time middle, x fastest. All values big-endian float32.

value(e,t,y,x) = e*100 + t*10 + y*2 + x, so member 2 reads 100..113.
"""
import struct
from pathlib import Path

NX, NY, NT, NE = 2, 2, 2, 2


def value(e, t, y, x):
    return e * 100.0 + t * 10.0 + y * 2.0 + x


def main():
    out = Path(__file__).with_name("ens.dat")
    words = []
    for e in range(NE):
        for t in range(NT):
            for y in range(NY):
                for x in range(NX):
                    words.append(value(e, t, y, x))
    out.write_bytes(struct.pack(">" + "f" * len(words), *words))
    print(f"wrote {out} ({len(words)} words)")


if __name__ == "__main__":
    main()
