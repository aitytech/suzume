"""Analysis behavior of the Python binding."""

from __future__ import annotations

import suzume
from suzume import ErrorCode, Mode, Morpheme, Suzume, SuzumeError
from suzume._labels import (
    EXTENDED_POS,
    conjugation_form,
    conjugation_type,
    extended_pos,
)


def test_extended_pos_labels_match_the_serialized_range() -> None:
    assert len(EXTENDED_POS) == 83
    assert extended_pos(32) == "AUX_開始"
    assert extended_pos(73) == "AUX_文語完了"
    assert extended_pos(77) == "SUFFIX_直後"
    assert extended_pos(80) == "AUX_よう"
    assert extended_pos(81) == "AUX_KURUWA_POLITE"
    assert extended_pos(82) == "AUX_文語過去キ"
    # One past the last label: a code the C ABI has but the table does not must
    # degrade to UNKNOWN rather than shift every later label by one.
    assert extended_pos(83) == "UNKNOWN"


def test_conjugation_labels_match_the_serialized_range() -> None:
    assert conjugation_type(14) == "ナ形容詞"
    assert conjugation_type(15) == "感動詞"
    assert conjugation_type(16) == "固有名詞・姓"
    assert conjugation_type(17) == "固有名詞・名"
    assert conjugation_type(18) is None

    assert conjugation_form(6) == "意志形"
    assert conjugation_form(7) is None


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


def test_conjugation_fields_are_none_for_non_conjugating_words() -> None:
    with Suzume() as sz:
        result = sz.analyze("東京都に住む")
    for m in result:
        assert m.conj_type is None or isinstance(m.conj_type, str)
        assert m.conj_form is None or isinstance(m.conj_form, str)
    # A particle such as に does not conjugate, so its fields are None (not "").
    particles = [m for m in result if m.surface == "に"]
    assert particles
    assert all(p.conj_type is None and p.conj_form is None for p in particles)


def test_empty_string_yields_no_morphemes() -> None:
    with Suzume() as sz:
        assert sz.analyze("") == []


def test_length_aware_analysis_preserves_embedded_null_and_normalized_text() -> None:
    with Suzume() as sz:
        result = sz.analyze_with_normalized_text("東京\0大阪")
        tags = sz.generate_tags("東京\0大阪", min_length=1)
    assert "大阪" in result.normalized_text
    assert any(m.surface == "大阪" for m in result.morphemes)
    assert any(tag.tag == "大阪" for tag in tags)


def test_invalid_unicode_reports_stable_error_code() -> None:
    with Suzume() as sz:
        try:
            sz.analyze("\ud800")
        except SuzumeError as error:
            assert error.code is ErrorCode.INVALID_UTF8
        else:  # pragma: no cover
            raise AssertionError("expected invalid UTF-8 error")


def test_extended_options_and_clear_are_public() -> None:
    with Suzume(
        skip_user_dictionary=True,
        skip_core_dictionary=True,
        scorer_options={"unary": {"noun_prior": 0.25}},
    ) as sz:
        assert sz.dictionary_warnings == []
        sz.clear_user_dictionaries()


def test_invalid_scorer_json_reports_parse_error() -> None:
    try:
        Suzume(scorer_options="{")
    except SuzumeError as error:
        assert error.code is ErrorCode.PARSE
    else:  # pragma: no cover
        raise AssertionError("expected scorer parse error")


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
