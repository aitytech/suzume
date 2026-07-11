"""Analysis behavior of the Python binding."""

from __future__ import annotations

import suzume
from suzume import Mode, Morpheme, Suzume, SuzumeError


def test_version_is_nonempty() -> None:
    assert suzume.version()


def test_analyze_returns_morphemes() -> None:
    with Suzume() as sz:
        result = sz.analyze("東京都に住む")
    assert result
    assert all(isinstance(m, Morpheme) for m in result)
    surfaces = [m.surface for m in result]
    assert "".join(surfaces)  # non-empty reconstruction
    # Particle に should be tagged as a particle somewhere in the stream.
    assert any(m.surface == "に" for m in result)


def test_offsets_are_ordered_and_within_text() -> None:
    with Suzume() as sz:
        result = sz.analyze("東京都に住む")
    for m in result:
        assert 0 <= m.start <= m.end
    for prev, cur in zip(result, result[1:], strict=False):
        assert cur.start >= prev.start


def test_empty_string_yields_no_morphemes() -> None:
    with Suzume() as sz:
        assert sz.analyze("") == []


def test_search_mode_constructs() -> None:
    with Suzume(mode=Mode.SEARCH) as sz:
        assert sz.analyze("東京都に住む")


def test_string_mode_alias() -> None:
    with Suzume(mode="split") as sz:
        assert sz.analyze("東京都に住む")


def test_use_after_close_raises() -> None:
    sz = Suzume()
    sz.close()
    try:
        sz.analyze("東京")
    except SuzumeError:
        pass
    else:  # pragma: no cover
        raise AssertionError("expected SuzumeError after close")


def test_close_is_idempotent() -> None:
    sz = Suzume()
    sz.close()
    sz.close()
