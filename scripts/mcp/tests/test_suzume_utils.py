"""Integration tests for get_expected_tokens (requires MeCab)."""

import shutil

import pytest

from suzume_mcp.core.suzume_utils import format_expected, get_expected_tokens, tokens_match

pytestmark = pytest.mark.skipif(
    shutil.which("mecab") is None,
    reason="MeCab not installed",
)


class TestGetExpectedTokens:
    """Test get_expected_tokens with representative inputs."""

    def test_simple_sentence(self):
        tokens, source, rule = get_expected_tokens("食べる")
        surfaces = [t["surface"] for t in tokens]
        assert "食べる" in surfaces

    def test_number_counter(self):
        tokens, source, rule = get_expected_tokens("3人")
        assert len(tokens) == 1
        assert tokens[0]["surface"] == "3人"

    def test_kanji_compound(self):
        tokens, source, rule = get_expected_tokens("経済成長")
        assert any(t["surface"] == "経済成長" for t in tokens)

    def test_date(self):
        tokens, source, rule = get_expected_tokens("2024年12月23日")
        assert any(t["surface"] == "2024年12月23日" for t in tokens)

    def test_slang_adjective(self):
        tokens, source, rule = get_expected_tokens("エモい")
        surfaces = [t["surface"] for t in tokens]
        assert "エモい" in surfaces or "エモ" in surfaces

    def test_janai_split(self):
        tokens, source, rule = get_expected_tokens("嫌じゃない")
        surfaces = [t["surface"] for t in tokens]
        assert "じゃ" in surfaces
        assert "ない" in surfaces

    def test_symbols_filtered(self):
        tokens, source, rule = get_expected_tokens("（テスト）")
        surfaces = [t["surface"] for t in tokens]
        assert "（" not in surfaces
        assert "）" not in surfaces

    def test_fullwidth_normalized(self):
        tokens, source, rule = get_expected_tokens("１２３")
        for t in tokens:
            s = t["surface"]
            assert "１" not in s  # Should be half-width


class TestSurfaceIsNeverLost:
    """The expected tokens must cover every non-punctuation character.

    MeCab labels anything outside IPADIC as 記号, and the symbol filter drops
    記号 tokens, so an unknown character can silently vanish and leave a test
    asserting a segmentation of text that is not the input.
    """

    def test_supplementary_plane_kanji_survives(self):
        tokens, _, _ = get_expected_tokens("𩸽を焼く")
        assert [t["surface"] for t in tokens] == ["𩸽", "を", "焼く"]

    def test_fullwidth_letter_survives(self):
        tokens, _, _ = get_expected_tokens("Ａさんに聞く")
        assert "A" in [t["surface"] for t in tokens]

    def test_punctuation_is_still_dropped(self):
        tokens, _, _ = get_expected_tokens("東京。")
        assert [t["surface"] for t in tokens] == ["東京"]

    def test_dropping_real_text_raises_instead_of_losing_it(self):
        # Hangul is outside IPADIC and has no correct_mecab_pos rule, so the
        # guard has to fail rather than quietly produce a lossy oracle.
        with pytest.raises(RuntimeError, match="symbol filter would drop"):
            get_expected_tokens("한국を見る")


class TestTokensMatch:
    def test_match(self):
        a = [{"surface": "食べ", "pos": "Verb"}, {"surface": "た", "pos": "Auxiliary"}]
        b = [{"surface": "食べ", "pos": "Verb"}, {"surface": "た", "pos": "Auxiliary"}]
        assert tokens_match(a, b)

    def test_pos_normalization(self):
        a = [{"surface": "食べ", "pos": "VERB"}]
        b = [{"surface": "食べ", "pos": "Verb"}]
        assert tokens_match(a, b)

    def test_mismatch_surface(self):
        a = [{"surface": "食べ", "pos": "Verb"}]
        b = [{"surface": "食", "pos": "Verb"}]
        assert not tokens_match(a, b)

    def test_mismatch_length(self):
        a = [{"surface": "食べ", "pos": "Verb"}]
        b = [{"surface": "食べ", "pos": "Verb"}, {"surface": "た", "pos": "Auxiliary"}]
        assert not tokens_match(a, b)


class TestFormatExpected:
    def test_basic(self):
        tokens = [{"surface": "食べ", "pos": "Verb", "lemma": "食べる"}]
        result = format_expected(tokens)
        assert result[0]["surface"] == "食べ"
        assert result[0]["pos"] == "Verb"
        assert result[0]["lemma"] == "食べる"

    def test_lemma_included_when_same(self):
        """Lemma is always included, even when same as surface."""
        tokens = [{"surface": "食べる", "pos": "Verb", "lemma": "食べる"}]
        result = format_expected(tokens)
        assert result[0]["lemma"] == "食べる"
