"""Tests for dict tools (MCP tool functions)."""

import asyncio
import importlib
import json
import shutil
import subprocess

import pytest

from suzume_mcp.tools.dict_tools import (
    dict_bulk_add,
    dict_bulk_move,
    dict_check,
    dict_cleanup,
    dict_disable,
    dict_enable,
    dict_grep,
    dict_remove,
    dict_remove_matching,
    dict_sort,
    dict_suggest,
    dict_validate,
)

bulk_tools = importlib.import_module("suzume_mcp.tools._dict_tools_bulk")
maintenance_tools = importlib.import_module("suzume_mcp.tools._dict_tools_maintenance")
operation_tools = importlib.import_module("suzume_mcp.tools._dict_tools_operations")
common_tools = importlib.import_module("suzume_mcp.tools._dict_tools_common")
_write_files_and_recompile = common_tools._write_files_and_recompile

pytestmark = pytest.mark.skipif(
    shutil.which("mecab") is None,
    reason="MeCab not installed",
)


def run(coro):
    return asyncio.run(coro)


def parse(result_str: str) -> dict:
    return json.loads(result_str)


class TestDictCheck:
    def test_known_word(self):
        result = parse(run(dict_check("食べる")))
        assert result["word"] == "食べる"
        assert result["mecab"]["is_single"] is True
        assert len(result["mecab"]["tokens"]) == 1

    def test_compound_word(self):
        result = parse(run(dict_check("Wi-Fi")))
        # Either MeCab splits it or it already exists
        assert "mecab" in result or result.get("in_dictionary") is True


class TestDictSuggest:
    def test_verb(self):
        result = parse(run(dict_suggest("食べる")))
        assert result["suggestion"]["pos"] == "VERB"
        assert result["suggestion"]["conj_type"] == "ICHIDAN"

    def test_noun(self):
        result = parse(run(dict_suggest("経済")))
        assert result["suggestion"]["pos"] == "NOUN"

    def test_every_mapped_suggestion_is_accepted_and_has_a_destination(self):
        for mapped_pos in common_tools._DICT_POS_MAP.values():
            assert mapped_pos in common_tools.VALID_POS
            assert mapped_pos in common_tools.POS_TO_FILE


def test_dictionary_pos_tables_share_one_cpp_alias_source():
    assert tuple(common_tools.POS_TO_FILE) == common_tools.VALID_POS
    assert common_tools._to_dict_pos("Auxiliary") == "AUX"
    assert common_tools._to_dict_pos("Symbol") == "SYMBOL"
    assert common_tools._to_dict_pos("Filler") == "OTHER"


class TestDictSort:
    def test_sort_dry_run(self):
        result = parse(run(dict_sort(file="data/core/verbs.tsv", dry_run=True)))
        assert result["total_entries"] > 0
        assert result.get("dry_run") is True
        assert any("Godan" in g["name"] or "Ichidan" in g["name"] for g in result["groups"])

    def test_sort_invalid_user(self):
        result = parse(run(dict_sort(user="nonexistent")))
        assert result["status"] == "error"
        assert "Invalid user category" in result["message"]

    def test_sort_no_args(self):
        result = parse(run(dict_sort()))
        assert result["status"] == "error"
        assert "Specify file" in result["message"]

    def test_sort_preserves_inline_comments_and_grammatical_homographs(self, tmp_path, monkeypatch):
        root = tmp_path
        target = root / "data/core/test.tsv"
        target.parent.mkdir(parents=True)
        target.write_text(
            "# header\n東京\tNOUN\n# keep with the following entry\n東京\tPROPER_NOUN\n東京\tNOUN\n",
            encoding="utf-8",
        )

        async def reading(surface):
            return surface

        async def recompile():
            return "ok"

        monkeypatch.setattr(maintenance_tools, "PROJECT_ROOT", root)
        monkeypatch.setattr(maintenance_tools, "_get_reading", reading)
        monkeypatch.setattr(maintenance_tools, "_recompile_core_dic", recompile)

        result = parse(run(dict_sort(file="data/core/test.tsv", dry_run=False)))
        content = target.read_text(encoding="utf-8")
        assert result["duplicates_removed"] == 1
        assert result["duplicate_entries"] == [{"surface": "東京", "pos": "NOUN", "conj_type": ""}]
        assert content.count("東京\tNOUN") == 1
        assert content.count("東京\tPROPER_NOUN") == 1
        assert "# keep with the following entry\n東京\tPROPER_NOUN" in content

    def test_sort_rolls_back_when_recompile_fails(self, tmp_path, monkeypatch):
        root = tmp_path
        target = root / "data/core/test.tsv"
        target.parent.mkdir(parents=True)
        original = "東京\tNOUN\nりんご\tNOUN\n"
        target.write_text(original, encoding="utf-8")

        async def reading(surface):
            return surface

        async def recompile():
            return "failed"

        monkeypatch.setattr(maintenance_tools, "PROJECT_ROOT", root)
        monkeypatch.setattr(maintenance_tools, "_get_reading", reading)
        monkeypatch.setattr(maintenance_tools, "_recompile_core_dic", recompile)

        result = parse(run(dict_sort(file="data/core/test.tsv", dry_run=False)))
        assert result["status"] == "error"
        assert result["rolled_back"] is True
        assert target.read_text(encoding="utf-8") == original


class TestDictBulkAdd:
    def test_verb_conjugation_is_written_and_recompiled(self, tmp_path, monkeypatch):
        root = tmp_path
        target = root / "data/core/verbs.tsv"
        target.parent.mkdir(parents=True)
        target.write_text("# verbs\n", encoding="utf-8")

        async def recompile():
            return "ok"

        monkeypatch.setattr(bulk_tools, "PROJECT_ROOT", root)
        monkeypatch.setattr(bulk_tools, "_load_all_entries", lambda: ([], {}))
        monkeypatch.setattr(bulk_tools, "mecab_analyze", lambda word: [{"surface": word, "pos": "動詞"}])
        monkeypatch.setattr(bulk_tools, "get_suzume_rule", lambda word: None)
        monkeypatch.setattr(bulk_tools, "_recompile_core_dic", recompile)

        result = parse(run(dict_bulk_add(words="試す", pos="VERB", conj_type="GODAN_SA", force=True)))
        assert result["status"] == "ok"
        assert "試す\tVERB\tGODAN_SA" in target.read_text(encoding="utf-8")

    def test_recompile_failure_rolls_back_batch(self, tmp_path, monkeypatch):
        root = tmp_path
        target = root / "data/core/nouns.tsv"
        target.parent.mkdir(parents=True)
        original = "# nouns\n東京\tNOUN\n"
        target.write_text(original, encoding="utf-8")

        async def recompile():
            return "failed"

        monkeypatch.setattr(bulk_tools, "PROJECT_ROOT", root)
        monkeypatch.setattr(bulk_tools, "_load_all_entries", lambda: ([], {}))
        monkeypatch.setattr(bulk_tools, "mecab_analyze", lambda word: [{"surface": word, "pos": "名詞"}])
        monkeypatch.setattr(bulk_tools, "get_suzume_rule", lambda word: None)
        monkeypatch.setattr(bulk_tools, "_recompile_core_dic", recompile)

        result = parse(run(dict_bulk_add(words="りんご", pos="NOUN", force=True)))
        assert result["status"] == "error"
        assert result["rolled_back"] is True
        assert target.read_text(encoding="utf-8") == original

    def test_partial_batch_writes_valid_entries_and_reports_skips(self, tmp_path, monkeypatch):
        target = tmp_path / "data/core/nouns.tsv"
        target.parent.mkdir(parents=True)
        target.write_text("# nouns\n", encoding="utf-8")

        async def recompile():
            return "ok"

        monkeypatch.setattr(bulk_tools, "PROJECT_ROOT", tmp_path)
        monkeypatch.setattr(bulk_tools, "_load_all_entries", lambda: ([], {}))
        monkeypatch.setattr(bulk_tools, "_recompile_core_dic", recompile)

        result = parse(run(dict_bulk_add(words="りんご\n#invalid\nりんご", force=True)))
        assert result["status"] == "ok"
        assert result["total_added"] == 1
        assert result["total_skipped"] == 2
        assert target.read_text(encoding="utf-8").count("りんご\tNOUN") == 1


class TestDictBulkMove:
    def test_apply_moves_found_entries_and_reports_missing_ones(self, tmp_path, monkeypatch):
        source = tmp_path / "data/user/common.tsv"
        destination = tmp_path / "data/user/places.tsv"
        source.parent.mkdir(parents=True)
        source.write_text("# common\n東京\tPROPER_NOUN\nりんご\tNOUN\n", encoding="utf-8")
        destination.write_text("# places\n", encoding="utf-8")

        async def recompile():
            return "ok"

        monkeypatch.setattr(bulk_tools, "PROJECT_ROOT", tmp_path)
        monkeypatch.setattr(bulk_tools, "_recompile_user_dic", recompile)

        result = parse(
            run(
                dict_bulk_move(
                    words="東京\n見つからない語",
                    from_user="common",
                    to_user="places",
                    dry_run=False,
                )
            )
        )
        assert result["status"] == "ok"
        assert result["moved"] == ["東京"]
        assert result["not_found"] == ["見つからない語"]
        assert "東京\tPROPER_NOUN" not in source.read_text(encoding="utf-8")
        assert "東京\tPROPER_NOUN" in destination.read_text(encoding="utf-8")

    def test_recompile_failure_rolls_back_both_categories(self, tmp_path, monkeypatch):
        source = tmp_path / "data/user/common.tsv"
        destination = tmp_path / "data/user/places.tsv"
        source.parent.mkdir(parents=True)
        source_original = "東京\tPROPER_NOUN\n"
        destination_original = "# places\n"
        source.write_text(source_original, encoding="utf-8")
        destination.write_text(destination_original, encoding="utf-8")

        async def recompile():
            return "failed"

        monkeypatch.setattr(bulk_tools, "PROJECT_ROOT", tmp_path)
        monkeypatch.setattr(bulk_tools, "_recompile_user_dic", recompile)

        result = parse(run(dict_bulk_move(words="東京", from_user="common", to_user="places", dry_run=False)))
        assert result["status"] == "error"
        assert result["rolled_back"] is True
        assert source.read_text(encoding="utf-8") == source_original
        assert destination.read_text(encoding="utf-8") == destination_original


class TestSingleEntryMutations:
    @staticmethod
    def prepare_core(tmp_path, monkeypatch, content):
        target = tmp_path / "data/core/nouns.tsv"
        target.parent.mkdir(parents=True)
        target.write_text(content, encoding="utf-8")

        async def recompile():
            return "ok"

        monkeypatch.setattr(common_tools, "PROJECT_ROOT", tmp_path)
        monkeypatch.setattr(operation_tools, "PROJECT_ROOT", tmp_path)
        monkeypatch.setattr(operation_tools, "_recompile_core_dic", recompile)
        return target

    def test_remove_disable_and_enable_apply_real_writes(self, tmp_path, monkeypatch):
        target = self.prepare_core(tmp_path, monkeypatch, "東京\tNOUN\nりんご\tNOUN\n")

        disabled = parse(run(dict_disable("東京")))
        assert disabled["status"] == "ok"
        assert "#DISABLED# 東京\tNOUN" in target.read_text(encoding="utf-8")

        enabled = parse(run(dict_enable("東京")))
        assert enabled["status"] == "ok"
        assert "#DISABLED#" not in target.read_text(encoding="utf-8")

        removed = parse(run(dict_remove("りんご")))
        assert removed["status"] == "ok"
        assert "りんご\tNOUN" not in target.read_text(encoding="utf-8")

        assert parse(run(dict_remove("見つからない語")))["status"] == "error"
        assert parse(run(dict_disable("見つからない語")))["status"] == "error"
        assert parse(run(dict_enable("見つからない語")))["status"] == "error"

    def test_remove_rolls_back_when_recompile_fails(self, tmp_path, monkeypatch):
        original = "東京\tNOUN\n"
        target = self.prepare_core(tmp_path, monkeypatch, original)

        async def recompile():
            return "failed"

        monkeypatch.setattr(operation_tools, "_recompile_core_dic", recompile)
        result = parse(run(dict_remove("東京")))
        assert result["status"] == "error"
        assert result["rolled_back"] is True
        assert target.read_text(encoding="utf-8") == original


class TestDictValidateMutation:
    def test_fix_removes_duplicate_and_recompiles(self, tmp_path, monkeypatch):
        target = tmp_path / "data/core/nouns.tsv"
        target.parent.mkdir(parents=True)
        target.write_text("東京\tNOUN\n東京\tNOUN\nりんご\tNOUN\n", encoding="utf-8")

        async def recompile():
            return "ok"

        monkeypatch.setattr(common_tools, "PROJECT_ROOT", tmp_path)
        monkeypatch.setattr(operation_tools, "PROJECT_ROOT", tmp_path)
        monkeypatch.setattr(
            operation_tools,
            "mecab_analyze",
            lambda surface: [{"surface": surface, "pos": "名詞"}],
        )
        monkeypatch.setattr(operation_tools, "_recompile_core_dic", recompile)

        result = parse(run(dict_validate(fix=True)))
        assert result["fixed"] is True
        assert result["removed_count"] == 1
        assert target.read_text(encoding="utf-8").count("東京\tNOUN") == 1


class TestDictRemoveMatching:
    def test_apply_removes_all_matches_and_invalid_regex_errors(self, tmp_path, monkeypatch):
        target = tmp_path / "data/core/nouns.tsv"
        target.parent.mkdir(parents=True)
        target.write_text("東京\tNOUN\n東京駅\tNOUN\nりんご\tNOUN\n", encoding="utf-8")

        async def recompile():
            return "ok"

        monkeypatch.setattr(common_tools, "PROJECT_ROOT", tmp_path)
        monkeypatch.setattr(maintenance_tools, "PROJECT_ROOT", tmp_path)
        monkeypatch.setattr(maintenance_tools, "_recompile_core_dic", recompile)

        result = parse(run(dict_remove_matching("^東京", dry_run=False)))
        assert result["applied"] is True
        assert result["total"] == 2
        assert target.read_text(encoding="utf-8") == "りんご\tNOUN\n"
        assert parse(run(dict_remove_matching("[")))["status"] == "error"


class TestDictCleanup:
    def test_cleanup_dry_run(self):
        result = parse(run(dict_cleanup(input_file="data/core/verbs.tsv")))
        assert result["total"] > 0
        assert result.get("dry_run") is True

    def test_cleanup_not_found(self):
        result = parse(run(dict_cleanup(input_file="nonexistent.tsv")))
        assert result["status"] == "error"
        assert "not found" in result["message"]

    def test_cleanup_runs_cli_from_project_root(self, tmp_path, monkeypatch):
        target = tmp_path / "sample.tsv"
        target.write_text("東京公園\tNOUN\n", encoding="utf-8")
        cli = tmp_path / "suzume-cli"
        cli.touch()
        observed = {}

        def fake_run(args, **kwargs):
            observed["cwd"] = kwargs.get("cwd")
            return subprocess.CompletedProcess(args, 0, "東京公園\tNOUN\nEOS\n", "")

        monkeypatch.setattr(maintenance_tools, "PROJECT_ROOT", tmp_path)
        monkeypatch.setattr(maintenance_tools, "get_cli_path", lambda: cli)
        monkeypatch.setattr(subprocess, "run", fake_run)

        result = parse(run(dict_cleanup(input_file="sample.tsv")))
        assert result["dry_run"] is True
        assert observed["cwd"] == tmp_path

    def test_cleanup_reports_nonzero_cli_exit(self, tmp_path, monkeypatch):
        target = tmp_path / "sample.tsv"
        target.write_text("東京公園\tNOUN\n", encoding="utf-8")
        cli = tmp_path / "suzume-cli"
        cli.touch()

        def fake_run(args, **kwargs):
            return subprocess.CompletedProcess(args, 2, "", "dictionary missing")

        monkeypatch.setattr(maintenance_tools, "PROJECT_ROOT", tmp_path)
        monkeypatch.setattr(maintenance_tools, "get_cli_path", lambda: cli)
        monkeypatch.setattr(subprocess, "run", fake_run)

        result = parse(run(dict_cleanup(input_file="sample.tsv")))
        assert result["status"] == "error"
        assert "exited with 2" in result["message"]


def test_multi_file_mutation_rolls_back_all_sources(tmp_path):
    first = tmp_path / "first.tsv"
    second = tmp_path / "second.tsv"
    first.write_text("first\n", encoding="utf-8")

    async def recompile():
        return "failed"

    status, error = run(
        _write_files_and_recompile(
            {first: "changed\n", second: "created\n"},
            recompile,
        )
    )
    assert status == "failed"
    assert error is not None
    assert first.read_text(encoding="utf-8") == "first\n"
    assert not second.exists()


class TestDictGrep:
    def test_grep_pattern(self):
        result = parse(run(dict_grep("^Wi")))
        assert result["total"] >= 0
        assert "matches" in result

    def test_grep_no_match(self):
        result = parse(run(dict_grep("^zzzznonexistent")))
        assert result["total"] == 0
        assert result["matches"] == []
