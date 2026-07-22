#!/usr/bin/env python3
"""Ratchet JSON-local oracle overrides while they are migrated to Python rules."""

from __future__ import annotations

import json
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
TEST_DATA_DIR = PROJECT_ROOT / "tests" / "data" / "tokenization"
BASELINE_PATH = PROJECT_ROOT / "scripts" / "oracle-override-baseline.txt"


def count_overrides() -> tuple[int, list[str], list[str]]:
    override_count = 0
    orphan_reasons: list[str] = []
    missing_reasons: list[str] = []
    for path in sorted(TEST_DATA_DIR.glob("*.json")):
        data = json.loads(path.read_text(encoding="utf-8"))
        cases = data.get("cases") or data.get("test_cases") or []
        for index, case in enumerate(cases):
            location = f"{path.stem}/{case.get('id', index)}"
            has_override = bool(case.get("suzume_expected"))
            has_reason = bool(case.get("accepted_diff"))
            override_count += int(has_override)
            if has_reason and not has_override:
                orphan_reasons.append(location)
            if has_override and not has_reason:
                missing_reasons.append(location)
    return override_count, orphan_reasons, missing_reasons


def main() -> int:
    override_count, orphan_reasons, missing_reasons = count_overrides()
    if len(sys.argv) > 1 and sys.argv[1] == "update":
        BASELINE_PATH.write_text(f"{override_count}\n", encoding="utf-8")
        print(f"oracle override baseline updated: {override_count}")
        return 0

    baseline = int(BASELINE_PATH.read_text(encoding="utf-8").strip())
    failed = False
    if override_count > baseline:
        print(f"oracle overrides increased: {override_count} > baseline {baseline}")
        failed = True
    if orphan_reasons:
        print("accepted_diff without suzume_expected: " + ", ".join(orphan_reasons))
        failed = True
    if missing_reasons:
        print("suzume_expected without accepted_diff: " + ", ".join(missing_reasons))
        failed = True
    if failed:
        return 1
    print(f"oracle override ratchet OK: {override_count} <= {baseline}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
