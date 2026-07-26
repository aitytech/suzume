#!/usr/bin/env bash
#
# Version-mirror gate. The version lives in the root CMakeLists.txt project()
# declaration (single source of truth) and is hand-mirrored into every binding
# manifest. This fails if any mirror drifts from the canonical version.
set -eu
cd "$(git rev-parse --show-toplevel)"

canonical=$(grep -Eo 'project\(suzume VERSION [0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt \
  | grep -Eo '[0-9]+\.[0-9]+\.[0-9]+')

if [ -z "${canonical:-}" ]; then
  echo "❌ could not read canonical version from CMakeLists.txt"
  exit 1
fi

status=0
check() {
  local label="$1" file="$2" found="$3"
  if [ "$found" != "$canonical" ]; then
    echo "❌ $label ($file): $found != $canonical"
    status=1
  else
    echo "✅ $label: $found"
  fi
}

wasm_ver=$(grep -Eo '"version": *"[0-9]+\.[0-9]+\.[0-9]+"' bindings/wasm/package.json \
  | grep -Eo '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
check "wasm package.json" bindings/wasm/package.json "$wasm_ver"

py_ver=$(grep -Eo '^version = "[0-9]+\.[0-9]+\.[0-9]+"' bindings/python/pyproject.toml \
  | grep -Eo '[0-9]+\.[0-9]+\.[0-9]+' | head -1)
check "python pyproject.toml" bindings/python/pyproject.toml "$py_ver"

py_lock_ver=$(awk '
  $0 == "name = \"suzume\"" {
    if (getline > 0 && $1 == "version" && $2 == "=") {
      gsub(/"/, "", $3)
      print $3
      exit
    }
  }
' bindings/python/uv.lock)
check "python uv.lock" bindings/python/uv.lock "$py_lock_ver"

if [ "$status" -eq 0 ]; then
  echo "All version mirrors consistent: $canonical"
fi
exit "$status"
