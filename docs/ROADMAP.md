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
- [x] M4 slice 1: multi-argument `max/min/pow` elementwise (scalar + array),
  max/min skip lone missing, `test_eval` +9, `test_expr` +7, CLI `d max`;
  `ctest` 25/25 green, zero warnings.
- [ ] M4 slice 2: time/ensemble aggregation (`ave` needs t-range syntax).
- [ ] Slicing/subscript dimensions beyond the selected (t,z).
- Done when: `(tsfc-273.16)*9/5+32`-class expressions match reference values.

## M4 — Core Analysis

- Most-used ops only (driven by real scripts): averaging, masking,
  interpolation stubs hardened into implementations.
- Done when: each op has unit + integration tests against synthetic data.

## M5 — Rendering

- Implement `render/canvas.h` with a dependency-free software rasterizer
  (PPM/PNG via miniz or stb-style single file); Cairo backend optional.
- Primitives: line, contour, shaded, vector, text, map frame; `print`/`gxprint`.
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
