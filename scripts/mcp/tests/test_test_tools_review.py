"""Focused tests for safe oracle override review tools."""

import asyncio
import importlib
import json

from suzume_mcp.tools import test_tools as _test_tools  # noqa: F401

review = importlib.import_module("suzume_mcp.tools._test_tools_review")
read_tools = importlib.import_module("suzume_mcp.tools._test_tools_read")


def run(coro):
    return asyncio.run(coro)


def parse(result: str) -> dict:
    return json.loads(result)


def token(surface: str) -> dict:
    return {"surface": surface, "pos": "Noun", "lemma": surface}


def write_suite(root, name: str, cases: list[dict]) -> None:
    path = root / "tests" / "data" / "tokenization" / f"{name}.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"cases": cases}, ensure_ascii=False), encoding="utf-8")


def make_case(case_id: str, input_text: str, **extra) -> dict:
    case = {"id": case_id, "input": input_text, "expected": [token("正解")]}
    case.update(extra)
    return case


def oracle_results(texts: list[str]) -> list[tuple[list[dict], str, str]]:
    return [([token("正解")], "MeCab", "") for _text in texts]


def test_reset_refuses_ambiguous_input(tmp_path, monkeypatch):
    write_suite(tmp_path, "first", [make_case("one", "重複", accepted_diff={"reason": "x"})])
    write_suite(tmp_path, "second", [make_case("two", "重複", accepted_diff={"reason": "x"})])
    monkeypatch.setattr(review, "PROJECT_ROOT", tmp_path)

    data = parse(run(review.test_reset_suzume(input_text="重複")))

    assert data["status"] == "error"
    assert "multiple tests" in data["message"]
    assert "first/one" in data["message"]
    assert "second/two" in data["message"]


def test_reset_by_id_only_removes_oracle_matches(tmp_path, monkeypatch):
    case = make_case(
        "safe",
        "対象",
        suzume_expected=[token("旧値")],
        accepted_diff={"reason": "old", "category": "tokenizer-difference"},
    )
    write_suite(tmp_path, "sample", [case])
    monkeypatch.setattr(review, "PROJECT_ROOT", tmp_path)
    monkeypatch.setattr(review, "get_expected_tokens_batch_subprocess", oracle_results)
    monkeypatch.setattr(review, "_get_suzume_tokens", lambda _text: [token("正解")])

    data = parse(run(review.test_reset_suzume(test_id="sample/safe", apply=True)))

    assert data["total"] == 1
    assert data["skipped"] == []
    stored = json.loads((tmp_path / "tests/data/tokenization/sample.json").read_text(encoding="utf-8"))
    assert "suzume_expected" not in stored["cases"][0]
    assert "accepted_diff" not in stored["cases"][0]


def test_reset_skips_active_override(tmp_path, monkeypatch):
    case = make_case(
        "active",
        "対象",
        suzume_expected=[token("現状")],
        accepted_diff={"reason": "known limitation", "category": "pos-limitation"},
    )
    write_suite(tmp_path, "sample", [case])
    monkeypatch.setattr(review, "PROJECT_ROOT", tmp_path)
    monkeypatch.setattr(review, "get_expected_tokens_batch_subprocess", oracle_results)
    monkeypatch.setattr(review, "_get_suzume_tokens", lambda _text: [token("現状")])

    data = parse(run(review.test_reset_suzume(test_id="sample/active", apply=True)))

    assert data["total"] == 0
    assert data["skipped"][0]["suzume_matches_oracle"] is False
    stored = json.loads((tmp_path / "tests/data/tokenization/sample.json").read_text(encoding="utf-8"))
    assert "suzume_expected" in stored["cases"][0]


def test_accept_diff_refreshes_existing_override(tmp_path, monkeypatch):
    case = make_case(
        "active",
        "対象",
        suzume_expected=[token("旧値")],
        accepted_diff={"reason": "old", "category": "old-category"},
    )
    write_suite(tmp_path, "sample", [case])
    monkeypatch.setattr(review, "PROJECT_ROOT", tmp_path)
    monkeypatch.setattr(review, "_get_suzume_tokens", lambda _text: [token("新値")])

    data = parse(
        run(
            review.test_accept_diff(
                test_id="sample/active",
                reason="updated",
                category="grammar-boundary",
                apply=True,
            )
        )
    )

    assert data["status"] == "ok"
    stored = json.loads((tmp_path / "tests/data/tokenization/sample.json").read_text(encoding="utf-8"))
    assert stored["cases"][0]["suzume_expected"] == [token("新値")]
    assert stored["cases"][0]["accepted_diff"] == {
        "reason": "updated",
        "category": "grammar-boundary",
    }


def test_audit_classifies_override_integrity(tmp_path, monkeypatch):
    cases = [
        make_case(
            "safe",
            "安全",
            suzume_expected=[token("旧値")],
            accepted_diff={"reason": "old", "category": "tokenizer-difference"},
        ),
        make_case(
            "active",
            "有効",
            suzume_expected=[token("現状")],
            accepted_diff={"reason": "limitation", "category": "pos-limitation"},
        ),
        make_case("orphan", "孤立", accepted_diff={"reason": "old", "category": "pos-limitation"}),
        make_case("missing_reason", "理由なし", suzume_expected=[token("現状")]),
    ]
    write_suite(tmp_path, "sample", cases)
    monkeypatch.setattr(review, "PROJECT_ROOT", tmp_path)
    test_file = tmp_path / "tests/data/tokenization/sample.json"
    monkeypatch.setattr(review, "_get_test_files_filtered", lambda _file: [test_file])
    monkeypatch.setattr(review, "get_expected_tokens_batch_subprocess", oracle_results)
    current = {
        "安全": [token("正解")],
        "有効": [token("現状")],
        "孤立": [token("正解")],
        "理由なし": [token("現状")],
    }
    monkeypatch.setattr(review, "_get_suzume_tokens", lambda text: current[text])

    data = parse(run(review.test_audit_oracle_overrides(limit=10)))

    assert data["summary"]["total"] == 4
    assert data["summary"]["safe_to_reset"] == 1
    assert data["summary"]["active_override"] == 1
    assert data["summary"]["orphan_accepted_diff"] == 1
    assert data["summary"]["override_without_accepted_diff"] == 1
    assert {entry["id"] for entry in data["entries"]} == {
        "sample/safe",
        "sample/active",
        "sample/orphan",
        "sample/missing_reason",
    }


def test_partial_oracle_sync_updates_only_selected_id(tmp_path, monkeypatch):
    cases = [make_case("first", "一つ"), make_case("second", "二つ")]
    write_suite(tmp_path, "sample", cases)
    test_file = tmp_path / "tests/data/tokenization/sample.json"
    monkeypatch.setattr(read_tools, "_get_test_files_filtered", lambda _file: [test_file])
    monkeypatch.setattr(
        read_tools,
        "get_expected_tokens_batch_subprocess",
        lambda texts: [([token("新値")], "MeCab+SuzumeRules", "test-rule") for _text in texts],
    )

    data = parse(run(read_tools.test_needs_suzume_update(test_ids=["sample/second"], apply=True)))

    assert data["total"] == 1
    assert data["needs_update"][0]["id"] == "sample/second"
    stored = json.loads(test_file.read_text(encoding="utf-8"))["cases"]
    assert stored[0]["expected"] == [token("正解")]
    assert stored[1]["expected"] == [token("新値")]
