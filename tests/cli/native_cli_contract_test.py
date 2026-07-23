"""Real-process contract checks for the native Suzume CLI."""

from __future__ import annotations

import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path


def run_cli(
    cli: Path,
    *args: str,
    input_text: str | None = None,
    input_bytes: bytes | None = None,
    expect_success: bool = True,
) -> subprocess.CompletedProcess[str]:
    if input_text is not None and input_bytes is not None:
        raise ValueError("input_text and input_bytes are mutually exclusive")
    process_input = input_bytes
    if input_text is not None:
        process_input = input_text.encode("utf-8")
    raw_result = subprocess.run(
        [str(cli), *args],
        input=process_input,
        capture_output=True,
        check=False,
    )
    result = subprocess.CompletedProcess(
        raw_result.args,
        raw_result.returncode,
        raw_result.stdout.decode("utf-8"),
        raw_result.stderr.decode("utf-8"),
    )
    if expect_success and result.returncode != 0:
        raise AssertionError(
            f"{' '.join(args)} failed with {result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    if not expect_success and result.returncode == 0:
        raise AssertionError(f"{' '.join(args)} unexpectedly succeeded\nstdout:\n{result.stdout}")
    return result


def assert_invalid_arguments(cli: Path) -> None:
    invalid_commands = (
        ("analyze", "--mode", "wide", "東京"),
        ("analyze", "--format", "yaml", "東京"),
        ("analyze", "--dict"),
        ("analyze", "--tag-pos", "other", "東京"),
        ("analyze", "--tag-min-length"),
        ("analyze", "--unknown-analysis-flag", "東京"),
    )
    for command in invalid_commands:
        result = run_cli(cli, *command, expect_success=False)
        assert result.stderr.startswith("error:"), (command, result.stderr)


def assert_version_contract(cli: Path) -> None:
    short = run_cli(cli, "-v")
    long = run_cli(cli, "--version")
    command = run_cli(cli, "version")
    assert short.stdout == long.stdout == command.stdout
    first_line = short.stdout.splitlines()[0]
    assert first_line.startswith("suzume-cli ")
    assert len(first_line.split()) == 2


def assert_help_contract(cli: Path) -> None:
    top_level = run_cli(cli, "--help")
    analyze = run_cli(cli, "analyze", "--help")
    assert "Commands:" in top_level.stdout
    assert "Output Formats:" not in top_level.stdout
    assert "Output Formats:" in analyze.stdout
    assert "Commands:" not in analyze.stdout


def assert_stdin_contract(cli: Path) -> None:
    cases = {
        "東京\n": "東京",
        "東京\n公園\n": "東京\n公園",
        "東京\n\n": "東京\n",
        "\ufeff東京": "東京",
        "\ufeff東京\r\n": "東京",
    }
    for input_text, expected in cases.items():
        result = run_cli(cli, "analyze", "--format", "json", input_text=input_text)
        payload = json.loads(result.stdout)
        assert payload["input"] == expected, (repr(input_text), repr(payload["input"]))


def assert_invalid_utf8_rejected(cli: Path) -> None:
    invalid_inputs = {
        "stray byte": b"\xff",
        "overlong scalar": b"\xc0\xaf",
        "surrogate": b"\xed\xa0\x80",
        "out-of-range scalar": b"\xf4\x90\x80\x80",
    }
    for label, input_bytes in invalid_inputs.items():
        result = run_cli(
            cli,
            "analyze",
            "--format",
            "json",
            input_bytes=input_bytes,
            expect_success=False,
        )
        assert result.stdout == "", (label, result.stdout)
        assert "Invalid UTF-8 input" in result.stderr, (label, result.stderr)


def assert_json_schema(cli: Path) -> None:
    result = run_cli(cli, "analyze", "--format", "json", "東京へ行く")
    payload = json.loads(result.stdout)
    assert set(payload) == {"input", "morphemes"}
    assert payload["morphemes"]
    assert set(payload["morphemes"][0]) == {
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
    }


def assert_score_precision(cli: Path) -> None:
    result = run_cli(
        cli,
        "analyze",
        "--no-core-dict",
        "--no-user-dict",
        "--format",
        "json",
        "へ",
    )
    payload = json.loads(result.stdout)
    assert re.findall(r'"score": ([^,}\s]+)', result.stdout) == ["0.200000003"]
    assert [morpheme["score"] for morpheme in payload["morphemes"]] == [0.200000003]


def assert_tag_filters(cli: Path) -> None:
    nouns = run_cli(
        cli,
        "analyze",
        "--format",
        "tags",
        "--tag-pos",
        "noun",
        "--tag-min-length",
        "1",
        "東京を走る",
    )
    noun_lines = [line.split("\t") for line in nouns.stdout.splitlines()]
    assert noun_lines
    assert {parts[1] for parts in noun_lines} == {"NOUN"}

    nouns_and_verbs = run_cli(
        cli,
        "analyze",
        "--format",
        "tags",
        "--tag-pos",
        "noun",
        "--tag-pos",
        "verb",
        "--tag-min-length",
        "1",
        "東京を走る",
    )
    selected_pos = {line.rsplit("\t", 1)[1] for line in nouns_and_verbs.stdout.splitlines()}
    assert selected_pos == {"NOUN", "VERB"}

    no_basic = run_cli(
        cli,
        "analyze",
        "--format",
        "tags",
        "--tag-exclude-basic",
        "--tag-min-length",
        "1",
        "りんご 東京",
    )
    tags = {line.split("\t", 1)[0] for line in no_basic.stdout.splitlines()}
    assert "東京" in tags
    assert "りんご" not in tags


def assert_dictionary_contract(cli: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="suzume-native-cli-") as temp_name:
        temp_dir = Path(temp_name)
        source = temp_dir / "compiled.tsv"
        compiled = temp_dir / "compiled.dic"
        source.write_text("東京テスト\tNOUN\n", encoding="utf-8")

        run_cli(cli, "dict", "compile", str(source), str(compiled))
        assert compiled.is_file()

        result = run_cli(cli, "analyze", "--dict", str(compiled), "--format", "json", "東京テスト")
        morphemes = json.loads(result.stdout)["morphemes"]
        matching = [morpheme for morpheme in morphemes if morpheme["surface"] == "東京テスト"]
        assert matching
        assert matching[0]["is_user_dict"] is True

        duplicate_binary = run_cli(
            cli,
            "analyze",
            "--dict",
            str(compiled),
            "--dict",
            str(compiled),
            "東京",
            expect_success=False,
        )
        assert "Only one binary .dic dictionary" in duplicate_binary.stderr

        first_text = temp_dir / "first.tsv"
        second_text = temp_dir / "second.tsv"
        first_text.write_text("りんご東京\tNOUN\n", encoding="utf-8")
        second_text.write_text("東京りんご\tNOUN\n", encoding="utf-8")
        mixed = run_cli(
            cli,
            "analyze",
            "--dict",
            str(first_text),
            "--dict",
            str(compiled),
            "--dict",
            str(second_text),
            "--format",
            "json",
            "東京テスト りんご東京 東京りんご",
        )
        mixed_morphemes = json.loads(mixed.stdout)["morphemes"]
        by_surface = {morpheme["surface"]: morpheme for morpheme in mixed_morphemes}
        for surface in ("東京テスト", "りんご東京", "東京りんご"):
            assert surface in by_surface, (surface, mixed_morphemes)
            assert by_surface[surface]["is_user_dict"] is True


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(sys.argv[0]).name} /path/to/suzume-cli", file=sys.stderr)
        return 2
    cli = Path(sys.argv[1]).resolve()
    if not cli.is_file():
        print(f"native CLI does not exist: {cli}", file=sys.stderr)
        return 2

    assert_invalid_arguments(cli)
    assert_version_contract(cli)
    assert_help_contract(cli)
    assert_stdin_contract(cli)
    assert_invalid_utf8_rejected(cli)
    assert_json_schema(cli)
    assert_score_precision(cli)
    assert_tag_filters(cli)
    assert_dictionary_contract(cli)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
