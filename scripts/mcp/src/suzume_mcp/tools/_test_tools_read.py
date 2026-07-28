"""Read-only test inspection and comparison MCP tools."""

import json
import re
from pathlib import Path

from ..core.diff_utils import classify_surface_diff
from ..core.pos_mapping import normalize_pos
from ..core.suzume_cli import (
    format_expected_from_tokens as format_expected,
)
from ..core.suzume_cli import (
    get_expected_tokens_batch_subprocess,
    get_mecab_tokens_batch_subprocess,
    get_suzume_debug_info,
)
from ..core.suzume_cli import (
    get_expected_tokens_subprocess as get_expected_tokens,
)
from ..core.suzume_utils import tokens_match
from ..core.test_file_utils import (
    cases_key as canonical_cases_key,
)
from ..core.test_file_utils import (
    find_test_by_id,
    find_test_by_input,
    get_failures_from_test_output,
    get_test_data_dir,
    get_test_files,
    load_json,
    save_json,
)
from ..server import PROJECT_ROOT, mcp
from ._test_tools_common import (
    _detect_segmentation_pattern,
    _format_expected_checked,
    _get_suzume_tokens,
    _get_test_files_filtered,
    _json_error,
    _json_result,
)


@mcp.tool()
async def test_show(
    input_text: str,
    mode: str = "default",
) -> str:
    """Compare MeCab expected vs Suzume output for a given input.

    Args:
        input_text: Japanese text to analyze.
        mode: Output mode - "default", "brief", "tsv", "debug", or "json".

    Returns:
        Comparison result showing expected tokens, Suzume tokens, and diff classification.
    """
    expected_tokens, source, rule = get_expected_tokens(input_text)
    expected_surfaces = [t["surface"] for t in expected_tokens]

    suzume_tokens = _get_suzume_tokens(input_text)
    suzume_surfaces = [t["surface"] for t in suzume_tokens]

    found = find_test_by_input(PROJECT_ROOT, input_text)

    # Diff classification
    surface_match = "|".join(expected_surfaces) == "|".join(suzume_surfaces)
    full_match = surface_match and tokens_match(expected_tokens, suzume_tokens)
    diff_type = classify_surface_diff(expected_surfaces, suzume_surfaces)
    diff_details = []

    if not surface_match:
        e_count = len(expected_surfaces)
        s_count = len(suzume_surfaces)
        max_len = max(e_count, s_count)
        for idx in range(max_len):
            exp = expected_surfaces[idx] if idx < e_count else ""
            suz = suzume_surfaces[idx] if idx < s_count else ""
            if exp != suz:
                diff_details.append({"index": idx, "expected": exp, "suzume": suz, "type": "surface"})
    elif not full_match:
        diff_type = "pos-lemma"
        for idx in range(len(expected_tokens)):
            exp = expected_tokens[idx]
            suz = suzume_tokens[idx]
            e_pos = normalize_pos(exp.get("pos", ""))
            s_pos = normalize_pos(suz.get("pos", ""))
            e_lemma = exp.get("lemma", exp["surface"])
            s_lemma = suz.get("lemma", suz["surface"])
            if e_pos != s_pos:
                diff_details.append(
                    {"index": idx, "surface": exp["surface"], "expected": e_pos, "suzume": s_pos, "type": "pos"}
                )
            if e_lemma != s_lemma:
                diff_details.append(
                    {"index": idx, "surface": exp["surface"], "expected": e_lemma, "suzume": s_lemma, "type": "lemma"}
                )

    # TSV mode - keep plain text for copy-paste
    if mode == "tsv":
        lines = []
        for tok in expected_tokens:
            line = f"{tok['surface']}\t{tok['pos']}"
            if tok.get("lemma") and tok["lemma"] != tok["surface"]:
                line += f"\t{tok['lemma']}"
            lines.append(line)
        return "\n".join(lines)

    # JSON mode - keep as-is (already returns JSON)
    if mode == "json":
        return json.dumps(format_expected(expected_tokens), ensure_ascii=False, indent=2)

    # Build result dict for default/brief/debug modes
    test_exists = None
    if found:
        test_exists = {
            "file": found["basename"],
            "id": found["case"].get("id", str(found["index"])),
        }

    result = {
        "input": input_text,
        "expected": expected_surfaces,
        "suzume": suzume_surfaces if suzume_surfaces else None,
        "match": full_match,
        "diff_type": diff_type,
        "diff_details": diff_details,
        "rule": rule,
        "test_exists": test_exists,
    }

    if mode == "brief":
        result["mode"] = "brief"

    # Debug info
    if mode == "debug":
        info = await get_suzume_debug_info(input_text)
        if info.get("best_path"):
            result["scoring"] = {
                "total_cost": info["total_cost"],
                "margin": info["margin"],
                "best_path": info["best_path"],
            }

    return _json_result(result)


@mcp.tool()
async def test_list() -> str:
    """List all test files with case counts."""
    test_dir = get_test_data_dir(PROJECT_ROOT)
    if not test_dir.exists():
        return _json_error("Test data directory not found")

    files = []
    total = 0
    for path in sorted(test_dir.glob("*.json")):
        try:
            data = load_json(path)
            cases = data.get(canonical_cases_key(data, str(path))) or []
            count = len(cases)
        except Exception:
            count = 0
        total += count
        files.append({"name": path.stem, "count": count})

    return _json_result({"files": files, "total": total})


@mcp.tool()
async def test_search(pattern: str, limit: int = 0) -> str:
    """Search test cases by regex pattern (matches input, surfaces, and ID).

    Args:
        pattern: Regex pattern to search for.
        limit: Max results to show (0 = unlimited).
    """
    try:
        regex = re.compile(pattern, re.IGNORECASE)
    except re.error as exc:
        return _json_error(f"Invalid regex: {exc}")

    matches = []
    for path in get_test_files(PROJECT_ROOT):
        try:
            data = load_json(path)
        except Exception as exc:
            return _json_error(f"Failed to parse JSON file {path}: {exc}")
        basename = path.stem
        cases = data.get(canonical_cases_key(data, str(path))) or []
        for idx, case in enumerate(cases):
            case_id = case.get("id", str(idx))
            inp = case.get("input", "")
            surfaces = " ".join(t.get("surface", "") for t in (case.get("expected") or []))
            if regex.search(inp) or regex.search(surfaces) or regex.search(str(case_id)):
                entry = {
                    "file": basename,
                    "index": idx,
                    "id": case_id,
                    "input": inp,
                    "expected": surfaces,
                }
                matches.append(entry)

    if limit > 0:
        matches_limited = matches[:limit]
    else:
        matches_limited = matches

    return _json_result(
        {
            "pattern": pattern,
            "matches": matches_limited,
            "total": len(matches),
        }
    )


@mcp.tool()
async def test_failed(
    test_output_file: str = "/tmp/test.txt",
    limit: int = 0,
    verbose: bool = False,
    grep: str = "",
) -> str:
    """List failed test inputs from test output file.

    Args:
        test_output_file: Path to ctest output file.
        limit: Max results (0 = unlimited).
        verbose: Show test IDs alongside inputs.
        grep: Filter pattern for inputs/IDs.
    """
    failures = get_failures_from_test_output(test_output_file)
    if not failures:
        return _json_result(
            {
                "source": test_output_file,
                "failures": [],
                "total": 0,
            }
        )

    if grep:
        try:
            rxp = re.compile(grep)
        except re.error:
            return _json_error(f"Invalid grep pattern: {grep}")
        failures = [f for f in failures if rxp.search(f["input"]) or rxp.search(f["id"])]

    if limit > 0:
        failures_limited = failures[:limit]
    else:
        failures_limited = failures

    return _json_result(
        {
            "source": test_output_file,
            "failures": [{"input": f["input"], "id": f["id"]} for f in failures_limited],
            "total": len(failures),
        }
    )


@mcp.tool()
async def test_compare(before_file: str, after_file: str) -> str:
    """Compare two test outputs to show improved/regressed cases.

    Args:
        before_file: Path to before test output.
        after_file: Path to after test output.
    """

    def extract_failures(filepath: str) -> dict[str, str]:
        failures = {}
        inp = ""
        for line in Path(filepath).read_text(encoding="utf-8").splitlines():
            m_inp = re.search(r"Input:\s*(.+)", line)
            if m_inp:
                inp = m_inp.group(1)
            m_fail = re.search(r"FAILED.*Tokenize/([^,]+)", line)
            if m_fail and inp:
                failures[m_fail.group(1)] = inp
                inp = ""
        return failures

    before = extract_failures(before_file)
    after = extract_failures(after_file)

    improved = sorted([{"id": key, "input": before[key]} for key in before if key not in after], key=lambda x: x["id"])
    regressed = sorted([{"id": key, "input": after[key]} for key in after if key not in before], key=lambda x: x["id"])

    net_change = len(after) - len(before)

    return _json_result(
        {
            "before_failures": len(before),
            "after_failures": len(after),
            "improved": improved,
            "regressed": regressed,
            "net_change": net_change,
        }
    )


@mcp.tool()
async def test_diff_suzume(
    limit: int = 10,
    test_output_file: str = "/tmp/test.txt",
) -> str:
    """Analyze test failures by category (segmentation, POS-only, matches-correct).

    Args:
        limit: Max items per category to show (0 = all). Also limits processing to limit*5 failures.
        test_output_file: Path to ctest output file.
    """
    failures = get_failures_from_test_output(test_output_file)
    if not failures:
        return _json_result(
            {
                "categories": {"matches_correct": [], "segmentation": {}, "pos_only": []},
                "summary": {
                    "matches_correct": 0,
                    "segmentation": 0,
                    "pos_only": 0,
                    "total_failures": 0,
                    "processed": 0,
                },
            }
        )

    total_failures = len(failures)
    max_process = limit * 5 if limit > 0 else 0

    categories: dict[str, list[dict]] = {"matches_correct": [], "segmentation": [], "pos_only": []}
    processed = 0

    for failure in failures:
        if max_process and processed >= max_process:
            break
        found = find_test_by_id(PROJECT_ROOT, failure["id"])
        if not found:
            continue

        test_expected = found["case"].get("expected") or []
        correct_tokens, source, rule = get_expected_tokens(failure["input"])
        suzume_tokens = _get_suzume_tokens(failure["input"])
        processed += 1

        test_str = "|".join(t.get("surface", "") for t in test_expected)
        suz_str = "|".join(t["surface"] for t in suzume_tokens)
        cor_str = "|".join(t["surface"] for t in correct_tokens)

        entry = {
            "id": failure["id"],
            "input": failure["input"],
            "test_expected": test_str,
            "suzume": suz_str,
            "correct": cor_str,
            "source": source,
            "rule": rule,
        }

        if tokens_match(correct_tokens, suzume_tokens):
            categories["matches_correct"].append(entry)
        elif cor_str != suz_str and len(cor_str.split("|")) != len(suz_str.split("|")):
            categories["segmentation"].append(entry)
        else:
            categories["pos_only"].append(entry)

    # Group segmentation by pattern
    seg_patterns: dict[str, list[dict]] = {}
    for entry in categories["segmentation"]:
        pat = _detect_segmentation_pattern(entry["correct"], entry["suzume"], entry["input"])
        seg_patterns.setdefault(pat, []).append(entry)

    per_cat = limit if limit > 0 else 0

    # Build output categories
    matches_correct_out = categories["matches_correct"][:per_cat] if per_cat else categories["matches_correct"]
    pos_only_out = categories["pos_only"][:per_cat] if per_cat else categories["pos_only"]

    seg_out = {}
    for pat in sorted(seg_patterns.keys(), key=lambda p: -len(seg_patterns[p])):
        entries = seg_patterns[pat]
        seg_out[pat] = {
            "count": len(entries),
            "examples": [
                {"id": ent["id"], "input": ent["input"], "correct": ent["correct"], "suzume": ent["suzume"]}
                for ent in (entries[:per_cat] if per_cat else entries)
            ],
        }

    return _json_result(
        {
            "categories": {
                "matches_correct": [
                    {
                        "id": ent["id"],
                        "input": ent["input"],
                        "test_expected": ent["test_expected"],
                        "suzume": ent["suzume"],
                        "correct": ent["correct"],
                        "rule": ent["rule"],
                        "source": ent["source"],
                    }
                    for ent in matches_correct_out
                ],
                "segmentation": seg_out,
                "pos_only": [
                    {"id": ent["id"], "input": ent["input"], "correct": ent["correct"], "suzume": ent["suzume"]}
                    for ent in pos_only_out
                ],
            },
            "summary": {
                "matches_correct": len(categories["matches_correct"]),
                "segmentation": len(categories["segmentation"]),
                "pos_only": len(categories["pos_only"]),
                "total_failures": total_failures,
                "processed": processed,
            },
        }
    )


@mcp.tool()
async def test_diff_mecab(file: str = "") -> str:
    """Find tests where expected differs from MeCab output, categorized by type.

    Args:
        file: Optional test file to check (without .json), or empty for all.
    """
    files = _get_test_files_filtered(file)
    if not files:
        return _json_error("No test files found")

    categories: dict[str, list[dict]] = {
        "intentional": [],
        "segmentation": [],
        "pos_only": [],
        "lemma_only": [],
    }
    total_cases = 0
    mecab_compatible = 0
    errors: list[dict] = []
    case_metadata: list[dict] = []
    inputs: list[str] = []

    for path in files:
        try:
            data = load_json(path)
        except Exception as exc:
            return _json_error(f"Failed to parse JSON file {path}: {exc}")
        basename = path.stem
        cases = data.get(canonical_cases_key(data, str(path))) or []
        for idx, case in enumerate(cases):
            inp = case.get("input", "")
            if not inp:
                continue
            total_cases += 1
            case_metadata.append({"basename": basename, "index": idx, "case": case, "input": inp})
            inputs.append(inp)

    mecab_results = get_mecab_tokens_batch_subprocess(inputs)
    for meta, (mecab, source, rule) in zip(case_metadata, mecab_results, strict=True):
        case = meta["case"]
        inp = meta["input"]
        idx = meta["index"]
        basename = meta["basename"]
        case_id = case.get("id", str(idx))
        if source == "error":
            errors.append({"id": f"{basename}/{case_id}", "input": inp, "error": rule})
            continue
        expected = case.get("expected") or []

        if tokens_match(expected, mecab):
            mecab_compatible += 1
            continue

        exp_str = "|".join(t.get("surface", "") for t in expected)
        mec_str = "|".join(t["surface"] for t in mecab)

        entry = {"id": f"{basename}/{case_id}", "input": inp, "expected": exp_str, "mecab": mec_str, "rule": rule}

        if exp_str == mec_str:
            exp_pos = "|".join(t.get("pos", "") for t in expected)
            mec_pos = "|".join(t.get("pos", "") for t in mecab)
            if exp_pos == mec_pos:
                entry["expected_full"] = "|".join(
                    f"{t.get('surface', '')}/{t.get('pos', '')}/{t.get('lemma', t.get('surface', ''))}"
                    for t in expected
                )
                entry["mecab_full"] = "|".join(
                    f"{t['surface']}/{t.get('pos', '')}/{t.get('lemma', t['surface'])}" for t in mecab
                )
                categories["intentional" if rule else "lemma_only"].append(entry)
            else:
                entry["expected_pos"] = exp_pos
                entry["mecab_pos"] = mec_pos
                categories["intentional" if rule else "pos_only"].append(entry)
        else:
            categories["intentional" if rule else "segmentation"].append(entry)

    if total_cases == 0:
        return _json_error("No test cases found")

    processed = total_cases - len(errors)
    incompatible = processed - mecab_compatible

    # Limit each category to 20 items in output
    cat_out: dict[str, list[dict]] = {}
    for cat_name in ("intentional", "segmentation", "pos_only", "lemma_only"):
        cat_out[cat_name] = categories[cat_name][:20]

    return _json_result(
        {
            "categories": cat_out,
            "errors": errors[:20],
            "summary": {
                "total_cases": total_cases,
                "processed": processed,
                "errors": len(errors),
                "mecab_compatible": mecab_compatible,
                "mecab_compatible_pct": round(100.0 * mecab_compatible / processed, 1) if processed else 0,
                "incompatible": incompatible,
                "intentional": len(categories["intentional"]),
                "segmentation": len(categories["segmentation"]),
                "pos_only": len(categories["pos_only"]),
                "lemma_only": len(categories["lemma_only"]),
            },
        }
    )


@mcp.tool()
async def test_needs_suzume_update(
    file: str = "",
    apply: bool = False,
    test_ids: list[str] | None = None,
) -> str:
    """Find tests where expected doesn't match MeCab+SuzumeRules.

    Args:
        file: Optional test file (without .json), or empty for all.
        apply: If True, update test expectations. Default is dry-run.
        test_ids: Optional stable basename/id selectors for a safe partial sync.
    """
    selected_ids = set(test_ids or [])
    if file and selected_ids:
        return _json_error("file and test_ids cannot be combined")

    files = _get_test_files_filtered(file)
    if not files:
        return _json_error("No test files found")

    needs_update = []
    by_rule: dict[str, list[dict]] = {}
    normalization_errors: list[dict] = []

    # Collect all cases with their metadata for batch processing
    all_cases_meta: list[dict] = []
    all_inputs: list[str] = []

    for path in files:
        try:
            data = load_json(path)
        except Exception as exc:
            return _json_error(f"Failed to parse JSON file {path}: {exc}")
        basename = path.stem
        cases_key = canonical_cases_key(data, str(path))
        cases = data.get(cases_key) or []

        for idx, case in enumerate(cases):
            inp = case.get("input", "")
            if not inp:
                continue
            case_id = case.get("id", str(idx))
            qualified_id = f"{basename}/{case_id}"
            if selected_ids and qualified_id not in selected_ids:
                continue
            all_cases_meta.append(
                {
                    "path": path,
                    "basename": basename,
                    "cases_key": cases_key,
                    "idx": idx,
                    "case": case,
                    "data": data,
                    "input": inp,
                }
            )
            all_inputs.append(inp)

    if selected_ids:
        found_ids = {f"{meta['basename']}/{meta['case'].get('id', str(meta['idx']))}" for meta in all_cases_meta}
        missing_ids = sorted(selected_ids - found_ids)
        if missing_ids:
            return _json_error(f"No tests found for ids: {', '.join(missing_ids)}")

    # Batch subprocess call: one process for all inputs
    if all_inputs:
        batch_results = get_expected_tokens_batch_subprocess(all_inputs)
    else:
        batch_results = []

    for meta, (correct, source, rule) in zip(all_cases_meta, batch_results, strict=True):
        if source == "error":
            case_id = meta["case"].get("id", str(meta["idx"]))
            normalization_errors.append({"id": f"{meta['basename']}/{case_id}", "input": meta["input"], "error": rule})
            continue
        expected = meta["case"].get("expected") or []
        rule = rule or ""

        if not tokens_match(expected, correct):
            case_id = meta["case"].get("id", str(meta["idx"]))
            exp_str = "|".join(t.get("surface", "") for t in expected)
            cor_str = "|".join(t["surface"] for t in correct)
            exp_pos = "|".join(t.get("pos", "") for t in expected)
            cor_pos = "|".join(t["pos"] for t in correct)
            diff_type = "surface" if exp_str != cor_str else ("pos" if exp_pos != cor_pos else "lemma")

            entry = {
                "id": f"{meta['basename']}/{case_id}",
                "file": meta["path"],
                "basename": meta["basename"],
                "index": meta["idx"],
                "input": meta["input"],
                "rule": rule or "mecab-only",
                "expected": exp_str,
                "correct": cor_str,
                "expected_pos": exp_pos,
                "correct_pos": cor_pos,
                "diff_type": diff_type,
                "correct_tokens": correct,
                "data": meta["data"],
                "cases_key": meta["cases_key"],
            }
            needs_update.append(entry)
            by_rule.setdefault(rule or "mecab-only", []).append(entry)

    if not needs_update:
        return _json_result(
            {
                "needs_update": [],
                "by_rule": {},
                "total": 0,
                "errors": normalization_errors,
                "applied": False,
            }
        )

    # Build output entries (without internal data/path objects)
    output_entries = []
    for entry in needs_update:
        output_entries.append(
            {
                "id": entry["id"],
                "input": entry["input"],
                "rule": entry["rule"],
                "diff_type": entry["diff_type"],
                "expected": entry["expected"],
                "correct": entry["correct"],
            }
        )

    by_rule_out: dict[str, list[str]] = {}
    for rule_name in sorted(by_rule.keys()):
        by_rule_out[rule_name] = [ent["id"] for ent in by_rule[rule_name]]

    if not apply:
        return _json_result(
            {
                "needs_update": output_entries,
                "by_rule": by_rule_out,
                "total": len(needs_update),
                "errors": normalization_errors,
                "applied": False,
            }
        )

    # Apply updates
    files_to_save: dict[Path, dict] = {}
    for entry in needs_update:
        try:
            formatted = _format_expected_checked(entry["correct_tokens"], entry["rule"])
        except RuntimeError as exc:
            return _json_error(str(exc))
        entry["data"][entry["cases_key"]][entry["index"]]["expected"] = formatted
        files_to_save[entry["file"]] = entry["data"]

    for path, data in files_to_save.items():
        save_json(path, data)

    return _json_result(
        {
            "needs_update": output_entries,
            "by_rule": by_rule_out,
            "total": len(needs_update),
            "errors": normalization_errors,
            "applied": True,
        }
    )
