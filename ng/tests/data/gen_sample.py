#!/usr/bin/env python3
"""Regenerate ng/tests/data/sample.dat (checked-in GrADS binary fixture).

Layout matches sample.ctl: nx=4, ny=3, nz=2, nt=2; vars tsfc (surface),
hgt (2 levels). Record order: variables in VARS order, time outer, level
inner, x fastest. All values big-endian float32 (classic GrADS stream).

value(t,z,y,x) = t*1000 + z*100 + y*10 + x + 0.25, except tsfc t=1 (y=2,x=3)
which carries the UNDEF sentinel (-9.99e33) to prove pass-through.
"""
import struct
import sys
from pathlib import Path

NX, NY, NT = 4, 3, 2
VARS = [("tsfc", 1), ("hgt", 2)]  # (name, stored levels)
UNDEF = -9.99e33


def value(t, z, y, x):
    return t * 1000.0 + z * 100.0 + y * 10.0 + x + 0.25


def main():
    out = Path(__file__).with_name("sample.dat")
    words = []
    for _name, nlev in VARS:
        for t in range(NT):
            for z in range(nlev):
                for y in range(NY):
                    for x in range(NX):
                        v = value(t, z, y, x)
                        if _name == "tsfc" and t == 1 and y == 2 and x == 3:
                            v = UNDEF
                        words.append(v)
    out.write_bytes(struct.pack(">" + "f" * len(words), *words))
    print(f"wrote {out} ({len(words)} words)")


if __name__ == "__main__":
    sys.exit(main())
