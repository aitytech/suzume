"""Test corpus mutation MCP tools."""

from ..core.suzume_cli import (
    get_expected_tokens_batch_subprocess,
)
from ..core.suzume_cli import (
    get_expected_tokens_subprocess as get_expected_tokens,
)
from ..core.test_file_utils import (
    find_test_by_id,
    find_test_by_input,
    generate_id,
    get_test_data_dir,
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
async def test_add(
    input_text: str,
    file: str,
    case_id: str = "",
    description: str = "",
    use_suzume: bool = False,
) -> str:
    """Add a new test case to a test file.

    Args:
        input_text: Japanese text for the test case.
        file: Target test file name (without .json).
        case_id: Optional custom ID (auto-generated if empty).
        description: Optional description.
        use_suzume: If True, use Suzume output instead of MeCab for expected.
    """
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

    if use_suzume:
        try:
            suzume_tokens = _get_suzume_tokens(input_text)
            tokens, source, rule = suzume_tokens, "Suzume", "forced"
        except RuntimeError as exc:
            return _json_error(str(exc))
    else:
        try:
            tokens, source, rule = get_expected_tokens(input_text)
        except RuntimeError as exc:
            return _json_error(str(exc))

    try:
        expected = _format_expected_checked(tokens, source)
    except RuntimeError as exc:
        return _json_error(str(exc))
    test_id = case_id or generate_id(input_text)

    path = get_test_data_dir(PROJECT_ROOT) / f"{file}.json"
    if path.exists():
        data = load_json(path)
    else:
        data = {"version": "1.0", "description": f"{file} tests", "cases": []}

    cases_key = "cases" if "cases" in data else "test_cases"
    data.setdefault(cases_key, [])

    # Deduplicate ID
    if not case_id:
        existing_ids = {c.get("id") for c in data[cases_key]}
        if test_id in existing_ids:
            suffix = 2
            while f"{test_id}_{suffix}" in existing_ids:
                suffix += 1
            test_id = f"{test_id}_{suffix}"

    new_case = {
        "id": test_id,
        "description": description or f"{input_text} - auto-generated",
        "input": input_text,
        "expected": expected,
    }

    data[cases_key].append(new_case)
    save_json(path, data)

    return _json_result(
        {
            "status": "ok",
            "file": file,
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
        use_suzume: If True, use Suzume output for expected.
    """
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

    if use_suzume:
        try:
            suzume_tokens = _get_suzume_tokens(input_text)
            tokens, source, rule = suzume_tokens, "Suzume", "forced"
        except RuntimeError as exc:
            return _json_error(str(exc))
    else:
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
        file: Target test file name (without .json).
        inputs: List of Japanese input texts to add.
        apply: If True, actually add. Default is dry-run preview.
        use_suzume: If True, use Suzume output for expected.
    """
    to_add = []
    skipped = []

    # First pass: filter existing, collect new inputs
    new_inputs: list[tuple[int, str]] = []  # (original_index, input_text)
    for i, inp in enumerate(inputs):
        existing = find_test_by_input(PROJECT_ROOT, inp)
        if existing:
            skipped.append({"input": inp, "reason": f"exists at {existing['basename']}/{existing['index']}"})
            continue
        new_inputs.append((i, inp))

    # Batch get expected tokens for all new inputs
    if not use_suzume and new_inputs:
        batch_texts = [inp for _, inp in new_inputs]
        batch_results = get_expected_tokens_batch_subprocess(batch_texts)
    else:
        batch_results = None

    for batch_idx, (_orig_idx, inp) in enumerate(new_inputs):
        if use_suzume:
            try:
                suzume_tokens = _get_suzume_tokens(inp)
                tokens, source, rule = suzume_tokens, "Suzume", "forced"
            except RuntimeError as exc:
                skipped.append({"input": inp, "reason": str(exc)})
                continue
        else:
            tokens, source, rule = batch_results[batch_idx]  # type: ignore[index]
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
            }
        )

    if not apply:
        # Strip internal fields for output
        to_add_out = [
            {
                "input": item["input"],
                "id": item["id"],
                "surfaces": item["surfaces"],
                "source": item["source"],
                "rule": item["rule"],
            }
            for item in to_add
        ]
        return _json_result(
            {
                "file": file,
                "to_add": to_add_out,
                "skipped": skipped,
                "applied": False,
            }
        )

    path = get_test_data_dir(PROJECT_ROOT) / f"{file}.json"
    if path.exists():
        data = load_json(path)
    else:
        data = {"version": "1.0", "description": f"{file} tests", "cases": []}

    cases_key = "cases" if "cases" in data else "test_cases"
    data.setdefault(cases_key, [])

    for item in to_add:
        data[cases_key].append(
            {
                "id": item["id"],
                "description": f"{item['input']} - regression test",
                "input": item["input"],
                "expected": item["_expected"],
            }
        )

    save_json(path, data)

    to_add_out = [
        {
            "input": item["input"],
            "id": item["id"],
            "surfaces": item["surfaces"],
            "source": item["source"],
            "rule": item["rule"],
        }
        for item in to_add
    ]
    return _json_result(
        {
            "file": file,
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
