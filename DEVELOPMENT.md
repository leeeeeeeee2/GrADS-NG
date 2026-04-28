# GrADS-NG Development System

A modernized GrADS implementation with cross-platform support and native headless mode.

## Overview

This project modernizes the official GrADS (Grid Analysis and Display System) core engine for contemporary platforms while preserving scientific correctness and behavioral compatibility.

## Project Structure

```
/workspaces/GrADS-NG/
|   
├── ng/                     # Next Generation implementation (NEW)
│   ├── include/            # Public API headers
│   ├── src/
│   │   ├── gs_parser.c    # Script lexer/parser  
│   │   ├── gx_virtual.c # Virtual display backend
│   │   ├── platform.c  # Cross-platform layer
│   │   └── main.c     # CLI entry point
│   ├── build/             # CMake build output
│   ├── tests/            # Test suite
│   ├── README.md
│   └── CMakeLists.txt
│
├── grads-2.2.1/          # Modernized upstream GrADS 2.2.1
│   ├── src/             # Modified source
│   ├── build/           # CMake build output
│   └── CMakeLists.txt
│
├── upstream/             # Original GrADS (reference only)
│   ├── example/         # Tutorial data
│   ├── docs/           # Documentation
│   ├── data/           # Sample datasets
│   └── cache/          # Downloaded tarballs
│
├── scripts/             # Build/utility scripts
├── .github/            # CI configuration
└── .gitignore
```

## Quick Start

### Build NG

```bash
cd ng/build
cmake ..
make

# Run
./grads-ng -b              # Batch mode
./grads-ng script.gs       # Execute script
./grads-ng -g 800x600     # Virtual display size
```

### Build GrADS 2.2.1

```bash
cd grads-2.2.1/build
cmake .. -DENABLE_HEADLESS=ON
make

# Run original GrADS
./grads -b -c "open model.ctl"
```

## Architecture

### NG Components

| Component | Purpose |
|----------|---------|
| `gs_parser.c` | Parse `.gs` scripts |
| `gx_virtual.c` | Off-screen RGBA buffer |
| `platform.c` | Cross-platform paths/threads |

### CLI Options

| Option | Description |
|--------|-----------|
| `-b` | Batch mode (no GUI) |
| `-g WxH` | Canvas size |
| `-h` | Help |

## Testing

```bash
# Run tests
cd ng/build
make test

# With GrADS 2.2.1
../../grads-2.2.1/build/grads -b -c "open ../../upstream/example/model.ctl"
```

## Development

### Adding Features

1. Expression engine → Reference `gaexpr.c`
2. Python bindings → Use ctypes  
3. Zarr support → Add to `src/io/`

### Git Workflow

```bash
# Track only NG changes
git add ng/
git add .gitignore
git add CLAUDE.md
git commit -m "NG: initial implementation"
```

## Compatibility

| Feature | grads-2.2.1 | ng |
|---------|-------------|-----|
| Batch mode | ✅ | ✅ |
| Virtual display | ❌ | ✅ |
| Script parser | Original | ✅ (new) |
| Cross-platform | Partial | ✅ |

## License

GrADS copyright George Mason University. NG modifications follow same license.