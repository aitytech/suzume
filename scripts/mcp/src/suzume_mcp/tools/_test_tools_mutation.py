"""Test corpus mutation MCP tools."""

from ..core.suzume_cli import (
    get_expected_tokens_batch_subprocess,
)
from ..core.suzume_cli import (
    get_expected_tokens_subprocess as get_expected_tokens,
)
from ..core.test_file_suggestions import suggest_test_files
from ..core.test_file_utils import (
    find_test_by_id,
    find_test_by_input,
    generate_id,
    get_test_data_dir,
    load_json,
    normalize_test_file_name,
    save_json,
)
from ..server import PROJECT_ROOT, mcp
from ._test_tools_common import (
    ORACLE_OVERRIDE_REMEDIATION,
    _format_expected_checked,
    _get_test_files_filtered,
    _json_error,
    _json_result,
)
from ._test_tools_organization import append_cases_partitioned

# Writing Suzume's own output into `expected` is the same banned per-case oracle as
# suzume_expected, only harder to see: the stored case keeps no trace of where the
# expectation came from, so nothing downstream can flag it.
_USE_SUZUME_REFUSAL = f"use_suzume is disabled by design. {ORACLE_OVERRIDE_REMEDIATION}"


def _deduplicate_generated_ids(items: list[dict]) -> None:
    existing_ids = set()
    test_dir = get_test_data_dir(PROJECT_ROOT)
    for path in test_dir.glob("*.json"):
        data = load_json(path)
        cases_key = "cases" if "cases" in data else "test_cases"
        existing_ids.update(str(case.get("id", "")) for case in data.get(cases_key) or [])

    for item in items:
        base_id = item["id"]
        test_id = base_id
        suffix = 2
        while test_id in existing_ids:
            test_id = f"{base_id}_{suffix}"
            suffix += 1
        item["id"] = test_id
        existing_ids.add(test_id)


@mcp.tool()
async def test_add(
    input_text: str,
    file: str = "",
    case_id: str = "",
    description: str = "",
    use_suzume: bool = False,
) -> str:
    """Add a new test case to a test file.

    Args:
        input_text: Japanese text for the test case.
        file: Optional logical test file name. Empty uses the highest-ranked automatic suggestion.
        case_id: Optional custom ID (auto-generated if empty).
        description: Optional description.
        use_suzume: Rejected. Kept only so the refusal is reached instead of a schema error.
    """
    if use_suzume:
        return _json_error(_USE_SUZUME_REFUSAL)

    # Check for duplicates
    existing = find_test_by_input(PROJECT_ROOT, input_text)
    if existing:
        return _json_result(
            {
                "status": "error",
                "message": "Duplicate input rejected",
                "existing": {"file": existing["basename"], "index": existing["index"]},
            }
        )

    try:
        tokens, source, rule = get_expected_tokens(input_text)
    except RuntimeError as exc:
        return _json_error(str(exc))

    try:
        expected = _format_expected_checked(tokens, source)
    except RuntimeError as exc:
        return _json_error(str(exc))
    test_id = case_id or generate_id(input_text)

    if file:
        try:
            file_name = normalize_test_file_name(file)
        except ValueError as exc:
            return _json_error(str(exc))
        placement_source = "requested"
    else:
        suggestions = suggest_test_files(input_text, tokens)
        if not suggestions:
            return _json_error("Could not suggest a test file")
        file_name = suggestions[0]
        placement_source = "suggested"

    if not case_id:
        id_holder = {"id": test_id}
        _deduplicate_generated_ids([id_holder])
        test_id = id_holder["id"]

    new_case = {
        "id": test_id,
        "description": description or f"{input_text} - auto-generated",
        "input": input_text,
        "expected": expected,
    }

    try:
        placement = append_cases_partitioned(PROJECT_ROOT, file_name, [new_case])
    except (OSError, ValueError) as exc:
        return _json_error(str(exc))
    actual_file = placement["case_locations"][0]["file"]

    return _json_result(
        {
            "status": "ok",
            "file": actual_file,
            "requested_file": file_name,
            "placement_source": placement_source,
            "split": placement["split"],
            "input": input_text,
            "id": test_id,
            "source": source,
            "rule": rule,
        }
    )


@mcp.tool()
async def test_update(
    input_text: str = "",
    test_id: str = "",
    use_suzume: bool = False,
) -> str:
    """Update an existing test case's expected value.

    Args:
        input_text: Input text to find and update (mutually exclusive with test_id).
        test_id: Test ID to update (format: file/index or file/id_string).
        use_suzume: Rejected. Kept only so the refusal is reached instead of a schema error.
    """
    if use_suzume:
        return _json_error(_USE_SUZUME_REFUSAL)

    if test_id:
        found = find_test_by_id(PROJECT_ROOT, test_id)
        if not found:
            return _json_error(f"Test not found: {test_id}")
        input_text = found["case"]["input"]
    elif input_text:
        found = find_test_by_input(PROJECT_ROOT, input_text)
        if not found:
            return _json_error(f"No test found for input: {input_text}")
    else:
        return _json_error("Either input_text or test_id is required.")

    try:
        tokens, source, rule = get_expected_tokens(input_text)
    except RuntimeError as exc:
        return _json_error(str(exc))

    try:
        expected = _format_expected_checked(tokens, source)
    except RuntimeError as exc:
        return _json_error(str(exc))
    cases_key = "cases" if "cases" in found["data"] else "test_cases"
    old_expected = found["case"].get("expected", [])

    found["data"][cases_key][found["index"]]["expected"] = expected
    save_json(found["file"], found["data"])

    old_surfaces = "|".join(t.get("surface", "") for t in old_expected)
    new_surfaces = "|".join(t["surface"] for t in expected)

    return _json_result(
        {
            "status": "ok",
            "file": found["basename"],
            "index": found["index"],
            "input": input_text,
            "old_surfaces": old_surfaces,
            "new_surfaces": new_surfaces,
            "source": source,
        }
    )


@mcp.tool()
async def test_delete(
    input_text: str = "",
    test_id: str = "",
) -> str:
    """Delete a test case.

    Args:
        input_text: Input text to find and delete.
        test_id: Test ID to delete (format: file/index or file/id_string).
    """
    if test_id:
        found = find_test_by_id(PROJECT_ROOT, test_id)
        if not found:
            return _json_error(f"Test not found: {test_id}")
    elif input_text:
        found = find_test_by_input(PROJECT_ROOT, input_text)
        if not found:
            return _json_error(f"No test found for input: {input_text}")
    else:
        return _json_error("Either input_text or test_id is required.")

    case = found["case"]
    case_id = case.get("id", found["index"])
    surfaces = " ".join(t.get("surface", "") for t in (case.get("expected") or []))

    cases_key = "cases" if "cases" in found["data"] else "test_cases"
    del found["data"][cases_key][found["index"]]
    save_json(found["file"], found["data"])

    return _json_result(
        {
            "status": "ok",
            "file": found["basename"],
            "id": case_id,
            "input": case.get("input", ""),
            "surfaces": surfaces,
        }
    )


@mcp.tool()
async def test_batch_add(
    file: str,
    inputs: list[str],
    apply: bool = False,
    use_suzume: bool = False,
) -> str:
    """Batch add multiple test cases.

    Args:
        file: Logical test file name. An empty string groups inputs by their highest-ranked suggestions.
        inputs: List of Japanese input texts to add.
        apply: If True, actually add. Default is dry-run preview.
        use_suzume: Rejected. Kept only so the refusal is reached instead of a schema error.
    """
    if use_suzume:
        return _json_error(_USE_SUZUME_REFUSAL)

    if file:
        try:
            file_name = normalize_test_file_name(file)
        except ValueError as exc:
            return _json_error(str(exc))
    else:
        file_name = ""

    to_add = []
    skipped = []

    # First pass: filter existing, collect new inputs
    new_inputs: list[tuple[int, str]] = []  # (original_index, input_text)
    seen_inputs: set[str] = set()
    for i, inp in enumerate(inputs):
        if inp in seen_inputs:
            skipped.append({"input": inp, "reason": "duplicate within request"})
            continue
        seen_inputs.add(inp)
        existing = find_test_by_input(PROJECT_ROOT, inp)
        if existing:
            skipped.append({"input": inp, "reason": f"exists at {existing['basename']}/{existing['index']}"})
            continue
        new_inputs.append((i, inp))

    # Batch get expected tokens for all new inputs
    batch_results = get_expected_tokens_batch_subprocess([inp for _, inp in new_inputs]) if new_inputs else []

    for batch_idx, (_orig_idx, inp) in enumerate(new_inputs):
        tokens, source, rule = batch_results[batch_idx]
        try:
            expected = _format_expected_checked(tokens, source)
        except RuntimeError as exc:
            skipped.append({"input": inp, "reason": str(exc)})
            continue
        to_add.append(
            {
                "input": inp,
                "id": generate_id(inp),
                "surfaces": "|".join(e["surface"] for e in expected),
                "source": source,
                "rule": rule,
                "_expected": expected,
                "_file": file_name or suggest_test_files(inp, tokens)[0],
            }
        )

    _deduplicate_generated_ids(to_add)

    placements = []
    grouped_items: dict[str, list[dict]] = {}
    for item in to_add:
        grouped_items.setdefault(item["_file"], []).append(item)
    for logical_file, items in grouped_items.items():
        new_cases = [
            {
                "id": item["id"],
                "description": f"{item['input']} - regression test",
                "input": item["input"],
                "expected": item["_expected"],
            }
            for item in items
        ]
        try:
            placement = append_cases_partitioned(PROJECT_ROOT, logical_file, new_cases, apply=apply)
        except (OSError, ValueError) as exc:
            return _json_error(str(exc))
        placements.append(placement)
        for item, location in zip(items, placement["case_locations"], strict=True):
            item["file"] = location["file"]

    if not apply:
        # Strip internal fields for output
        to_add_out = [
            {
                "input": item["input"],
                "id": item["id"],
                "surfaces": item["surfaces"],
                "source": item["source"],
                "rule": item["rule"],
                "file": item["file"],
            }
            for item in to_add
        ]
        return _json_result(
            {
                "file": file_name,
                "files": [entry for placement in placements for entry in placement["files"]],
                "to_add": to_add_out,
                "skipped": skipped,
                "applied": False,
            }
        )

    to_add_out = [
        {
            "input": item["input"],
            "id": item["id"],
            "surfaces": item["surfaces"],
            "source": item["source"],
            "rule": item["rule"],
            "file": item["file"],
        }
        for item in to_add
    ]
    return _json_result(
        {
            "file": file_name,
            "files": [entry for placement in placements for entry in placement["files"]],
            "to_add": to_add_out,
            "skipped": skipped,
            "applied": True,
        }
    )


@mcp.tool()
async def test_replace_pos(
    old_pos: str,
    new_pos: str,
    file: str = "",
    apply: bool = False,
) -> str:
    """Replace POS in all test files (dry-run by default).

    Args:
        old_pos: POS value to replace.
        new_pos: New POS value.
        file: Optional test file filter (without .json), or empty for all.
        apply: If True, apply changes. Default is dry-run.
    """
    files = _get_test_files_filtered(file or "all")
    if not files:
        return _json_error("No test files found")

    changes = []
    for path in files:
        try:
            data = load_json(path)
        except Exception as exc:
            return _json_error(f"Failed to parse JSON file {path}: {exc}")
        cases_key = "cases" if "cases" in data else "test_cases"
        cases = data.get(cases_key) or []
        file_changes = 0

        for case in cases:
            for token in case.get("expected") or []:
                if token.get("pos") == old_pos:
                    changes.append({"file": path.stem, "case_id": case.get("id", ""), "surface": token["surface"]})
                    if apply:
                        token["pos"] = new_pos
                    file_changes += 1

        if apply and file_changes > 0:
            save_json(path, data)

    if not changes:
        return _json_result(
            {
                "old_pos": old_pos,
                "new_pos": new_pos,
                "changes": [],
                "total": 0,
                "applied": apply,
            }
        )

    return _json_result(
        {
            "old_pos": old_pos,
            "new_pos": new_pos,
            "changes": changes,
            "total": len(changes),
            "applied": apply,
        }
    )


@mcp.tool()
async def test_map_pos(
    surface: str,
    old_pos: str,
    new_pos: str,
    file: str = "",
    apply: bool = False,
) -> str:
    """Replace POS only for a specific surface (dry-run by default).

    Args:
        surface: Token surface to match.
        old_pos: Current POS value to replace.
        new_pos: New POS value.
        file: Optional test file filter.
        apply: If True, apply changes.
    """
    files = _get_test_files_filtered(file or "all")
    if not files:
        return _json_error("No test files found")

    changes = []
    for path in files:
        try:
            data = load_json(path)
        except Exception as exc:
            return _json_error(f"Failed to parse JSON file {path}: {exc}")
        cases_key = "cases" if "cases" in data else "test_cases"
        cases = data.get(cases_key) or []
        file_changes = 0

        for case in cases:
            for token in case.get("expected") or []:
                if token.get("surface") == surface and token.get("pos") == old_pos:
                    changes.append({"file": path.stem, "case_id": case.get("id", ""), "surface": surface})
                    if apply:
                        token["pos"] = new_pos
                    file_changes += 1

        if apply and file_changes > 0:
            save_json(path, data)

    if not changes:
        return _json_result(
            {
                "old_pos": old_pos,
                "new_pos": new_pos,
                "surface": surface,
                "changes": [],
                "total": 0,
                "applied": apply,
            }
        )

    return _json_result(
        {
            "old_pos": old_pos,
            "new_pos": new_pos,
            "surface": surface,
            "changes": changes,
            "total": len(changes),
            "applied": apply,
        }
    )


@mcp.tool()
async def test_list_pos(file: str = "") -> str:
    """List all POS values used in test expectations.

    Args:
        file: Optional test file filter (without .json).
    """
    files = _get_test_files_filtered(file or "all")
    if not files:
        return _json_error("No test files found")

    pos_counts: dict[str, int] = {}
    pos_examples: dict[str, list[str]] = {}

    for path in files:
        try:
            data = load_json(path)
        except Exception as exc:
            return _json_error(f"Failed to parse JSON file {path}: {exc}")
        cases = data.get("cases") or data.get("test_cases") or []
        for case in cases:
            for token in case.get("expected") or []:
                pos = token.get("pos", "UNKNOWN")
                pos_counts[pos] = pos_counts.get(pos, 0) + 1
                if pos not in pos_examples:
                    pos_examples[pos] = []
                if len(pos_examples[pos]) < 3:
                    pos_examples[pos].append(token.get("surface", ""))

    pos_values = []
    for pos in sorted(pos_counts.keys(), key=lambda p: -pos_counts[p]):
        pos_values.append(
            {
                "pos": pos,
                "count": pos_counts[pos],
                "examples": pos_examples[pos],
            }
        )

    return _json_result({"pos_values": pos_values})
