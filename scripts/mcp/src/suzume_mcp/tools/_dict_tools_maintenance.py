"""Dictionary search, sorting, and cleanup MCP tools."""

import re
from pathlib import Path

from ..core.mecab import mecab_analyze
from ..core.suzume_cli import get_cli_path
from ..core.suzume_utils import get_char_types
from ..server import PROJECT_ROOT, mcp
from ._dict_tools_common import (
    ALL_DICT_FILES,
    USER_CATEGORIES,
    VALID_CONJ,
    VALID_POS,
    _all_user_files,
    _atomic_write_text,
    _iter_dictionary_lines,
    _json_result,
    _recompile_core_dic,
    _recompile_user_dic,
    _write_files_and_recompile,
)


@mcp.tool()
async def dict_grep(pattern: str, user: str = "", search_all: bool = False) -> str:
    """Search dictionary entries matching a regex pattern (surface match).

    Args:
        pattern: Regex pattern to match against entry surfaces.
        user: Search specific user category dict. Empty for core dict.
        search_all: Search both core and all user dicts.
    """
    try:
        rx = re.compile(pattern)
    except re.error as exc:
        return _json_result({"status": "error", "message": f"Invalid regex: {exc}"})

    files: list[str] = []
    if user:
        files.append(f"data/user/{user}.tsv")
    elif search_all:
        files = list(ALL_DICT_FILES) + _all_user_files()
    else:
        files = list(ALL_DICT_FILES)

    matches = [
        {"file": file_rel, "entry": line}
        for file_rel, _, line in _iter_dictionary_lines(files)
        if rx.search(line.split("\t")[0])
    ]

    return _json_result({"pattern": pattern, "matches": matches, "total": len(matches)})


@mcp.tool()
async def dict_sort(
    file: str = "",
    user: str = "",
    dry_run: bool = True,
) -> str:
    """Sort dictionary entries by conjugation type and あいうえお order.

    Groups entries by conj_type (for verbs/adjectives) or POS, then sorts
    within each group by Japanese reading (using MeCab for kanji readings).

    Args:
        file: Specific dict file path relative to project root (e.g. "data/core/verbs.tsv").
        user: User dictionary category to sort (entertainment, adult, etc.).
        dry_run: If True (default), preview the sorted result. Set False to apply.
    """
    if user:
        if user not in USER_CATEGORIES:
            return _json_result(
                {"status": "error", "message": f"Invalid user category: {user}. Valid: {', '.join(USER_CATEGORIES)}"}
            )
        target_rel = f"data/user/{user}.tsv"
    elif file:
        target_rel = file
    else:
        return _json_result(
            {"status": "error", "message": "Specify file (e.g. 'data/core/verbs.tsv') or user category."}
        )

    filepath = PROJECT_ROOT / target_rel
    if not filepath.exists():
        return _json_result({"status": "error", "message": f"File not found: {target_rel}"})

    content = filepath.read_text(encoding="utf-8")
    raw_lines = content.splitlines()

    # Extract header comments (contiguous block at the top)
    header_lines = []
    data_start = 0
    for idx, line in enumerate(raw_lines):
        if line.startswith("#") or not line.strip():
            header_lines.append(line)
            data_start = idx + 1
        else:
            break

    # Parse entries (skip inline comments and disabled entries)
    entries = []
    disabled_entries = []
    pending_comments = []
    for line in raw_lines[data_start:]:
        if not line.strip():
            continue
        if line.startswith("#DISABLED# "):
            disabled_entries.append({"raw": line, "comments": pending_comments})
            pending_comments = []
            continue
        if line.startswith("#"):
            if not (line.startswith("# --- ") and line.endswith(" ---")):
                pending_comments.append(line)
            continue
        fields = line.split("\t")
        entries.append(
            {
                "surface": fields[0],
                "pos": fields[1] if len(fields) > 1 else "",
                "conj_type": fields[2] if len(fields) > 2 else "",
                "raw": line,
                "comments": pending_comments,
            }
        )
        pending_comments = []
    trailing_comments = pending_comments

    if not entries:
        return _json_result({"status": "error", "message": f"No entries found in {target_rel}"})

    # Get readings for sorting (あいうえお order)
    reading_cache: dict[str, str] = {}
    for entry in entries:
        surface = entry["surface"]
        if surface not in reading_cache:
            reading_cache[surface] = await _get_reading(surface)

    # Determine grouping strategy
    has_conj = any(ent["conj_type"] for ent in entries)
    has_mixed_pos = len(set(ent["pos"] for ent in entries)) > 1

    if has_conj:
        # Group by conj_type (verbs.tsv, adjectives.tsv)
        group_key = "conj_type"
        # Order: defined conjugation types first, then ungrouped
        group_order = [ctype for ctype in VALID_CONJ if any(ent["conj_type"] == ctype for ent in entries)]
        # Add any conj_type not in VALID_CONJ
        for ent in entries:
            if ent["conj_type"] and ent["conj_type"] not in group_order:
                group_order.append(ent["conj_type"])
        # Entries without conj_type go last
        if any(not ent["conj_type"] for ent in entries):
            group_order.append("")
    elif has_mixed_pos:
        # Group by POS (user dicts, expressions.tsv)
        group_key = "pos"
        group_order = [pos for pos in VALID_POS if any(ent["pos"] == pos for ent in entries)]
        for ent in entries:
            if ent["pos"] and ent["pos"] not in group_order:
                group_order.append(ent["pos"])
        if any(not ent["pos"] for ent in entries):
            group_order.append("")
    else:
        # Single POS, no conj_type — just sort alphabetically
        group_key = None
        group_order = [None]

    # Group and sort
    groups: dict[str | None, list[dict]] = {}
    for ent in entries:
        key = ent.get(group_key, None) if group_key else None
        groups.setdefault(key, []).append(ent)

    _CONJ_LABELS = {
        "I_ADJ": "I-adjectives (い形容詞)",
        "NA_ADJ": "NA-adjectives (な形容詞)",
        "GODAN_KA": "Godan-KA verbs (カ行五段)",
        "GODAN_GA": "Godan-GA verbs (ガ行五段)",
        "GODAN_SA": "Godan-SA verbs (サ行五段)",
        "GODAN_TA": "Godan-TA verbs (タ行五段)",
        "GODAN_NA": "Godan-NA verbs (ナ行五段)",
        "GODAN_BA": "Godan-BA verbs (バ行五段)",
        "GODAN_MA": "Godan-MA verbs (マ行五段)",
        "GODAN_RA": "Godan-RA verbs (ラ行五段)",
        "GODAN_WA": "Godan-WA verbs (ワ行五段)",
        "ICHIDAN": "Ichidan verbs (一段動詞)",
        "SURU": "Suru verbs (サ変)",
        "KURU": "Kuru verbs (カ変)",
        "IRREGULAR": "Irregular verbs (不規則)",
    }

    _POS_LABELS = {
        "ADJECTIVE": "Adjectives (形容詞)",
        "ADVERB": "Adverbs (副詞)",
        "VERB": "Verbs (動詞)",
        "NOUN": "Nouns (名詞)",
        "PROPER_NOUN": "Proper Nouns (固有名詞)",
        "PRONOUN": "Pronouns (代名詞)",
        "PREFIX": "Prefixes (接頭辞)",
        "SUFFIX": "Suffixes (接尾辞)",
        "INTERJECTION": "Interjections (感動詞)",
        "ADNOMINAL": "Adnominals (連体詞)",
        "CONJUNCTION": "Conjunctions (接続詞)",
        "PARTICLE": "Particles (助詞)",
        "AUX": "Auxiliaries (助動詞)",
        "OTHER": "Other (その他)",
        "PHRASE": "Phrases (フレーズ)",
    }

    # Deduplicate exact grammatical entries while preserving legitimate
    # homographs that differ by POS or conjugation type.
    seen_keys: set[tuple[str, str, str]] = set()
    duplicates_removed = []
    deduped_entries = []
    for ent in entries:
        key = (ent["surface"], ent["pos"], ent["conj_type"])
        if key in seen_keys:
            duplicates_removed.append({"surface": ent["surface"], "pos": ent["pos"], "conj_type": ent["conj_type"]})
            continue
        seen_keys.add(key)
        deduped_entries.append(ent)
    entries = deduped_entries

    # Re-group after dedup
    groups = {}
    for ent in entries:
        key = ent.get(group_key, None) if group_key else None
        groups.setdefault(key, []).append(ent)

    # Build sorted output
    output_lines = list(header_lines)
    group_stats = []
    for key in group_order:
        group_entries = groups.get(key, [])
        if not group_entries:
            continue

        # Sort by reading
        group_entries.sort(key=lambda ent: reading_cache.get(ent["surface"], ent["surface"]))

        # Section comment
        if group_key:
            if group_key == "conj_type":
                label = _CONJ_LABELS.get(key, key or "Other")
            else:
                label = _POS_LABELS.get(key, key or "Other")
            output_lines.append(f"\n# --- {label} ---")
            group_stats.append({"name": label, "count": len(group_entries)})

        for ent in group_entries:
            output_lines.extend(ent["comments"])
            output_lines.append(ent["raw"])

    # Append disabled entries at the end
    if disabled_entries:
        output_lines.append("\n# --- Disabled entries ---")
        for disabled in disabled_entries:
            output_lines.extend(disabled["comments"])
            output_lines.append(disabled["raw"])
    output_lines.extend(trailing_comments)

    sorted_content = "\n".join(output_lines) + "\n"

    result: dict = {
        "file": target_rel,
        "total_entries": len(entries),
        "duplicates_removed": len(duplicates_removed),
        "duplicate_entries": duplicates_removed,
        "groups": group_stats,
        "disabled": len(disabled_entries),
        "applied": False,
    }

    if dry_run:
        result["dry_run"] = True
        preview_lines = sorted_content.splitlines()
        if len(preview_lines) > 100:
            preview = "\n".join(preview_lines[:100]) + f"\n... ({len(preview_lines) - 100} more lines)"
        else:
            preview = sorted_content
        result["preview"] = preview
        return _json_result(result)

    is_user = target_rel.startswith("data/user/")
    recompile_status, error = await _write_files_and_recompile(
        {filepath: sorted_content},
        _recompile_user_dic if is_user else _recompile_core_dic,
    )
    if error:
        result.update(
            {
                "status": "error",
                "message": error,
                "recompile": recompile_status,
                "rolled_back": True,
            }
        )
        return _json_result(result)
    result["applied"] = True
    result["recompile"] = recompile_status
    return _json_result(result)


async def _get_reading(surface: str) -> str:
    """Get hiragana reading for a surface using MeCab, for sort ordering."""
    import regex

    # If already all hiragana/katakana, convert to hiragana for sorting
    if regex.fullmatch(r"[\p{Hiragana}\p{Katakana}ー]+", surface):
        # Convert katakana to hiragana
        return "".join(chr(ord(c) - 0x60) if "\u30a1" <= c <= "\u30f6" else c for c in surface)

    # Use MeCab to get reading
    tokens = mecab_analyze(surface)
    if tokens:
        readings = []
        for tok in tokens:
            reading = tok.get("reading", "")
            if reading and reading != "*":
                # Convert katakana reading to hiragana
                readings.append("".join(chr(ord(c) - 0x60) if "\u30a1" <= c <= "\u30f6" else c for c in reading))
            else:
                readings.append(tok["surface"])
        return "".join(readings)

    return surface


@mcp.tool()
async def dict_remove_matching(
    pattern: str,
    user: str = "",
    dry_run: bool = True,
) -> str:
    """Bulk remove dictionary entries matching a regex pattern.

    Args:
        pattern: Regex pattern to match against entry surfaces.
        user: User dictionary category (empty for core dict).
        dry_run: If True (default), preview without removing. Set False to apply.
    """
    try:
        rx = re.compile(pattern)
    except re.error as exc:
        return _json_result({"status": "error", "message": f"Invalid regex: {exc}"})

    files: list[str] = []
    if user:
        files.append(f"data/user/{user}.tsv")
    else:
        files = list(ALL_DICT_FILES)

    matches = []
    for file_rel, _, line in _iter_dictionary_lines(files):
        surface = line.split("\t")[0]
        if rx.search(surface):
            matches.append({"file": file_rel, "surface": surface, "entry": line})

    if not matches:
        return _json_result({"pattern": pattern, "matches": [], "total": 0, "applied": False})

    result: dict = {
        "pattern": pattern,
        "matches": matches,
        "total": len(matches),
        "applied": False,
    }

    if dry_run:
        result["dry_run"] = True
        return _json_result(result)

    # Group by file and remove
    by_file: dict[str, set[str]] = {}
    for match in matches:
        by_file.setdefault(match["file"], set()).add(match["surface"])

    updates = {}
    for file_rel, surfaces_to_remove in by_file.items():
        filepath = PROJECT_ROOT / file_rel
        file_lines = filepath.read_text(encoding="utf-8").splitlines()
        new_lines = []
        for line in file_lines:
            if line.strip() and not line.startswith("#"):
                surface = line.split("\t")[0]
                if surface in surfaces_to_remove:
                    continue
            new_lines.append(line)
        updates[filepath] = "\n".join(new_lines) + "\n"

    recompile_status, error = await _write_files_and_recompile(
        updates,
        _recompile_user_dic if user else _recompile_core_dic,
    )
    if error:
        result.update(
            {
                "status": "error",
                "message": error,
                "recompile": recompile_status,
                "rolled_back": True,
            }
        )
        return _json_result(result)
    result["applied"] = True
    result["recompile"] = recompile_status
    return _json_result(result)


@mcp.tool()
async def dict_cleanup(
    input_file: str,
    dry_run: bool = True,
) -> str:
    """Analyze dictionary entries and separate needed/unneeded ones.

    Checks each entry against Suzume to determine if it's still needed
    (i.e., if Suzume would split the word without the dictionary entry).

    Args:
        input_file: Path to TSV dictionary file to analyze (relative to project root).
        dry_run: If True (default), only report. Set False to write keep/noise files.
    """
    import subprocess

    filepath = PROJECT_ROOT / input_file
    if not filepath.exists():
        return _json_result({"status": "error", "message": f"File not found: {input_file}"})

    cli = get_cli_path()
    if not cli.exists():
        return _json_result({"status": "error", "message": "suzume-cli not found (build first)"})

    def is_fixed_expression(surface: str) -> bool:
        import regex

        return bool(regex.search(r"[\p{Han}][\p{Hiragana}][\p{Han}\p{Hiragana}\p{Katakana}]", surface))

    def is_split_by_suzume(surface: str) -> bool:
        # --no-user-dict: judge whether suzume splits the word on its OWN merits,
        # not because of the very user-dict entry we are evaluating for removal.
        result = subprocess.run(
            [str(cli), "--no-user-dict", surface],
            capture_output=True,
            text=True,
            timeout=10,
            cwd=PROJECT_ROOT,
        )
        if result.returncode != 0:
            detail = result.stderr.strip() or result.stdout.strip() or "no diagnostic output"
            raise RuntimeError(f"suzume-cli exited with {result.returncode}: {detail}")
        lines = [line for line in result.stdout.strip().split("\n") if line and line != "EOS"]
        return len(lines) > 1

    keep_lines = []
    noise_lines = []
    total = 0
    kept = 0
    details = []

    try:
        input_lines = filepath.read_text(encoding="utf-8").splitlines()
        for line in input_lines:
            if line.startswith("#") or not line.strip():
                keep_lines.append(line)
                continue

            fields = line.split("\t")
            if len(fields) < 2:
                keep_lines.append(line)
                continue

            surface = fields[0]
            total += 1

            # Determine if entry is needed
            reason = ""
            keep = True

            if len(surface) <= 2:
                keep = False
                reason = "too_short"
            elif not is_split_by_suzume(surface):
                keep = False
                reason = "handled_by_suzume"
            elif is_fixed_expression(surface):
                reason = "fixed_expression"
            elif len(get_char_types(surface)) > 1:
                reason = "mixed_chartype_compound"
            elif get_char_types(surface) and get_char_types(surface)[0] == "kanji" and len(surface) >= 4:
                reason = "kanji_compound"
            else:
                reason = "split_other"

            action = "KEEP" if keep else "DROP"
            details.append({"surface": surface, "action": action, "reason": reason})

            if keep:
                keep_lines.append(line)
                kept += 1
            else:
                noise_lines.append(f"{line}\t# {reason}")
    except (OSError, subprocess.SubprocessError, RuntimeError) as exc:
        return _json_result({"status": "error", "message": f"Cleanup analysis failed: {exc}"})

    result: dict = {
        "file": input_file,
        "total": total,
        "keep": kept,
        "drop": total - kept,
        "details": details,
        "applied": False,
    }

    if dry_run:
        result["dry_run"] = True
        return _json_result(result)

    base = str(filepath).rsplit(".", 1)[0]
    keep_path = Path(f"{base}_keep.tsv")
    noise_path = Path(f"{base}_noise.tsv")

    _atomic_write_text(keep_path, "\n".join(keep_lines) + "\n")
    _atomic_write_text(noise_path, "\n".join(noise_lines) + "\n")

    result["applied"] = True
    result["keep_file"] = str(keep_path.relative_to(PROJECT_ROOT))
    result["noise_file"] = str(noise_path.relative_to(PROJECT_ROOT))
    return _json_result(result)
