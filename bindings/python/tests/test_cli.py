"""End-to-end contract tests for the installed Python console command."""

from __future__ import annotations

import json
import os
import subprocess
import sysconfig
from io import StringIO
from pathlib import Path
from typing import TypedDict, cast

import pytest

import suzume
from suzume import cli

_JSON_MORPHEME_KEYS = [
    "surface",
    "pos",
    "lemma",
    "start",
    "end",
    "extended_pos",
    "is_user_dict",
    "is_formal_noun",
    "is_low_info",
    "is_unknown",
    "is_from_dictionary",
    "score",
]


class _JsonPayload(TypedDict):
    input: str
    morphemes: list[dict[str, object]]


def _console_script() -> str:
    executable = "suzume.exe" if os.name == "nt" else "suzume"
    script = Path(sysconfig.get_path("scripts")) / executable
    assert script.is_file(), "the active Python environment has no suzume console script"
    return str(script)


def _run_cli(
    *args: str,
    input_text: str | None = None,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [_console_script(), *args],
        input=input_text,
        text=True,
        capture_output=True,
        check=False,
    )


def _run_cli_bytes(*args: str, input_bytes: bytes) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        [_console_script(), *args],
        input=input_bytes,
        capture_output=True,
        check=False,
    )


def _json_result(*args: str, input_text: str | None = None) -> _JsonPayload:
    result = _run_cli("--format", "json", *args, input_text=input_text)
    assert result.returncode == 0, result.stderr
    return cast(_JsonPayload, json.loads(result.stdout))


def test_console_script_comes_from_active_environment() -> None:
    script = Path(_console_script()).resolve()
    scripts_dir = Path(sysconfig.get_path("scripts")).resolve()
    assert script.parent == scripts_dir


@pytest.mark.parametrize("alias", [["--version"], ["-v"], ["version"]])
def test_version_aliases(alias: list[str]) -> None:
    result = _run_cli(*alias)
    assert result.returncode == 0
    assert result.stdout.strip() == f"suzume {suzume.version()}"


def test_top_level_help_contains_analysis_options() -> None:
    result = _run_cli("--help")
    assert result.returncode == 0
    for option in (
        "--dict",
        "--mode",
        "--format",
        "--normalize-vu",
        "--lowercase",
        "--preserve-symbols",
        "--no-lemmatize",
        "--merge-compounds",
        "--tag-pos",
        "--tag-min-length",
        "--tag-max-tags",
    ):
        assert option in result.stdout


def test_implicit_and_explicit_analyze_have_exact_same_input() -> None:
    implicit = _json_result("東京", "公園")
    command_first = _json_result("analyze", "東京", "公園")
    command_after_options = _run_cli(
        "--mode", "search", "analyze", "--format", "json", "東京", "公園"
    )
    assert command_after_options.returncode == 0, command_after_options.stderr

    after_options_payload = json.loads(command_after_options.stdout)
    assert implicit["input"] == "東京 公園"
    assert command_first["input"] == "東京 公園"
    assert after_options_payload["input"] == "東京 公園"
    assert command_first["morphemes"] == implicit["morphemes"]


def test_double_dash_preserves_analyze_and_option_text_literally() -> None:
    payload = _json_result("--", "analyze", "--format")
    assert payload["input"] == "analyze --format"

    explicit = _json_result("analyze", "--", "analyze")
    assert explicit["input"] == "analyze"


def test_json_output_matches_native_schema_exactly() -> None:
    result = _run_cli("analyze", "--format", "json", "東京の公園")
    assert result.returncode == 0
    payload = json.loads(result.stdout)
    assert list(payload) == ["input", "morphemes"]
    assert payload["input"] == "東京の公園"
    assert payload["morphemes"]
    assert list(payload["morphemes"][0]) == _JSON_MORPHEME_KEYS
    assert "\\u6771" not in result.stdout


def test_json_scores_use_native_float_max_digits10_precision() -> None:
    morpheme = suzume.Morpheme(
        surface="東京",
        pos="NOUN",
        base_form="東京",
        pos_ja="名詞",
        extended_pos="NOUN",
        start=0,
        end=2,
        is_user_dict=False,
        is_formal_noun=False,
        is_low_info=False,
        is_unknown=True,
        is_from_dictionary=False,
        score=0.20000000298023224,
    )
    assert cli._morpheme_dict(morpheme)["score"] == 0.200000003


@pytest.mark.parametrize(
    ("stdin_text", "expected"),
    [
        ("東京", "東京"),
        ("東京\n", "東京"),
        ("東京\n公園\n", "東京\n公園"),
        ("東京\n\n", "東京\n"),
        ("\ufeff東京\n", "東京"),
        ("東京\ufeff公園\n", "東京\ufeff公園"),
    ],
)
def test_stdin_normalization_matches_native(stdin_text: str, expected: str) -> None:
    payload = _json_result(input_text=stdin_text)
    assert payload["input"] == expected


@pytest.mark.parametrize(
    ("argument_text", "expected"),
    [
        ("\ufeff東京", "東京"),
        ("東京\n", "東京"),
        ("東京\n公園\n", "東京\n公園"),
        ("東京\n\n", "東京\n"),
    ],
)
def test_argument_normalization_matches_stdin(argument_text: str, expected: str) -> None:
    argument_payload = _json_result(argument_text)
    stdin_payload = _json_result(input_text=argument_text)
    assert argument_payload["input"] == stdin_payload["input"] == expected


def test_crlf_stdin_normalization() -> None:
    assert cli._read_text([], StringIO("\ufeff東京\r\n公園\r\n")) == "東京\r\n公園"


def test_interactive_stdin_is_never_read() -> None:
    class InteractiveInput(StringIO):
        def isatty(self) -> bool:
            return True

        def read(self, size: int | None = -1) -> str:
            raise AssertionError("interactive stdin must not be read")

    assert cli._read_text([], InteractiveInput()) == ""


@pytest.mark.parametrize("mode", ["normal", "search", "split"])
def test_all_analysis_modes_parse(mode: str) -> None:
    parser = cli._build_parser()
    args = parser.parse_args(cli._without_explicit_command(["--mode", mode, "analyze", "東京"]))
    assert args.mode == mode
    assert args.text == ["東京"]


def test_normalization_lemmatize_compound_and_tag_options_are_wired() -> None:
    parser = cli._build_parser()
    args = parser.parse_args(
        cli._without_explicit_command(
            [
                "--normalize-vu",
                "--lowercase",
                "--preserve-symbols",
                "--no-lemmatize",
                "--merge-compounds",
                "--tag-pos",
                "noun",
                "--tag-pos",
                "verb",
                "--tag-exclude-basic",
                "--tag-use-surface",
                "--tag-min-length",
                "1",
                "--tag-max-tags",
                "3",
                "--include-particles",
                "--include-auxiliaries",
                "--include-formal-nouns",
                "--include-low-info",
                "--tag-keep-duplicates",
                "analyze",
                "東京",
            ]
        )
    )
    assert args.normalize_vu
    assert args.lowercase
    assert args.preserve_symbols
    assert args.no_lemmatize
    assert args.merge_compounds
    assert args.tag_pos == ["noun", "verb"]
    assert args.tag_exclude_basic
    assert args.tag_use_surface
    assert args.tag_min_length == 1
    assert args.tag_max_tags == 3
    assert args.include_particles
    assert args.include_auxiliaries
    assert args.include_formal_nouns
    assert args.include_low_info
    assert args.tag_keep_duplicates
    assert args.text == ["東京"]


def test_normalization_and_symbol_options_change_analysis() -> None:
    preserved = _json_result("--preserve-symbols", "TEST。")
    normalized = _json_result("--lowercase", "--preserve-symbols", "TEST。")
    assert preserved["input"] == normalized["input"] == "TEST。"
    assert preserved["morphemes"] != normalized["morphemes"]
    assert any(morpheme["surface"] == "。" for morpheme in preserved["morphemes"])


def test_tag_and_chasen_outputs() -> None:
    tags = _run_cli(
        "--format",
        "tags",
        "--tag-pos",
        "noun",
        "--tag-min-length",
        "1",
        "東京の公園",
    )
    assert tags.returncode == 0
    assert "\tNOUN" in tags.stdout

    chasen = _run_cli("--format", "chasen", "りんごを食べる")
    assert chasen.returncode == 0
    assert chasen.stdout.endswith("EOS\n")


def test_repeatable_text_dictionaries_affect_analysis(tmp_path: Path) -> None:
    first = tmp_path / "first.tsv"
    second = tmp_path / "second.tsv"
    first.write_text("青空庭園\tNOUN\n", encoding="utf-8")
    second.write_text("東京果樹園\tNOUN\n", encoding="utf-8")
    result = _run_cli(
        "--dict",
        str(first),
        "--dict",
        str(second),
        "--format",
        "json",
        "青空庭園",
        "東京果樹園",
    )
    assert result.returncode == 0, result.stderr
    morphemes = json.loads(result.stdout)["morphemes"]
    user_surfaces = {item["surface"] for item in morphemes if item["is_user_dict"]}
    assert user_surfaces == {"青空庭園", "東京果樹園"}


def test_missing_and_invalid_text_dictionaries_fail(tmp_path: Path) -> None:
    missing = _run_cli("--dict", str(tmp_path / "missing.tsv"), "東京")
    assert missing.returncode == 1
    assert "error" in missing.stderr

    invalid = tmp_path / "invalid.csv"
    invalid.write_text('"東京,NOUN,0.5\n', encoding="utf-8")
    malformed = _run_cli("--dict", str(invalid), "東京")
    assert malformed.returncode == 1
    assert "Invalid CSV quoting" in malformed.stderr


def test_invalid_binary_dictionary_fails(tmp_path: Path) -> None:
    dictionary = tmp_path / "invalid.dic"
    dictionary.write_bytes(b"invalid")
    result = _run_cli("--dict", str(dictionary), "東京")
    assert result.returncode == 1
    assert "Dictionary file too small" in result.stderr


def test_more_than_one_binary_dictionary_is_rejected(tmp_path: Path) -> None:
    first = tmp_path / "first.dic"
    second = tmp_path / "second.dic"
    result = _run_cli("--dict", str(first), "--dict", str(second), "東京")
    assert result.returncode == 1
    assert "at most one binary .dic dictionary" in result.stderr


@pytest.mark.parametrize(
    "args",
    [
        ["--tag-min-length", "-1", "東京"],
        ["--tag-max-tags", "-1", "東京"],
        ["--mode"],
        ["--format"],
        ["--dict"],
    ],
)
def test_negative_and_missing_option_values_are_syntax_errors(args: list[str]) -> None:
    result = _run_cli(*args)
    assert result.returncode == 2
    assert "error" in result.stderr


def test_missing_input_returns_one_and_uses_stderr() -> None:
    result = _run_cli(input_text="")
    assert result.returncode == 1
    assert result.stdout == ""
    assert "no input text provided" in result.stderr


def test_invalid_utf8_stdin_fails_cleanly() -> None:
    result = _run_cli_bytes("--format", "json", input_bytes=b"\xff\n")
    assert result.returncode == 1
    assert b"error" in result.stderr
