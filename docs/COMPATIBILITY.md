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
| `set x A [B]`, `set y A [B]` | ✅ grid windows | one index fixes, two select an ascending in-file window; fractional positions snap round-half-up (`set x 1.5` → grid 2); every `d` evaluation clips to it; `q dims` reports `A..B of N`; echoes reference-style world values (`LON set to 0 90`) |
| `set lon/lat/lev` | ✅ world snapping | LINEAR: round-half-up of the inverse map; LEVELS: nearest entry (observed tie → higher index); strict in-file storage; `lev` takes one value; `set z` echoes `LEV set to …` the same way |
| `set time WHEN` | ✅ absolute snap | `[HH[MM]Z]DDMMMYYYY` (case-insensitive, 2-digit years pivot at 50), nearest step ties up, strict half-step window, reference-style `Time values set:` echo; `q dims` shows `Time = 00Z03JAN1987`; two-valued ranges need the varying-T path |
| `set e` | ✅ reference-verified | one numeric token, never validated (`E set to 2 2`; `2.0`/`+2` stick, bare/non-numeric/trailing-junk/names fail with `SET error: Missing or invalid arguments for E option`); EDEF member names parsed (same-line or following-line) and shown in `q dims` (`Ens = memB`, `(null)` when out of range, number when unnamed); out-of-range `d` warns and degrades to missing, matching the reference — all probed against 2.2.1.oga.1 |
| `close N` | ✅ reference-verified | requires a number; only the last open file may close (`Missing/Invalid file number`, `Only last file may be closed`, `File N has been closed` — all probed against 2.2.1.oga.1); trailing words ignored |
| `sdfopen`, `xdfopen`, `define`, `clear`, `draw`, `print`, `enable`, `disable`, `reinit`, `reset` | ❌ | parse-level rejection with a helpful error; see ROADMAP M4–M6 |

Unknown commands fail with the command name echoed plus the supported list —
never a bare "Error occurred."

## Layer 2 — Expression language (M3: elementwise over the selected slice)

Supported: `+ - * / ^` (yes, `^` is power, matching GrADS), parentheses, unary
minus, comparisons, `&&`, `||`, `!`, scientific notation (`1.5e-3`),
variable references, scalar broadcast, single-argument calls
(`abs/sqrt/exp/log`-natural/`sin/cos`, case-insensitive), two-argument
elementwise `pow`, and dimension reductions `max/min/ave/sum(expr, dim=a,
dim=b)` over `t`/`z` index ranges (`lev` accepted as a `z` alias).
`grads-ng -e '<expr>'` still evaluates pure-scalar arithmetic (reductions
are rejected there with "reduces over a dimension range; use d"); `d <expr>`
evaluates elementwise over the selected (t,z) slice.
Missing values (UNDEF→NaN) propagate through every operator, comparison,
and function; division by zero, overflow, and domain errors yield missing
rather than failing the display. Reductions skip missing values (mean/sum
over the valid steps, max/min over the valid extreme); an all-missing
element stays missing. Unknown variables/functions are hard errors.

Correction (reference-probed): GrADS `max`/`min` are reductions only —
`max(hgt,100)` and `max(hgt,tsfc)` both fail in 2.2.1.oga.1 with
"Too many or too few args", so NG's earlier elementwise `max/min` was
removed in favor of exact parity. `pow` stays elementwise (reference plots
`pow(hgt,2)`).

Reduction bounds are 1-based grid indices and must ascend inside the file;
`x`/`y`/`e` ranges, multi-dimension calls, and world-coordinate bounds are
honest errors. Intentional deviation: the reference echoes out-of-range
bounds (`t=1,t=9` on a 2-step file) instead of failing; NG rejects them
and names the valid range, since averaging steps the file does not hold
cannot be made scientifically meaningful.

Not yet supported: `x`/`y`/`e` reduction ranges, multi-dimension
reductions, world-coordinate reduction bounds, varying `t`/`z`
display, dimension subscripts in `d`, relative time offsets, spatial ops.

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
