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

Baseline result: build succeeds with **zero warnings**, `ctest` **32/32
green** (5 unit binaries: `test_expr`, `test_ctl`, `test_grid`, `test_eval`,
`test_core`; 27 CLI integration tests).

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
  39 checks), `test_grid` (binary reads, 38 checks), `test_eval` (array
  eval incl. max/min/pow), `test_core` (session lifecycle).
- CLI: version/help/expr/inspect/open-missing/q-no-files/unknown/future-cmd/
  d-values/d-unknown/d-expression/d-function/d-maxmin/set-dims/set-bad/
  set-unknown/close×7 — all via `grads-ng -c` or piped REPL sessions.
- Fixtures: `sample.ctl` (4×3×2×2, vars `tsfc`/`hgt`, UNDEF `-9.99e33`),
  `sample.dat` (regenerable via `gen_sample.py`), `levels.ctl`.
