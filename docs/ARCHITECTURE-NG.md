# OpenGrADS-NG Target Architecture

Target subsystem boundaries from the project goal, mapped to the actual
`ng/` files that implement (or will implement) each layer. This is a
direction of travel, not a reorganization order: milestones migrate
feature-by-feature and every step stays buildable (`cmake --build`) and
tested (`ctest`). Current per-layer status lives in `COMPATIBILITY.md`;
sequencing in `ROADMAP.md`; measured starting point in `NG_BASELINE.md`.

```text
                    CLI  (ng/src/main.c: grads-ng, -b/-c/-e/-f, REPL)
                     |
                     v
              Command Parser  (dispatch in main.c exec_command;
                     |         canonical AST: parser/grads_ng_parser.h)
                     v
              Command Runtime  (open/q/d/set/close today; M6: run/if/while)
                     |
          +----------+----------+
          |                     |
          v                     v
    Expression Engine      Session State
    (parser/: lexer ->    (core/grads_ng.c: grads_ng_session_t,
     AST -> scalar eval;   open files, error slot; dim selection
     eval/array.c: array   moving onto grads_ng_file_t)
     operations)
          |
          v
      Data Model  (grads_ng_file_t / grads_ng_var_t: dims, UNDEF,
          |        borrowed handles; io/ctl.h: ng_ctl_t descriptor)
   +------+------+------+
   |             |      |
   v             v      v
Data Sources  Coordinates Metadata
(io/grid.c:    (core dim     (ctl TITLE/VARS
 BE/LE binary,  selection;    long names; DTYPE/
 stream/seq;    X/Y/Z/T/E     OPTIONS; UNDEF per
 M4+: NetCDF/   centraliza-   variable; attributes
 GRIB adapters  tion: M4)     API: M4+)
 never linked
 into core)
   |
   v
Analysis / Numerical Engine  (M4: ave/masking/interp over ng_array_t;
   |                          missing-aware by construction, NaN = UNDEF)
   v
Rendering API  (render/canvas.h contract; value summaries today)
   |
   +---------+---------+
   |         |         |
   v         v         v
GX/Legacy  Modern    Future
Backend    Backend   Backends
 excluded   first:      Cairo/NetCDF
 from       software    optional, never
 default    rasterizer  in default build
 build      (M5)
```

## Layer principles (binding on new work)

1. **Core never learns file formats.** The expression engine reads through
   `grads_ng_var_slice()` (per `(t,z)` slice, `x` fastest, UNDEF→NaN at the
   boundary). Format adapters (`io/`, future NetCDF/GRIB) sit below that
   call; nothing above it branches on DTYPE.
2. **Missing values are a type-level fact.** NaN = missing flows through
   every operator, comparison, and function (verified in `test_eval`);
   domain errors (0/0, overflow, `sqrt(-1)`) yield missing, never abort the
   display. Never materialize missing as zero at any layer.
3. **Reference behavior is the spec.** Before a command migrates off the
   `ng_future_cmds` rejection list, its edge cases are probed against the
   2.2.1.oga.1 bundle and recorded in `NG_BASELINE.md` §6 (as done for
   `close N`). Quirks we preserve get a regression test at implementation
   time; intentional deviations are documented in `COMPATIBILITY.md`.
4. **State moves into the session, not into globals.** New state goes on
   `grads_ng_session_t` / `grads_ng_file_t` behind the opaque handles in
   `grads_ng.h`; the CLI keeps only its file-number table. The two platform
   layers (`backend/platform.*` for core I/O, `src/platform/` `ng_*` API)
   merge at M2 once mmap ownership settles — not before.
5. **No dependency enters the default build without a fallback.**
   Platform specifics stay behind the platform layer; rendering backends
   stay behind `render/canvas.h`.
