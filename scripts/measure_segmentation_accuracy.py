#!/usr/bin/env python3
"""Measure segmentation accuracy against the repository's own gold cases.

Suzume deliberately does not treat a MeCab match rate as a success metric,
since the two tools are not meant to produce interchangeable output. That
leaves the question of how accurate it actually is unanswered unless some
other measure is published, so this script scores the tokenizer against the
expected segmentations already committed under tests/data/tokenization.

Three numbers are reported:

  boundary F1  - agreement on where tokens start and end, ignoring labels.
                 This is the figure that matters for search indexing.
  token F1     - agreement on whole tokens, i.e. both edges correct at once.
  exact match  - fraction of inputs segmented identically end to end.

Usage:
    python3 scripts/measure_segmentation_accuracy.py [--cli build/bin/suzume-cli]
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any

REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CLI = REPOSITORY_ROOT / "build" / "bin" / "suzume-cli"
GOLD_DIR = REPOSITORY_ROOT / "tests" / "data" / "tokenization"

# Inputs are fed to one process at a time, separated by newlines, and split
# apart again by character offset. Batching keeps a few thousand cases to a
# handful of invocations instead of a few thousand.
BATCH_SIZE = 200


def load_cases() -> list[dict[str, Any]]:
    """Collect every gold case that carries an input and an expected split."""
    cases: list[dict[str, Any]] = []
    for path in sorted(GOLD_DIR.glob("*.json")):
        payload = json.loads(path.read_text(encoding="utf-8"))
        for case in payload.get("cases", []):
            text = case.get("input")
            expected = case.get("expected")
            if not text or not expected:
                continue
            surfaces = [item.get("surface", "") for item in expected]
            if any(not surface for surface in surfaces):
                continue
            # A gold split whose surfaces do not reassemble into the input
            # cannot be scored by offset, so it is left out rather than
            # silently counted as a miss.
            if "".join(surfaces) != text or "\n" in text:
                continue
            cases.append({"file": path.stem, "id": case.get("id", ""), "input": text, "gold": surfaces})
    return cases


def analyze_batch(cli: Path, texts: list[str]) -> list[list[tuple[int, int]]] | None:
    """Return predicted (start, end) spans per input, or None if unusable."""
    joined = "\n".join(texts)
    result = subprocess.run(
        [str(cli), "analyze", "--format=json"],
        input=joined,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return None
    payload = json.loads(result.stdout)
    # Offsets only line up with the input when normalization left it alone.
    if payload.get("normalized_text") != joined:
        return None

    # Character offset at which each input starts inside the joined document.
    starts: list[int] = []
    cursor = 0
    for text in texts:
        starts.append(cursor)
        cursor += len(text) + 1

    spans: list[list[tuple[int, int]]] = [[] for _ in texts]
    bounds = [(start, start + len(text)) for start, text in zip(starts, texts, strict=True)]
    for morpheme in payload.get("morphemes", []):
        begin, end = morpheme["start"], morpheme["end"]
        for index, (low, high) in enumerate(bounds):
            if begin >= low and end <= high:
                spans[index].append((begin - low, end - low))
                break
    return spans


def spans_from_surfaces(surfaces: list[str]) -> list[tuple[int, int]]:
    spans = []
    cursor = 0
    for surface in surfaces:
        spans.append((cursor, cursor + len(surface)))
        cursor += len(surface)
    return spans


def f1(matched: int, predicted: int, gold: int) -> tuple[float, float, float]:
    precision = matched / predicted if predicted else 0.0
    recall = matched / gold if gold else 0.0
    score = 2 * precision * recall / (precision + recall) if precision + recall else 0.0
    return precision, recall, score


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cli", type=Path, default=DEFAULT_CLI)
    parser.add_argument("--json-output", type=Path, default=None)
    parser.add_argument("--per-category", action="store_true", help="Break the score down by gold file")
    args = parser.parse_args()

    if not args.cli.exists():
        print(f"CLI not found at {args.cli}. Run 'make build' first.", file=sys.stderr)
        sys.exit(1)

    cases = load_cases()
    if not cases:
        print("No scorable gold cases found.", file=sys.stderr)
        sys.exit(1)

    boundary_matched = boundary_predicted = boundary_gold = 0
    token_matched = token_predicted = token_gold = 0
    exact = 0
    skipped = 0
    by_category: dict[str, list[int]] = defaultdict(lambda: [0, 0, 0, 0])

    for offset in range(0, len(cases), BATCH_SIZE):
        batch = cases[offset : offset + BATCH_SIZE]
        predictions = analyze_batch(args.cli, [case["input"] for case in batch])
        if predictions is None:
            # Fall back to one call per input so a single unnormalizable case
            # does not discard its whole batch.
            predictions = []
            for case in batch:
                single = analyze_batch(args.cli, [case["input"]])
                predictions.append(single[0] if single else None)

        for case, predicted_spans in zip(batch, predictions, strict=True):
            if predicted_spans is None:
                skipped += 1
                continue
            gold_spans = spans_from_surfaces(case["gold"])

            # Interior boundaries only: the document edges are free.
            gold_bounds = {end for _, end in gold_spans[:-1]}
            pred_bounds = {end for _, end in predicted_spans[:-1]}
            hit = len(gold_bounds & pred_bounds)
            boundary_matched += hit
            boundary_predicted += len(pred_bounds)
            boundary_gold += len(gold_bounds)

            token_hit = len(set(gold_spans) & set(predicted_spans))
            token_matched += token_hit
            token_predicted += len(predicted_spans)
            token_gold += len(gold_spans)

            if predicted_spans == gold_spans:
                exact += 1

            bucket = by_category[case["file"]]
            bucket[0] += hit
            bucket[1] += len(pred_bounds)
            bucket[2] += len(gold_bounds)
            bucket[3] += 1

    scored = len(cases) - skipped
    b_precision, b_recall, b_f1 = f1(boundary_matched, boundary_predicted, boundary_gold)
    t_precision, t_recall, t_f1 = f1(token_matched, token_predicted, token_gold)

    print(f"Gold cases scored : {scored:,} of {len(cases):,} ({skipped} unscorable)")
    print(f"Gold tokens       : {token_gold:,}")
    print()
    print(f"Boundary precision: {b_precision:.4f}")
    print(f"Boundary recall   : {b_recall:.4f}")
    print(f"Boundary F1       : {b_f1:.4f}")
    print()
    print(f"Token precision   : {t_precision:.4f}")
    print(f"Token recall      : {t_recall:.4f}")
    print(f"Token F1          : {t_f1:.4f}")
    print()
    print(f"Sentence exact    : {exact / scored:.4f} ({exact:,} of {scored:,})")
    print()
    print("These cases are the suite the tokenizer is developed against, so this")
    print("is an in-sample score: it says the committed behaviour holds, not how")
    print("the tokenizer generalizes. Read it as a regression measure. A held-out")
    print("estimate needs text that was never used to fix a bug here.")

    if args.per_category:
        print("\nWeakest categories by boundary F1:")
        rows = []
        for name, (hit, pred, gold, count) in by_category.items():
            _, _, score = f1(hit, pred, gold)
            rows.append((score, name, count))
        for score, name, count in sorted(rows)[:15]:
            print(f"  {score:.4f}  {name} ({count} cases)")

    if args.json_output:
        args.json_output.write_text(
            json.dumps(
                {
                    "casesScored": scored,
                    "casesTotal": len(cases),
                    "goldTokens": token_gold,
                    "boundary": {"precision": b_precision, "recall": b_recall, "f1": b_f1},
                    "token": {"precision": t_precision, "recall": t_recall, "f1": t_f1},
                    "sentenceExactMatch": exact / scored,
                },
                indent=2,
            ),
            encoding="utf-8",
        )


if __name__ == "__main__":
    main()
