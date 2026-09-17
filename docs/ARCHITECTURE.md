# GrADS-NG Architecture (M0 — as observed, Sep 2026)

## 1. What this repository actually is

- **New project, not a GrADS fork.** No upstream GrADS/OpenGrADS source is vendored
  (`upstream/`, `grads-2.2.1/` referenced by `DEVELOPMENT.md` do not exist in the repo).
  There is no `.ctl` tutorial data, no `gaexpr.c`, no graphics engine to preserve.
  Behavioral compatibility therefore has no in-repo reference implementation yet.
- **Language/toolchain:** C11, CMake ≥ 3.16, subtracted to the C standard library
  plus `libm`. No Python, NetCDF, Cairo, or X11 in the default build path.
- **Entry point:** `ng/src/main.c` → `grads-ng` CLI (`-b/-c/-e/-f/-o/-h/-v`, REPL).
- **Public API:** `ng/include/grads_ng.h` (session, file, canvas, expr, scripting).

## 2. Module map (actual files)

```text
ng/src/main.c                  CLI: dispatch (open/q/d/set/help), REPL
ng/src/core/grads_ng.c         Session, descriptor-backed files, variables,
                               dimension selection (all in libngcore)
ng/src/eval/array.c            Elementwise expression evaluator, NaN=missing
ng/src/parser/grads_ng_parser.c  Lexer → AST → scalar eval + math functions
ng/src/io/ctl.c / grid.c       Descriptor parser + mmap binary reader (libngio)
ng/src/backend/platform.c      OS layer used by core (paths, files, mmap)
ng/src/platform/platform.c     Second OS layer (ng_* API; libngplatform)
ng/src/gs_parser.c             LEGACY file-based .gs tokenizer (own main-era CLI)
ng/src/gx_virtual.c            LEGACY RGBA canvas + PNG export (needs cairo/libpng)
ng/src/grads_ng.c              LEGACY standalone main (old -b/-g CLI)
ng/src/interp/interp.h         CONTRACT ONLY: .gs interpreter front-end (no .c yet)
ng/src/render/canvas.h         CONTRACT ONLY: inch-coordinate canvas API (no .c yet)
ng/tests/                      test_expr/core/ctl/grid/eval + CLI suite (ctest)
```

## 3. Problems found (M0)

1. **Build is broken.** Top-level `CMakeLists.txt` compiles the three legacy
   files; `gx_virtual.c` unconditionally `#include`s `<cairo.h>`/`<png.h>`,
   which are not declared as dependencies and are absent on a stock machine.
2. **Two CLIs, two parsers, two platform layers.** `src/grads_ng.c` vs
   `src/main.c`; `src/gs_parser.c` vs `src/parser/grads_ng_parser.c` (plus a
   third, conflicting token enum in `src/interp/interp.h`); `src/backend/`
   (`platform_*`) vs `src/platform/` (`ng_*`). Only one of each can survive.
3. **Wrong include:** `src/core/grads_ng.c` includes `../core/platform.h`,
   which does not exist; it means `backend/platform.h`.
4. **Opaque-struct violation:** `src/main.c` reads `file->nx` etc., but
   `grads_ng_file_t` is forward-declared opaque in the public header.
5. **Lexer gaps:** `&`, `|`, `!` rejected as "unknown character" (so `&&`, `||`,
   `!` can never lex); `parse_factor` drops the `*`/`/`/`^` operator;
   operators stored via `(char)tokentype` truncation. No evaluator exists —
   `-e` only reports "parsed successfully".
6. **No tests, no docs/.** `ng/docs/` is empty; there is no `docs/` tree.

## 4. Decisions (kept minimal, reversible)

- **Keep the `main.c` + `core` + `parser/grads_ng_parser.c` stack** as the
  canonical M1 line. Demote `src/grads_ng.c`, `src/gs_parser.c`,
  `src/gx_virtual.c` to legacy (excluded from the default build until the
  renderer milestone; tracked in ROADMAP).
- **Keep both platform layers for now** with distinct roles: `backend/platform.h`
  serves core I/O; `src/platform/` (`ng_*`) is the2013 forward-looking portable
  API for mmap/dlopen. Merge them at M2 when the data layer needs mmap.
- **Canonical script IR is `parser/grads_ng_parser.h`** (`grads_ng_ast_node_t`).
  `interp/interp.h`'s duplicate token/AST enums are superseded; they will be
  replaced by an evaluator + command-dispatch header at M3/M6, not patched.
- **Contracts stay:** `io/ctl.h`, `render/canvas.h` are good boundaries and are
  implemented in M2/M5 against real backends (miniz + software rasterizer first,
  Cairo/NetCDF optional later).
- **No new dependencies in M1.** Cairo/libpng/NetCDF stay optional and out of
  the default build so `cmake && ctest` works on a bare toolchain.

## 5. Target layering (unchanged from goal, mapped to files)

```text
CLI (main.c) → Command dispatch (new: core/commands) → Session (core)
  → Expression engine (parser/: lexer → AST → eval)
  → Data/variable model (core + io/ctl) → Analysis (new)
  → Rendering API (render/canvas.h) → backends (software first, cairo later)
Platform (backend/platform.c, src/platform/) underpins core and io only.
```

## 6. M1 vertical slice (definition of done)

`grads-ng -e '<arith>'` prints a numeric result; `-f file.ctl` prints real
dims; `-c` and the REPL execute `open`/`q file`/`quit` with GrADS-style errors;
`ctest` covers lexer/parser/eval/CLI; this doc set exists. See ROADMAP.md.
