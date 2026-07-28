"""Tests for native Suzume CLI discovery."""

import io
import json
import os
from pathlib import Path

import pytest

from suzume_mcp import __main__ as normalization_cli
from suzume_mcp.core import suzume_cli
from suzume_mcp.core.suzume_cli import get_cli_path

CLI_NAME = "suzume-cli.exe" if os.name == "nt" else "suzume-cli"


@pytest.fixture(autouse=True)
def clear_cli_override(monkeypatch: pytest.MonkeyPatch) -> None:
    """Keep each discovery test independent of the developer environment."""
    monkeypatch.delenv("SUZUME_CLI_PATH", raising=False)


def _touch_cli(path: Path) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.touch()
    return path


def test_cli_path_env_override_is_authoritative(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    override = tmp_path / "custom" / "suzume-cli"
    monkeypatch.setenv("SUZUME_CLI_PATH", str(override))

    assert get_cli_path(tmp_path) == override


def test_cli_path_relative_override_uses_project_root(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("SUZUME_CLI_PATH", "artifacts/suzume-cli")

    assert get_cli_path(tmp_path) == tmp_path / "artifacts" / "suzume-cli"


def test_cli_path_prefers_flat_build(tmp_path: Path) -> None:
    flat_cli = _touch_cli(tmp_path / "build" / "bin" / CLI_NAME)
    _touch_cli(tmp_path / "build" / "bin" / "Debug" / CLI_NAME)

    assert get_cli_path(tmp_path) == flat_cli


@pytest.mark.parametrize("configuration", ["Debug", "Release", "RelWithDebInfo", "MinSizeRel"])
def test_cli_path_finds_multi_config_build(tmp_path: Path, configuration: str) -> None:
    configured_cli = _touch_cli(tmp_path / "build" / "bin" / configuration / CLI_NAME)

    assert get_cli_path(tmp_path) == configured_cli


def test_cli_path_supports_alternate_multi_config_layout(tmp_path: Path) -> None:
    configured_cli = _touch_cli(tmp_path / "build" / "Release" / "bin" / CLI_NAME)

    assert get_cli_path(tmp_path) == configured_cli


def test_cli_path_returns_flat_candidate_when_unbuilt(tmp_path: Path) -> None:
    assert get_cli_path(tmp_path) == tmp_path / "build" / "bin" / CLI_NAME


def test_normalization_batch_preserves_per_item_errors(monkeypatch: pytest.MonkeyPatch, capsys) -> None:
    monkeypatch.setattr("sys.stdin", io.StringIO(json.dumps([1, None])))

    normalization_cli.cmd_normalize(["--batch"])

    results = json.loads(capsys.readouterr().out)
    assert len(results) == 2
    assert "Expected text string" in results[0]["error"]
    assert "Expected text string" in results[1]["error"]


def test_batch_wrapper_keeps_successes_around_an_error(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(
        suzume_cli,
        "_run_normalize_cli",
        lambda _texts, **_kwargs: [
            {"tokens": [{"surface": "東京", "pos": "Noun"}], "source": "MeCab", "rule": ""},
            {"error": "bad input"},
        ],
    )

    results = suzume_cli.get_expected_tokens_batch_subprocess(["東京", "失敗"])

    assert results[0][0][0]["surface"] == "東京"
    assert results[1] == ([], "error", "normalization failed for '失敗': bad input")


def test_mecab_batch_wrapper_selects_raw_mode(monkeypatch: pytest.MonkeyPatch) -> None:
    calls = []

    def fake_run(texts, *, raw_mecab=False):
        calls.append((texts, raw_mecab))
        return [{"tokens": [], "source": "MeCab", "rule": "intentional-rule"}]

    monkeypatch.setattr(suzume_cli, "_run_normalize_cli", fake_run)

    assert suzume_cli.get_mecab_tokens_batch_subprocess(["東京"]) == [([], "MeCab", "intentional-rule")]
    assert calls == [(["東京"], True)]
