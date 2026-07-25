#!/usr/bin/env python3
"""Keep the binding label tables in step with the C ABI enums.

The npm and PyPI bindings decode the numeric POS codes the C ABI serializes by
indexing a table of label strings. Adding a value to PartOfSpeech or
ExtendedPOS therefore has to extend both tables, but nothing in the C++ suite
notices when it does not: the code simply falls off the end and decodes as
UNKNOWN, and only the binding suites catch it — after the C++ change has
already landed.

This compares the enum size against both tables so the gap fails in the same
job as the other guardrails.

Usage:
  scripts/check_binding_labels.py
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path

# Enum name -> (header, label table in the WASM binding, label table in Python).
# Only enums whose bindings mirror EVERY value belong here. The conjugation
# tables are deliberately partial and would report a false gap.
MIRRORED_ENUMS = {
    "PartOfSpeech": ("src/core/types.h", ("POS_ENGLISH", "POS_JAPANESE"), ("POS_ENGLISH", "POS_JAPANESE")),
    "ExtendedPOS": ("src/core/types.h", ("EXTENDED_POS",), ("EXTENDED_POS",)),
}

WASM_LABELS = "bindings/wasm/js/abi_labels.ts"
PYTHON_LABELS = "bindings/python/src/suzume/_labels.py"


def repo_root() -> Path:
    out = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True, check=True)
    return Path(out.stdout.strip())


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def enum_value_count(header: str, name: str) -> int:
    """Number of real values in a C++ enum, excluding the Count_ sentinel."""
    match = re.search(rf"enum class {name}\s*:\s*\w+\s*\{{(.*?)\n\}};", Path(header).read_text(), re.S)
    if match is None:
        raise SystemExit(f"❌ {header}: cannot find `enum class {name}`")
    body = strip_comments(match.group(1))
    members = [item.strip().split("=")[0].strip() for item in body.split(",") if item.strip()]
    if members[-1] != "Count_":
        raise SystemExit(f"❌ {header}: `{name}` no longer ends with the Count_ sentinel")
    return len(members) - 1


def wasm_table_length(name: str) -> int:
    match = re.search(rf"const {name}[^=]*=\s*\[(.*?)\n\]", Path(WASM_LABELS).read_text(), re.S)
    if match is None:
        raise SystemExit(f"❌ {WASM_LABELS}: cannot find table `{name}`")
    return len(re.findall(r"'[^']*'|null", match.group(1)))


def python_table_length(name: str) -> int:
    match = re.search(rf"^{name}[^=]*=\s*\((.*?)\n\)", Path(PYTHON_LABELS).read_text(), re.S | re.M)
    if match is None:
        raise SystemExit(f"❌ {PYTHON_LABELS}: cannot find table `{name}`")
    return len(re.findall(r'"[^"]*"|None', match.group(1)))


def main() -> int:
    os.chdir(repo_root())

    failed = False
    for enum_name, (header, wasm_tables, python_tables) in MIRRORED_ENUMS.items():
        expected = enum_value_count(header, enum_name)
        for table in wasm_tables:
            actual = wasm_table_length(table)
            if actual != expected:
                print(f"❌ {WASM_LABELS}: {table} has {actual} labels, but {enum_name} has {expected} values")
                failed = True
        for table in python_tables:
            actual = python_table_length(table)
            if actual != expected:
                print(f"❌ {PYTHON_LABELS}: {table} has {actual} labels, but {enum_name} has {expected} values")
                failed = True

    if failed:
        print()
        print("A code the bindings cannot label decodes as UNKNOWN. Extend the table(s)")
        print("above and move the out-of-range assertion in each binding's label test.")
        return 1

    print("✅ binding label tables match the C ABI enums")
    return 0


if __name__ == "__main__":
    sys.exit(main())
