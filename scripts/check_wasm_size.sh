#!/usr/bin/env bash
#
# WASM size regression gate. Suzume ships to the browser, so binary size is a product
# constraint: L1 entries, hardcoded lists, and static data all land in the .wasm and
# nobody notices until release. This fails when the built artifact exceeds the committed
# baseline by more than TOLERANCE_PCT, and always prints the current size + delta.
#
# Usage:
#   scripts/check_wasm_size.sh <path-to.wasm>          # check against baseline
#   scripts/check_wasm_size.sh <path-to.wasm> update   # set baseline to current size

set -eu
cd "$(git rev-parse --show-toplevel)"

WASM="${1:?usage: check_wasm_size.sh <path-to.wasm> [update]}"
MODE="${2:-check}"
BASELINE="scripts/wasm-size-baseline.txt"
TOLERANCE_PCT=5

[ -f "$WASM" ] || { echo "❌ wasm artifact not found: $WASM"; exit 1; }
size=$(wc -c < "$WASM" | tr -d ' ')

if [ "$MODE" = "update" ]; then
  echo "$size" > "$BASELINE"
  echo "✅ wasm-size baseline set to $size bytes ($((size/1024)) KB)"
  exit 0
fi

[ -f "$BASELINE" ] || { echo "❌ missing $BASELINE (run: scripts/check_wasm_size.sh $WASM update)"; exit 1; }
base=$(tr -d ' \n' < "$BASELINE")
limit=$(( base + base * TOLERANCE_PCT / 100 ))
delta=$(( size - base ))
pct=$(( delta * 100 / base ))

printf 'wasm size: %d KB (baseline %d KB, delta %+d KB / %+d%%)\n' \
  "$((size/1024))" "$((base/1024))" "$((delta/1024))" "$pct"
[ -n "${GITHUB_STEP_SUMMARY:-}" ] && \
  printf '### WASM size\n%d KB (baseline %d KB, %+d%%)\n' "$((size/1024))" "$((base/1024))" "$pct" >> "$GITHUB_STEP_SUMMARY"

if [ "$size" -gt "$limit" ]; then
  echo "❌ wasm grew more than ${TOLERANCE_PCT}% over baseline."
  echo "   Investigate (new static data / L1 entries / hardcoded lists)."
  echo "   If intentional, run: scripts/check_wasm_size.sh $WASM update"
  exit 1
fi
echo "✅ wasm size within +${TOLERANCE_PCT}% of baseline"
