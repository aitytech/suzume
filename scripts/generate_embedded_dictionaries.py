#!/usr/bin/env python3
"""Generate a C++ translation unit containing the WASM dictionaries."""

from __future__ import annotations

import argparse
from pathlib import Path


def format_bytes(data: bytes) -> str:
    lines = []
    for start in range(0, len(data), 16):
        chunk = data[start : start + 16]
        lines.append("  " + ", ".join(f"0x{value:02x}" for value in chunk) + ",")
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--user", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    core = args.core.read_bytes()
    user = args.user.read_bytes()
    source = f"""// Generated file. Do not edit.
#include \"embedded_dictionaries.h\"

namespace suzume::embedded {{

alignas(16) const uint8_t kCoreDictionary[] = {{
{format_bytes(core)}
}};
const size_t kCoreDictionarySize = sizeof(kCoreDictionary);

alignas(16) const uint8_t kUserDictionary[] = {{
{format_bytes(user)}
}};
const size_t kUserDictionarySize = sizeof(kUserDictionary);

}}  // namespace suzume::embedded
"""
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding="utf-8") != source:
        args.output.write_text(source, encoding="utf-8")


if __name__ == "__main__":
    main()
