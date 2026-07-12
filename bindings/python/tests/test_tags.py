"""Tag-generation behavior of the Python binding."""

from __future__ import annotations

from suzume import Suzume, Tag


def test_generate_tags_returns_tags() -> None:
    with Suzume() as sz:
        tags = sz.generate_tags("東京都に住んでいます")
    assert all(isinstance(t, Tag) for t in tags)
    assert all(t.tag for t in tags)


def test_min_length_filters_short_tags() -> None:
    with Suzume() as sz:
        tags = sz.generate_tags("東京都に住む", min_length=2)
    assert all(len(t.tag) >= 2 for t in tags)


def test_max_tags_limits_count() -> None:
    with Suzume() as sz:
        tags = sz.generate_tags("東京都に住んでいます", max_tags=1)
    assert len(tags) <= 1


def test_remove_duplicates() -> None:
    with Suzume() as sz:
        tags = sz.generate_tags("東京東京東京", remove_duplicates=True)
    texts = [t.tag for t in tags]
    assert len(texts) == len(set(texts))


def test_pos_filter_accepts_names() -> None:
    with Suzume() as sz:
        by_names = sz.generate_tags("美味しいラーメンを食べた", pos_filter=["noun"])
        by_bitmask = sz.generate_tags("美味しいラーメンを食べた", pos_filter=1)
    assert [(t.tag, t.pos) for t in by_names] == [(t.tag, t.pos) for t in by_bitmask]
    assert all(t.pos == "NOUN" for t in by_names)


def test_pos_filter_rejects_unknown_name() -> None:
    with Suzume() as sz:
        try:
            sz.generate_tags("東京", pos_filter=["bogus"])
        except ValueError:
            pass
        else:  # pragma: no cover
            raise AssertionError("expected ValueError for unknown POS name")
