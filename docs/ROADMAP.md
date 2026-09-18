# GrADS-NG Roadmap (M0)

Each milestone leaves the tree buildable (`cmake --build`) and tested (`ctest`).
Milestone scope is capped: if a milestone grows past one coherent change set,
split it rather than landing a giant rewrite.

## M0 — Architecture ✅

- [x] Inspect repo; record toolchain, modules, gaps.
- [x] `docs/ARCHITECTURE.md`, `docs/COMPATIBILITY.md`, `docs/ROADMAP.md`.
- [x] Green default build with zero non-system dependencies.
- [x] First CTest suite (lexer/parser/eval/CLI smoke).

## M1 — Minimal Runtime ✅ (vertical slice landed)

Goal: smallest useful vertical slice through CLI → dispatch → session.

- [x] `grads-ng -e '<arith>'` prints numeric result with GrADS precedence.
- [x] `-f file.ctl` prints real dims; `-c` + REPL run `open`, `q file`,
      `quit`/`exit`, `help` with GrADS-style errors.
- [x] Positional `.gs` arg parses for validation (execution: M6).
- [x] File-dimension accessors on the public API (no opaque-struct leaks).
- [x] Legacy sources (`src/grads_ng.c`, `src/gs_parser.c`, `src/gx_virtual.c`)
      excluded from default build, disposable behind `GRADS_NG_LEGACY=ON`.
- [x] Done: `cmake -B build && cmake --build build && ctest` green (13/13)
  on a bare Linux toolchain; COMPATIBILITY.md Layer 1/2 rows marked ✅ hold.

## M2 — Dataset Support (in progress)

- [x] Implement `ng_ctl_parse()` per `ng/src/io/ctl.h`: DSET/TITLE/UNDEF/
  XDEF/YDEF/ZDEF (LINEAR + LEVELS)/TDEF-count/EDEF-count/VARS/DTYPE/OPTIONS.
- [x] Template detection + `ng_ctl_expand_template()` (`%y4 %y2 %m2 %m1
  %d2 %d1 %h2 %h1`, `%%`); unknown `%` sequences pass through verbatim.
- [x] `test_ctl` (39 checks) + `levels.ctl` fixture; `ctest` 15/15 green.
- [x] GrADS binary reader (`io/grid.c`): mmap'd stream/sequential float32,
  big-endian default with BYTESWAPPED support, Fortran-marker validation.
- [x] Synthetic `tests/data/sample.dat` (+`gen_sample.py`) + byte-exact
  `test_grid` (38 checks: all slices, UNDEF pass-through, LE-sequential
  round-trip, truncation/index/template rejections).
- [x] Session wiring: `open` parses via `ng_ctl_parse` (session error slot
  names the reason), borrowed var handles, lazy grid open, `d <var>`
  value-summary + `q file` name listing; `test_core` +19 checks, CLI
  end-to-end `d` tests; `ctest` 19/19 green, zero warnings.
- [ ] Merge the two platform layers (`backend/platform.*` + `src/platform/`)
  once mmap ownership is settled.
- Done when: `d` on a synthetic variable returns byte-exact values.

## M3 — Expression Engine (in progress)

- [x] CALL-node parsing (`f(x, ...)`, ≤16 args) + shared `grads_ng_math_apply`
      (abs/sqrt/exp/log-natural/sin/cos, case-insensitive) for scalar + array.
- [x] Array evaluator (`src/eval/array.c` in `ngcore` lib): elementwise ops
      over the selected (t,z) slice, scalars broadcast, NaN = missing with
      full propagation (arithmetic, comparisons, functions, div-by-zero,
      overflow); unknown names/shapes are hard errors. `test_eval` (26 checks).
- [x] `set t/z` selection + `q dims`; `d <expr>` end to end; `ctest` 24/24
      green, zero warnings.
- [x] M4 slice 1: `pow` elementwise (scalar + array); `test_expr`/`test_eval`
  cover it. (Earlier elementwise `max/min` removed: reference-probed as
  reductions-only — `max(hgt,100)` and `max(hgt,tsfc)` both fail in
  2.2.1.oga.1 with "Too many or too few args".)
- [x] `close N`: reference-verified against 2.2.1.oga.1 (number required,
  last-file-only, trailing words ignored); 7 CLI tests; `ctest` 32/32 green.
- [x] M4 slice 2: `max/min/ave/sum(expr, dim=a, dim=b)` reductions over t/z
  index ranges (EQUAL-node dim args, `lev` = z alias, strict in-file bounds,
  missing skipped, selection restored); unit + CLI tests; `ctest` 35/35
  green, zero warnings. Left for later: x/y/e ranges, multi-dim calls,
  world-coordinate bounds, strict-vs-reference overshoot documented in
  COMPATIBILITY.md.
- [x] Slicing slice 1: `set x/y` grid-index windows (file state + API +
  evaluator clipping + `q dims`), strict in-file bounds; `ctest` 39/39
  green, zero warnings.
- [x] Slicing slice 2: world-coordinate `set lon/lat/lev` (LINEAR inverse
  round-half-up, LEVELS nearest with observed tie rule, strict storage,
  reference-style echoes, fractional grid snapping); unit + CLI tests;
  `ctest` 42/42 green, zero warnings.
- [x] Time slice 1: `ng/src/time/` subsystem (real Gregorian calendar,
  start-anchored MO/YR stepping with forward spill, nearest-step snap
  ties up, eager TDEF validation), `set time`, `q dims` Time; unit +
  CLI tests; `ctest` 52/52 green, zero warnings. Left for later:
  varying `t`/`z` ranges, subscript syntax in `d`, relative
  time offsets.
- [x] `set e`: EDEF names + `sel_e` + ensemble-outermost reader offsets +
  CLI (`E set to N N` echo, `q dims` Ens, out-of-range degrade-to-missing
  with warning); unit + CLI tests; `ctest` 60/60 green, zero warnings.
- Done when: `(tsfc-273.16)*9/5+32`-class expressions match reference values.

## M4 — Core Analysis

- Most-used ops only (driven by real scripts): averaging, masking,
  interpolation stubs hardened into implementations.
- Done when: each op has unit + integration tests against synthetic data.

## M5 — Rendering (in progress)

- Implement `render/canvas.h` with a dependency-free software rasterizer
  (PPM/PNG via miniz or stb-style single file); Cairo backend optional.
- Primitives: line, contour, shaded, vector, text, map frame; `print`/`gxprint`.
- [x] Slice 1: shaded-grid PPM backend (`ng/src/render/shade.h/.c`,
  dependency-free, byte-deterministic: linear gray 0-255 over valid range,
  NaN = magenta, y=0 at bottom, 10px cells) wired behind `gxprint` (+ `-o`
  output dir; legacy `print` explains it is superseded); `test_render`
  (18 checks incl. byte-exact header/pixel rows) + 4 CLI tests;
  `ctest` 57/57 green, zero warnings. Left for later: line/contour/
  vector/text/map primitives, PNG output.
- [x] Slice 2: contour lines + PNG. Marching-squares tracing
  (`contour.h/.c`: nice even levels spanning the valid range, NaN cells
  skipped, saddle disambiguation) rasterized as 1px black lines over the
  shared RGB raster, plus a dependency-free byte-deterministic PNG writer
  (`png.h/.c`: stored deflate blocks, CRC/Adler, tEXt label). `gxprint`
  accepts `.ppm`/`.png`; PPM and PNG share the raster byte-for-byte.
  `test_render` (48 checks incl. CRC test vectors, chunk framing, stride
  regression) + CLI tests (png signature via `od`); instance-tested on
  nClimGrid July-15 tmax (2420x2420: south-hot-light vs north-cool-dark,
  contour presence, magenta missing, PPM/PNG identity) with a permanent
  `nclimgrid_render_plot` regression test. Left for later: vector/text/
  map primitives, compressed PNG, `set gxout` display modes.
- Done when: commands produce byte-comparable output files in CI.

## M6 — Script Compatibility

- Replace `interp/interp.h` duplicate enums with evaluator + command dispatch
  over the canonical AST; implement `say/pull/if/while/function`, `%var%`
  substitution, `run`.
- Done when: a corpus of representative `.gs` scripts executes correctly.

## M7 — Compatibility Expansion

- Vendor reference script/data corpus with expected outputs; numerical and
  structural comparison (not pixel comparison).
- Done when: compatibility matrix in COMPATIBILITY.md is measured, not asserted.

## M8 — Performance

- Profile real workloads (large TDEF reads, expression chains); optimize only
  measured bottlenecks: chunked I/O, lazy eval, parallelism.
- Done when: benchmark suite in `benchmarks/` with before/after numbers.

## M9 — Production Release

- Stable CLI, packages, CI matrix (Linux/macOS/Windows), migration guide,
  compatibility matrix, benchmark suite.

## Non-goals per milestone

- M1: no NetCDF/GRIB/HDF, no Cairo, no Python bridge, no contours.
- No new dependency enters the default build without a fallback; anything
  platform-specific stays behind the platform layer.
