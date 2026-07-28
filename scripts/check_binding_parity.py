#!/usr/bin/env python3
"""Compare native, Python, and WASM analysis at the public API boundary."""

from __future__ import annotations

import json
import subprocess
from pathlib import Path
from typing import TypedDict, cast

from suzume import Suzume


class MorphemeRecord(TypedDict):
    surface: str
    pos: str
    lemma: str
    start: int
    end: int


class AnalysisRecord(TypedDict):
    normalized_text: str
    morphemes: list[MorphemeRecord]


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
NATIVE_CLI = REPOSITORY_ROOT / "build" / "bin" / "suzume-cli"
WASM_RUNNER = REPOSITORY_ROOT / "scripts" / "binding_parity.mjs"
CASES = [
    "東京でりんごを食べる",
    "ｶﾀｶﾅとＡＢＣ１２３",
    "か\u3099く",
    "家族👨‍👩‍👧と東京",
    "長いｰｰ音",
    "葛\U000e0100城市",
    "HTTPs://example.com/a_(b)",
]


def _morpheme_record(surface: str, pos: str, lemma: str, start: int, end: int) -> MorphemeRecord:
    return {
        "surface": surface,
        "pos": pos,
        "lemma": lemma,
        "start": start,
        "end": end,
    }


def _native_records() -> list[list[MorphemeRecord]]:
    records: list[list[MorphemeRecord]] = []
    for text in CASES:
        completed = subprocess.run(
            [str(NATIVE_CLI), "--format", "json", text],
            cwd=REPOSITORY_ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        payload = json.loads(completed.stdout)
        records.append(
            [
                _morpheme_record(
                    item["surface"],
                    item["pos"],
                    item["lemma"],
                    item["start"],
                    item["end"],
                )
                for item in payload["morphemes"]
            ]
        )
    return records


def _python_records() -> list[AnalysisRecord]:
    records: list[AnalysisRecord] = []
    with Suzume() as analyzer:
        for text in CASES:
            result = analyzer.analyze_with_normalized_text(text)
            records.append(
                {
                    "normalized_text": result.normalized_text,
                    "morphemes": [
                        _morpheme_record(
                            item.surface,
                            item.pos,
                            item.base_form,
                            item.start,
                            item.end,
                        )
                        for item in result.morphemes
                    ],
                }
            )
    return records


def _wasm_records() -> list[AnalysisRecord]:
    completed = subprocess.run(
        ["node", str(WASM_RUNNER)],
        cwd=REPOSITORY_ROOT,
        check=True,
        input=json.dumps(CASES, ensure_ascii=False),
        capture_output=True,
        text=True,
    )
    return cast(list[AnalysisRecord], json.loads(completed.stdout))


def _validate_surface_ranges(record: AnalysisRecord, text: str, surface: str) -> None:
    normalized_codepoints = list(record["normalized_text"])
    for morpheme in record["morphemes"]:
        extracted = "".join(normalized_codepoints[morpheme["start"] : morpheme["end"]])
        if extracted != morpheme["surface"]:
            raise AssertionError(f"{surface} range mismatch for {text!r}: {morpheme['surface']!r} != {extracted!r}")


def main() -> None:
    native = _native_records()
    python = _python_records()
    wasm = _wasm_records()
    for index, text in enumerate(CASES):
        if python[index] != wasm[index]:
            raise AssertionError(
                f"Python/WASM mismatch for {text!r}:\n"
                f"{json.dumps(python[index], ensure_ascii=False, indent=2)}\n"
                f"{json.dumps(wasm[index], ensure_ascii=False, indent=2)}"
            )
        if native[index] != python[index]["morphemes"]:
            raise AssertionError(
                f"native/Python mismatch for {text!r}:\n"
                f"{json.dumps(native[index], ensure_ascii=False, indent=2)}\n"
                f"{json.dumps(python[index]['morphemes'], ensure_ascii=False, indent=2)}"
            )
        _validate_surface_ranges(python[index], text, "Python")
        _validate_surface_ranges(wasm[index], text, "WASM")
    print(f"Binding parity passed: {len(CASES)} Unicode cases across native, Python, and WASM")


if __name__ == "__main__":
    main()
