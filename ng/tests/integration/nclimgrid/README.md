# nClimGrid-Daily test fixture

Real-world scientific fixture for the OpenGrADS-NG integration suite
(`ng/tests/integration/nclimgrid`). Nothing large is committed here: this
directory holds the acquisition scripts, the recorded schema, and small
golden files. Bulk data lives in the local cache (default
`~/.cache/grads-ng-test-data`, override with `NCLIMGRID_CACHE`).

## Dataset identity

- Product: NOAA nClimGrid-Daily v1.0.0, 1962-07-01 through 1962-07-31,
  release 20220829.
- URL (canonical, also in `fixture.conf` — the single source of truth):
  `https://www.ncei.noaa.gov/data/nclimgrid-daily/archive/1962/nclimgrid-daily_v1-0-0_complete_s19620701_e19620731_c20220829.tar.gz`
- Archive: 157,280,362 bytes,
  sha256 `b3ba72145a98af6c3502684e1c8c1a6315b303ced37012ac07d7f67f79cd7077`.

## Recorded schema (from the actual files, via independent NetCDF reads)

- Grid: lon 1385 (`-124.6875` … `-67.0208`), lat 596 (`24.5625` …
  `49.3542`), both ascending at 1/24°; time 31 daily steps.
- Time encoding: int32 `days since 1800-01-01 00:00:00`, gregorian calendar,
  values 59350…59380 = 1962-07-01…1962-07-31.
- Variables: `tmax`, `tmin`, `tavg` (`degree_Celsius`), `prcp`
  (`millimeter`); per-variable float32 files plus the float64 `ncdd` file
  holding all four. Dims `(time, lat, lon)`, chunked `(1, 144, 337)`,
  zlib+shuffle. `_FillValue` is NaN; there is **no** `missing_value`,
  `scale_factor`, or `add_offset`.
- Missing: 355,702 of 825,460 cells (43%) per day — a static land mask
  (oceans, Great Lakes, outside-CONUS), identical every day. Verified: the
  exact downtown-Chicago cell is lake water.
- Full details: `expected/schema.json` (regenerate with `gen_goldens.py`).

## Converted fixture (for the NG path)

`make_fixture.py` converts the four per-variable files to classic GrADS
binary: `conv/julall.ctl` + `julall.dat` (big-endian float32, UNDEF `-999.0`,
record order vars/time/y/x-fastest), with a `manifest.json` of source
checksums. Regeneration is idempotent.

## Reference behavior pinned by this suite

- `set t N` selects July N; reductions skip missing (`ave`/`min`/`max`/
  `sum` yield UNDEF only when *all* inputs are missing).
- `set lon/lat` snaps the window **outward** to enclose the request;
  single-coordinate snaps resolve exact ties to the **higher** index.
- GrADS `sum()` over the month at all-missing cells yields UNDEF (not 0).
- `tavg` equals `(tmax+tmin)/2` to ±0.006 °C (packing rounding).
- The bundle's `run` mangles script lines in batch mode and `*` comments
  are not honored on stdin: the suite feeds `.gs` files on stdin with
  `*` lines stripped (see `run_gs.sh`).

## Known NG differences (regression-tracked, see suite doc)

- NG `set lon/lat` uses nearest-grid (ties down); reference snaps outward.
  Pinned by `test_world_coord_window`.
- NG `d <unknown-name>` once printed freed memory (use-after-free, fixed in
  `ng/src/main.c`); pinned by `ErrorTest.test_unknown_var_names_it`.
