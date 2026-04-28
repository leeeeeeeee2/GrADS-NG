# GrADS-NG

Next-generation GrADS: a clean-room reimplementation of the Grid Analysis and Display System with a modern C core, decoupled renderer, and 100% `.gs` script compatibility.

---

## Why

Original GrADS 2.2.1 is tightly coupled to X11, compiled against decade-old glibc, and requires Cygwin on Windows. GrADS-NG solves the three immediate pain points:

| Pain point | GrADS-NG fix |
|---|---|
| X11 hard-dependency | Off-screen RGBA canvas — no display server needed |
| Cygwin on Windows | Native MSVC/MinGW build, `C:\` paths work as-is |
| Rosetta 2 on Apple Silicon | Native ARM64 target, no x86 fallback required |
| Headless servers crash | Default batch mode, PNG/SVG output, zero GUI init |

---

## Architecture

```
 .gs script
     │
     ▼
 ┌──────────┐   tokens   ┌──────────┐   AST   ┌──────────┐
 │  Lexer   │──────────▶│  Parser  │────────▶│   VM     │
 └──────────┘           └──────────┘         └────┬─────┘
                                                   │ command dispatch
                                                   ▼
                                          ┌─────────────────┐
                                          │  Command Table  │
                                          │ open/set/display│
                                          └────┬────────────┘
                                               │             │
                                    ┌──────────▼──┐   ┌─────▼──────┐
                                    │  CTL Reader │   │  Canvas    │
                                    │  (io/ctl.c) │   │ (canvas.c) │
                                    └─────────────┘   └─────┬──────┘
                                                            │
                                               ┌────────────▼──────────┐
                                               │   Cairo image surface  │
                                               │   (backend_cairo.c)    │
                                               └────────────┬──────────┘
                                                            │
                                                     PNG / SVG / RGBA
```

---

## Directory layout

```
ng/
├── CMakeLists.txt             # Top-level build
├── include/
│   ├── grads_ng.h             # Public C API (session, file, canvas, expr)
│   ├── gs_parser.h            # GS script parser types
│   ├── gx_virtual.h           # Virtual display context
│   └── backend/platform.h     # Platform portability layer
├── src/
│   ├── main.c                 # CLI: -b/-c/-e/-f/-o flags, REPL
│   ├── core/grads_ng.c        # Session, file open/close, canvas, stubs
│   ├── parser/grads_ng_parser.c  # Full lexer + recursive-descent parser + AST + VM
│   ├── backend/platform.c     # mmap, dlopen, mkdir_p, path normalize
│   ├── gs_parser.c            # Line-oriented GS tokenizer
│   ├── gx_virtual.c           # RGBA off-screen canvas + GrADS 16-color palette
│   ├── interp/interp.h        # Interpreter contract (ng_interp_t)
│   ├── io/ctl.h               # CTL descriptor parser contract (ng_ctl_t)
│   ├── render/canvas.h        # Canvas API contract (inch coordinates)
│   └── platform/platform.c   # Cross-platform portability implementation
├── tests/
└── docs/
```

---

## What is implemented

### Lexer (`src/parser/grads_ng_parser.c`)
- Full tokenization of GrADS scripting language
- Keywords: `say`, `pull`, `if`/`else`/`elseif`/`endif`, `while`/`endwhile`,
  `for`/`endfor`, `break`, `continue`, `return`, `display`, `set`, `open`, `close`, `define`
- String literals (single and double quoted), numeric literals (integer, float, scientific notation)
- Two-character operators: `==`, `<=`, `>=`, `<>`, `&&`, `||`
- Comment handling (`** comment`)

### Parser + AST
- Recursive-descent parser producing typed AST nodes
- Node types: program, assign, if, while, break, return, say, pull, call, func\_def, binop, unop, number, string, ident
- Operator precedence: unary → power → mul/div → add/sub → comparison → AND → OR

### VM skeleton (`grads_ng_vm_t`)
- AST walker entry point `grads_ng_vm_exec()` — **next implementation target**

### Session and file API (`src/core/grads_ng.c`)
- `grads_ng_init()` / `grads_ng_destroy()` — session lifecycle with config
- `grads_ng_open()` — parses DSET, XDEF, YDEF, ZDEF, TDEF, VARS, DTYPE from `.ctl`
- `grads_ng_canvas_create()` — allocates RGBA pixel buffer with white background
- All rendering and expression functions present as safe stubs (return `-1`)

### Virtual display (`src/gx_virtual.c`)
- GrADS standard 16-color RGBA palette (matches original index-to-RGB mapping)
- Off-screen `uint32_t` pixel buffer with Bresenham line drawing
- Foundation for Cairo backend

### Platform layer (`src/platform/platform.c`)
- `ng_path_normalize()` — `/` ↔ `\` by platform
- `ng_path_is_absolute()` — handles Windows `C:\` and UNC paths
- `ng_mkdir_p()` — recursive mkdir, cross-platform
- `ng_dlopen()` / `ng_dlsym()` / `ng_dlclose()` — unified dynamic loader (Win32 + POSIX)
- `ng_mmap_open()` / `ng_mmap_close()` — memory-mapped files (Win32 + POSIX)

### CLI (`src/main.c`)
| Flag | Effect |
|---|---|
| `-b` / `--batch` | Headless mode |
| `-c CMD` | Execute one command string |
| `-e EXPR` | Parse and validate expression, report result |
| `-f FILE` | Open `.ctl` file, print dimension summary |
| `-o DIR` | Set output directory |
| `-v` | Print version |

---

## What is next (priority order)

1. **VM evaluator** — walk AST, evaluate arithmetic, string concat (`#`), variable lookup
2. **Variable store** — `var = expr`, `%var%` substitution in command strings
3. **CTL parser** — implement `ng_ctl_parse()` from contract in `src/io/ctl.h`
4. **Cairo canvas** — implement `ng_canvas_t` on `cairo_image_surface_t`, PNG export
5. **Command dispatch** — `open`, `set lat/lon/lev/time`, `display expr`, `draw title`
6. **Python bridge** — `run_python_script` via embedded CPython

---

## Building

### Linux / macOS
```bash
cd ng
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Linux headless (CI / server)
```bash
cmake -B build -DENABLE_HEADLESS=ON -DENABLE_CAIRO=ON
cmake --build build -j$(nproc)
```

### Windows (MSVC)
```cmd
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### Dependencies

| Library | Required | Purpose |
|---|---|---|
| zlib | yes | compression |
| libpng | yes | PNG output |
| Cairo | recommended | off-screen rendering, PNG/SVG |
| libnetcdf | optional | NetCDF data support |
| libhdf5 | optional | HDF5 data support |

---

## Usage
```bash
# Validate a .gs expression
./build/grads_ng -e '(temp-273.15)*1.8+32'

# Inspect a CTL file
./build/grads_ng -f /path/to/data.ctl

# Batch mode (headless, CI-safe)
./build/grads_ng -b -c 'open data.ctl'

# Run a GrADS script (unmodified from 2.2.1)
./build/grads_ng script.gs
```

---

## Compatibility commitment

Every valid GrADS 2.2.1 `.gs` script must produce identical scientific output on GrADS-NG.
Quirks in the original (whitespace tolerance, expression precedence edge cases, implicit
variable scoping) are **features to preserve**, not bugs to fix.

The behavioral reference is the official COLA/GMU GrADS 2.2.1 source at
`../upstream/src/grads-2.2.1/`, not OpenGrADS or downstream distro patches.
