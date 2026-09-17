#!/bin/bash
# run_gs.sh - execute one suite .gs script under a reference OpenGrADS.
#
#   REF_GRADS_BIN=/path/to/grads GADDIR=/path/to/SupportData \
#     run_gs.sh <script.gs> <payload_dir> <out_dir>
#
# The bundle's `run` command mangles script lines in batch mode, so the
# script is fed on stdin (equivalent for straight-line command scripts).
# __PAYLOAD__ in the script is replaced with the payload dir; outputs land
# in <out_dir>. Prints the log path. Exits nonzero only when grads itself
# cannot start; command errors are asserted from the log by the caller.
set -u
SCRIPT="$1"; PAYLOAD="$2"; OUTDIR="$3"
REF="${REF_GRADS_BIN:-}"
[[ -x "$REF" ]] || { echo "SKIP: REF_GRADS_BIN not set to a reference grads" >&2; exit 3; }
[[ -n "${GADDIR:-}" ]] || { echo "ERROR: GADDIR must point at SupportData" >&2; exit 1; }
mkdir -p "$OUTDIR"
# Headless batch runs need a GX plug-in table: alias display+print to the
# dummy GX library shipped next to the reference binary, unless the caller
# provided GAUDPT (e.g. a real Cairo/X11 setup for rendering tests).
if [[ -z "${GAUDPT:-}" ]]; then
    GEX="$(dirname "$REF")/gex"
    DUMMY=""
    for cand in "$GEX/libgxdummy.so" \
                "$(dirname "$REF")/../../Contents/Linux/Versions/2.2.1.oga.1/x86_64/gex/libgxdummy.so"; do
        [[ -f "$cand" ]] && DUMMY="$cand" && break
    done
    if [[ -n "$DUMMY" ]]; then
        { echo "gxdisplay  gxdummy  $DUMMY"
          echo "gxdisplay  Cairo    $DUMMY"
          echo "*"
          echo "gxprint    gxdummy  $DUMMY"
          echo "gxprint    Cairo    $DUMMY"; } > "$OUTDIR/udpt"
        export GAUDPT="$OUTDIR/udpt"
    fi
fi
BASE="$(basename "$SCRIPT" .gs)"
SUB="$OUTDIR/$BASE.sub.gs"
# `*` comment lines are only honored by `run`, not on stdin: drop them so
# logs stay error-clean (the .gs files keep them for readers).
sed -e "s|__PAYLOAD__|$PAYLOAD|g" -e "/^\*/d" "$SCRIPT" > "$SUB"
LOG="$OUTDIR/$BASE.log"
LD_LIBRARY_PATH="$(dirname "$REF")/gex:${LD_LIBRARY_PATH:-}" \
GAUDPT="${GAUDPT:-/tmp}" "$REF" -bp < "$SUB" > "$LOG" 2>&1
echo "$LOG"
