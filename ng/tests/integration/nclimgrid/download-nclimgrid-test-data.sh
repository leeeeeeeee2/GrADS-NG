#!/bin/bash
# download-nclimgrid-test-data.sh - acquire and verify the nClimGrid-Daily
# external test fixture, then build the converted GrADS-NG fixture.
#
#   ./download-nclimgrid-test-data.sh [--check-only]
#
# Layout (never inside the repo):
#   ${NCLIMGRID_CACHE:-~/.cache/grads-ng-test-data}/
#     <archive>.tar.gz          # downloaded archive (kept as cache)
#     nclimgrid-jul1962/        # extracted NetCDF payload
#     conv/                     # converted classic-GrADS fixture (julall.ctl/.dat)
#
# Exit 0 prints the payload dir on the last line. Prints "SKIP: <reason>"
# and exits 0 when the fixture cannot be acquired (offline CI): CTest marks
# those runs skipped via SKIP_REGULAR_EXPRESSION, never silently passed.
# Any present-but-corrupt state is a hard failure (exit 1).
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$HERE/fixture.conf"

CACHE="${NCLIMGRID_CACHE:-$HOME/.cache/grads-ng-test-data}"
ARCHIVE="$CACHE/$NCLIMGRID_ARCHIVE_NAME"
PAYLOAD="$CACHE/$NCLIMGRID_EXTRACT_DIR"
CHECK_ONLY=0
if [[ "${1:-}" == "--check-only" ]]; then CHECK_ONLY=1; fi

fail() { echo "FAIL: $1" >&2; exit 1; }
skip() { echo "SKIP: $1"; exit 0; }

need() { command -v "$1" >/dev/null 2>&1 || fail "required tool '$1' not found"; }
need curl; need tar; need sha256sum; need gzip

verify_bytes() { # path expected_size
    local got
    got="$(stat -c%s "$1")" || return 1
    [[ "$got" == "$2" ]]
}

if [[ ! -f "$ARCHIVE" ]]; then
    [[ $CHECK_ONLY == 1 ]] && skip "archive not cached at $ARCHIVE (offline?)"
    echo "downloading $NCLIMGRID_TEST_URL" >&2
    mkdir -p "$CACHE"
    if ! curl -fL --retry 2 -o "$ARCHIVE.part" "$NCLIMGRID_TEST_URL"; then
        rm -f "$ARCHIVE.part"
        skip "download failed (offline or NCEI unreachable)"
    fi
    mv "$ARCHIVE.part" "$ARCHIVE"
else
    echo "using cached archive $ARCHIVE" >&2
fi

verify_bytes "$ARCHIVE" "$NCLIMGRID_ARCHIVE_SIZE" \
    || fail "archive size mismatch (incomplete download?) at $ARCHIVE"
echo "$NCLIMGRID_ARCHIVE_SHA256  $ARCHIVE" | sha256sum -c - \
    || fail "archive sha256 mismatch (corrupt download) at $ARCHIVE"
gzip -t "$ARCHIVE" || fail "archive is not valid gzip: $ARCHIVE"

mkdir -p "$PAYLOAD"
for spec in "$NCLIMGRID_NC_TMAX" "$NCLIMGRID_NC_TMIN" "$NCLIMGRID_NC_TAVG" \
            "$NCLIMGRID_NC_PRCP" "$NCLIMGRID_NC_NCDD" "$NCLIMGRID_VERSION_TXT"; do
    name="${spec%%:*}"; size="${spec##*:}"
    if [[ ! -f "$PAYLOAD/$name" ]] || ! verify_bytes "$PAYLOAD/$name" "$size"; then
        [[ $CHECK_ONLY == 1 ]] && fail "payload file missing/wrong size: $name"
        echo "extracting $name" >&2
        tar -xzf "$ARCHIVE" -C "$PAYLOAD" "$name" \
            || fail "extraction failed for $name"
        verify_bytes "$PAYLOAD/$name" "$size" \
            || fail "extracted size mismatch for $name"
    fi
done
# NetCDF magic check (HDF5 signature for these NetCDF-4 files).
for nc in "$PAYLOAD"/*.nc; do
    head -c 4 "$nc" | grep -q $'^\211HDF' \
        || fail "not a NetCDF-4 file: $nc"
done

if [[ $CHECK_ONLY == 1 ]]; then echo "fixture OK: $PAYLOAD" >&2; echo "$PAYLOAD"; exit 0; fi

# Converted classic-GrADS fixture for the NG path (needs netCDF4).
# Prefer the project venv (see repo .venv), then system python3.
PY=python3
ROOT="$(cd "$HERE/../../../.." && pwd)"
if [[ -x "$ROOT/.venv/bin/python" ]] \
    && "$ROOT/.venv/bin/python" -c "import netCDF4, numpy" 2>/dev/null; then
    PY="$ROOT/.venv/bin/python"
fi
export NCLIMGRID_PYTHON="$PY"
if "$PY" -c "import netCDF4, numpy" 2>/dev/null; then
    "$PY" "$HERE/make_fixture.py" "$PAYLOAD" "$CACHE/conv" \
        || fail "fixture conversion failed"
else
    echo "WARNING: python3+netCDF4 unavailable; skipping converted fixture" >&2
    echo "(NG-path tests will SKIP until conv/julall.ctl exists)" >&2
fi

echo "$PAYLOAD"
