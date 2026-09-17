# GrADS-NG Compatibility (M0)

Compatibility is layered per the project goal. This file records the M1
baseline: what is promised, what holds today, and what is intentionally absent.

## Layer 1 — Core syntax (M1: partial)

| Command | M1 status | Notes |
|---|---|---|
| `open file.ctl` | ✅ full descriptor | parsed by `ng_ctl_parse`; failures name the reason |
| `q file` | ✅ summary | prints dims + var count + names |
| `d var` | ✅ values summary | t=1,z=1 slice stats (UNDEF-aware); plots in M5, expressions in M3 |
| `quit` / `exit` | ✅ | leaves REPL / ends `-c` |
| `help` | ✅ | lists supported commands |
| `set t/z`, `q dims` | ✅ selection | 1-based validated indices on the default file |
| `sdfopen`, `xdfopen`, `close`, `define`, `clear`, `draw`, `print`, `enable`, `disable`, `reinit`, `reset` | ❌ | parse-level rejection with a helpful error; see ROADMAP M4–M6 |

Unknown commands fail with the command name echoed plus the supported list —
never a bare "Error occurred."

## Layer 2 — Expression language (M3: elementwise over the selected slice)

Supported: `+ - * / ^` (yes, `^` is power, matching GrADS), parentheses, unary
minus, comparisons, `&&`, `||`, `!`, scientific notation (`1.5e-3`),
variable references, scalar broadcast, single-argument calls
(`abs/sqrt/exp/log`-natural/`sin/cos`, case-insensitive), and two-argument
elementwise `max/min/pow` (max/min let a lone missing lose to valid data).
`grads-ng -e '<expr>'` still evaluates pure-scalar arithmetic; `d <expr>`
evaluates elementwise over the selected (t,z) slice.
Missing values (UNDEF→NaN) propagate through every operator, comparison,
and function; division by zero, overflow, and domain errors yield missing
rather than failing the display. Unknown variables/functions are hard errors.

Not yet supported: aggregation (`ave`, … — needs range syntax, M4),
dimension slicing/subscripts, temporal/spatial ops, ensemble handling.

Operator precedence (matches GrADS): unary → `^` → `*`/`/` → `+`/`-` →
comparisons → `&&` → `||`.

## Layer 3 — Scripts (M1: none executed)

`.gs` files are accepted on the command line and parsed for validation, but
control flow is not executed yet (M6). `say`/`pull`/`if`/`while` lex and parse;
the VM entry `grads_ng_vm_exec` is a stub that returns success without acting.
No script may silently do the wrong thing: unexecuted constructs report
"not yet implemented" rather than fake results.

## Layer 4 — Datasets (M2 partial: full descriptor, no data reads yet)

`ng_ctl_parse()` implements the `ng_ctl_t` contract in `ng/src/io/ctl.h`:
DSET/TITLE/UNDEF, LINEAR + LEVELS X/Y/Z axes, TDEF/EDEF counts, VARS entries
(name/levels/GRIB code/long name), DTYPE mapping, OPTIONS
(byteswapped/sequential/template), and `%y4`-style template expansion.
Honest rejections (not silent ignores): PDEF, unknown directives/DTYPEs,
unsupported OPTIONs, unclosed VARS, wrapped EDEF names. Binary reads work for
non-template BINARY datasets (stream + sequential, UNDEF passes through
unmasked); templated/non-binary descriptors are rejected pending the time
axis (M3/M4) and format adapters. Still open: absolute TDEF time axis,
NetCDF/GRIB/HDF adapters (never linked into the core engine).

## Layer 5 — Behavioral compatibility (M1: unmeasured)

No reference corpus exists in-repo (no `upstream/`, no tutorial data). M7 will
vendor a small script/data corpus with expected outputs; until then, every
quirk we preserve gets a regression test at the time it is implemented.

## Intentional incompatibilities (standing)

1. `grads-ng` starts headless/batch-friendly; no X11 init is ever attempted.
2. `^` in expressions is power (as in GrADS); it is never bitwise XOR.
3. The CLI adds `-e` (evaluate expression) and `-f` (inspect descriptor),
   which have no GrADS 2.2.1 equivalents — debugging aids, not replacements.
4. PNG/SVG export will be driven by explicit output commands/flags, never by
   display-server state.
