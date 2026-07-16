"""Test corpus review, validation, and coverage MCP tools."""

import re
from pathlib import Path

from ..core.suzume_cli import (
    get_expected_tokens_subprocess as get_expected_tokens,
)
from ..core.test_file_utils import (
    find_test_by_input,
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


@mcp.tool()
async def test_accept_diff(
    input_text: str = "",
    reason: str = "",
    category: str = "pos-limitation",
    all_failed: bool = False,
    test_output_file: str = "/tmp/test.txt",
    apply: bool = False,
) -> str:
    """Accept Suzume's current output as valid by adding suzume_expected field.

    Keeps the normalized reference expected for comparison while marking Suzume's
    output as an intentional tokenizer difference.

    Args:
        input_text: Input text to accept diff for (mutually exclusive with all_failed).
        reason: Reason for accepting the diff (required).
        category: Diff category (default: pos-limitation).
        all_failed: If True, process all failed tests from test output file.
        test_output_file: Path to ctest output file (used with all_failed).
        apply: If True, apply changes. Default is dry-run.
    """
    if not reason:
        return _json_error("reason is required. Explain why this diff is acceptable.")

    if not input_text and not all_failed:
        return _json_error("Either input_text or all_failed=True is required.")

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
            found = find_test_by_input(PROJECT_ROOT, failure["input"])
            if not found:
                continue
            if found["case"].get("suzume_expected"):
                continue
            to_update.append({"input": failure["input"], "found": found})
    else:
        found = find_test_by_input(PROJECT_ROOT, input_text)
        if not found:
            return _json_error(f"No test found for input: {input_text}")
        if found["case"].get("suzume_expected"):
            return _json_error(f"Test already has suzume_expected: {found['basename']}/{found['index']}")
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
) -> str:
    """Remove stale suzume_expected field from test cases.

    Use when Suzume now matches expected (no override needed).

    Args:
        input_text: Input text to reset (mutually exclusive with all_tests).
        all_tests: If True, find all tests with suzume_expected.
        file: Optional test file filter (without .json).
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
                if case.get("suzume_expected"):
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
    elif input_text:
        found = find_test_by_input(PROJECT_ROOT, input_text)
        if not found:
            return _json_error(f"No test found for input: {input_text}")
        if not found["case"].get("suzume_expected"):
            return _json_error(f"Test has no suzume_expected: {found['basename']}/{found['index']}")
        to_reset.append({"input": input_text, "found": found})
    else:
        return _json_error("Either input_text or all_tests=True is required.")

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
    files_to_save: dict[Path, dict] = {}
    removed = 0

    for item in to_reset:
        found = item["found"]
        reset_entries.append(
            {
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
            removed += 1

    if apply:
        for path, data in files_to_save.items():
            save_json(path, data)

    return _json_result(
        {
            "status": "ok",
            "reset": reset_entries,
            "total": len(to_reset),
            "applied": apply,
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

    pos_count: dict[str, int] = {}
    for tok in tokens:
        pos_count[tok["pos"]] = pos_count.get(tok["pos"], 0) + 1

    suggestions = []
    first_pos = tokens[0]["pos"]
    has_verb = "Verb" in pos_count
    has_aux = "Auxiliary" in pos_count
    has_adj = "Adjective" in pos_count

    if has_adj and first_pos == "Adjective":
        if input_text.endswith("そう"):
            suggestions.append("adjective_i_compound")
        elif "く" in input_text:
            suggestions.append("adjective_i_ku")
        elif "かっ" in input_text:
            suggestions.append("adjective_i_katta")
        else:
            suggestions.append("adjective_i_basic")

    if has_verb:
        for tok in tokens:
            if tok["pos"] == "Verb":
                lemma = tok.get("lemma", "")
                if lemma.endswith("する"):
                    suggestions.append("verb_suru")
                elif re.search(r"(来る|くる)$", lemma):
                    suggestions.append("verb_irregular")
                else:
                    suggestions.append("verb_godan_misc")
                break
        if any(tok["surface"] in ("て", "で") for tok in tokens):
            suggestions.append("verb_te_ta")

    if has_aux and not has_verb and not has_adj:
        suggestions.append("auxiliary_modality")

    if first_pos == "Noun":
        suggestions.append("noun_general")

    if re.search(r"(です|ます)", input_text):
        suggestions.append("auxiliary_politeness")
    if input_text.endswith("ない"):
        suggestions.append("auxiliary_negation")
    if re.search(r"(だ|である)", input_text):
        suggestions.append("copula")

    if not suggestions:
        suggestions.append("basic")

    # Deduplicate while preserving order
    seen: set[str] = set()
    unique = []
    for sug in suggestions:
        if sug not in seen:
            seen.add(sug)
            unique.append(sug)

    token_list = [{"surface": tok["surface"], "pos": tok["pos"]} for tok in tokens]

    return _json_result(
        {
            "input": input_text,
            "tokens": token_list,
            "suggestions": unique,
        }
    )
