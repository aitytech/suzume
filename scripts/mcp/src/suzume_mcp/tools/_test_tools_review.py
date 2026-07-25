"""Test corpus review, validation, and coverage MCP tools."""

import re
from pathlib import Path

from ..core.suzume_cli import (
    get_expected_tokens_subprocess as get_expected_tokens,
)
from ..core.test_file_suggestions import suggest_test_files
from ..core.test_file_utils import (
    find_test_by_id,
    find_test_by_input,
    find_tests_by_input,
    generate_id,
    load_json,
    save_json,
)
from ..server import PROJECT_ROOT, mcp
from ._test_tools_common import (
    _get_test_files_filtered,
    _json_error,
    _json_result,
)

# Per-case oracle overrides. Banned: a case that carries its own expectation for
# Suzume silences that one case instead of generalizing the rule.
BANNED_ORACLE_KEYS = ("suzume_expected", "accepted_diff")

# Mirrored verbatim from kOracleOverrideRemediation in tests/common/test_case.h and
# scripts/check_oracle_overrides.py so the remediation reads the same in every layer.
ORACLE_OVERRIDE_REMEDIATION = (
    "A test case must not carry its own oracle. Encode the intentional MeCab difference as a "
    "normalization rule under scripts/mcp/src/suzume_mcp/core/ (merge_rules.py, split_rules.py, "
    "postprocessors.py, pos_mapping.py), then sync expectations with "
    "test_needs_suzume_update(apply=True) and drop the field with test_reset_suzume(apply=True). "
    "See AGENTS.md section 7 (Tokenization Design)."
)


def _banned_keys_in(case: dict) -> list[str]:
    return [key for key in BANNED_ORACLE_KEYS if case.get(key)]


def _case_location(found: dict) -> str:
    case_id = found["case"].get("id", str(found["index"]))
    return f"{found['basename']}/{case_id}"


def _resolve_single_test(input_text: str, test_id: str) -> tuple[dict | None, str]:
    """Resolve a test case without silently choosing among duplicate inputs."""
    if test_id:
        found = find_test_by_id(PROJECT_ROOT, test_id)
        if not found:
            return None, f"No test found for id: {test_id}"
        if input_text and found["case"].get("input") != input_text:
            return None, f"Input does not match test id: {test_id}"
        return found, ""

    matches = find_tests_by_input(PROJECT_ROOT, input_text)
    if not matches:
        return None, f"No test found for input: {input_text}"
    if len(matches) > 1:
        locations = ", ".join(_case_location(found) for found in matches)
        return None, f"Input matches multiple tests; specify test_id: {locations}"
    return matches[0], ""


@mcp.tool()
async def test_accept_diff(
    input_text: str = "",
    reason: str = "",
    category: str = "pos-limitation",
    all_failed: bool = False,
    test_output_file: str = "/tmp/test.txt",
    apply: bool = False,
    test_id: str = "",
) -> str:
    """Disabled: refuses to promote Suzume's current output to a test expectation.

    This tool used to write a per-case oracle override. It is kept registered, and
    kept refusing, so that reaching for it returns the correct path instead of
    nothing. The arguments are accepted only to reach the refusal.

    A failing case means either the implementation is wrong (fix it) or the oracle
    is wrong (fix the normalization rule). There is no third option.
    """
    return _json_error(f"test_accept_diff is disabled by design. {ORACLE_OVERRIDE_REMEDIATION}")


@mcp.tool()
async def test_reset_suzume(
    input_text: str = "",
    all_tests: bool = False,
    file: str = "",
    apply: bool = False,
    test_id: str = "",
) -> str:
    """Strip banned oracle-override fields (suzume_expected / accepted_diff) from cases.

    Removal is unconditional: the fields are banned outright, so this never refuses
    on the grounds that Suzume still differs from the oracle. Re-sync expectations
    afterwards with test_needs_suzume_update(apply=True).

    Args:
        input_text: Input text to strip (mutually exclusive with all_tests).
        test_id: Stable basename/id selector. Required when input_text is duplicated.
        all_tests: If True, strip every case carrying a banned field.
        file: Optional test file filter (without .json), used with all_tests.
        apply: If True, apply changes. Default is dry-run.
    """
    to_reset = []

    if all_tests:
        files = _get_test_files_filtered(file or "all")
        for path in files:
            try:
                data = load_json(path)
            except Exception as exc:
                return _json_error(f"Failed to parse JSON file {path}: {exc}")
            basename = path.stem
            cases_key = "cases" if "cases" in data else "test_cases"
            cases = data.get(cases_key) or []
            for idx, case in enumerate(cases):
                if _banned_keys_in(case):
                    to_reset.append(
                        {
                            "found": {
                                "file": path,
                                "data": data,
                                "case": case,
                                "index": idx,
                                "basename": basename,
                            },
                        }
                    )
    elif input_text or test_id:
        found, error = _resolve_single_test(input_text, test_id)
        if not found:
            return _json_error(error)
        if not _banned_keys_in(found["case"]):
            return _json_error(f"Test has no oracle override to strip: {_case_location(found)}")
        to_reset.append({"found": found})
    else:
        return _json_error("Either input_text or all_tests=True, or test_id is required.")

    reset_entries = []
    files_to_save: dict[Path, dict] = {}

    for item in to_reset:
        found = item["found"]
        case = found["case"]
        reset_entries.append(
            {
                "id": _case_location(found),
                "file": found["basename"],
                "index": found["index"],
                "input": case.get("input", ""),
                "removed": _banned_keys_in(case),
            }
        )

        if apply:
            cases_key = "cases" if "cases" in found["data"] else "test_cases"
            stored = found["data"][cases_key][found["index"]]
            for key in BANNED_ORACLE_KEYS:
                stored.pop(key, None)
            files_to_save[found["file"]] = found["data"]

    if apply:
        for path, data in files_to_save.items():
            save_json(path, data)

    return _json_result(
        {
            "status": "ok",
            "reset": reset_entries,
            "total": len(reset_entries),
            "applied": apply,
            "next_step": ("test_needs_suzume_update(apply=True) to re-sync expectations" if reset_entries else ""),
        }
    )


@mcp.tool()
async def test_validate_ids(
    file: str = "",
    apply: bool = False,
) -> str:
    """Detect and fix non-ASCII or duplicate test case IDs.

    Args:
        file: Optional test file filter (without .json), or empty for all.
        apply: If True, fix invalid IDs. Default is report-only.
    """
    files = _get_test_files_filtered(file or "all")
    if not files:
        return _json_error("No test files found")

    problems = []

    for path in files:
        try:
            data = load_json(path)
        except Exception as exc:
            return _json_error(f"Failed to parse JSON file {path}: {exc}")
        basename = path.stem
        cases_key = "cases" if "cases" in data else "test_cases"
        cases = data.get(cases_key) or []
        file_ids: dict[str, bool] = {}

        for idx, case in enumerate(cases):
            case_id = case.get("id", "")
            inp = case.get("input", "")

            # Sanitize ID
            sanitized = re.sub(r"[^a-zA-Z0-9_]", "_", case_id)
            sanitized = re.sub(r"_+", "_", sanitized)
            sanitized = sanitized.strip("_")

            has_non_ascii = bool(re.search(r"[^\x00-\x7F]", case_id))
            becomes_empty = not sanitized or sanitized == "_"
            is_dup = sanitized in file_ids

            if has_non_ascii or becomes_empty or is_dup:
                new_id = generate_id(inp)
                suffix = 1
                base_id = new_id
                while new_id in file_ids:
                    new_id = f"{base_id}_{suffix}"
                    suffix += 1

                reason = (
                    "non-ASCII" if has_non_ascii else "empty after sanitize" if becomes_empty else "duplicate in file"
                )
                problems.append(
                    {
                        "file": path,
                        "basename": basename,
                        "index": idx,
                        "old_id": case_id,
                        "new_id": new_id,
                        "input": inp,
                        "reason": reason,
                        "data": data,
                        "cases_key": cases_key,
                    }
                )
                file_ids[new_id] = True
            else:
                file_ids[sanitized] = True

    if not problems:
        return _json_result(
            {
                "problems": [],
                "total": 0,
                "applied": False,
            }
        )

    problems_out = [
        {
            "file": p["basename"],
            "index": p["index"],
            "old_id": p["old_id"],
            "new_id": p["new_id"],
            "reason": p["reason"],
        }
        for p in problems
    ]

    if not apply:
        return _json_result(
            {
                "problems": problems_out,
                "total": len(problems),
                "applied": False,
            }
        )

    files_to_save: dict[Path, dict] = {}
    for prob in problems:
        prob["data"][prob["cases_key"]][prob["index"]]["id"] = prob["new_id"]
        files_to_save[prob["file"]] = prob["data"]

    for path, data in files_to_save.items():
        save_json(path, data)

    return _json_result(
        {
            "problems": problems_out,
            "total": len(problems),
            "applied": True,
        }
    )


@mcp.tool()
async def test_check_coverage(inputs: list[str]) -> str:
    """Check which inputs have existing tests.

    Args:
        inputs: List of Japanese input texts to check.
    """
    existing = []
    missing = []

    for inp in inputs:
        found = find_test_by_input(PROJECT_ROOT, inp)
        if found:
            existing.append({"input": inp, "location": f"{found['basename']}/{found['index']}"})
        else:
            missing.append(inp)

    return _json_result(
        {
            "existing": existing,
            "missing": missing,
            "summary": {"existing": len(existing), "missing": len(missing)},
        }
    )


@mcp.tool()
async def test_suggest_file(input_text: str) -> str:
    """Suggest which test file an input should go into based on MeCab analysis.

    Args:
        input_text: Japanese text to analyze.
    """
    tokens, _, _ = get_expected_tokens(input_text)
    if not tokens:
        return _json_error("No tokens found for input")

    suggestions = suggest_test_files(input_text, tokens)

    token_list = [{"surface": tok["surface"], "pos": tok["pos"]} for tok in tokens]

    return _json_result(
        {
            "input": input_text,
            "tokens": token_list,
            "suggestions": suggestions,
        }
    )
