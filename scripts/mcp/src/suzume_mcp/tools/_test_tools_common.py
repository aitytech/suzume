"""Test tools ported from test_tool.pl - MCP tool registration."""

import re
from pathlib import Path

from ..core.constants import TARI_ADVERB_STEMS
from ..core.json_utils import json_error as _json_error  # noqa: F401
from ..core.json_utils import json_result as _json_result  # noqa: F401
from ..core.pos_mapping import normalize_pos
from ..core.suzume_cli import (
    format_expected_from_tokens as format_expected,
)
from ..core.test_file_utils import (
    get_test_data_dir,
    get_test_files,
)
from ..server import PROJECT_ROOT


def _get_suzume_tokens(text: str) -> list[dict]:
    """Get Suzume CLI tokens with POS and lemma.

    Uses --no-user-dict to match the C++ tokenization test runner oracle.
    """
    cli = PROJECT_ROOT / "build" / "bin" / "suzume-cli"
    if not cli.exists():
        raise RuntimeError(f"Suzume CLI not found: {cli}")

    import subprocess

    result = subprocess.run(
        [str(cli), "--no-user-dict", text],
        capture_output=True,
        text=True,
        timeout=30,
        cwd=PROJECT_ROOT,
    )
    if result.returncode != 0:
        raise RuntimeError(f"Suzume CLI failed: {result.stderr.strip() or 'non-zero exit'}")

    tokens = []
    for line in result.stdout.split("\n"):
        if not line or line == "EOS":
            continue
        parts = line.split("\t")
        surface = parts[0]
        pos = normalize_pos(parts[1]) if len(parts) > 1 else "Other"
        lemma = parts[2] if len(parts) > 2 else surface
        # Tari adverb lemma normalization
        for stem in TARI_ADVERB_STEMS:
            if surface == f"{stem}と" and lemma == f"{stem}と":
                lemma = stem
                break
        tokens.append({"surface": surface, "pos": pos, "lemma": lemma})
    return tokens


def _format_expected_checked(tokens: list[dict], source: str) -> list[dict]:
    """Format expected tokens, rejecting empty oracle output before any write."""
    if not tokens:
        raise RuntimeError(f"{source} produced no tokens; refusing to write empty expected output")
    return format_expected(tokens)


def _get_test_files_filtered(file_filter: str = "") -> list[Path]:
    """Get test files, optionally filtered by name."""
    if file_filter and file_filter != "all":
        path = get_test_data_dir(PROJECT_ROOT) / f"{file_filter}.json"
        return [path] if path.exists() else []
    return get_test_files(PROJECT_ROOT)


def _detect_segmentation_pattern(correct_str: str, suzume_str: str, input_text: str) -> str:
    """Detect segmentation failure pattern."""
    correct = correct_str.split("|")
    suzume = suzume_str.split("|")

    pos = 0
    correct_spans = []
    for tok in correct:
        length = len(tok)
        correct_spans.append({"token": tok, "start": pos, "end": pos + length})
        pos += length

    pos = 0
    for suz_tok in suzume:
        suz_len = len(suz_tok)
        suz_end = pos + suz_len
        covered = [s for s in correct_spans if s["start"] >= pos and s["end"] <= suz_end]
        if len(covered) > 1:
            merged = "".join(s["token"] for s in covered)
            if re.search(r"く.?ない$", merged):
                return "くない未分割"
            if re.search(r".?ん$", merged) and len(covered) >= 2:
                return "ん未分割"
            if any(s["token"] == "て" for s in covered):
                return "て形未分割"
            if re.search(r"て.?(?:い|いる|いた)$", merged):
                return "ている未分割"
            if any(s["token"] == "たい" for s in covered):
                return "たい未分割"
            if any(re.match(r"^ま[すせし]", s["token"]) for s in covered):
                return "ます未分割"
            if any(re.match(r"^[たっだ]$", s["token"]) for s in covered):
                return "た/だ未分割"
            if any(s["token"] in ("ない", "なかっ") for s in covered):
                return "ない未分割"
            if any(re.match(r"^[らりれろ]れ", s["token"]) or re.match(r"^れ[るた]", s["token"]) for s in covered):
                return "れる/られる未分割"
            if any(re.match(r"^さ?せ", s["token"]) for s in covered):
                return "せる/させる未分割"
            pattern = "+".join(s["token"] for s in covered)
            return f"未分割({pattern})"
        pos = suz_end

    if len(suzume) > len(correct):
        return "過分割"
    return "その他"
