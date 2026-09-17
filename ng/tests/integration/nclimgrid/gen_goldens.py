#!/usr/bin/env python3
"""Regenerate committed golden files for the nClimGrid-Daily suite.

Reads the cached payload (<cache>/nclimgrid-jul1962, incl. reference *.gr
dumps produced by gs/dumps.gs under a reference OpenGrADS) and writes
expected/*. Goldens pin the cross-validated values; the CTest drivers
compare fresh reads (direct NetCDF, reference re-runs, NG CLI) against them.

Usage: gen_goldens.py <cache_dir> <suite_dir>
Requires: netCDF4, numpy (project .venv).
"""
import csv
import hashlib
import json
import struct
import sys
import warnings
from pathlib import Path

import netCDF4
import numpy as np

CITIES = (("nyc", -74.0, 40.7), ("chi", -87.65, 41.9), ("mia", -80.2, 25.8),
          ("den", -105.0, 39.7), ("lax", -118.2, 34.1), ("sea", -122.3, 47.6))
OCEANS = (("atl", -68.0, 32.0), ("pac", -124.0, 30.0), ("gulf", -90.0, 26.0))
NC = {"tmax": "tmax-196207-grd-scaled.nc", "tmin": "tmin-196207-grd-scaled.nc",
      "tavg": "tavg-196207-grd-scaled.nc", "prcp": "prcp-196207-grd-scaled.nc",
      "ncdd": "ncdd-196207-grd-scaled.nc"}


def fread(path):
    raw = Path(path).read_bytes()
    assert len(raw) % 4 == 0, path
    return np.array(struct.unpack("<" + "f" * (len(raw) // 4), raw),
                    dtype="float64")


def stats(a):
    good = a[a > -1e8]
    return {"n": int(a.size), "nundef": int((a <= -1e8).sum()),
            "min": float(good.min()), "max": float(good.max()),
            "mean": float(good.mean())}


def main():
    cache, suite = Path(sys.argv[1]), Path(sys.argv[2])
    payload = cache / "nclimgrid-jul1962"
    exp = suite / "expected"
    exp.mkdir(parents=True, exist_ok=True)
    conf = {}
    for line in (suite / "fixture.conf").read_text().splitlines():
        if "=" in line and not line.startswith("#"):
            k, v = line.split("=", 1)
            conf[k] = v

    arch = cache / conf["NCLIMGRID_ARCHIVE_NAME"]
    files = sorted(p.name for p in payload.glob("*") if not p.name.startswith("ref_"))
    (exp / "dataset.json").write_text(json.dumps({
        "url": conf["NCLIMGRID_TEST_URL"], "product": conf["NCLIMGRID_PRODUCT"],
        "version": conf["NCLIMGRID_VERSION"],
        "period": [conf["NCLIMGRID_PERIOD_START"], conf["NCLIMGRID_PERIOD_END"]],
        "release": conf["NCLIMGRID_RELEASE"],
        "archive": {"name": arch.name, "size": arch.stat().st_size,
                    "sha256": hashlib.sha256(arch.read_bytes()).hexdigest()},
        "payload_files": [
            {"name": n, "size": (payload / n).stat().st_size} for n in files],
    }, indent=2) + "\n")

    ds = netCDF4.Dataset(payload / NC["tmax"])
    lon = np.asarray(ds.variables["lon"][:]).astype("float64")
    lat = np.asarray(ds.variables["lat"][:]).astype("float64")
    t = ds.variables["time"]
    tvals = np.asarray(t[:]).tolist()
    dates = [d.strftime("%Y-%m-%d") for d in
             netCDF4.num2date(tvals, t.units, calendar=t.getncattr("calendar"))]
    schema = {
        "dims": {k: {"len": len(v), "unlimited": v.isunlimited()}
                 for k, v in ds.dimensions.items()},
        "coords": {
            "lon": {"n": len(lon), "min": float(lon.min()),
                    "max": float(lon.max()), "ascending": True,
                    "units": ds.variables["lon"].getncattr("units")},
            "lat": {"n": len(lat), "min": float(lat.min()),
                    "max": float(lat.max()), "ascending": True,
                    "units": ds.variables["lat"].getncattr("units")}},
        "time": {"units": t.units, "calendar": t.getncattr("calendar"),
                 "values": tvals, "dates": dates},
        "title": ds.getncattr("title"),
        "files": {},
    }
    ds.close()
    for key, fn in NC.items():
        d = netCDF4.Dataset(payload / fn)
        rec = {"dims": {k: len(v) for k, v in d.dimensions.items()}, "vars": {}}
        for name, var in d.variables.items():
            if var.ndim != 3:
                continue
            rec["vars"][name] = {
                "dtype": str(var.dtype), "dims": list(var.dimensions),
                "shape": list(var.shape),
                "units": var.getncattr("units"),
                "long_name": var.getncattr("long_name"),
                "standard_name": var.getncattr("standard_name"),
                "fill": repr(var.getncattr("_FillValue")),
                "has_missing_value": "missing_value" in var.ncattrs(),
                "has_scale": "scale_factor" in var.ncattrs(),
                "has_offset": "add_offset" in var.ncattrs(),
                "valid": [float(var.getncattr("valid_min")),
                          float(var.getncattr("valid_max"))],
            }
        d.close()
        schema["files"][fn] = rec
    (exp / "schema.json").write_text(json.dumps(schema, indent=2) + "\n")
    # repr() round-trips the float32 grid values exactly through float64.
    exp.joinpath("coords_lon.csv").write_text(
        "".join(repr(float(v)) + "\n" for v in lon))
    exp.joinpath("coords_lat.csv").write_text(
        "".join(repr(float(v)) + "\n" for v in lat))
    with open(exp / "times.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["t_index_1based", "raw_value", "date"])
        for i, (v, d_) in enumerate(zip(tvals, dates)):
            w.writerow([i + 1, v, d_])

    def snap(clon, clat):
        # GrADS nearest-grid rule as observed on the reference build:
        # exact ties resolve to the higher index.
        xi = int(np.argmin(np.abs(lon - clon)))
        while xi + 1 < len(lon) and abs(lon[xi + 1] - clon) <= abs(lon[xi] - clon) + 1e-12:
            xi += 1
        yi = int(np.argmin(np.abs(lat - clat)))
        while yi + 1 < len(lat) and abs(lat[yi + 1] - clat) <= abs(lat[yi] - clat) + 1e-12:
            yi += 1
        return xi, yi

    with open(exp / "cities.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["city", "lon_req", "lat_req", "x_0based", "y_0based",
                    "lon_snap", "lat_snap"] +
                   [f"{v}_t{d_}" for d_ in (1, 15, 31)
                    for v in ("tmax", "tmin", "tavg", "prcp")])
        for city, clon, clat in CITIES:
            xi, yi = snap(clon, clat)
            vals = fread(payload / f"ref_city_{city}.gr")
            assert vals.size == 12, (city, vals.size)
            w.writerow([city, clon, clat, xi, yi, f"{lon[xi]:.4f}",
                        f"{lat[yi]:.4f}"] + [f"{v:.4f}" for v in vals])
    lake = fread(payload / "ref_city_chi_lake.gr")
    assert (lake <= -1e8).all()
    with open(exp / "oceans.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["point", "lon_req", "lat_req", "x_0based", "y_0based",
                    "tmax", "tmax_plus_1", "tmax_times_2", "tmax_div_2",
                    "tmax_julmean", "tmax_minus_tmin"])
        for oc, clon, clat in OCEANS:
            xi, yi = snap(clon, clat)
            vals = fread(payload / f"ref_ocean_{oc}.gr")
            assert vals.size == 6 and (vals <= -1e8).all(), (oc, vals)
            w.writerow([oc, clon, clat, xi, yi] + [f"{v:.4e}" for v in vals])

    fields = {}
    for name in ("ref_tmax_t01", "ref_tmax_t31", "ref_tmax_julmean",
                 "ref_tmax_julmin", "ref_tmax_julmax", "ref_prcp_t15",
                 "ref_prcp_julsum", "ref_region_gp_tmax",
                 "ref_region_gp_trange"):
        p = payload / (name + ".gr")
        fields[name] = {"file": p.name, "size": p.stat().st_size,
                        **stats(fread(p))}
    def ncstat(varfile, var, day, transform=None):
        d = netCDF4.Dataset(payload / varfile)
        a = np.asarray(d.variables[var][day]).astype("float64")
        d.close()
        if transform:
            a = transform(a)
        g = a[~np.isnan(a)]
        return {"source": "netcdf-direct", "n": int(a.size),
                "nundef": int(np.isnan(a).sum()), "min": float(g.min()),
                "max": float(g.max()), "mean": float(g.mean())}

    fields["nc_tmax_t15"] = ncstat("tmax-196207-grd-scaled.nc", "tmax", 14)
    dmax = netCDF4.Dataset(payload / "tmax-196207-grd-scaled.nc")
    amax = np.asarray(dmax.variables["tmax"][14]).astype("float64")
    dmax.close()
    dmin = netCDF4.Dataset(payload / "tmin-196207-grd-scaled.nc")
    amin = np.asarray(dmin.variables["tmin"][14]).astype("float64")
    dmin.close()
    davg = netCDF4.Dataset(payload / "tavg-196207-grd-scaled.nc")
    aavg = np.asarray(davg.variables["tavg"][14]).astype("float64")
    davg.close()
    for name, arr in (("nc_trange_t15", amax - amin),
                      ("nc_tavgdiff_t15", aavg - (amax + amin) / 2)):
        g = arr[~np.isnan(arr)]
        fields[name] = {"source": "netcdf-direct", "n": int(arr.size),
                        "nundef": int(np.isnan(arr).sum()),
                        "min": float(g.min()), "max": float(g.max()),
                        "mean": float(g.mean())}
    fields["ref_region_gp_tmax"]["window"] = {
        "request": {"lon": [-100.0, -90.0], "lat": [35.0, 45.0]},
        "grid_0based": {"x": [592, 833], "y": [250, 491]},
        "rule": "GrADS snaps outward to enclose the requested range"}
    (exp / "fields.json").write_text(json.dumps(fields, indent=2) + "\n")

    monthly = {}
    for key in ("tmax", "tmin", "tavg", "prcp"):
        d = netCDF4.Dataset(payload / NC[key])
        a = np.asarray(d.variables[key][:]).astype("float64")
        d.close()
        if key == "prcp":
            # Masked monthly total: all-missing stays NaN (GrADS sum()
            # yields UNDEF there; nansum would silently emit 0.0).
            cnt = (~np.isnan(a)).sum(axis=0)
            with np.errstate(invalid="ignore"):
                red = np.where(cnt > 0, np.nansum(a, axis=0), np.nan)
            op = "sum"
        else:
            with warnings.catch_warnings():
                warnings.simplefilter("ignore", RuntimeWarning)
                red = np.nanmean(a, axis=0)
            op = "mean"
        g = red[~np.isnan(red)]
        monthly[key] = {"op": op,
                        "min": float(g.min()), "max": float(g.max()),
                        "mean": float(g.mean()), "nvalid": int(g.size)}
    (exp / "monthly.json").write_text(json.dumps(monthly, indent=2) + "\n")
    print(f"wrote goldens to {exp}")


if __name__ == "__main__":
    sys.exit(main())
