#!/bin/sh
# Pre-commit hook: check formatting of staged files across every language in the
# repo (C++ core, WASM binding, Python binding, MCP server), mirroring the
# check-only fan-out of `make format-check` but scoped to what is being committed.
#
# Installed into .git/hooks/pre-commit by simple-git-hooks (see the
# `simple-git-hooks` field in bindings/wasm/package.json). Git runs hooks from
# the repository top-level directory.
#
# Auto-fix everything with `make format`. Bypass this hook with
# SKIP_SIMPLE_GIT_HOOKS=1 (e.g. mid-refactor when toolchains are unavailable).

if [ "$SKIP_SIMPLE_GIT_HOOKS" = "1" ]; then
    echo "[pre-commit] SKIP_SIMPLE_GIT_HOOKS=1, skipping."
    exit 0
fi

root=$(git rev-parse --show-toplevel) || exit 1
cd "$root" || exit 1

staged=$(git diff --cached --name-only --diff-filter=ACM)
[ -z "$staged" ] && exit 0

status=0

# --- C++ core and native tooling (clang-format, scoped to staged files) ---
cpp=$(printf '%s\n' "$staged" | grep -E '^(src|include|tools|tests|examples)/.*\.(cpp|h|hpp|c)$')
if [ -n "$cpp" ] && command -v clang-format >/dev/null 2>&1; then
    if ! printf '%s\n' "$cpp" | xargs clang-format --dry-run --Werror; then
        echo "[pre-commit] C++ formatting issues (clang-format)."
        status=1
    fi
fi

# --- WASM binding (biome, whole sub-project when any of its files are staged) ---
if printf '%s\n' "$staged" | grep -qE '^bindings/wasm/(js|tests)/.*\.(ts|js)$' \
    && [ -d bindings/wasm/node_modules ]; then
    if ! (cd bindings/wasm && yarn lint >/dev/null 2>&1); then
        echo "[pre-commit] WASM binding lint issues (biome)."
        status=1
    fi
fi

# --- Python binding + MCP server (ruff, whole sub-project each) ---
if command -v uv >/dev/null 2>&1; then
    if printf '%s\n' "$staged" | grep -qE '^bindings/python/.*\.py$'; then
        if ! (cd bindings/python && uv run --extra dev ruff format --check . \
            && uv run --extra dev ruff check .) >/dev/null 2>&1; then
            echo "[pre-commit] Python binding formatting/lint issues (ruff)."
            status=1
        fi
    fi
    if printf '%s\n' "$staged" | grep -qE '^scripts/mcp/.*\.py$'; then
        if ! (cd scripts/mcp && uv run ruff format --check . \
            && uv run ruff check .) >/dev/null 2>&1; then
            echo "[pre-commit] MCP server formatting/lint issues (ruff)."
            status=1
        fi
    fi
fi

if [ "$status" -ne 0 ]; then
    echo "[pre-commit] Fix with:  make format"
    echo "[pre-commit] Or bypass with:  SKIP_SIMPLE_GIT_HOOKS=1 git commit ..."
fi

exit "$status"
