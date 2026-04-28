# GrADS-NG Test Suite

## Test Data

Tests use the GrADS tutorial data from upstream:

- `upstream/example/model.ctl` - GrADS descriptor file
- `upstream/example/model.dat` - Binary data (72x46, 7 levels, 5 days)

### Data Format

```
DSET   ^model.dat
XDEF 72 LINEAR  0.0 5.0      # 72 grid points, 0 to 355 degrees
YDEF 46 LINEAR  -90.0 4.0    # 46 grid points, -90 to 90 degrees
ZDEF 7 LEVELS 1000 850 700 500 300 200 100
TDEF 5 LINEAR 02JAN1987 1DY    # 5 days starting Jan 2, 1987
VARS 8
ps     0   99   Surface Pressure
u      7   99   U Winds
v      7   99   V Winds
hgt    7   99   Geopotential Heights
tair   7   99   Air Temperature
q      5   99   Specific Humidity
tsfc   0   99   Surface Temperature
p      0   99   Precipitation
ENDVARS
```

## Functional Tests

### Test 1: Build Verification

```bash
cd ng/build
make
# Expected: no errors
./grads-ng --version
```

### Test 2: CLI Help

```bash
./grads-ng -h
# Expected: shows help message
```

### Test 3: Batch Mode

```bash
./grads-ng -b
# Expected: "Batch mode: exporting results..."
```

### Test 4: Virtual Display

```bash
./grads-ng -g 800x600
# Expected: "Virtual display: 800x600 pixels"
```

### Test 5: GrADS 2.2.1 Open Descriptor

```bash
../../grads-2.2.1/build/grads -b -c "open ../../upstream/example/model.ctl"
# Expected: file opens successfully
```

### Test 6: Query File

```bash
../../grads-2.2.1/build/grads -b -c "open ../../upstream/example/model.ctl
query file"
# Expected: displays file information
```

### Test 7: Display Variable

```bash
../../grads-2.2.1/build/grads -b -c "open ../../upstream/example/model.ctl
d ps"
# Expected: displays surface pressure
```

### Test 8: Expression Evaluation

```bash
../../grads-2.2.1/build/grads -b -c "open ../../upstream/example/model.ctl
d ps-1000"
# Expected: displays ps - 1000
```

### Test 9: Dimension Commands

```bash
../../grads-2.2.1/build/grads -b -c "open ../../upstream/example/model.ctl
set lon -90
set lat 40
set lev 500
d hgt"
# Expected: single value output
```

### Test 10: GRIB Index Creation

```bash
cd ../../upstream/example
../../grads-2.2.1/build/gribmap -i model.ctl
ls -la model.idx
# Expected: index file created
```

## Regression Tests

### Test 11: Numerical Accuracy

Compare output between original GrADS and NG for expression `(tsfc-273.16)*9/5+32`:

```bash
# Original
../../grads-2.2.1/build/grads -b -c "open ../../upstream/example/model.ctl
d tsfc
set lon -90
set lat 40
d (tsfc-273.16)*9/5+32"

# NG (when implemented)
./grads-ng -e "(tsfc-273.16)*9/5+32"
```

## Performance Tests

### Test 12: Large Data Access

```bash
time ../../grads-2.2.1/build/grads -b -c "open ../../upstream/example/model.ctl
set t 1 5
d hgt"
```

## Compatibility Matrix

| Feature | grads-2.2.1 | ng |
|---------|---------------|-----|
| Open .ctl | ✅ | ✅ (stub) |
| Display variable | ✅ | ❌ |
| Batch mode | ✅ | ✅ |
| Expression eval | ✅ | ❌ |
| Vector display | ✅ | ❌ |
| Contour plot | ✅ | ❌ |
| Script (.gs) | ✅ | ✅ (parser) |

## Running All Tests

```bash
# Via Makefile test target
cd ng/build
make test

# Or manually
cd ../..
for test in test*.gs; do
    echo "Running $test..."
    ./ng/build/grads-ng -b "$test"
done
```