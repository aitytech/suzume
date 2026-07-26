"""Dictionary tools ported from dict_tool.pl - MCP tool registration."""

from collections.abc import Iterator
from pathlib import Path

from ..core.file_utils import append_lines_atomic as _append_lines_atomic  # noqa: F401
from ..core.file_utils import atomic_write_text as _atomic_write_text  # noqa: F401
from ..core.json_utils import json_result as _json_result  # noqa: F401
from ..core.suzume_cli import get_cli_path, recompile_dic
from ..server import PROJECT_ROOT

# User dictionary categories
USER_CATEGORIES = [
    "entertainment",
    "music",
    "internet",
    "names",
    "historical",
    "politicians",
    "idols",
    "celebrities",
    "vtubers",
    "places",
    "brands",
    "organizations",
    "adult",
    "common",
]

# POS → dictionary file mapping
POS_TO_FILE = {
    "ADJECTIVE": "data/core/adjectives.tsv",
    "ADVERB": "data/core/adverbs.tsv",
    "VERB": "data/core/verbs.tsv",
    "NOUN": "data/core/nouns.tsv",
    "PROPER_NOUN": "data/core/nouns.tsv",
    "PRONOUN": "data/core/expressions.tsv",
    "INTERJECTION": "data/core/expressions.tsv",
    "CONJUNCTION": "data/core/expressions.tsv",
    "PARTICLE": "data/core/expressions.tsv",
    "SUFFIX": "data/core/suffixes.tsv",
    "PREFIX": "data/core/suffixes.tsv",
    "OTHER": "data/core/expressions.tsv",
    "PHRASE": "data/core/expressions.tsv",
    "AUX": "data/core/expressions.tsv",
}

ALL_DICT_FILES = [
    "data/core/adjectives.tsv",
    "data/core/adverbs.tsv",
    "data/core/verbs.tsv",
    "data/core/nouns.tsv",
    "data/core/expressions.tsv",
    "data/core/suffixes.tsv",
]

VALID_POS = [
    "ADJECTIVE",
    "ADVERB",
    "VERB",
    "NOUN",
    "PROPER_NOUN",
    "PRONOUN",
    "PREFIX",
    "SUFFIX",
    "INTERJECTION",
    "ADNOMINAL",
    "CONJUNCTION",
    "PARTICLE",
    "AUX",
    "OTHER",
]

VALID_CONJ = [
    "ICHIDAN",
    "GODAN_KA",
    "GODAN_GA",
    "GODAN_SA",
    "GODAN_TA",
    "GODAN_NA",
    "GODAN_BA",
    "GODAN_MA",
    "GODAN_RA",
    "GODAN_WA",
    "SURU",
    "KURU",
    "I_ADJ",
    "NA_ADJ",
    "FAMILY",
    "GIVEN",
]


def _validate_surface(word: str) -> str | None:
    """Return an error message if a dictionary surface is unsafe."""
    if not word:
        return "Word must not be empty."
    if any(ch in word for ch in ("\t", "\n", "\r")):
        return "Word must not contain tab or newline characters."
    if word.startswith("#"):
        return "Word must not start with '#'."
    return None


# POS mapping: SuzumeUtils POS → dictionary format
_DICT_POS_MAP = {
    "Noun": "NOUN",
    "Verb": "VERB",
    "Adjective": "ADJECTIVE",
    "Adverb": "ADVERB",
    "Prefix": "PREFIX",
    "Suffix": "SUFFIX",
    "Pronoun": "PRONOUN",
    "Interjection": "INTERJECTION",
    "Adnominal": "ADNOMINAL",
    "Conjunction": "CONJUNCTION",
    "Particle": "PARTICLE",
    "Auxiliary": "AUXILIARY",
    "Symbol": "SYMBOL",
    "Filler": "FILLER",
    "Other": "OTHER",
}


def _to_dict_pos(pos: str) -> str:
    return _DICT_POS_MAP.get(pos, pos.upper())


def _map_conj_type(token: dict) -> str:
    ctype = token.get("conj_type", "")
    if "一段" in ctype:
        return "ICHIDAN"
    for row, name in [
        ("カ行", "GODAN_KA"),
        ("ガ行", "GODAN_GA"),
        ("サ行", "GODAN_SA"),
        ("タ行", "GODAN_TA"),
        ("ナ行", "GODAN_NA"),
        ("バ行", "GODAN_BA"),
        ("マ行", "GODAN_MA"),
        ("ラ行", "GODAN_RA"),
        ("ワ行", "GODAN_WA"),
    ]:
        if f"五段・{row}" in ctype:
            return name
    if "サ変" in ctype:
        return "SURU"
    if "カ変" in ctype:
        return "KURU"
    if "形容詞" in ctype:
        return "I_ADJ"
    return ""


def _iter_dictionary_lines(file_list: list[str], *, include_disabled: bool = False) -> Iterator[tuple[str, int, str]]:
    """Yield (relative path, line number, active entry text) for dictionary data."""
    disabled_prefix = "#DISABLED# "
    for file_rel in file_list:
        filepath = PROJECT_ROOT / file_rel
        if not filepath.exists():
            continue
        for line_num, line in enumerate(filepath.read_text(encoding="utf-8").splitlines(), 1):
            if line.startswith(disabled_prefix):
                if include_disabled:
                    yield file_rel, line_num, line[len(disabled_prefix) :]
                continue
            if line.startswith("#") or not line.strip():
                continue
            yield file_rel, line_num, line


def _load_entries_from_files(file_list: list[str]) -> tuple[list[dict], dict[str, list[dict]]]:
    """Load dictionary entries from a list of file paths."""
    entries = []
    by_surface: dict[str, list[dict]] = {}
    for file_rel, line_num, line in _iter_dictionary_lines(file_list):
        fields = line.split("\t")
        entry = {
            "surface": fields[0],
            "pos": fields[1] if len(fields) > 1 else "",
            "conj_type": fields[2] if len(fields) > 2 else "",
            "line_num": line_num,
            "raw": line,
            "file": file_rel,
        }
        entries.append(entry)
        by_surface.setdefault(fields[0], []).append(entry)
    return entries, by_surface


def _load_dictionary() -> tuple[list[dict], dict[str, list[dict]]]:
    """Load all core dictionary entries."""
    return _load_entries_from_files(list(ALL_DICT_FILES))


def _all_user_files() -> list[str]:
    """Return list of all user dictionary file paths."""
    return [f"data/user/{cat}.tsv" for cat in USER_CATEGORIES]


def _load_all_entries() -> tuple[list[dict], dict[str, list[dict]]]:
    """Load all dictionary entries (core + user)."""
    return _load_entries_from_files(list(ALL_DICT_FILES) + _all_user_files())


def _find_word_in_files(word: str) -> str | None:
    """Find word in core dict files, return file path or None."""
    for file_rel, _, line in _iter_dictionary_lines(list(ALL_DICT_FILES), include_disabled=True):
        if line.split("\t")[0] == word:
            return file_rel
    return None


def _find_word_in_user_files(word: str) -> str | None:
    """Find word in user dict files."""
    for file_rel, _, line in _iter_dictionary_lines(_all_user_files()):
        if line.split("\t")[0] == word:
            return file_rel
    return None


def _token_to_dict(token: dict) -> dict:
    """Convert a MeCab token dict to a clean JSON-serializable dict."""
    return {
        "surface": token.get("surface", ""),
        "pos": token.get("pos", ""),
        "pos_sub1": token.get("pos_sub1", ""),
        "conj_form": token.get("conj_form", ""),
        "lemma": token.get("lemma", ""),
    }


async def _recompile_core_dic() -> str:
    """Recompile core dictionary."""
    cli = get_cli_path()
    if not cli.exists():
        return "not_found"

    import tempfile

    output_path = PROJECT_ROOT / "data/core.dic"
    with tempfile.NamedTemporaryFile(dir=output_path.parent, suffix=".dic", delete=False) as output:
        tmp_output_path = Path(output.name)

    try:
        if await recompile_dic("data/core/*.tsv", str(tmp_output_path)):
            tmp_output_path.replace(output_path)
            return "ok"
        return "failed"
    finally:
        tmp_output_path.unlink(missing_ok=True)


async def _recompile_user_dic() -> str:
    """Recompile user dictionary."""
    cli = get_cli_path()
    if not cli.exists():
        return "not_found"

    import tempfile

    output_path = PROJECT_ROOT / "data/user.dic"
    with tempfile.NamedTemporaryFile(dir=output_path.parent, suffix=".dic", delete=False) as output:
        tmp_output_path = Path(output.name)

    try:
        if await recompile_dic("data/user/*.tsv", str(tmp_output_path)):
            tmp_output_path.replace(output_path)
            return "ok"
        return "failed"
    finally:
        tmp_output_path.unlink(missing_ok=True)
