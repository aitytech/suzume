"""Real-process contract checks for the native Suzume CLI."""

from __future__ import annotations

import json
import os
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
    env: dict[str, str] | None = None,
    cwd: Path | None = None,
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
        env=env,
        cwd=cwd,
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

    invalid = run_cli(cli, "-v", "--mode", "invalid", "東京", expect_success=False)
    assert invalid.returncode == 2
    assert "Invalid mode" in invalid.stderr


def assert_help_contract(cli: Path) -> None:
    top_level = run_cli(cli, "--help")
    analyze = run_cli(cli, "analyze", "--help")
    dictionary = run_cli(cli, "dict", "--help")
    assert "Commands:" in top_level.stdout
    assert "Output Formats:" not in top_level.stdout
    assert "Output Formats:" in analyze.stdout
    assert "Commands:" not in analyze.stdout
    assert "-VV, --very-verbose" in analyze.stdout
    assert "--debug" in top_level.stdout
    assert "--debug" in analyze.stdout
    assert "SUZUME_DEBUG" in top_level.stdout
    assert "SUZUME_SCORER_{SECTION}_{KEY}" in analyze.stdout
    assert "tsv                    Alias of morpheme" in analyze.stdout
    assert "compile <in1.tsv> <in2.tsv>... <out.dic>" in dictionary.stdout
    assert "--filter-trivial" in dictionary.stdout
    for value in ("CONJUNCTION", "DETERMINER", "PRONOUN", "PREFIX", "SUFFIX", "INTERJECTION"):
        assert value in dictionary.stdout
    assert "FAMILY, GIVEN" in dictionary.stdout

    morpheme = run_cli(cli, "analyze", "--format", "morpheme", "東京")
    tsv = run_cli(cli, "analyze", "--format", "tsv", "東京")
    assert tsv.stdout == morpheme.stdout


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


def assert_control_characters_do_not_corrupt_tsv(cli: Path) -> None:
    result = run_cli(
        cli,
        "analyze",
        "--preserve-symbols",
        "--format",
        "tsv",
        "東京\tに\n行く\\確認",
    )
    rows = [line.split("\t") for line in result.stdout.splitlines()]
    assert rows
    assert all(len(row) == 5 for row in rows), rows
    assert "\\t" in result.stdout
    assert "\\n" in result.stdout
    assert "\\\\" in result.stdout


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


def assert_chasen_contract(cli: Path) -> None:
    result = run_cli(cli, "analyze", "--format", "chasen", "りんごを食べる")
    assert result.stdout == (
        "りんご\t*\tりんご\t名詞\t*\t*\n"
        "を\t*\tを\t助詞\t*\t*\n"
        "食べる\t*\t食べる\t動詞\t一段\t終止形\n"
        "EOS\n"
    )


def assert_debug_uses_production_pipeline(cli: Path) -> None:
    text = "2024年12月23日にhttps://example.com/abcを見た。価格は1,000円。"
    production = run_cli(cli, "analyze", "--format", "tsv", text)
    debug = run_cli(cli, "analyze", "--debug", text)
    marker = "=== Result ===\n"
    assert marker in debug.stdout
    assert debug.stdout.split(marker, 1)[1] == production.stdout


def assert_single_test_contract(cli: Path) -> None:
    result = run_cli(cli, "test", "--verbose", "東京へ", "--expect", "東京")
    assert "PASS: 東京へ" in result.stdout, result.stdout
    run_cli(
        cli,
        "test",
        "--tag-min-length",
        "1",
        "--include-particles",
        "東京へ",
        "--expect",
        "東京,へ",
    )


def assert_cli_failure_and_structural_diff_contract(cli: Path) -> None:
    missing_input = run_cli(cli, "analyze", expect_success=False)
    assert missing_input.stdout == ""
    assert "Usage:" in missing_input.stderr

    no_dict_args = run_cli(cli, "dict", expect_success=False)
    assert no_dict_args.stdout == ""
    assert "Usage:" in no_dict_args.stderr

    unknown_test = run_cli(
        cli,
        "test",
        "東京",
        "--expect",
        "東京",
        "--unexpected",
        expect_success=False,
    )
    assert "Unknown test option" in unknown_test.stderr
    unknown_benchmark = run_cli(
        cli,
        "test",
        "benchmark",
        "--iterrations=1",
        expect_success=False,
    )
    assert "Unknown benchmark option" in unknown_benchmark.stderr

    with tempfile.TemporaryDirectory(prefix="suzume-cli-contract-") as temp_name:
        temp_dir = Path(temp_name)
        dictionary = temp_dir / "lemma.tsv"
        dictionary.write_text("東京\tNOUN\t\tとうきょう\n", encoding="utf-8")
        compared = run_cli(cli, "analyze", "--compare", "--dict", str(dictionary), "東京")
        assert "No structural difference" not in compared.stdout
        assert "- 0:" in compared.stdout
        assert "+ 0:" in compared.stdout

        crlf_tests = temp_dir / "tests.tsv"
        crlf_tests.write_bytes("東京へ\t東京\r\n".encode())
        crlf_result = run_cli(cli, "test", "--file", str(crlf_tests))
        assert "1 passed, 0 failed" in crlf_result.stdout

        directory_result = run_cli(cli, "dict", "info", str(temp_dir), expect_success=False)
        assert "not a regular file" in directory_result.stderr

        (temp_dir / "first.tsv").write_text("東京\tNOUN\n", encoding="utf-8")
        (temp_dir / "second.tsv").write_text("りんご\tNOUN\n", encoding="utf-8")
        bare_glob = run_cli(
            cli,
            "dict",
            "compile",
            "*.tsv",
            cwd=temp_dir,
            expect_success=False,
        )
        assert "requires an explicit output" in bare_glob.stderr


def assert_bundled_user_dictionary_contract(cli: Path) -> None:
    result = run_cli(cli, "analyze", "--format", "json", "セーラー服を着る")
    morphemes = json.loads(result.stdout)["morphemes"]
    assert morphemes[0]["surface"] == "セーラー服"
    assert morphemes[0]["is_user_dict"] is True

    engine_owned_cases = {
        "キャミソールとタンクトップを選ぶ": ["キャミソール", "と", "タンクトップ", "を", "選ぶ"],
        "いつの間にか終わった": ["いつ", "の", "間", "に", "か", "終わっ", "た"],
        "つめあわせを作る": ["つめあわせ", "を", "作る"],
        "病みかわが好き": ["病みかわ", "が", "好き"],
    }
    for input_text, expected_surfaces in engine_owned_cases.items():
        analyzed = run_cli(cli, "analyze", "--format", "json", input_text)
        analyzed_morphemes = json.loads(analyzed.stdout)["morphemes"]
        assert [morpheme["surface"] for morpheme in analyzed_morphemes] == expected_surfaces
        assert all(not morpheme["is_user_dict"] for morpheme in analyzed_morphemes)


def assert_dictionary_contract(cli: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="suzume-native-cli-") as temp_name:
        temp_dir = Path(temp_name)
        source = temp_dir / "compiled.tsv"
        compiled = temp_dir / "compiled.dic"
        source.write_text("東京テスト\tNOUN\n", encoding="utf-8")

        created = temp_dir / "created.tsv"
        run_cli(cli, "dict", "new", str(created))
        created_header = created.read_text(encoding="utf-8")
        assert "surface<TAB>pos<TAB>conj_type<TAB>lemma" in created_header
        assert "reading<TAB>cost" not in created_header
        for value in ("CONJUNCTION", "DETERMINER", "PRONOUN", "PREFIX", "SUFFIX", "INTERJECTION"):
            assert value in created_header
        assert "FAMILY, GIVEN" in created_header
        created.write_text(created_header + "東京公園\tNOUN\n", encoding="utf-8")
        run_cli(cli, "dict", "compile", str(created), str(temp_dir / "created.dic"))

        filter_source = temp_dir / "filter.tsv"
        filter_binary = temp_dir / "filter.dic"
        filter_source.write_text("東京公園\tNOUN\nりん\tNOUN\n", encoding="utf-8")
        filtered = run_cli(
            cli,
            "dict",
            "compile",
            "--filter-trivial",
            str(filter_source),
            str(filter_binary),
        )
        assert "Filtered 1 trivial entries (kept 1)" in filtered.stderr

        iku_lookup = run_cli(cli, "dict", "lookup", "行く")
        assert "行っ- (音便形)" in iku_lookup.stdout
        assert "行い- (音便形)" not in iku_lookup.stdout

        run_cli(cli, "dict", "compile", str(source), str(compiled))
        assert compiled.is_file()
        binary_search = run_cli(cli, "dict", "search", str(compiled), "東京*")
        assert "東京テスト\tNOUN" in binary_search.stdout
        assert "(1 matches)" in binary_search.stdout

        uppercase_binary = temp_dir / "uppercase.DIC"
        run_cli(cli, "dict", "compile", str(source), str(uppercase_binary))
        uppercase_list = run_cli(cli, "dict", "list", str(uppercase_binary))
        assert "東京テスト\tNOUN" in uppercase_list.stdout
        uppercase_search = run_cli(cli, "dict", "search", str(uppercase_binary), "東京*")
        assert "東京テスト\tNOUN" in uppercase_search.stdout

        default_dump = compiled.with_suffix(".dump.tsv")
        run_cli(cli, "dict", "decompile", str(compiled))
        assert default_dump.is_file()
        assert source.read_text(encoding="utf-8") == "東京テスト\tNOUN\n"
        refused_dump = run_cli(cli, "dict", "decompile", str(compiled), expect_success=False)
        assert "Refusing to overwrite" in refused_dump.stderr
        run_cli(cli, "dict", "decompile", str(compiled), "--force")

        explicit_dump = temp_dir / "explicit.tsv"
        explicit_dump.write_text("preserve me\n", encoding="utf-8")
        refused_explicit = run_cli(
            cli,
            "dict",
            "decompile",
            str(compiled),
            str(explicit_dump),
            expect_success=False,
        )
        assert explicit_dump.read_text(encoding="utf-8") == "preserve me\n"
        assert "use --force" in refused_explicit.stderr
        run_cli(cli, "dict", "decompile", str(compiled), str(explicit_dump), "--force")
        assert explicit_dump.read_text(encoding="utf-8") != "preserve me\n"

        binary_test = run_cli(
            cli,
            "test",
            "--verbose",
            "--dict",
            str(compiled),
            "東京テスト",
            "--expect",
            "東京テスト",
        )
        assert "PASS: 東京テスト" in binary_test.stdout, binary_test.stdout

        result = run_cli(cli, "analyze", "--dict", str(compiled), "--format", "json", "東京テスト")
        morphemes = json.loads(result.stdout)["morphemes"]
        matching = [morpheme for morpheme in morphemes if morpheme["surface"] == "東京テスト"]
        assert matching
        assert matching[0]["is_user_dict"] is True

        adjective_source = temp_dir / "adjective.tsv"
        adjective_source.write_text("静か\tADJECTIVE\tNA_ADJ\n", encoding="utf-8")
        adjective_list = run_cli(cli, "dict", "list", str(adjective_source), "--pos=ADJECTIVE")
        assert "静か\tADJ" in adjective_list.stdout
        interactive_list = run_cli(
            cli,
            "dict",
            "-i",
            str(adjective_source),
            input_text="list --pos=ADJECTIVE\nlist --pos=NOT_A_POS\nquit\n",
        )
        assert "静か\tADJ" in interactive_list.stdout
        assert "Invalid POS: NOT_A_POS" in interactive_list.stderr
        invalid_pos = run_cli(
            cli,
            "dict",
            "list",
            str(adjective_source),
            "--pos=NOT_A_POS",
            expect_success=False,
        )
        assert "Invalid POS: NOT_A_POS" in invalid_pos.stderr

        second_binary_source = temp_dir / "second-binary.tsv"
        second_binary = temp_dir / "second-binary.dic"
        second_binary_source.write_text("テスト公園\tNOUN\n", encoding="utf-8")
        run_cli(cli, "dict", "compile", str(second_binary_source), str(second_binary))
        additive_binary = run_cli(
            cli,
            "analyze",
            "--dict",
            str(compiled),
            "--dict",
            str(second_binary),
            "--format",
            "json",
            "東京テスト テスト公園",
        )
        additive_surfaces = {
            morpheme["surface"] for morpheme in json.loads(additive_binary.stdout)["morphemes"]
        }
        assert {"東京テスト", "テスト公園"} <= additive_surfaces

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

        round_trip_source = temp_dir / "names.tsv"
        round_trip_source.write_text(
            "東京\tNOUN\tFAMILY\n"
            "テスト\tNOUN\tGIVEN\n",
            encoding="utf-8",
        )
        run_cli(cli, "dict", "-i", str(round_trip_source), input_text="save\nquit\n")
        saved_lines = {
            line
            for line in round_trip_source.read_text(encoding="utf-8").splitlines()
            if line and not line.startswith("#")
        }
        assert saved_lines == {
            "東京\tNOUN\tFAMILY",
            "テスト\tNOUN\tGIVEN",
        }
        run_cli(cli, "dict", "compile", str(round_trip_source), str(temp_dir / "names.dic"))

        session_source = temp_dir / "session.tsv"
        session = run_cli(
            cli,
            "dict",
            "-i",
            str(session_source),
            input_text=(
                "add 青空りんご園 NOUN\n"
                "analyze 青空りんご園\n"
                "add 東京 VERB\n"
                "add 東京 NOUN FAMILY\n"
                "add テスト NOUN GIVEN\n"
                "list --limit=0\n"
                "quit\n"
            ),
        )
        assert "青空りんご園\tNOUN\t青空りんご園" in session.stdout
        assert "Missing conjugation type: 東京" in session.stderr
        saved_session = session_source.read_text(encoding="utf-8")
        assert "東京\tNOUN\tFAMILY" in saved_session
        assert "テスト\tNOUN\tGIVEN" in saved_session

        eof_source = temp_dir / "eof.tsv"
        eof = run_cli(
            cli,
            "dict",
            "-i",
            str(eof_source),
            input_text="add 東京公園 NOUN\n",
        )
        assert "Saved unsaved changes at EOF" in eof.stderr
        assert "東京公園\tNOUN" in eof_source.read_text(encoding="utf-8")

        quoted_source = temp_dir / "quoted.tsv"
        quoted = run_cli(
            cli,
            "dict",
            "-i",
            str(quoted_source),
            input_text="add '' NOUN\nadd '東京 NOUN\nquit\n",
        )
        assert "Surface must not be empty" in quoted.stderr
        assert "Unterminated quoted argument" in quoted.stderr
        assert not quoted_source.exists()

        metachar_surface = r"記号.^$+()[]{}|\終"
        wildcard_source = temp_dir / "wildcards.tsv"
        wildcard_source.write_text(
            f"{metachar_surface}\tNOUN\n"
            "{a\tNOUN\n"
            "a}\tNOUN\n",
            encoding="utf-8",
        )
        literal_match = run_cli(cli, "dict", "search", str(wildcard_source), metachar_surface)
        assert f"{metachar_surface}\tNOUN" in literal_match.stdout
        wildcard_match = run_cli(cli, "dict", "search", str(wildcard_source), "記号*終")
        assert f"{metachar_surface}\tNOUN" in wildcard_match.stdout
        for brace_surface in ("{a", "a}"):
            brace_match = run_cli(cli, "dict", "search", str(wildcard_source), brace_surface)
            assert f"{brace_surface}\tNOUN" in brace_match.stdout

        excessive_pattern = "*" * 65
        excessive_search = run_cli(
            cli,
            "dict",
            "search",
            str(wildcard_source),
            excessive_pattern,
            expect_success=False,
        )
        assert excessive_search.returncode != -6, excessive_search
        assert "too many '*'" in excessive_search.stderr

        interactive_excessive = run_cli(
            cli,
            "dict",
            "-i",
            str(wildcard_source),
            input_text=f"list --pattern={excessive_pattern}\nsearch {excessive_pattern}\nquit\n",
        )
        assert interactive_excessive.returncode == 0
        assert interactive_excessive.stderr.count("too many '*'") == 2

        data_root = temp_dir / "data-root"
        source_dir = data_root / "core"
        source_dir.mkdir(parents=True)
        source_lookup = source_dir / "lookup.tsv"
        source_lookup.write_text("東京テスト公園\tNOUN\n", encoding="utf-8")
        lookup_env = os.environ.copy()
        lookup_env["SUZUME_DATA_DIR"] = str(data_root)
        source_result = run_cli(
            cli,
            "dict",
            "lookup",
            "東京テスト公園",
            env=lookup_env,
        )
        assert "東京テスト公園\tNOUN" in source_result.stdout


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
    assert_control_characters_do_not_corrupt_tsv(cli)
    assert_score_precision(cli)
    assert_tag_filters(cli)
    assert_chasen_contract(cli)
    assert_debug_uses_production_pipeline(cli)
    assert_single_test_contract(cli)
    assert_cli_failure_and_structural_diff_contract(cli)
    assert_bundled_user_dictionary_contract(cli)
    assert_dictionary_contract(cli)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
