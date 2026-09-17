# OpenGrADS-NG Baseline (Sep 2026)

Measured state of the repository before further modernization. Everything
below was observed directly; causes that were not investigated are flagged.

## 1. What lives in this repository

- `ng/` — the NG implementation (C11, CMake). This is where all active work
  happens. It is a clean-room implementation, not a fork: no upstream
  GrADS/OpenGrADS source is vendored.
- `opengrads-2.2.1.oga.1-bundle-*.tar.gz` — the behavioral reference: a
  prebuilt OpenGrADS 2.2.1.oga.1 binary bundle (ELF x86-64, 2019). There is no
  reference *source* in the repo, only this binary.
- `docs/` — `ARCHITECTURE.md`, `ARCHITECTURE-NG.md`, `COMPATIBILITY.md`,
  `ROADMAP.md`, and this file.
- `.github/workflows/ci.yml` — CI (upstream-fetch + CMake build jobs).
- `DEVELOPMENT.md`, `ACKNOWLEDGEMENT.md` — project notes.

## 2. Toolchain and dependencies

Measured on the dev machine (Ubuntu 24.04):

- `cmake 3.28.3`, `gcc 13.3.0`, `CMAKE_BUILD_TYPE=Release`
- C standard: C11, warnings `-Wall -Wextra` (non-MSVC)
- Default build dependencies: C standard library + `libm` only. No NetCDF,
  Cairo, X11, or Python in the default path. Legacy sources needing
  Cairo/libpng are excluded unless `GRADS_NG_LEGACY=ON`.

## 3. How to build

```bash
cmake -B build -S ng -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Baseline result: build succeeds with **zero warnings**, `ctest` **60/60
green** (7 unit binaries: `test_expr`, `test_ctl`, `test_grid`,
`test_time`, `test_eval`, `test_core`, `test_render` with 18 checks;
46 CLI integration tests incl. `cli_set_e*` on the `ens.ctl`/`ens.dat`
ensemble fixture; 7 external-data integration tests).

## 4. How to run

```bash
./build/grads-ng -v                 # GrADS-NG 0.1.0
./build/grads-ng -h                 # usage
./build/grads-ng -e 'sin(3.14159/4)'
./build/grads-ng -f ng/tests/data/sample.ctl
printf 'open ng/tests/data/sample.ctl\nd tsfc\nquit\n' | ./build/grads-ng
./build/grads-ng -b -c 'open mydata.ctl'
```

`grads-ng script.gs` currently **validates** (parses) the script and reports
success; execution arrives in M6. `-o DIR` is accepted but output routing
arrives in M5.

## 5. Running the reference (original OpenGrADS)

The bundle needs its shipped libraries and support data on the environment:

```bash
REF=opengrads-2.2.1.oga.1/Contents/Linux/Versions/2.2.1.oga.1/x86_64
export LD_LIBRARY_PATH=$REF/gex:$LD_LIBRARY_PATH
export GADDIR=$PWD/opengrads-2.2.1.oga.1/Contents/Resources/SupportData
export GAUDPT=$REF/gex/udpt
$REF/grads -bl        # -b batch, -l landscape (skips the orientation prompt)
```

Without `GAUDPT` the GX package aborts (`Could not find a record for the
printing plug-in named "Cairo"`). Without `-l`, interactive runs prompt
`Landscape mode?`. Measured: `q config` reports
`v2.2.1.oga.1 little-endian readline grib2 netcdf hdf4-sds hdf5
opendap-grids,stn athena geotiff shapefile`.

## 6. Reference behaviors probed (primary compatibility evidence)

`close` matrix, probed 2026-09-17 against the bundle on
`ng/tests/data/sample.ctl`:

| Input | Reference output |
|---|---|
| `close` (bare) | `Close Error: Missing file number` |
| `close foo` | `Close Error: Invalid file number` |
| `close 0` / `close 99` / `close 1` (not last) / `close 1` (none open) | `Close Error: Only last file may be closed` |
| `close 1 extra junk` | closes; trailing words ignored |
| `close N` (last) | `File N has been closed` |
| `q file` (none open) | `No Files Open` |

NG `close N` reproduces all six rows (7 CTest cases); NG error lines carry an
`ERROR:` prefix per CLI convention, the reference core text verbatim after it.

Reduction (`ave`/`sum`/`max`/`min`) matrix, probed same day/session:

| Input | Reference output |
|---|---|
| `d ave(hgt,t=1,t=2)` | `Averaging. dim = 3, start = 1, end = 2` |
| `d ave(hgt)` / `d ave(hgt,t=1)` | `Error from AVE: Too many or too few args` |
| `d ave(hgt,t=2,t=1)` (reversed) | `Error from AVE: 2nd dimension expression invalid` |
| `d ave(hgt,t=1,t=2,z=1,z=2)` (two dims) | `Error from AVE: Invalid option flags` |
| `d ave(hgt,t=1,t=9)` (past end, nt=2) | accepted: `Averaging. dim = 3, start = 1, end = 9` |
| `d ave(hgt,t=5,t=9)` (fully outside) | accepted, same echo form |
| `d ave(hgt,q=1,q=2)` | `Syntax Error: Invalid dimension expression` |
| `d ave(hgt,x=1,x=2)` / `z=1,z=2` / `e=1,e=1` | `Averaging. dim = 0/2/4, ...` (x=0,y=1,z=2,t=3,e=4) |
| `d max(hgt,t=1,t=2)` / `d min(...)` / `d sum(...)` | `MAXing./MINing./SUMing. dim = 3, start = 1, end = 2` |
| `d max(hgt,100)` / `d max(hgt,tsfc)` | `Error from MAX: Too many or too few args` — **no elementwise max in GrADS** |
| `d pow(hgt,2)` | plots (elementwise power exists) |

`set x/y` matrix: `set x 2` → `LON set to 90 90` (grid index → world echo);
`set x 1 2` → `LON set to 0 90`; `set x 0` / `1 9` / `2 1` / `45` / `500`
all accepted with pure-arithmetic mapping (never validated, never clamped).
`set x` takes grid indices while `set lon 90` takes world values (`LON set
to 90 90`, not 8010) — same for `lat`/`lev` (`set lat 0`, `set lev 500`).
Fresh-open `q dims` shows X/Y varying over the full range, Z/T/E fixed at 1.
NG mirrors the grid-index half and now the world spellings too, but
validates strictly: out-of-range and reversed ranges are errors naming the
valid range, since NG reads only stored data.

World-coordinate rules (all probed): LINEAR world→grid is round-half-up of
the inverse map (`lon 44`→grid 1, `45`→2, `46`→2; `lat 0`→grid 2;
`lon -90`→grid 0 and `lon 400`→grid 5 with no wrapping or clamping).
LEVELS snaps to the nearest entry (`lev 300`→500; the `750` tie between
1000/500 resolved to the higher index, z=2 — single observation).
Fractional grid input snaps the same way (`set x 1.5`→`LON set to 90 90`,
X=2). `set lev` takes one value (`500 1000` varies Z in the reference and
is an honest NG error).

Time matrix: real Gregorian calendar — `29FEB1988` exists as a step.
`31JAN1987 + 1MO` spills to `03MAR1987` (T displays 3!); a second step
anchors `31MAR1987` (start-anchored, not incremental). `29FEB1988 + 1YR`
spills to `01MAR1989` (T displays 2.08333 = 1 + 13/12 ordinal months).
`set time` snaps nearest round-half-up (`12Z02JAN1987`→step 2,
`0030Z...` parses HHMM minutes); forms accepted: `00Z03JAN1987`,
`03JAN1987`, lowercase, 2-digit years with fixed pivot 50
(87→1987, 00→2000, 50→1950). `set time A B` varies T (NG honest error).
Garbage/`29FEB1987`/`24Z...` → `Syntax Error: Invalid Date/Time value.`
+ `SET error: ...`; bad TDEF date/unit or N<1 fails the open; lowercase
units accepted. Out-of-range times are accepted by the reference (clamped);
NG rejects them strictly, naming the valid span.

`set e` matrix (2-member EDEF fixture, ne=2, names memA/memB on following
lines): `set e 2` → `E set to 2 2`; `set e 0` / `set e 3` accepted
unvalidated (never range-checked, even with no EDEF); `set e 2.0` /
`set e +2` stick as 2; `set e 9999999999` sticks, echoes `1e+10`;
bare / `foo` / `memB` (names rejected) / trailing junk → `SET error:
Missing or invalid arguments for E option`. `q dims` shows
`E is fixed  Ens = memB  E = 2` (`(null)` when out of range, the number
when unnamed). File layout is ensemble-outermost: member 1 holds its full
nt×nz run, then member 2 (`d hgt` at e=2 reads 100..103). `d` at e=0/3
warns `Request is completely outside file limits` and contours all-missing.
NG mirrors all of this; the stored `set e` index truncates toward zero.

NG mirrors the protocol with two documented deviations (see COMPATIBILITY.md
Layer 2): out-of-range bounds are hard errors naming the valid range
(reference echoes and proceeds), and `x`/`y`/`e` reduction ranges are
honest not-yet errors (they change output shape).
NG reduction numerics were verified against an independent Python oracle
computed from `gen_sample.py` (Min/Max/Mean exact on ave/max/min; sentinel
skip-missing confirmed via the mean, not just the extrema).

## 7. Known failures / quirks (not fixed, recorded)

1. Reference `d tsfc` on the synthetic `sample.ctl`/`sample.dat` fixture
   prints `Constant field.  Value = 5.8294e-42` instead of the fixture's
   ramp values. The fixture is big-endian float32 (`gen_sample.py`,
   `struct.pack(">"…)`); the reference on this little-endian host appears
   not to byte-swap it. Cause not investigated; NG's own `test_grid`
   covers BE reads byte-exactly, so this is a reference-vs-fixture quirk,
   not an NG defect. Do not "fix" NG to match the garbage value.
2. `ng/tests/TESTING.md` still references `upstream/example/model.ctl` and
   `grads-2.2.1/` paths that do not exist in this repo — stale doc, the
   real fixtures are `ng/tests/data/`.
3. Reference `set gxout fwrite` + `d ave(tsfc,t=1,t=2)` wrote 60 bytes
   (15 float32 words) for a 4×3 = 12-word field, and none of the words —
   decoded little- or big-endian — match the fixture values under any
   byte-swap hypothesis (all ~5.9e-39 LE). Combined with quirk 1, reference
   numeric output on this BE fixture/LE host is unusable as ground truth;
   only protocol/edge behavior is mirrored from the bundle. Numeric ground
   truth is the fixture generator plus hand computation.

## 8. Major subsystems and entry points

```text
ng/src/main.c                  CLI + command dispatcher (exec_command)
ng/src/core/grads_ng.c         session, files, variables, dim selection (libngcore)
ng/src/eval/array.c            elementwise evaluator, NaN = missing (libngcore)
ng/src/parser/grads_ng_parser.c lexer -> AST -> scalar eval + math fns (libngparser)
ng/src/io/ctl.c + grid.c       descriptor parser + binary grid reader (libngio)
ng/src/backend/platform.c      OS layer used by core
ng/src/platform/platform.c     second OS layer (ng_* portable API)
ng/src/grads_ng.c, gs_parser.c, gx_virtual.c   LEGACY, out of default build
ng/src/interp/interp.h         contract only (no .c yet)
ng/src/render/canvas.h         contract only (no .c yet)
ng/include/grads_ng.h          public API (session/file/var/expr/canvas/scripting)
```

Dependency direction: `main -> ngcore -> {ngio, ngparser, ngplatform} -> libc/m`.
Legacy GX/plugin system exists only inside the reference bundle
(`libgxdCairo/X11/dummy`, `libgxpCairo/GD`, `udpt` plugin table).

## 9. Test infrastructure

CTest suite in `ng/tests/` (see `TESTING.md` modulo the stale paths in §7):

- Unit: `test_expr` (lexer/parser/scalar eval), `test_ctl` (descriptor,
  39 checks), `test_grid` (binary reads, 38 checks), `test_time` (calendar,
  stepping, snapping), `test_eval` (array eval incl. `pow` plus
  `max`/`min`/`ave`/`sum` t/z reductions), `test_core` (session lifecycle).
- CLI: version/help/expr/inspect/open-missing/q-no-files/unknown/future-cmd/
  d-values/d-unknown/d-expression/d-function/d-maxmin/d-ave/d-badrange/
  d-sum/set-dims/set-bad/set-unknown/set-lon/lat/lev/time/x-window/close×7 —
  all via `grads-ng -c` or piped REPL sessions.
- Fixtures: `sample.ctl` (4×3×2×2, vars `tsfc`/`hgt`, UNDEF `-9.99e33`),
  `sample.dat` (regenerable via `gen_sample.py`), `levels.ctl`.
