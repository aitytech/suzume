"""Dictionary lookup and single-entry mutation MCP tools."""

import re

from ..core.mecab import mecab_analyze
from ..core.pos_mapping import map_mecab_pos
from ..core.suzume_utils import get_suzume_rule
from ..server import PROJECT_ROOT, mcp
from ._dict_tools_common import (
    POS_TO_FILE,
    USER_CATEGORIES,
    VALID_CONJ,
    VALID_POS,
    _find_word_in_files,
    _json_result,
    _load_all_entries,
    _load_dictionary,
    _map_conj_type,
    _recompile_core_dic,
    _recompile_user_dic,
    _to_dict_pos,
    _token_to_dict,
    _validate_surface,
    _write_files_and_recompile,
)


async def _append_entry_and_recompile(target_file, entry_line, recompile) -> tuple[str, str | None]:
    """Append one entry, restoring the source file if compilation fails."""
    previous_content = target_file.read_text(encoding="utf-8") if target_file.exists() else ""
    separator = "" if not previous_content or previous_content.endswith("\n") else "\n"
    updated_content = previous_content + separator + entry_line + "\n"
    return await _write_files_and_recompile({target_file: updated_content}, recompile)


@mcp.tool()
async def dict_check(word: str) -> str:
    """Check if a word can be added to the dictionary (MeCab analysis + duplicates).

    Args:
        word: Japanese word to check.
    """
    result: dict = {"word": word}

    # Suzume rule check
    suzume_rule = get_suzume_rule(word)
    result["suzume_rule"] = suzume_rule if suzume_rule else None

    # MeCab analysis
    tokens = mecab_analyze(word)
    is_single = len(tokens) == 1
    is_base = is_single and tokens[0].get("conj_form", "") == "基本形"

    result["mecab"] = {
        "is_single": is_single,
        "is_base_form": is_base,
        "tokens": [_token_to_dict(t) for t in tokens],
    }

    # Dictionary check (core + user)
    _, by_surface = _load_all_entries()
    result["in_dictionary"] = word in by_surface
    if word in by_surface:
        result["existing_entries"] = [
            f"{e['file']}: {e['surface']}\t{e['pos']}\t{e['conj_type']}" for e in by_surface[word]
        ]
        result["suggestion"] = None
        return _json_result(result)
    else:
        result["existing_entries"] = []

    # Suggestion
    result["suggestion"] = None
    if is_single:
        suggested_pos = _to_dict_pos(map_mecab_pos(tokens[0]))
        suggested_conj = _map_conj_type(tokens[0])
        if suggested_pos:
            entry = f"{word}\t{suggested_pos}"
            if suggested_conj:
                entry += f"\t{suggested_conj}"
            cmd = f'dict_add word="{word}" pos="{suggested_pos}"'
            if suggested_conj:
                cmd += f' conj_type="{suggested_conj}"'
            result["suggestion"] = {"entry": entry, "command": cmd}
    elif len(tokens) > 1:
        result["suggestion"] = {
            "entry": f"{word}\tNOUN",
            "command": f'dict_add word="{word}" pos="NOUN" force=True',
        }

    return _json_result(result)


@mcp.tool()
async def dict_suggest(word: str) -> str:
    """Suggest POS and conjugation type for a word based on MeCab analysis.

    Args:
        word: Japanese word to analyze.
    """
    tokens = mecab_analyze(word)
    if not tokens:
        return _json_result({"status": "error", "message": "Unknown word (not in MeCab dictionary)"})

    if len(tokens) > 1:
        result = {
            "word": word,
            "is_split": True,
            "tokens": [_token_to_dict(t) for t in tokens],
            "mecab": None,
            "suggestion": {
                "pos": "NOUN",
                "conj_type": None,
                "command": f'dict_add word="{word}" pos="NOUN"',
            },
        }
        return _json_result(result)

    tok = tokens[0]
    pos = _to_dict_pos(map_mecab_pos(tok))
    conj = _map_conj_type(tok)

    result: dict = {
        "word": word,
        "is_split": False,
        "mecab": {
            "pos": tok.get("pos", ""),
            "pos_sub1": tok.get("pos_sub1", ""),
            "lemma": tok.get("lemma", ""),
            "conj_form": tok.get("conj_form", ""),
        },
        "suggestion": None,
    }

    if pos:
        suggestion: dict = {"pos": pos, "conj_type": conj if conj else None}
        cmd = f'dict_add word="{word}" pos="{pos}"'
        if conj:
            cmd += f' conj_type="{conj}"'
        suggestion["command"] = cmd
        result["suggestion"] = suggestion
    else:
        result["suggestion"] = None
        result["message"] = f"Cannot suggest POS for: {tok['pos']}. This may be a closed-class word (use L1 instead)."

    return _json_result(result)


@mcp.tool()
async def dict_add(
    word: str,
    pos: str,
    conj_type: str = "",
    user: str = "",
    force: bool = False,
    dry_run: bool = False,
) -> str:
    """Add a word to the dictionary with safety checks.

    Args:
        word: Word to add.
        pos: POS value (NOUN, VERB, ADJECTIVE, ADVERB, PROPER_NOUN, etc.).
        conj_type: Conjugation type (I_ADJ, NA_ADJ, GODAN_KA, ICHIDAN, etc.).
        user: User dictionary category (entertainment, adult, etc.). Empty for core dict.
        force: Allow adding even if MeCab splits the word.
        dry_run: Preview only, don't modify files.
    """
    if pos not in VALID_POS:
        return _json_result({"status": "error", "message": f"Invalid POS: {pos}. Valid values: {', '.join(VALID_POS)}"})
    surface_error = _validate_surface(word)
    if surface_error:
        return _json_result({"status": "error", "message": surface_error, "word": word})
    if conj_type and conj_type not in VALID_CONJ:
        return _json_result(
            {"status": "error", "message": f"Invalid conj_type: {conj_type}. Valid values: {', '.join(VALID_CONJ)}"}
        )
    if user and user not in USER_CATEGORIES:
        return _json_result(
            {
                "status": "error",
                "message": f"Invalid user category: {user}. Valid categories: {', '.join(USER_CATEGORIES)}",
            }
        )

    # Cross-dictionary duplicate check (core + user)
    _, by_surface = _load_all_entries()
    if word in by_surface:
        existing = by_surface[word]
        files = sorted(set(e["file"] for e in existing))
        return _json_result(
            {
                "status": "error",
                "message": f"DUPLICATE: '{word}' already exists in {', '.join(files)}",
            }
        )

    # MeCab check
    tokens = mecab_analyze(word)
    skip_conj_check = pos == "PROPER_NOUN"
    suzume_rule = get_suzume_rule(word)

    if len(tokens) > 1 and not force and not skip_conj_check:
        if suzume_rule:
            pass  # OK, suzume keeps as 1 token
        else:
            if len(tokens) == 2 and tokens[1]["pos"] in ("助動詞", "助詞"):
                return _json_result(
                    {
                        "status": "error",
                        "message": f"REJECT: This appears to be a conjugated form. Register '{tokens[0].get('lemma', '')}' instead, or use PROPER_NOUN.",
                        "word": word,
                        "tokens": [_token_to_dict(t) for t in tokens],
                    }
                )
            return _json_result(
                {
                    "status": "error",
                    "message": f"MeCab splits '{word}' into {len(tokens)} tokens. To add anyway, use force=True.",
                    "word": word,
                    "tokens": [_token_to_dict(t) for t in tokens],
                }
            )

    entry_line = f"{word}\t{pos}"
    if conj_type:
        entry_line += f"\t{conj_type}"

    if user:
        target_file = PROJECT_ROOT / f"data/user/{user}.tsv"
        target_rel = f"data/user/{user}.tsv"
        if dry_run:
            return _json_result(
                {"status": "ok", "word": word, "entry": entry_line, "file": target_rel, "dry_run": True}
            )

        recompile_status, error = await _append_entry_and_recompile(target_file, entry_line, _recompile_user_dic)
        if error:
            return _json_result(
                {
                    "status": "error",
                    "message": error,
                    "word": word,
                    "entry": entry_line,
                    "file": target_rel,
                    "recompile": recompile_status,
                    "rolled_back": True,
                }
            )
        return _json_result(
            {"status": "ok", "word": word, "entry": entry_line, "file": target_rel, "recompile": recompile_status}
        )

    # Core dictionary
    target_file_rel = POS_TO_FILE.get(pos, "data/core/nouns.tsv")
    target_file = PROJECT_ROOT / target_file_rel

    if dry_run:
        return _json_result(
            {"status": "ok", "word": word, "entry": entry_line, "file": target_file_rel, "dry_run": True}
        )

    recompile_status, error = await _append_entry_and_recompile(target_file, entry_line, _recompile_core_dic)
    if error:
        return _json_result(
            {
                "status": "error",
                "message": error,
                "word": word,
                "entry": entry_line,
                "file": target_file_rel,
                "recompile": recompile_status,
                "rolled_back": True,
            }
        )
    return _json_result(
        {"status": "ok", "word": word, "entry": entry_line, "file": target_file_rel, "recompile": recompile_status}
    )


@mcp.tool()
async def dict_remove(word: str, user: str = "", dry_run: bool = False) -> str:
    """Remove a word from the dictionary.

    Args:
        word: Word to remove.
        user: User dictionary category (empty for core dict).
        dry_run: Preview only.
    """
    if user:
        filepath = PROJECT_ROOT / f"data/user/{user}.tsv"
        file_rel = f"data/user/{user}.tsv"
        if not filepath.exists():
            return _json_result({"status": "error", "message": f"User dict file not found: {file_rel}"})
    else:
        file_rel = _find_word_in_files(word)
        if not file_rel:
            return _json_result({"status": "error", "message": f"Word not found in dictionary: {word}"})
        filepath = PROJECT_ROOT / file_rel

    lines = filepath.read_text(encoding="utf-8").splitlines()
    new_lines = []
    removed = None
    for line in lines:
        if not line.startswith("#") and line.strip():
            surface = line.split("\t")[0]
            if surface == word:
                removed = line
                continue
        new_lines.append(line)

    if not removed:
        return _json_result({"status": "error", "message": f"Word not found: {word}"})
    if dry_run:
        return _json_result({"status": "ok", "word": word, "removed_entry": removed, "file": file_rel, "dry_run": True})

    updated_content = "\n".join(new_lines) + ("\n" if new_lines else "")
    recompile_status, error = await _write_files_and_recompile(
        {filepath: updated_content},
        _recompile_user_dic if user else _recompile_core_dic,
    )
    if error:
        return _json_result(
            {
                "status": "error",
                "message": error,
                "word": word,
                "removed_entry": removed,
                "file": file_rel,
                "recompile": recompile_status,
                "rolled_back": True,
            }
        )
    return _json_result(
        {"status": "ok", "word": word, "removed_entry": removed, "file": file_rel, "recompile": recompile_status}
    )


@mcp.tool()
async def dict_disable(word: str, dry_run: bool = False) -> str:
    """Disable a dictionary entry by commenting it out (keeps in file but inactive).

    Args:
        word: Word to disable.
        dry_run: Preview only.
    """
    file_rel = _find_word_in_files(word)
    if not file_rel:
        return _json_result({"status": "error", "message": f"Word not found in dictionary: {word}"})

    filepath = PROJECT_ROOT / file_rel
    lines = filepath.read_text(encoding="utf-8").splitlines()
    found = False
    for idx, line in enumerate(lines):
        if not line.startswith("#") and line.strip():
            surface = line.split("\t")[0]
            if surface == word:
                found = True
                if dry_run:
                    return _json_result(
                        {"status": "ok", "word": word, "file": file_rel, "entry": line, "dry_run": True}
                    )
                lines[idx] = f"#DISABLED# {line}"
                break

    if not found:
        return _json_result({"status": "error", "message": f"Word not found: {word}"})

    recompile_status, error = await _write_files_and_recompile({filepath: "\n".join(lines) + "\n"}, _recompile_core_dic)
    if error:
        return _json_result(
            {
                "status": "error",
                "message": error,
                "word": word,
                "file": file_rel,
                "recompile": recompile_status,
                "rolled_back": True,
            }
        )
    return _json_result({"status": "ok", "word": word, "file": file_rel, "recompile": recompile_status})


@mcp.tool()
async def dict_enable(word: str, dry_run: bool = False) -> str:
    """Re-enable a disabled dictionary entry.

    Args:
        word: Word to enable.
        dry_run: Preview only.
    """
    file_rel = _find_word_in_files(word)
    if not file_rel:
        return _json_result({"status": "error", "message": f"Disabled word not found: {word}"})

    filepath = PROJECT_ROOT / file_rel
    lines = filepath.read_text(encoding="utf-8").splitlines()
    found = False
    for idx, line in enumerate(lines):
        if line.startswith("#DISABLED# "):
            disabled_surface = line[len("#DISABLED# ") :].split("\t")[0]
            if disabled_surface == word:
                found = True
                if dry_run:
                    return _json_result(
                        {"status": "ok", "word": word, "file": file_rel, "entry": line, "dry_run": True}
                    )
                lines[idx] = line[len("#DISABLED# ") :]
                break

    if not found:
        return _json_result({"status": "error", "message": f"Disabled word not found: {word}"})

    recompile_status, error = await _write_files_and_recompile({filepath: "\n".join(lines) + "\n"}, _recompile_core_dic)
    if error:
        return _json_result(
            {
                "status": "error",
                "message": error,
                "word": word,
                "file": file_rel,
                "recompile": recompile_status,
                "rolled_back": True,
            }
        )
    return _json_result({"status": "ok", "word": word, "file": file_rel, "recompile": recompile_status})


@mcp.tool()
async def dict_validate(fix: bool = False) -> str:
    """Validate dictionary for issues (conjugated forms, duplicates).

    Args:
        fix: If True, remove problematic entries. Default is report-only.
    """
    entries, by_surface = _load_dictionary()

    conjugated_forms = []
    duplicates = []
    mecab_split = []

    for entry in entries:
        tokens = mecab_analyze(entry["surface"])
        if len(tokens) > 1:
            # Check for conjugated forms
            is_conj = _is_conjugated_form(tokens, entry)
            if is_conj:
                conjugated_forms.append(
                    {
                        "entry": entry,
                        "tokens": tokens,
                        "lemma": tokens[0].get("lemma", ""),
                        "reason": is_conj,
                    }
                )
            else:
                mecab_split.append({"entry": entry, "tokens": tokens})

    for surface, entries_list in by_surface.items():
        if len(entries_list) > 1:
            duplicates.append({"surface": surface, "entries": entries_list})

    # Cross-dictionary duplicates (core vs user)
    _, all_by_surface = _load_all_entries()
    cross_duplicates = []
    for surface, all_entries in all_by_surface.items():
        files = set(e["file"] for e in all_entries)
        has_core = any(f.startswith("data/core/") for f in files)
        has_user = any(f.startswith("data/user/") for f in files)
        if has_core and has_user:
            cross_duplicates.append({"surface": surface, "files": sorted(files)})

    result: dict = {
        "total_entries": len(entries),
        "conjugated_forms": [
            {
                "surface": cf["entry"]["surface"],
                "line_num": cf["entry"]["line_num"],
                "reason": cf["reason"],
                "lemma": cf["lemma"],
            }
            for cf in conjugated_forms
        ],
        "duplicates": [{"surface": dup["surface"], "count": len(dup["entries"])} for dup in duplicates],
        "cross_duplicates": [{"surface": cd["surface"], "files": cd["files"]} for cd in cross_duplicates],
        "compound_words": len(mecab_split),
        "fixed": False,
    }

    issues = len(conjugated_forms) + len(duplicates) + len(cross_duplicates)
    if issues > 0 and fix:
        # Remove lines (conjugated + duplicate extras)
        lines_to_remove = set()
        for cf in conjugated_forms:
            lines_to_remove.add((cf["entry"]["file"], cf["entry"]["line_num"]))
        for dup in duplicates:
            for entry in dup["entries"][1:]:
                lines_to_remove.add((entry["file"], entry["line_num"]))

        updates = {}
        for file_rel in set(f for f, _ in lines_to_remove):
            filepath = PROJECT_ROOT / file_rel
            file_lines = filepath.read_text(encoding="utf-8").splitlines()
            remove_nums = {ln for f, ln in lines_to_remove if f == file_rel}
            new_lines = [line for idx, line in enumerate(file_lines, 1) if idx not in remove_nums]
            updates[filepath] = "\n".join(new_lines) + "\n"

        recompile_status, error = await _write_files_and_recompile(updates, _recompile_core_dic)
        result["fixed"] = error is None
        result["recompile"] = recompile_status
        if error:
            result["status"] = "error"
            result["message"] = error
            result["rolled_back"] = True
        else:
            result["removed_count"] = len(lines_to_remove)

    return _json_result(result)


def _is_conjugated_form(tokens: list[dict], entry: dict) -> str:
    """Check if a multi-token result is a conjugated form that shouldn't be in dictionary."""
    pos = entry.get("pos", "")
    surface = entry.get("surface", "")

    # Fixed expressions whitelist
    fixed = {"申し訳ない", "仕方ない", "仕方がない", "違いない", "やむを得ない"}
    if surface in fixed:
        return ""

    suzume_rule = get_suzume_rule(surface)
    if suzume_rule:
        return ""

    # Skip certain POS
    if pos in ("ADVERB", "INTERJECTION", "CONJUNCTION", "PROPER_NOUN"):
        return ""

    if len(tokens) == 2:
        t1, t2 = tokens
        if t2["pos"] == "助動詞" and re.match(r"^(ない|た|だ|です|ます|れる|られる|せる|させる|ぬ|ん)$", t2["surface"]):
            return f"verb/adj + {t2['surface']} (auxiliary)"
        if t2["surface"] == "て" and "連用" in t1.get("conj_form", ""):
            return "renyokei + te"
        if t2["surface"] == "ば" and t1["pos"] == "形容詞":
            return "adjective + ba (conditional)"
        if t2["surface"] == "た" and "連用" in t1.get("conj_form", ""):
            return "renyokei + ta"

    return ""
