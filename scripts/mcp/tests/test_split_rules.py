"""Tests for split rules."""

from suzume_mcp.core.split_rules import apply_suzume_split


def _tok(surface, pos="名詞", **kw):
    t = {"surface": surface, "pos": pos, "lemma": surface}
    t.update(kw)
    return t


class TestFixedLeadingSearchUnit:
    def test_splits_closed_unit_from_following_noun(self):
        cases = [
            ("わが国", "わが", "連体詞", "国"),
            ("ひととおり目", "ひととおり", "副詞", "目"),
        ]

        for surface, leading, pos, noun in cases:
            result, rule = apply_suzume_split([_tok(surface)])
            assert result == [
                {"surface": leading, "pos": pos, "lemma": leading},
                {"surface": noun, "pos": "名詞", "lemma": noun},
            ]
            assert rule == "fixed-leading-search-unit"

    def test_keeps_standalone_closed_unit(self):
        result, rule = apply_suzume_split([_tok("わが", pos="連体詞")])
        assert result == [_tok("わが", pos="連体詞")]
        assert rule is None


class TestPluralRaSplit:
    def test_karera(self):
        tokens = [_tok("彼ら")]
        result, rule = apply_suzume_split(tokens)
        assert len(result) == 2
        assert result[0]["surface"] == "彼"
        assert result[1]["surface"] == "ら"
        assert rule == "ra-suffix-split"


class TestTtaraSplit:
    def test_anata_ttara(self):
        tokens = [_tok("あなたったら")]
        result, rule = apply_suzume_split(tokens)
        assert len(result) == 2
        assert result[0]["surface"] == "あなた"
        assert result[1]["surface"] == "ったら"


class TestNetaiSplit:
    def test_netai(self):
        tokens = [_tok("ねたい", pos="形容詞")]
        result, rule = apply_suzume_split(tokens)
        assert len(result) == 2
        assert result[0]["surface"] == "ね"
        assert result[0]["pos"] == "動詞"
        assert result[1]["surface"] == "たい"


class TestPrefectureCitySplit:
    def test_kanagawa_yokohama(self):
        tokens = [_tok("神奈川県横浜市")]
        result, rule = apply_suzume_split(tokens)
        assert len(result) == 2
        assert result[0]["surface"] == "神奈川県"
        assert result[1]["surface"] == "横浜市"


class TestKanjiKatakanaSplit:
    def test_split(self):
        tokens = [_tok("二次サンプル", pos="名詞")]
        result, rule = apply_suzume_split(tokens)
        assert len(result) == 2
        assert result[0]["surface"] == "二次"
        assert result[1]["surface"] == "サンプル"

    def test_skip_user_dict(self):
        tokens = [_tok("東京テスト", pos="名詞")]
        result, _ = apply_suzume_split(tokens)
        assert len(result) == 1
        assert result[0]["surface"] == "東京テスト"


class TestCopulaNegationSplit:
    def test_janai(self):
        tokens = [_tok("じゃない", pos="助動詞")]
        result, rule = apply_suzume_split(tokens)
        assert len(result) == 2
        assert result[0]["surface"] == "じゃ"
        assert result[1]["surface"] == "ない"
        assert rule == "copula-negation-split"


class TestNoSplit:
    def test_godan_sa_mizenkei_before_passive_is_preserved(self):
        tokens = [
            _tok("果たさ", pos="動詞", lemma="果たす", conj_form="未然形"),
            _tok("れ", pos="動詞", lemma="れる", pos_sub1="接尾"),
        ]
        result, rule = apply_suzume_split(tokens)
        assert [token["surface"] for token in result] == ["果たさ", "れ"]
        assert rule is None

    def test_passthrough(self):
        tokens = [_tok("食べ", pos="動詞"), _tok("た", pos="助動詞")]
        result, rule = apply_suzume_split(tokens)
        assert len(result) == 2
        assert rule is None
