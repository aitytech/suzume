#!/usr/bin/env python3
"""Verify that every public numeric label table matches the C++ enums.

The C ABI transports compact numeric codes.  The header-only C++ wrapper and
the npm/PyPI bindings turn those codes back into strings with index-addressed
tables, so a missing, reordered, or misspelled entry silently changes public
output.  This guard treats the C++ enum order as the numeric ABI and compares
the full contents of every mirror, not only its length.

Usage:
  scripts/check_binding_labels.py
"""

from __future__ import annotations

import ast
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

CPP_TYPES = "src/core/types.cpp"
CPP_WRAPPER = "include/suzume/suzume.hpp"
WASM_LABELS = "bindings/wasm/js/abi_labels.ts"
PYTHON_LABELS = "bindings/python/src/suzume/_labels.py"


@dataclass(frozen=True)
class EnumSpec:
    header: str
    sentinel: str | None


# These are all enums whose numeric values are decoded into public labels.
MIRRORED_ENUMS = {
    "PartOfSpeech": EnumSpec("src/core/types.h", "Count_"),
    "ExtendedPOS": EnumSpec("src/core/types.h", "Count_"),
    "ConjugationType": EnumSpec("src/dictionary/dictionary.h", None),
    "ConjForm": EnumSpec("src/grammar/conjugation.h", "Count_"),
}

# ConjugationType has no complete C++ label function yet (the core formatter
# accepts VerbType, whose domain stops at IAdjective).  Until the C ABI exposes
# a canonical label API, keep this snapshot keyed by enum member.  Comparing
# its keys with the enum makes additions and reorderings fail closed.
CONJUGATION_TYPE_LABELS = {
    "None": None,
    "Ichidan": "一段",
    "GodanKa": "五段・カ行",
    "GodanGa": "五段・ガ行",
    "GodanSa": "五段・サ行",
    "GodanTa": "五段・タ行",
    "GodanNa": "五段・ナ行",
    "GodanBa": "五段・バ行",
    "GodanMa": "五段・マ行",
    "GodanRa": "五段・ラ行",
    "GodanWa": "五段・ワ行",
    "Suru": "サ変",
    "Kuru": "カ変",
    "IAdjective": "形容詞",
    "NaAdjective": "ナ形容詞",
    "Interjection": "感動詞",
    "ProperFamily": "固有名詞・姓",
    "ProperGiven": "固有名詞・名",
}

CONJUGATION_FORM_LABELS = {
    "Base": "終止形",
    "Mizenkei": "未然形",
    "Renyokei": "連用形",
    "Onbinkei": "連用形",
    "Kateikei": "仮定形",
    "Meireikei": "命令形",
    "Ishikei": "意志形",
}


def repo_root() -> Path:
    out = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        capture_output=True,
        text=True,
        check=True,
    )
    return Path(out.stdout.strip())


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def enclosed(text: str, opening: int, left: str, right: str, source: str) -> str:
    """Return the contents of the balanced delimiter at ``opening``."""
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == left:
            depth += 1
        elif text[index] == right:
            depth -= 1
            if depth == 0:
                return text[opening + 1 : index]
    raise SystemExit(f"❌ {source}: unterminated `{left}`")


def enum_members(name: str, spec: EnumSpec) -> list[str]:
    """Return enum members in their serialized numeric order."""
    text = Path(spec.header).read_text()
    match = re.search(rf"enum class {name}\s*:\s*\w+\s*\{{", text)
    if match is None:
        raise SystemExit(f"❌ {spec.header}: cannot find `enum class {name}`")
    body = strip_comments(enclosed(text, match.end() - 1, "{", "}", spec.header))

    values: dict[int, str] = {}
    next_value = 0
    for item in body.split(","):
        item = item.strip()
        if not item:
            continue
        parts = [part.strip() for part in item.split("=", 1)]
        member = parts[0]
        if len(parts) == 2:
            try:
                next_value = int(parts[1], 0)
            except ValueError as error:
                raise SystemExit(f"❌ {spec.header}: `{name}::{member}` needs a literal numeric value") from error
        if next_value in values:
            raise SystemExit(f"❌ {spec.header}: duplicate `{name}` value {next_value}")
        values[next_value] = member
        next_value += 1

    ordered = [values[index] for index in range(len(values))]
    if spec.sentinel is not None:
        if not ordered or ordered[-1] != spec.sentinel:
            raise SystemExit(f"❌ {spec.header}: `{name}` no longer ends with `{spec.sentinel}`")
        ordered.pop()
    return ordered


def function_body(path: str, name: str) -> str:
    text = Path(path).read_text()
    match = re.search(rf"\b{name}\s*\([^)]*\)\s*\{{", text)
    if match is None:
        raise SystemExit(f"❌ {path}: cannot find function `{name}`")
    return enclosed(text, match.end() - 1, "{", "}", path)


def cpp_switch_labels(path: str, function: str, enum_name: str, members: list[str]) -> list[str]:
    """Read a string-returning enum switch, including grouped fallback cases."""
    body = strip_comments(function_body(path, function))
    labels: dict[str, str] = {}
    pattern = rf"((?:\s*case\s+{enum_name}::\w+\s*:\s*)+)(?:default\s*:\s*)?return\s+\"([^\"]*)\"\s*;"
    for match in re.finditer(pattern, body):
        for member in re.findall(rf"{enum_name}::(\w+)", match.group(1)):
            labels[member] = match.group(2)

    missing = [member for member in members if member not in labels]
    if missing:
        raise SystemExit(f"❌ {path}: `{function}` has no labels for {', '.join(missing)}")
    return [labels[member] for member in members]


def literal_values(body: str, quote_pattern: str, null_tokens: tuple[str, ...]) -> list[str | None]:
    tokens = re.findall(quote_pattern + "|" + "|".join(map(re.escape, null_tokens)), body)
    values: list[str | None] = []
    for token in tokens:
        if token in null_tokens:
            values.append(None)
        else:
            value = ast.literal_eval(token)
            values.append(value if value != "" else None)
    return values


def named_table(
    path: str,
    name: str,
    left: str,
    right: str,
    quote_pattern: str,
    nulls: tuple[str, ...],
) -> list[str | None]:
    text = Path(path).read_text()
    match = re.search(rf"\b{name}\b[^=]*=\s*{re.escape(left)}", text)
    if match is None:
        raise SystemExit(f"❌ {path}: cannot find table `{name}`")
    body = enclosed(text, match.end() - 1, left, right, path)
    return literal_values(body, quote_pattern, nulls)


def cpp_local_table(function: str, table: str) -> list[str | None]:
    body = function_body(CPP_WRAPPER, function)
    match = re.search(rf"\b{table}\b\s*=\s*\{{", body)
    if match is None:
        raise SystemExit(f"❌ {CPP_WRAPPER}: `{function}` has no table `{table}`")
    values = enclosed(body, match.end() - 1, "{", "}", CPP_WRAPPER)
    return literal_values(values, r'"(?:\\.|[^"\\])*"', ("nullptr",))


def compare(
    table_path: str,
    table_name: str,
    members: list[str],
    expected: list[str | None],
    actual: list[str | None],
) -> bool:
    if actual == expected:
        return False

    print(f"❌ {table_path}: `{table_name}` does not match the C++ enum labels")
    if len(actual) != len(expected):
        print(f"   length: expected {len(expected)}, got {len(actual)}")
    for index, member in enumerate(members):
        got = actual[index] if index < len(actual) else "<missing>"
        if got != expected[index]:
            print(f"   [{index}] {member}: expected {expected[index]!r}, got {got!r}")
    if len(actual) > len(expected):
        print(f"   extra values: {actual[len(expected) :]!r}")
    return True


def main() -> int:
    os.chdir(repo_root())

    members = {name: enum_members(name, spec) for name, spec in MIRRORED_ENUMS.items()}
    canonical_calls = {
        "posLabel": "suzume_pos_label",
        "conjugationTypeLabel": "suzume_conjugation_type_label",
    }
    for function, canonical in canonical_calls.items():
        if canonical not in function_body(CPP_WRAPPER, function):
            raise SystemExit(f"❌ {CPP_WRAPPER}: `{function}` must call `{canonical}`")

    expected = {
        "pos_en": cpp_switch_labels(CPP_TYPES, "posToString", "PartOfSpeech", members["PartOfSpeech"]),
        "pos_ja": cpp_switch_labels(CPP_TYPES, "posToJapanese", "PartOfSpeech", members["PartOfSpeech"]),
        "extended": cpp_switch_labels(CPP_TYPES, "extendedPosToString", "ExtendedPOS", members["ExtendedPOS"]),
        "conj_type": [CONJUGATION_TYPE_LABELS[name] for name in members["ConjugationType"]],
        "conj_form": [CONJUGATION_FORM_LABELS[name] for name in members["ConjForm"]],
    }

    checks = [
        (
            CPP_WRAPPER,
            "posLabel::japanese_labels",
            members["PartOfSpeech"],
            expected["pos_ja"],
            cpp_local_table("posLabel", "japanese_labels"),
        ),
        (
            CPP_WRAPPER,
            "extendedPosLabel::labels",
            members["ExtendedPOS"],
            expected["extended"],
            cpp_local_table("extendedPosLabel", "labels"),
        ),
        (
            CPP_WRAPPER,
            "conjugationFormLabel::labels",
            members["ConjForm"],
            expected["conj_form"],
            cpp_local_table("conjugationFormLabel", "labels"),
        ),
        (
            WASM_LABELS,
            "POS_ENGLISH",
            members["PartOfSpeech"],
            expected["pos_en"],
            named_table(WASM_LABELS, "POS_ENGLISH", "[", "]", r"'(?:\\.|[^'\\])*'", ("null",)),
        ),
        (
            WASM_LABELS,
            "POS_JAPANESE",
            members["PartOfSpeech"],
            expected["pos_ja"],
            named_table(WASM_LABELS, "POS_JAPANESE", "[", "]", r"'(?:\\.|[^'\\])*'", ("null",)),
        ),
        (
            WASM_LABELS,
            "EXTENDED_POS",
            members["ExtendedPOS"],
            expected["extended"],
            named_table(WASM_LABELS, "EXTENDED_POS", "[", "]", r"'(?:\\.|[^'\\])*'", ("null",)),
        ),
        (
            WASM_LABELS,
            "CONJUGATION_TYPE_JAPANESE",
            members["ConjugationType"],
            expected["conj_type"],
            named_table(
                WASM_LABELS,
                "CONJUGATION_TYPE_JAPANESE",
                "[",
                "]",
                r"'(?:\\.|[^'\\])*'",
                ("null",),
            ),
        ),
        (
            WASM_LABELS,
            "CONJUGATION_FORM_JAPANESE",
            members["ConjForm"],
            expected["conj_form"],
            named_table(
                WASM_LABELS,
                "CONJUGATION_FORM_JAPANESE",
                "[",
                "]",
                r"'(?:\\.|[^'\\])*'",
                ("null",),
            ),
        ),
        (
            PYTHON_LABELS,
            "POS_ENGLISH",
            members["PartOfSpeech"],
            expected["pos_en"],
            named_table(PYTHON_LABELS, "POS_ENGLISH", "(", ")", r'"(?:\\.|[^"\\])*"', ("None",)),
        ),
        (
            PYTHON_LABELS,
            "POS_JAPANESE",
            members["PartOfSpeech"],
            expected["pos_ja"],
            named_table(PYTHON_LABELS, "POS_JAPANESE", "(", ")", r'"(?:\\.|[^"\\])*"', ("None",)),
        ),
        (
            PYTHON_LABELS,
            "EXTENDED_POS",
            members["ExtendedPOS"],
            expected["extended"],
            named_table(PYTHON_LABELS, "EXTENDED_POS", "(", ")", r'"(?:\\.|[^"\\])*"', ("None",)),
        ),
        (
            PYTHON_LABELS,
            "CONJUGATION_TYPES",
            members["ConjugationType"],
            expected["conj_type"],
            named_table(
                PYTHON_LABELS,
                "CONJUGATION_TYPES",
                "(",
                ")",
                r'"(?:\\.|[^"\\])*"',
                ("None",),
            ),
        ),
        (
            PYTHON_LABELS,
            "CONJUGATION_FORMS",
            members["ConjForm"],
            expected["conj_form"],
            named_table(
                PYTHON_LABELS,
                "CONJUGATION_FORMS",
                "(",
                ")",
                r'"(?:\\.|[^"\\])*"',
                ("None",),
            ),
        ),
    ]

    results = [compare(*check) for check in checks]
    failed = any(results)
    if failed:
        print()
        print("A mislabeled numeric code changes public output. Update every mirror above in enum order.")
        return 1

    print("✅ C++, WASM, and Python label contents match the C++ enums")
    return 0


if __name__ == "__main__":
    sys.exit(main())
