"""Bulk dictionary mutation MCP tools."""

from ..core.mecab import mecab_analyze
from ..core.suzume_utils import get_suzume_rule
from ..server import PROJECT_ROOT, mcp
from ._dict_tools_common import (
    POS_TO_FILE,
    USER_CATEGORIES,
    VALID_CONJ,
    VALID_POS,
    _json_result,
    _load_all_entries,
    _recompile_core_dic,
    _recompile_user_dic,
    _validate_surface,
    _write_files_and_recompile,
)


@mcp.tool()
async def dict_bulk_add(
    words: str,
    pos: str = "NOUN",
    conj_type: str = "",
    user: str = "",
    force: bool = False,
    dry_run: bool = False,
) -> str:
    """Add multiple words to the dictionary at once.

    Args:
        words: Newline-separated list of words to add.
        pos: POS value for all words (default: NOUN).
        conj_type: Conjugation type required by VERB and ADJECTIVE entries.
        user: User dictionary category.
        force: Allow adding even if MeCab splits words.
        dry_run: Preview only.
    """
    if pos not in VALID_POS:
        return _json_result({"status": "error", "message": f"Invalid POS: {pos}. Valid values: {', '.join(VALID_POS)}"})
    if conj_type and conj_type not in VALID_CONJ:
        return _json_result(
            {"status": "error", "message": f"Invalid conj_type: {conj_type}. Valid values: {', '.join(VALID_CONJ)}"}
        )
    if pos in ("VERB", "ADJECTIVE") and not conj_type:
        return _json_result({"status": "error", "message": f"conj_type is required for {pos} entries."})
    if user and user not in USER_CATEGORIES:
        return _json_result(
            {
                "status": "error",
                "message": f"Invalid user category: {user}. Valid categories: {', '.join(USER_CATEGORIES)}",
            }
        )

    word_list = [line.strip() for line in words.split("\n") if line.strip()]
    if not word_list:
        return _json_result({"status": "error", "message": "No words provided."})

    # Load all entries once for duplicate checking
    _, by_surface = _load_all_entries()

    added = []
    skipped = []
    lines_to_append = []
    skip_conj_check = pos == "PROPER_NOUN"

    for word in word_list:
        surface_error = _validate_surface(word)
        if surface_error:
            skipped.append({"word": word, "reason": surface_error})
            continue

        # Duplicate check
        if word in by_surface:
            existing = by_surface[word]
            files = sorted(set(entry["file"] for entry in existing))
            skipped.append({"word": word, "reason": f"DUPLICATE: already exists in {', '.join(files)}"})
            continue

        # MeCab split check
        if not force and not skip_conj_check:
            tokens = mecab_analyze(word)
            suzume_rule = get_suzume_rule(word)
            if len(tokens) > 1 and not suzume_rule:
                if len(tokens) == 2 and tokens[1]["pos"] in ("助動詞", "助詞"):
                    skipped.append(
                        {
                            "word": word,
                            "reason": f"REJECT: conjugated form. Register '{tokens[0].get('lemma', '')}' instead.",
                        }
                    )
                    continue
                skipped.append(
                    {"word": word, "reason": f"MeCab splits into {len(tokens)} tokens. Use force=True to override."}
                )
                continue

        entry_line = f"{word}\t{pos}"
        if conj_type:
            entry_line += f"\t{conj_type}"
        lines_to_append.append(entry_line)
        added.append({"word": word, "entry": entry_line})
        # Track as added so subsequent duplicates within the batch are caught
        by_surface.setdefault(word, []).append({"surface": word, "pos": pos, "file": "pending"})

    if dry_run:
        return _json_result(
            {
                "status": "ok",
                "added": added,
                "skipped": skipped,
                "total_added": len(added),
                "total_skipped": len(skipped),
                "dry_run": True,
            }
        )

    if not lines_to_append:
        return _json_result(
            {
                "status": "ok",
                "added": added,
                "skipped": skipped,
                "total_added": 0,
                "total_skipped": len(skipped),
            }
        )

    # Determine target file
    if user:
        target_file = PROJECT_ROOT / f"data/user/{user}.tsv"
        target_rel = f"data/user/{user}.tsv"
    else:
        target_rel = POS_TO_FILE.get(pos, "data/core/nouns.tsv")
        target_file = PROJECT_ROOT / target_rel

    previous_content = target_file.read_text(encoding="utf-8") if target_file.exists() else ""
    separator = "" if not previous_content or previous_content.endswith("\n") else "\n"
    updated_content = previous_content + separator + "\n".join(lines_to_append) + "\n"
    recompile_status, error = await _write_files_and_recompile(
        {target_file: updated_content},
        _recompile_user_dic if user else _recompile_core_dic,
    )
    if error:
        return _json_result(
            {
                "status": "error",
                "message": error,
                "added": added,
                "skipped": skipped,
                "total_added": 0,
                "total_skipped": len(skipped),
                "file": target_rel,
                "recompile": recompile_status,
                "rolled_back": True,
            }
        )

    return _json_result(
        {
            "status": "ok",
            "added": added,
            "skipped": skipped,
            "total_added": len(added),
            "total_skipped": len(skipped),
            "file": target_rel,
            "recompile": recompile_status,
        }
    )


@mcp.tool()
async def dict_bulk_move(
    words: str,
    from_user: str,
    to_user: str,
    dry_run: bool = True,
) -> str:
    """Move entries between user dictionary categories.

    Args:
        words: Newline-separated list of words to move. Use "*" to move all entries.
        from_user: Source user dictionary category.
        to_user: Destination user dictionary category.
        dry_run: If True (default), preview only.
    """
    if from_user not in USER_CATEGORIES:
        return _json_result(
            {
                "status": "error",
                "message": f"Invalid source category: {from_user}. Valid categories: {', '.join(USER_CATEGORIES)}",
            }
        )
    if to_user not in USER_CATEGORIES:
        return _json_result(
            {
                "status": "error",
                "message": f"Invalid destination category: {to_user}. Valid categories: {', '.join(USER_CATEGORIES)}",
            }
        )
    if from_user == to_user:
        return _json_result({"status": "error", "message": "Source and destination categories must differ."})

    source_file = PROJECT_ROOT / f"data/user/{from_user}.tsv"
    source_rel = f"data/user/{from_user}.tsv"
    dest_file = PROJECT_ROOT / f"data/user/{to_user}.tsv"
    dest_rel = f"data/user/{to_user}.tsv"

    if not source_file.exists():
        return _json_result({"status": "error", "message": f"Source file not found: {source_rel}"})

    move_all = words.strip() == "*"
    if not move_all:
        word_list = [line.strip() for line in words.split("\n") if line.strip()]
        if not word_list:
            return _json_result({"status": "error", "message": "No words provided."})
        target_words = set(word_list)
    else:
        target_words = None  # move everything

    source_lines = source_file.read_text(encoding="utf-8").splitlines()
    remaining_lines = []
    matched_entries = []
    matched_surfaces = set()

    for line in source_lines:
        if line.startswith("#") or not line.strip():
            remaining_lines.append(line)
            continue
        surface = line.split("\t")[0]
        if move_all or surface in target_words:
            matched_entries.append(line)
            matched_surfaces.add(surface)
        else:
            remaining_lines.append(line)

    moved = [line.split("\t")[0] for line in matched_entries]
    not_found = []
    if not move_all and target_words is not None:
        not_found = sorted(target_words - matched_surfaces)

    if dry_run:
        return _json_result(
            {
                "status": "ok",
                "moved": moved,
                "not_found": not_found,
                "total_moved": len(moved),
                "from": source_rel,
                "to": dest_rel,
                "dry_run": True,
            }
        )

    if not matched_entries:
        return _json_result(
            {
                "status": "ok",
                "moved": [],
                "not_found": not_found,
                "total_moved": 0,
                "from": source_rel,
                "to": dest_rel,
            }
        )

    source_content = "\n".join(remaining_lines) + ("\n" if remaining_lines else "")
    destination_content = dest_file.read_text(encoding="utf-8") if dest_file.exists() else ""
    separator = "" if not destination_content or destination_content.endswith("\n") else "\n"
    destination_content += separator + "\n".join(matched_entries) + "\n"
    recompile_status, error = await _write_files_and_recompile(
        {source_file: source_content, dest_file: destination_content},
        _recompile_user_dic,
    )
    if error:
        return _json_result(
            {
                "status": "error",
                "message": error,
                "moved": moved,
                "not_found": not_found,
                "total_moved": 0,
                "from": source_rel,
                "to": dest_rel,
                "recompile": recompile_status,
                "rolled_back": True,
            }
        )

    return _json_result(
        {
            "status": "ok",
            "moved": moved,
            "not_found": not_found,
            "total_moved": len(moved),
            "from": source_rel,
            "to": dest_rel,
            "recompile": recompile_status,
        }
    )
