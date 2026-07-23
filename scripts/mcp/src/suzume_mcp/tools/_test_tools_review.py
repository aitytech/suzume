"""Test corpus review, validation, and coverage MCP tools."""

import re
from pathlib import Path

from ..core.suzume_cli import (
    get_expected_tokens_batch_subprocess,
)
from ..core.suzume_cli import (
    get_expected_tokens_subprocess as get_expected_tokens,
)
from ..core.suzume_utils import tokens_match
from ..core.test_file_suggestions import suggest_test_files
from ..core.test_file_utils import (
    find_test_by_id,
    find_test_by_input,
    find_tests_by_input,
    generate_id,
    get_failures_from_test_output,
    load_json,
    save_json,
)
from ..server import PROJECT_ROOT, mcp
from ._test_tools_common import (
    _format_expected_checked,
    _get_suzume_tokens,
    _get_test_files_filtered,
    _json_error,
    _json_result,
)


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
    """Accept Suzume's current output as valid by adding suzume_expected field.

    Keeps the normalized reference expected for comparison while marking Suzume's
    output as an intentional tokenizer difference.

    Args:
        input_text: Input text to accept diff for (mutually exclusive with all_failed).
        test_id: Stable basename/id selector. Required when input_text is duplicated.
        reason: Reason for accepting the diff (required).
        category: Diff category (default: pos-limitation).
        all_failed: If True, process all failed tests from test output file.
        test_output_file: Path to ctest output file (used with all_failed).
        apply: If True, apply changes. Default is dry-run.
    """
    if not reason:
        return _json_error("reason is required. Explain why this diff is acceptable.")

    if not input_text and not test_id and not all_failed:
        return _json_error("Either input_text or all_failed=True, or test_id is required.")

    if all_failed and test_id:
        return _json_error("test_id cannot be combined with all_failed=True.")

    to_update = []

    if all_failed:
        failures = get_failures_from_test_output(test_output_file)
        if not failures:
            return _json_result(
                {
                    "status": "ok",
                    "reason": reason,
                    "category": category,
                    "updates": [],
                    "applied": False,
                }
            )

        for failure in failures:
            matches = find_tests_by_input(PROJECT_ROOT, failure["input"])
            if len(matches) != 1:
                continue
            found = matches[0]
            if found["case"].get("suzume_expected"):
                continue
            to_update.append({"input": failure["input"], "found": found})
    else:
        found, error = _resolve_single_test(input_text, test_id)
        if not found:
            return _json_error(error)
        input_text = found["case"].get("input", "")
        to_update.append({"input": input_text, "found": found})

    if not to_update:
        return _json_result(
            {
                "status": "ok",
                "reason": reason,
                "category": category,
                "updates": [],
                "applied": False,
            }
        )

    updates = []
    for item in to_update:
        inp = item["input"]
        found = item["found"]
        try:
            suzume_tokens = _get_suzume_tokens(inp)
            suzume_expected = _format_expected_checked(suzume_tokens, "Suzume")
        except RuntimeError as exc:
            return _json_error(str(exc))

        exp_surfaces = "|".join(t.get("surface", "") for t in (found["case"].get("expected") or []))
        suz_surfaces = "|".join(t["surface"] for t in suzume_expected)

        updates.append(
            {
                "file": found["basename"],
                "index": found["index"],
                "input": inp,
                "mecab_expected": exp_surfaces,
                "suzume_expected": suz_surfaces,
            }
        )

        if apply:
            cases_key = "cases" if "cases" in found["data"] else "test_cases"
            found["data"][cases_key][found["index"]]["suzume_expected"] = suzume_expected
            found["data"][cases_key][found["index"]]["accepted_diff"] = {
                "reason": reason,
                "category": category,
            }
            save_json(found["file"], found["data"])

    return _json_result(
        {
            "status": "ok",
            "reason": reason,
            "category": category,
            "updates": updates,
            "applied": apply,
        }
    )


@mcp.tool()
async def test_reset_suzume(
    input_text: str = "",
    all_tests: bool = False,
    file: str = "",
    apply: bool = False,
    test_id: str = "",
    only_if_matches_oracle: bool = True,
) -> str:
    """Remove stale suzume_expected field from test cases.

    Use when Suzume now matches expected (no override needed).

    Args:
        input_text: Input text to reset (mutually exclusive with all_tests).
        test_id: Stable basename/id selector. Required when input_text is duplicated.
        all_tests: If True, find all tests with suzume_expected.
        file: Optional test file filter (without .json).
        only_if_matches_oracle: Require JSON expected and current Suzume to match the latest oracle.
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
                if case.get("suzume_expected") or case.get("accepted_diff"):
                    to_reset.append(
                        {
                            "input": case.get("input", ""),
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
        if not found["case"].get("suzume_expected") and not found["case"].get("accepted_diff"):
            return _json_error(f"Test has no Suzume override metadata: {_case_location(found)}")
        to_reset.append({"input": found["case"].get("input", ""), "found": found})
    else:
        return _json_error("Either input_text or all_tests=True, or test_id is required.")

    if not to_reset:
        return _json_result(
            {
                "status": "ok",
                "reset": [],
                "total": 0,
                "applied": False,
            }
        )

    reset_entries = []
    skipped_entries = []
    files_to_save: dict[Path, dict] = {}

    oracle_results = get_expected_tokens_batch_subprocess([item["input"] for item in to_reset])

    for item, (oracle_tokens, _source, rule) in zip(to_reset, oracle_results, strict=True):
        found = item["found"]
        expected = found["case"].get("expected") or []
        try:
            suzume_tokens = _get_suzume_tokens(item["input"])
        except RuntimeError as exc:
            return _json_error(str(exc))

        expected_matches_oracle = tokens_match(expected, oracle_tokens)
        suzume_matches_oracle = tokens_match(suzume_tokens, oracle_tokens)
        if only_if_matches_oracle and not (expected_matches_oracle and suzume_matches_oracle):
            skipped_entries.append(
                {
                    "id": _case_location(found),
                    "input": item["input"],
                    "expected_matches_oracle": expected_matches_oracle,
                    "suzume_matches_oracle": suzume_matches_oracle,
                    "rule": rule or "mecab-only",
                }
            )
            continue

        reset_entries.append(
            {
                "id": _case_location(found),
                "file": found["basename"],
                "index": found["index"],
                "input": item["input"],
            }
        )

        if apply:
            cases_key = "cases" if "cases" in found["data"] else "test_cases"
            case = found["data"][cases_key][found["index"]]
            case.pop("suzume_expected", None)
            case.pop("accepted_diff", None)
            files_to_save[found["file"]] = found["data"]

    if apply:
        for path, data in files_to_save.items():
            save_json(path, data)

    return _json_result(
        {
            "status": "ok",
            "reset": reset_entries,
            "skipped": skipped_entries,
            "total": len(reset_entries),
            "applied": apply,
        }
    )


@mcp.tool()
async def test_audit_oracle_overrides(
    file: str = "",
    limit: int = 100,
) -> str:
    """Audit JSON-local Suzume overrides against the latest oracle and implementation."""
    files = _get_test_files_filtered(file or "all")
    if not files:
        return _json_error("No test files found")

    candidates = []
    for path in files:
        try:
            data = load_json(path)
        except Exception as exc:
            return _json_error(f"Failed to parse JSON file {path}: {exc}")
        cases = data.get("cases") or data.get("test_cases") or []
        for index, case in enumerate(cases):
            if not case.get("suzume_expected") and not case.get("accepted_diff"):
                continue
            candidates.append(
                {
                    "found": {
                        "file": path,
                        "basename": path.stem,
                        "index": index,
                        "case": case,
                        "data": data,
                    },
                    "input": case.get("input", ""),
                }
            )

    oracle_results = get_expected_tokens_batch_subprocess([item["input"] for item in candidates])
    entries = []
    counts = {
        "active_override": 0,
        "safe_to_reset": 0,
        "orphan_accepted_diff": 0,
        "override_without_accepted_diff": 0,
        "redundant_override": 0,
    }

    for item, (oracle_tokens, _source, rule) in zip(candidates, oracle_results, strict=True):
        found = item["found"]
        case = found["case"]
        expected = case.get("expected") or []
        override = case.get("suzume_expected") or []
        try:
            suzume_tokens = _get_suzume_tokens(item["input"])
        except RuntimeError as exc:
            return _json_error(str(exc))

        has_override = bool(override)
        has_reason = bool(case.get("accepted_diff"))
        expected_matches_oracle = tokens_match(expected, oracle_tokens)
        suzume_matches_oracle = tokens_match(suzume_tokens, oracle_tokens)
        override_matches_expected = has_override and tokens_match(override, expected)

        if has_reason and not has_override:
            status = "orphan_accepted_diff"
        elif has_override and not has_reason:
            status = "override_without_accepted_diff"
        elif expected_matches_oracle and suzume_matches_oracle:
            status = "safe_to_reset"
        else:
            status = "active_override"
        counts[status] += 1
        if override_matches_expected:
            counts["redundant_override"] += 1

        entries.append(
            {
                "id": _case_location(found),
                "input": item["input"],
                "status": status,
                "category": (case.get("accepted_diff") or {}).get("category", ""),
                "rule": rule or "mecab-only",
                "expected_matches_oracle": expected_matches_oracle,
                "suzume_matches_oracle": suzume_matches_oracle,
                "override_matches_expected": override_matches_expected,
            }
        )

    output_limit = max(0, limit)
    return _json_result(
        {
            "entries": entries[:output_limit],
            "summary": {"total": len(entries), **counts},
            "truncated": len(entries) > output_limit,
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
