#!/usr/bin/env python3
"""Reject per-case oracle overrides in the tokenization corpus.

A test case must not carry its own expectation for Suzume. The oracle is the
Python normalization pipeline, so `suzume_expected` / `accepted_diff` in a JSON
case silences that single case instead of generalizing the rule.

The keys are still parsed by the C++ loader (tests/common/json_loader.h) so a
local run fails on them too; this script is the whole-corpus view for CI.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
TEST_DATA_DIR = PROJECT_ROOT / "tests" / "data" / "tokenization"

BANNED_KEYS = ("suzume_expected", "accepted_diff")

# Mirrored verbatim from kOracleOverrideRemediation in tests/common/test_case.h
# and _test_tools_review.py so the remediation reads the same in every layer.
REMEDIATION = """A test case must not carry its own oracle. Encode the intentional MeCab difference as a
normalization rule under scripts/mcp/src/suzume_mcp/core/ (merge_rules.py, split_rules.py,
postprocessors.py, pos_mapping.py), then sync expectations with
test_needs_suzume_update(apply=True) and drop the field with test_reset_suzume(apply=True).
See AGENTS.md section 7 (Tokenization Design)."""


def find_violations() -> list[str]:
    """Return "file/case_id: key, key" for every case carrying a banned key."""
    violations: list[str] = []
    for path in sorted(TEST_DATA_DIR.glob("*.json")):
        data = json.loads(path.read_text(encoding="utf-8"))
        cases = data.get("cases") or data.get("test_cases") or []
        for index, case in enumerate(cases):
            present = [key for key in BANNED_KEYS if case.get(key)]
            if present:
                location = f"{path.stem}/{case.get('id', index)}"
                violations.append(f"{location}: {', '.join(present)}")
    return violations


def main() -> int:
    violations = find_violations()
    if not violations:
        print("oracle override check OK: no per-case oracle overrides")
        return 0

    print(f"Banned oracle override in {len(violations)} test case(s):")
    for violation in violations:
        print(f"  {violation}")
    print()
    print(REMEDIATION)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
