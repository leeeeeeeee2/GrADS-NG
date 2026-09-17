#!/usr/bin/env python3
"""Build the converted classic-GrADS fixture from cached nClimGrid NetCDF.

Reads <payload>/{tmax,tmin,tavg,prcp}-196207-grd-scaled.nc and writes
<outdir>/julall.ctl + julall.dat: big-endian float32, record order VARS /
time / level(1) / y / x-fastest, NaN mapped to UNDEF_OUT. A manifest with
source sizes and output checksums makes the conversion reproducible and
re-runnable (skipped when up to date).

Usage: make_fixture.py <payload_dir> <out_dir>
"""
import hashlib
import json
import struct
import sys
from pathlib import Path

import netCDF4
import numpy as np

VARS = ("tmax", "tmin", "tavg", "prcp")
UNDEF_OUT = -999.0
CTL_TEMPLATE = """DSET   ^julall.dat
TITLE  nClimGrid-Daily July 1962 converted for GrADS-NG tests
UNDEF  {undef}
XDEF   1385  LINEAR  -124.6875  0.0416666667
YDEF   596   LINEAR  24.5625    0.0416666667
ZDEF   1     LEVELS  0
TDEF   31    LINEAR  00Z01JUL1962  1DY
VARS   4
tmax   0  99  Temperature, daily maximum (degree_Celsius)
tmin   0  99  Temperature, daily minimum (degree_Celsius)
tavg   0  99  Temperature, daily average (degree_Celsius)
prcp   0  99  Precipitation, daily total (millimeter)
ENDVARS
"""


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for blk in iter(lambda: f.read(1 << 20), b""):
            h.update(blk)
    return h.hexdigest()


def main():
    payload, outdir = Path(sys.argv[1]), Path(sys.argv[2])
    outdir.mkdir(parents=True, exist_ok=True)
    ctl_path, dat_path, man_path = (outdir / "julall.ctl",
                                    outdir / "julall.dat",
                                    outdir / "manifest.json")
    srcs = {v: payload / f"{v}-196207-grd-scaled.nc" for v in VARS}
    for v, p in srcs.items():
        if not p.is_file():
            print(f"FAIL: source missing: {p}", file=sys.stderr)
            return 1
    fingerprint = {v: {"size": p.stat().st_size, "sha256": sha(p)}
                   for v, p in srcs.items()}
    if man_path.is_file():
        try:
            if json.loads(man_path.read_text())["sources"] == fingerprint \
                    and dat_path.is_file() and ctl_path.is_file():
                print(f"converted fixture up to date: {dat_path}")
                return 0
        except (ValueError, KeyError):
            pass
    ctl_path.write_text(CTL_TEMPLATE.format(undef=UNDEF_OUT))
    with open(dat_path, "wb") as f:
        for v in VARS:
            ds = netCDF4.Dataset(srcs[v])
            arr = np.asarray(ds.variables[v][:]).astype("float64")
            assert arr.shape == (31, 596, 1385), (v, arr.shape)
            ds.close()
            blk = np.where(np.isnan(arr), UNDEF_OUT, arr).astype(">f4")
            f.write(blk.tobytes(order="C"))
            print(f"wrote {v}: n={blk.size} "
                  f"missing={(blk == np.float32(UNDEF_OUT)).sum()}")
    man = {"sources": fingerprint,
           "undef": UNDEF_OUT, "order": "vars/time/y/x",
           "julall_dat_size": dat_path.stat().st_size,
           "julall_dat_sha256": sha(dat_path)}
    man_path.write_text(json.dumps(man, indent=2) + "\n")
    print(f"wrote {dat_path} ({man['julall_dat_size']} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
