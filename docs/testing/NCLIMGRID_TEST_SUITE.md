# nClimGrid-Daily Test Suite

Real-world scientific regression benchmark for OpenGrADS-NG, built on the
NOAA NCEI nClimGrid-Daily July-1962 monthly product. It answers, automatically:

> "Does OpenGrADS-NG correctly read, interpret, calculate, subset,
> visualize, and process a real NOAA nClimGrid-Daily dataset?"

Suite root: `ng/tests/integration/nclimgrid/`. Fixture notes:
`ng/tests/integration/nclimgrid/README.md`.

## 1. Dataset

| Item | Value |
|---|---|
| Product | NOAA nClimGrid-Daily v1.0.0 |
| Period | 1962-07-01 through 1962-07-31 (31 daily steps) |
| Release | 20220829 |
| URL | `https://www.ncei.noaa.gov/data/nclimgrid-daily/archive/1962/nclimgrid-daily_v1-0-0_complete_s19620701_e19620731_c20220829.tar.gz` |
| Archive size | 157,280,362 bytes |
| Archive sha256 | `b3ba72145a98af6c3502684e1c8c1a6315b303ced37012ac07d7f67f79cd7077` |
| Grids | `tmax/tmin/tavg-196207-grd-scaled.nc` (float32), `prcp-…nc`, `ncdd-…nc` (all four, float64) |
| Grid | 1385 lon × 596 lat × 31 time, 1/24°, CONUS |
| Time encoding | int32 days since 1800-01-01, gregorian, 59350…59380 |
| Missing | NaN `_FillValue`; static 43% land mask; no `missing_value`/`scale_factor`/`add_offset` |

The URL and integrity data live in exactly one place: `fixture.conf`.

## 2. Test architecture (three arms)

```text
Reference OpenGrADS (sdfopen + .gs + fwrite)
        |
        v
Committed goldens (expected/)
        |
        +------> OpenGrADS-NG (converted .ctl + CLI)
        +------> Direct NetCDF (netCDF4/numpy, independent)
```

- `tests/test_fixture.py` — discovery/extraction/integrity (stdlib only).
  FAILs on corrupt state; SKIP only when never acquired.
- `tests/test_netcdf.py` — schema, coords, time, vars, units, mask vs goldens.
- `tests/test_reference.py` — runs every `gs/*.gs` under reference GrADS,
  checks logs are error-clean, and re-validates reference fwrite dumps and
  render data-path vs goldens. Needs `REF_GRADS_BIN` (+ `GADDIR`); SKIPs
  otherwise. Headless rendering asserts contour computation + render input,
  or a real PNG when a display GX exists.
- `tests/test_ng.py` — grads-ng CLI on `conv/julall.ctl`: open/q, time
  selection, grid-index and world-coordinate windows, city/ocean points,
  expressions, reductions, math, errors.
- `tests/test_workflow.py` — end-to-end July workflow incl. a verified
  written summary artifact.
- `tests/test_perf.py` — baseline timings (open 0.01 s, monthly mean over
  25.6 M cells 0.35 s on the dev machine; bounds are anti-hang guards).

The dataset has no vector fields and NG has no renderer yet (M5): vector
coverage asserts the absence honestly, and rendering coverage asserts the
data path into the renderer plus documents the pixel gap. Nothing passes
silently: unavailable stages SKIP with `SKIP:` reasons surfaced by CTest.

## 3. How to run

```bash
# one-time: fetch + verify + convert (~700 MB under ~/.cache)
ng/tests/integration/nclimgrid/download-nclimgrid-test-data.sh

# configure (once), giving the reference binary if available
cmake -B build -S ng -DREF_GRADS_BIN=/path/to/grads -DREF_GADDIR=/path/to/SupportData
cmake --build build -j

# fast unit tests only (default loop; never downloads)
ctest --test-dir build -LE external-data

# the full nClimGrid suite (skips cleanly offline)
ctest --test-dir build -L nclimgrid --output-on-failure

# same via target:  cmake --build build --target test-nclimgrid
```

Offline: every external test prints `SKIP: <reason>` and CTest reports it
skipped (`SKIP_REGULAR_EXPRESSION`). A present-but-corrupt fixture FAILs.

Environment: `NCLIMGRID_CACHE` (cache root), `NCLIMGRID_PYTHON` (exported by
the download script; project `.venv` preferred), `REF_GRADS_BIN`, `GADDIR`.
Fixture conversion and golden regeneration need `netCDF4` + `numpy`
(`.venv` in the repo root; wheels installable offline via
`pip install --no-index <wheels>` when the index is blocked).

## 4. Updating the fixture / regenerating goldens

```bash
python3 ng/tests/integration/nclimgrid/gen_goldens.py <cache> ng/tests/integration/nclimgrid
```

Goldens are cross-validated reference↔NetCDF before committing. Never edit
an expectation to match a new implementation without going through the
§6 discrepancy procedure below.

## 5. Compatibility methodology

For each behavior: reproduce on reference → compare raw NetCDF → compare
metadata/coords/missing/precision → decide which is correct → document →
pin with a regression test. Discrepancies found so far:

1. **nansum trap (generator bug, fixed):** `np.nansum` emits 0.0 over
   all-missing cells while GrADS `sum()` yields UNDEF. Goldens now use a
   masked sum; `monthly.json` prcp mean = 73.6295 over 469,758 land cells.
2. **Window snap (known NG difference, pinned):** reference `set lon/lat`
   snaps outward to enclose; NG uses nearest-grid (ties down). E.g. lat
   35.51…44.49 → reference y 263…480, NG y 264…479 (1-based).
3. **Use-after-free (NG bug, fixed):** `d <unknown>` printed freed memory;
   `ng/src/main.c` now copies the name before freeing the AST. Pinned by
   `ErrorTest.test_unknown_var_names_it`.

## 6. Numerical tolerances

- Reference↔NetCDF: float32 round-trip, maxdiff 0 on fields/min/max,
  ≤2e-6 on means.
- NG↔goldens: absolute 1e-2 + relative 1e-4 (means 5e-2/5e-1 where float32
  accumulation over 25 M cells justifies it). No exact float equality.
- Coordinates golden round-trips exactly (`repr`); city values to 4 dp.

## 7. Rendering comparison methodology

No committed pixels (font/GPU nondeterminism). Committed instead: the exact
render-input field (`contour_tmax.gr` companion, full-grid stats golden),
the contour-level log line, and the command. When a display GX is present,
`printim` output >1 KB counts as rendered. Same policy will cover NG's M5
renderer when it lands.
