"""Tag-generation behavior of the Python binding."""

from __future__ import annotations

from suzume import Suzume, Tag


def test_generate_tags_returns_tags() -> None:
    with Suzume() as sz:
        tags = sz.generate_tags("東京都に住んでいます")
    assert all(isinstance(t, Tag) for t in tags)
    assert all(t.text for t in tags)


def test_min_length_filters_short_tags() -> None:
    with Suzume() as sz:
        tags = sz.generate_tags("東京都に住む", min_length=2)
    assert all(len(t.text) >= 2 for t in tags)


def test_max_tags_limits_count() -> None:
    with Suzume() as sz:
        tags = sz.generate_tags("東京都に住んでいます", max_tags=1)
    assert len(tags) <= 1


def test_remove_duplicates() -> None:
    with Suzume() as sz:
        tags = sz.generate_tags("東京東京東京", remove_duplicates=True)
    texts = [t.text for t in tags]
    assert len(texts) == len(set(texts))
