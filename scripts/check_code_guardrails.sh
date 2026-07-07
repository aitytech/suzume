#!/usr/bin/env bash
#
# Mechanical guardrails for the design principles in CLAUDE.md that were previously
# only enforced by convention. Ratchet-based: per analysis source file it counts
#   - surface_cmp:    surface-string equality comparisons (hardcoded word tests)
#   - score_literals: raw float score literals (magic numbers not via named constants)
#   - lines:          file length (single-file growth / split pressure)
# and fails if any metric EXCEEDS the committed baseline. Metrics may only go down;
# an intentional reduction is recorded by re-running with `update`, which shows up as
# a baseline diff in the commit.
#
# Also verifies WASM-core purity: no <fstream>/<thread>/<filesystem>/<mutex> in core
# layers outside the explicit __EMSCRIPTEN__-guarded allowlist.
#
# Portable to bash 3.2 (macOS) and bash 4+ (CI): no associative arrays, greps guarded.
#
# Usage:
#   scripts/check_code_guardrails.sh          # check (CI)
#   scripts/check_code_guardrails.sh update   # regenerate baseline after an intentional drop

set -eu
cd "$(git rev-parse --show-toplevel)"

BASELINE="scripts/guardrail-baseline.tsv"
MODE="${1:-check}"

count() { grep -oE "$1" "$2" 2>/dev/null | wc -l | tr -d ' '; }

metrics_for() {
  local f="$1" surf floats lines
  surf=$(count '(->|\.)surface *[!=]=' "$f")
  floats=$(count '[0-9]+\.[0-9]+F' "$f")
  lines=$(wc -l < "$f" | tr -d ' ')
  printf '%s\t%s\t%s\t%s\n' "$f" "$surf" "$floats" "$lines"
}

FILES=$(find src/analysis -name '*.cpp' | sort)

gen_baseline() {
  printf '# file\tsurface_cmp\tscore_literals\tlines\n'
  for f in $FILES; do metrics_for "$f"; done
}

# WASM-core purity: file-I/O / thread headers must never reach the WASM build. A file
# may use them only behind an __EMSCRIPTEN__ guard. We flag any file that includes such a
# header but contains NO __EMSCRIPTEN__ guard at all (self-maintaining: no manual allowlist).
PURITY_DIRS="src/core src/normalize src/analysis src/dictionary src/postprocess src/grammar src/pretokenizer"
# The .dic loaders read the (embedded) dictionary via fstream in every build; accepted.
PURITY_ALLOW="src/dictionary/binary_dict.cpp src/dictionary/user_dict.cpp"

check_purity() {
  local bad=0 hit
  for hit in $(grep -rlE '#include *<(fstream|thread|filesystem|mutex)>' $PURITY_DIRS 2>/dev/null || true); do
    grep -q '__EMSCRIPTEN__' "$hit" && continue
    case " $PURITY_ALLOW " in *" $hit "*) continue ;; esac
    echo "❌ WASM-core purity: $hit includes a file-I/O/thread header without an __EMSCRIPTEN__ guard"
    bad=1
  done
  return $bad
}

if [ "$MODE" = "update" ]; then
  gen_baseline > "$BASELINE"
  echo "✅ baseline written to $BASELINE"
  exit 0
fi

[ -f "$BASELINE" ] || { echo "❌ missing $BASELINE (run: scripts/check_code_guardrails.sh update)"; exit 1; }

fail=0
for f in $FILES; do
  cur=$(metrics_for "$f")
  s=$(echo "$cur"  | cut -f2); fl=$(echo "$cur" | cut -f3); l=$(echo "$cur" | cut -f4)
  base=$(grep -F "$(printf '%s\t' "$f")" "$BASELINE" || true)
  if [ -z "$base" ]; then
    echo "❌ new analysis file not in baseline: $f (run: update)"; fail=1; continue
  fi
  bs=$(echo "$base" | cut -f2); bf=$(echo "$base" | cut -f3); bl=$(echo "$base" | cut -f4)
  [ "$s"  -gt "$bs" ] && { echo "❌ $f surface comparisons $s > baseline $bs (generalize with grammar rules; don't add word tests)"; fail=1; } || true
  [ "$fl" -gt "$bf" ] && { echo "❌ $f raw score literals $fl > baseline $bf (use named constants in *_constants.h)"; fail=1; } || true
  [ "$l"  -gt "$bl" ] && { echo "❌ $f length $l > baseline $bl (extract helpers / split the file)"; fail=1; } || true
done

check_purity || fail=1

if [ "$fail" = 0 ]; then
  echo "✅ code guardrails OK (ratchet + WASM-core purity)"
else
  echo ""
  echo "A rise means a design-principle regression. Fix it, or after an intentional REDUCTION run:"
  echo "  scripts/check_code_guardrails.sh update"
fi
exit $fail
