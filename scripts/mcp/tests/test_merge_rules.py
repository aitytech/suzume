"""Tests for merge rules - individual pattern tests using mock token lists."""

from suzume_mcp.core.merge_rules import apply_suzume_merge


def _token(surface: str, pos: str) -> dict:
    return {"surface": surface, "pos": pos, "lemma": surface}


class TestFixedFunctionSearchUnits:
    def test_merges_closed_words_split_by_reference_analyzer(self):
        cases = [
            ("然程", [_token("然", "副詞"), _token("程", "名詞")], "副詞"),
            ("更なる", [_token("更", "名詞"), _token("なる", "動詞")], "連体詞"),
            ("どのみち", [_token("どの", "連体詞"), _token("みち", "名詞")], "副詞"),
            ("ふいに", [_token("ふい", "動詞"), _token("に", "助詞")], "副詞"),
            ("ほどなく", [_token("ほど", "助詞"), _token("なく", "形容詞")], "副詞"),
            ("そんなら", [_token("そん", "動詞"), _token("なら", "助動詞")], "接続詞"),
            ("ありさま", [_token("あり", "動詞"), _token("さま", "名詞")], "名詞"),
            ("おそれ", [_token("お", "接頭詞"), _token("それ", "代名詞")], "名詞"),
            ("おかげ", [_token("お", "接頭詞"), _token("かげ", "名詞")], "名詞"),
            ("おのれ", [_token("お", "接頭詞"), _token("のれ", "名詞")], "代名詞"),
            ("だけ", [_token("だ", "助動詞"), _token("け", "助詞")], "助詞"),
            ("だに", [_token("だ", "助動詞"), _token("に", "助詞")], "助詞"),
            ("がてら", [_token("が", "助詞"), _token("てら", "動詞")], "助詞"),
        ]

        for text, tokens, pos in cases:
            merged, rule = apply_suzume_merge(tokens, text)
            assert merged == [{"surface": text, "pos": pos, "lemma": text}]
            assert rule == "fixed-function-search-unit"

    def test_does_not_absorb_a_longer_token(self):
        tokens = [_token("おそれる", "動詞")]
        merged, rule = apply_suzume_merge(tokens, "おそれる")
        assert [token["surface"] for token in merged] == ["おそれる"]
        assert rule is None


class TestFixedInflectedFunctionUnits:
    def test_merges_split_humble_potential_before_polite_auxiliary(self):
        tokens = [
            _token("い", "動詞"),
            _token("た", "助動詞"),
            _token("だけ", "助詞"),
            _token("ませ", "助動詞"),
            _token("ん", "助動詞"),
        ]
        merged, rule = apply_suzume_merge(tokens, "いただけません")
        assert merged[0] == {"surface": "いただけ", "pos": "動詞", "lemma": "いただける"}
        assert [token["surface"] for token in merged] == ["いただけ", "ませ", "ん"]
        assert rule == "fixed-inflected-function-unit"

    def test_does_not_consume_prefix_of_finite_form(self):
        tokens = [_token("いただける", "動詞")]
        merged, rule = apply_suzume_merge(tokens, "いただける")
        assert [token["surface"] for token in merged] == ["いただける"]
        assert rule is None

    def test_preceding_renyokei_does_not_absorb_first_split_piece(self):
        tokens = [
            _tok("読み", pos="動詞", conj_form="連用形", lemma="読む"),
            _tok("い", pos="動詞", lemma="いる"),
            _tok("た", pos="助動詞"),
            _tok("だけ", pos="助詞"),
            _tok("ませ", pos="助動詞"),
            _tok("ん", pos="助動詞"),
        ]
        merged, rule = apply_suzume_merge(tokens, "読みいただけません")
        assert [token["surface"] for token in merged] == ["読み", "いただけ", "ませ", "ん"]
        assert merged[1]["lemma"] == "いただける"
        assert rule == "fixed-inflected-function-unit"


def _tok(surface, pos="名詞", **kw):
    """Helper to create a token dict."""
    t = {"surface": surface, "pos": pos, "lemma": surface}
    t.update(kw)
    return t


class TestDateMerge:
    def test_full_date(self):
        tokens = [_tok("2024"), _tok("年"), _tok("12"), _tok("月"), _tok("23"), _tok("日")]
        text = "2024年12月23日"
        result, rule = apply_suzume_merge(tokens, text)
        assert len(result) == 1
        assert result[0]["surface"] == "2024年12月23日"
        assert rule == "date"


class TestNumberUnit:
    def test_number_counter(self):
        tokens = [
            _tok("3", pos="名詞", pos_sub1="数"),
            _tok("人", pos="名詞", pos_sub1="接尾", pos_sub2="助数詞"),
        ]
        text = "3人"
        result, rule = apply_suzume_merge(tokens, text)
        assert len(result) == 1
        assert result[0]["surface"] == "3人"

    def test_large_number(self):
        tokens = [
            _tok("100", pos="名詞", pos_sub1="数"),
            _tok("万", pos="名詞", pos_sub1="数"),
            _tok("円", pos="名詞", pos_sub1="接尾", pos_sub2="助数詞"),
        ]
        text = "100万円"
        result, rule = apply_suzume_merge(tokens, text)
        assert len(result) == 1
        assert result[0]["surface"] == "100万円"

    def test_kana_counter_from_arbitrary_mecab_split(self):
        tokens = [_tok("い", pos="動詞"), _tok("ちまい", pos="動詞")]
        result, rule = apply_suzume_merge(tokens, "いちまい")
        assert result == [{"surface": "いちまい", "pos": "名詞", "pos_sub1": "数", "lemma": "いちまい"}]
        assert rule == "kana-number+unit"

    def test_kana_counter_from_syllable_tokens(self):
        tokens = [_tok("よ", pos="形容詞"), _tok("ん", pos="助詞"), _tok("に", pos="助詞"), _tok("ん", pos="助詞")]
        result, rule = apply_suzume_merge(tokens, "よんにん")
        assert result == [{"surface": "よんにん", "pos": "名詞", "pos_sub1": "数", "lemma": "よんにん"}]
        assert rule == "kana-number+unit"

    def test_native_numeral_with_kanji_counter(self):
        tokens = [_tok("ふた", pos="名詞", pos_sub1="数"), _tok("月", pos="名詞", pos_sub1="接尾")]
        result, rule = apply_suzume_merge(tokens, "ふた月")
        assert result == [{"surface": "ふた月", "pos": "名詞", "pos_sub1": "数", "lemma": "ふた月"}]
        assert rule == "kana-number+unit"


class TestKanjiCompound:
    def test_two_kanji(self):
        tokens = [_tok("経済", pos="名詞"), _tok("成長", pos="名詞")]
        text = "経済成長"
        result, rule = apply_suzume_merge(tokens, text)
        assert len(result) == 1
        assert result[0]["surface"] == "経済成長"

    def test_merge_productive_suffix(self):
        """Productive suffix tokens merge into one search unit."""
        tokens = [_tok("経済", pos="名詞"), _tok("的", pos="名詞", pos_sub1="接尾")]
        text = "経済的"
        result, _ = apply_suzume_merge(tokens, text)
        assert len(result) == 1
        assert result[0]["surface"] == "経済的"

    def test_skip_honorific_suffix(self):
        """Honorific suffix tokens should not merge."""
        tokens = [_tok("田中", pos="名詞"), _tok("様", pos="名詞", pos_sub1="接尾")]
        text = "田中様"
        result, _ = apply_suzume_merge(tokens, text)
        assert len(result) == 2

    def test_merges_productive_role_suffix_independent_of_dictionary_coverage(self):
        tokens = [_tok("部門", pos="名詞"), _tok("長", pos="名詞", pos_sub1="接尾")]
        result, rule = apply_suzume_merge(tokens, "部門長")
        assert result == [{"surface": "部門長", "pos": "名詞", "lemma": "部門長"}]
        assert rule == "noun+suffix"


class TestDemoAdverbialParticle:
    def test_merges_split_demo_independent_of_following_predicate(self):
        for predicate in ("よい", "構わない"):
            tokens = [
                _tok("方法", pos="名詞"),
                _tok("で", pos="助詞", pos_sub1="格助詞"),
                _tok("も", pos="助詞", pos_sub1="係助詞"),
                _tok(predicate, pos="形容詞"),
            ]
            result, _ = apply_suzume_merge(tokens, f"方法でも{predicate}")
            assert [token["surface"] for token in result] == ["方法", "でも", predicate]

    def test_keeps_existing_demo_independent_of_following_predicate(self):
        tokens = [
            _tok("何", pos="名詞"),
            _tok("でも", pos="助詞", pos_sub1="副助詞"),
            _tok("よい", pos="形容詞"),
        ]
        result, _ = apply_suzume_merge(tokens, "何でもよい")
        assert [token["surface"] for token in result] == ["何", "でも", "よい"]

    def test_keeps_na_adjective_copula_and_focus_particle_separate(self):
        tokens = [
            _tok("特別", pos="名詞", pos_sub1="形容動詞語幹"),
            _tok("で", pos="助詞", pos_sub1="格助詞"),
            _tok("も", pos="助詞", pos_sub1="係助詞"),
            _tok("ない", pos="形容詞"),
        ]
        result, _ = apply_suzume_merge(tokens, "特別でもない")
        assert [token["surface"] for token in result] == ["特別", "で", "も", "ない"]

    def test_merges_demo_after_quotative_particle(self):
        tokens = [
            _tok("確認", pos="名詞"),
            _tok("と", pos="助詞", pos_sub1="格助詞"),
            _tok("で", pos="助詞", pos_sub1="格助詞"),
            _tok("も", pos="助詞", pos_sub1="係助詞"),
            _tok("いう", pos="動詞"),
        ]
        result, _ = apply_suzume_merge(tokens, "確認とでもいう")
        assert [token["surface"] for token in result] == ["確認", "と", "でも", "いう"]


class TestKatakanaCompound:
    def test_katakana_merge(self):
        tokens = [_tok("セット", pos="名詞"), _tok("リスト", pos="名詞")]
        text = "セットリスト"
        result, rule = apply_suzume_merge(tokens, text)
        assert len(result) == 1
        assert result[0]["surface"] == "セットリスト"


class TestNaiAdjective:
    def test_merge_darashinai(self):
        tokens = [_tok("だらし", pos="名詞"), _tok("ない", pos="形容詞")]
        text = "だらしない"
        result, rule = apply_suzume_merge(tokens, text)
        assert len(result) == 1
        assert result[0]["surface"] == "だらしない"
        assert result[0]["pos"] == "形容詞"
        assert rule == "nai-adjective"


class TestTariAdverb:
    def test_merge_taizento(self):
        tokens = [_tok("泰然", pos="名詞"), _tok("と", pos="助詞")]
        text = "泰然と"
        result, rule = apply_suzume_merge(tokens, text)
        assert len(result) == 1
        assert result[0]["surface"] == "泰然と"
        assert result[0]["pos"] == "副詞"


class TestCompoundVerb:
    def test_merge_yomitsuzukeru(self):
        tokens = [
            _tok("読み", pos="動詞", conj_form="連用形"),
            _tok("続ける", pos="動詞", lemma="続ける"),
        ]
        text = "読み続ける"
        result, rule = apply_suzume_merge(tokens, text)
        assert len(result) == 1
        assert result[0]["surface"] == "読み続ける"
        assert rule == "compound-verb"

    def test_merge_recent_closed_v2_forms_without_compound_word_entries(self):
        cases = [
            ("書き", "置く", "書き置く"),
            ("書き", "足す", "書き足す"),
            ("書き", "交わす", "書き交わす"),
        ]
        for v1, v2, compound in cases:
            tokens = [
                _tok(v1, pos="動詞", conj_form="連用形"),
                _tok(v2, pos="動詞", lemma=v2),
            ]
            result, rule = apply_suzume_merge(tokens, compound)
            assert result == [{"surface": compound, "pos": "動詞", "lemma": compound}]
            assert rule == "compound-verb"

    def test_merge_nominal_tagged_v1_before_closed_v2(self):
        tokens = [_tok("座り", pos="名詞"), _tok("直る", pos="動詞", lemma="直る")]
        result, rule = apply_suzume_merge(tokens, "座り直る")
        assert result == [{"surface": "座り直る", "pos": "動詞", "lemma": "座り直る"}]
        assert rule == "compound-verb"

    def test_merge_compound_renyokei_nominal(self):
        tokens = [
            _tok("押し", pos="動詞", lemma="押す", conj_form="連用形"),
            _tok("下げ", pos="名詞"),
            _tok("を", pos="助詞"),
        ]
        result, rule = apply_suzume_merge(tokens, "押し下げを")
        assert [token["surface"] for token in result] == ["押し下げ", "を"]
        assert result[0]["pos"] == "名詞"
        assert rule == "compound-renyokei-nominal"

    def test_merge_nominal_s_row_v1_with_godan_v2(self):
        tokens = [
            _tok("押し", pos="名詞"),
            _tok("返し", pos="接尾辞"),
            _tok("を", pos="助詞"),
        ]
        result, rule = apply_suzume_merge(tokens, "押し返しを")
        assert [token["surface"] for token in result] == ["押し返し", "を"]
        assert rule == "compound-renyokei-nominal"

    def test_keep_parallel_godan_renyokei_split(self):
        tokens = [
            _tok("上がり", pos="名詞"),
            _tok("下がり", pos="名詞"),
            _tok("を", pos="助詞"),
        ]
        result, rule = apply_suzume_merge(tokens, "上がり下がりを")
        assert [token["surface"] for token in result] == ["上がり", "下がり", "を"]
        assert rule != "compound-renyokei-nominal"

    def test_merge_suffix_tagged_godan_v2_nominal(self):
        tokens = [
            _tok("入り", pos="名詞"),
            _tok("混じり", pos="名詞", pos_sub1="接尾"),
            _tok("を", pos="助詞"),
        ]
        result, rule = apply_suzume_merge(tokens, "入り混じりを")
        assert [token["surface"] for token in result] == ["入り混じり", "を"]
        assert result[0]["pos"] == "名詞"
        assert rule == "compound-renyokei-nominal"


class TestURLMerge:
    def test_url(self):
        tokens = [
            _tok("https"),
            _tok(":"),
            _tok("//"),
            _tok("example"),
            _tok("."),
            _tok("com"),
        ]
        text = "https://example.com"
        result, rule = apply_suzume_merge(tokens, text)
        assert len(result) == 1
        assert result[0]["surface"] == "https://example.com"


class TestFamilyMerge:
    def test_o_niichan(self):
        tokens = [_tok("お", pos="接頭詞"), _tok("兄ちゃん", pos="名詞")]
        text = "お兄ちゃん"
        result, rule = apply_suzume_merge(tokens, text)
        assert len(result) == 1
        assert result[0]["surface"] == "お兄ちゃん"


class TestPostprocessKanjiMerge:
    def test_kanji_prefix_compound_uses_complete_canonical_paradigm(self):
        for suffix in ("笑み", "笑む", "笑ん", "笑え", "笑っ", "笑わ", "笑い"):
            tokens = [_tok("微", pos="接頭詞"), _tok(suffix, pos="名詞")]
            result, rule = apply_suzume_merge(tokens, f"微{suffix}")
            assert [token["surface"] for token in result] == [f"微{suffix}"]
            assert rule == "kanji-merge"

    def test_ascii_joiner_merge_has_one_canonical_rule_name(self):
        tokens = [_tok("tool", pos="名詞"), _tok(".", pos="記号"), _tok("example", pos="名詞")]
        result, rule = apply_suzume_merge(tokens, "tool.example")
        assert [token["surface"] for token in result] == ["tool.example"]
        assert rule == "ascii-joiner-merge"

    def test_ascii_joiner_merge_covers_every_word_internal_joiner(self):
        for joiner, head, tail in (("-", "Coca", "Cola"), ("'", "McDonald", "s"), ("&", "H", "M"), ("/", "CI", "CD")):
            tokens = [_tok(head, pos="名詞"), _tok(joiner, pos="記号"), _tok(tail, pos="名詞")]
            surface = f"{head}{joiner}{tail}"
            result, rule = apply_suzume_merge(tokens, surface)
            assert [token["surface"] for token in result] == [surface]
            assert rule == "ascii-joiner-merge"

    def test_kanji_merge_post(self):
        """Post-process kanji merge after main pass."""
        tokens = [
            _tok("二", pos="名詞", pos_sub1="数"),  # Will not trigger #6 (pos_sub1=数)
            _tok("次", pos="名詞"),
        ]
        text = "二次"
        result, _ = apply_suzume_merge(tokens, text)
        # The post-process kanji merge should catch this
        assert any(t["surface"] == "二次" for t in result)

    def test_temporal_noun_does_not_absorb_verb_stem(self):
        tokens = [
            _tok("日", pos="名詞"),
            _tok("見", pos="動詞", lemma="見る", conj_form="連用形"),
            _tok("た", pos="助動詞"),
        ]
        result, rule = apply_suzume_merge(tokens, "日見た")
        assert [token["surface"] for token in result] == ["日", "見", "た"]
        assert rule is None

    def test_iteration_mark_stays_attached_to_preceding_kanji(self):
        tokens = [_tok("黒", pos="名詞"), _tok("々", pos="記号")]
        result, rule = apply_suzume_merge(tokens, "黒々")
        assert [token["surface"] for token in result] == ["黒々"]
        assert rule == "kanji-merge"

    def test_search_unit_suffix_merges_with_nominal_host(self):
        tokens = [_tok("夜", pos="名詞", pos_sub1="副詞可能"), _tok("風", pos="名詞", pos_sub1="接尾")]
        result, rule = apply_suzume_merge(tokens, "夜風")
        assert [token["surface"] for token in result] == ["夜風"]
        assert rule == "kanji-merge"


class TestZuNiMerge:
    def test_zu_ni(self):
        tokens = [_tok("ず", pos="助動詞"), _tok("に", pos="助詞")]
        text = "ずに"
        result, rule = apply_suzume_merge(tokens, text)
        assert len(result) == 1
        assert result[0]["surface"] == "ずに"
        assert rule == "zu-ni-merge"


class TestProductiveTateSuffixMerge:
    def test_mecab_past_plus_te_reading_becomes_suffix(self):
        tokens = [
            _tok("でき", pos="動詞", lemma="できる"),
            _tok("た", pos="助動詞", lemma="た"),
            _tok("て", pos="助詞", lemma="て"),
        ]
        result, rule = apply_suzume_merge(tokens, "できたて")
        assert [(token["surface"], token["pos"]) for token in result] == [
            ("でき", "動詞"),
            ("たて", "接尾辞"),
        ]
        assert rule == "productive-tate-suffix"

    def test_punctuation_prevents_cross_sentence_merge(self):
        tokens = [
            _tok("でき", pos="動詞", lemma="できる"),
            _tok("た", pos="助動詞", lemma="た"),
            _tok("。", pos="記号"),
            _tok("て", pos="助詞", lemma="て"),
        ]
        result, rule = apply_suzume_merge(tokens, "できた。て")
        assert [token["surface"] for token in result] == ["でき", "た", "。", "て"]
        assert rule is None


class TestTeAruSplit:
    def test_keeps_lexical_adverb_before_aru(self):
        tokens = [_tok("初めて", pos="副詞", lemma="初めて"), _tok("ある", pos="動詞", lemma="ある")]
        result, rule = apply_suzume_merge(tokens, "初めてある")
        assert [token["surface"] for token in result] == ["初めて", "ある"]
        assert rule == "fixed-te-search-unit-before-aru"

    def test_keeps_split_compound_particle_before_inflected_aru(self):
        tokens = [
            _tok("に", pos="助詞", lemma="に"),
            _tok("つい", pos="動詞", lemma="つく"),
            _tok("て", pos="助詞", lemma="て"),
            _tok("あっ", pos="動詞", lemma="ある"),
        ]
        result, rule = apply_suzume_merge(tokens, "についてあっ")
        assert [token["surface"] for token in result] == ["について", "あっ"]
        assert result[0]["pos"] == "助詞"
        assert rule == "fixed-te-search-unit-before-aru"

    def test_keeps_productive_te_form_split(self):
        tokens = [_tok("並べて", pos="動詞", lemma="並べる"), _tok("あれ", pos="動詞", lemma="ある")]
        result, rule = apply_suzume_merge(tokens, "並べてあれ")
        assert [token["surface"] for token in result] == ["並べ", "て", "あれ"]
        assert rule == "te-aru-split"


class TestMechaMerge:
    def test_mecha(self):
        tokens = [_tok("め", pos="名詞"), _tok("ちゃ", pos="助詞")]
        text = "めちゃ"
        result, rule = apply_suzume_merge(tokens, text)
        assert len(result) == 1
        assert result[0]["surface"] == "めちゃ"


class TestColloquialPronoun:
    def test_koitsu(self):
        tokens = [_tok("こい", pos="動詞"), _tok("つ", pos="助動詞")]
        text = "こいつ"
        result, rule = apply_suzume_merge(tokens, text)
        assert len(result) == 1
        assert result[0]["surface"] == "こいつ"
        assert result[0]["pos"] == "代名詞"


class TestKamoMerge:
    def test_kamo(self):
        tokens = [_tok("か", pos="助詞"), _tok("も", pos="助詞")]
        text = "かも"
        result, _ = apply_suzume_merge(tokens, text)
        assert len(result) == 1
        assert result[0]["surface"] == "かも"


class TestLiteraryAdjectiveTerminalRestore:
    def test_restores_terminal_before_particle_run_and_preserves_spelling(self):
        tokens = [
            _tok("恐し", pos="形容詞", lemma="恐い", conj_form="文語基本形"),
            _tok("いとも", pos="副詞"),
        ]
        result, rule = apply_suzume_merge(tokens, "恐しいとも")
        assert result == [
            {"surface": "恐しい", "pos": "形容詞", "lemma": "恐しい"},
            {"surface": "と", "pos": "助詞", "lemma": "と"},
            {"surface": "も", "pos": "助詞", "lemma": "も"},
        ]
        assert rule == "adj-bungo-fix"

    def test_restores_terminal_when_reference_split_is_one_mora(self):
        tokens = [
            _tok("恐し", pos="形容詞", lemma="恐い", conj_form="文語基本形"),
            _tok("い", pos="助動詞"),
        ]
        result, rule = apply_suzume_merge(tokens, "恐しい")
        assert result == [{"surface": "恐しい", "pos": "形容詞", "lemma": "恐しい"}]
        assert rule == "adj-bungo-fix"


class TestProductiveMimeticNormalization:
    def test_merges_heterogeneous_four_mora_shape(self):
        tokens = [_tok("ちく", pos="名詞"), _tok("たく", pos="動詞"), _tok("と", pos="助詞")]
        result, rule = apply_suzume_merge(tokens, "ちくたくと")
        assert result == [
            {"surface": "ちくたく", "pos": "副詞", "lemma": "ちくたく"},
            {"surface": "と", "pos": "助詞", "lemma": "と"},
        ]
        assert rule == "productive-mimetic"

    def test_splits_particle_from_reduplicated_reference_token(self):
        tokens = [_tok("ざぶんざぶんと", pos="副詞")]
        result, rule = apply_suzume_merge(tokens, "ざぶんざぶんと")
        assert [token["surface"] for token in result] == ["ざぶんざぶん", "と"]
        assert rule == "productive-mimetic"

    def test_merges_sokuon_tto_as_one_adverb(self):
        tokens = [_tok("に", pos="助詞"), _tok("こっ", pos="動詞"), _tok("と", pos="助詞")]
        result, rule = apply_suzume_merge(tokens, "にこっと")
        assert result == [{"surface": "にこっと", "pos": "副詞", "lemma": "にこっと"}]
        assert rule == "productive-mimetic"

    def test_merges_two_nasal_closures_and_keeps_particle(self):
        tokens = [_tok("がたん", pos="名詞"), _tok("ご", pos="接頭詞"), _tok("とんと", pos="副詞")]
        result, rule = apply_suzume_merge(tokens, "がたんごとんと")
        assert [token["surface"] for token in result] == ["がたんごとん", "と"]
        assert rule == "productive-mimetic"

    def test_retags_productive_sokuon_ri_shape(self):
        for surface in ("ふっくら", "しっかり", "ばったり"):
            result, rule = apply_suzume_merge([_tok(surface, pos="その他")], surface)
            assert result == [{"surface": surface, "pos": "副詞", "lemma": surface}]
            assert rule == "productive-mimetic"

    def test_does_not_absorb_preceding_case_particle(self):
        tokens = [
            _tok("が", pos="助詞"),
            _tok("ざぶんざぶんと", pos="副詞"),
        ]
        result, rule = apply_suzume_merge(tokens, "がざぶんざぶんと")
        assert [token["surface"] for token in result] == ["が", "ざぶんざぶん", "と"]
        assert rule == "productive-mimetic"

    def test_does_not_absorb_object_particle_into_tto_adverb(self):
        tokens = [
            _tok("紐", pos="名詞"),
            _tok("を", pos="助詞"),
            _tok("きゅっと", pos="副詞"),
            _tok("結ぶ", pos="動詞"),
        ]
        result, _ = apply_suzume_merge(tokens, "紐をきゅっと結ぶ")
        assert [token["surface"] for token in result] == ["紐", "を", "きゅっと", "結ぶ"]


class TestStructuralNominalSearchUnits:
    def test_recovers_known_hiragana_noun_from_particle_homograph(self):
        tokens = [_tok("たま", pos="名詞"), _tok("ごと", pos="名詞"), _tok("みかん", pos="名詞")]
        result, rule = apply_suzume_merge(tokens, "たまごとみかん")
        assert [token["surface"] for token in result] == ["たまご", "と", "みかん"]
        assert rule == "hiragana-compound"

    def test_merges_destination_suffix_without_place_name_list(self):
        tokens = [
            _tok("東京", pos="名詞", pos_sub1="固有名詞", pos_sub2="地域"),
            _tok("行き", pos="名詞", pos_sub1="接尾"),
            _tok("は", pos="助詞"),
        ]
        result, rule = apply_suzume_merge(tokens, "東京行きは")
        assert [token["surface"] for token in result] == ["東京行き", "は"]
        assert rule == "destination-suffix"

    def test_keeps_particle_delimited_motion_verb(self):
        tokens = [
            _tok("東京", pos="名詞", pos_sub1="固有名詞", pos_sub2="地域"),
            _tok("へ", pos="助詞"),
            _tok("行き", pos="動詞", lemma="行く", conj_form="連用形"),
        ]
        result, _ = apply_suzume_merge(tokens, "東京へ行き")
        assert [token["surface"] for token in result] == ["東京", "へ", "行き"]

    def test_merges_repeated_quantity_unit(self):
        tokens = [_tok("一語", pos="名詞"), _tok("一語", pos="名詞"), _tok("確認", pos="名詞")]
        result, rule = apply_suzume_merge(tokens, "一語一語確認")
        assert [token["surface"] for token in result] == ["一語一語", "確認"]
        assert rule in {"distributive-quantity", "kanji-compound"}

    def test_keeps_repeated_bare_number_split(self):
        tokens = [_tok("十一", pos="名詞"), _tok("十一", pos="名詞")]
        result, rule = apply_suzume_merge(tokens, "十一十一")
        # Existing kanji normalization may retain the whole number run, but it
        # must not classify a bare repeated number as a distributive unit.
        assert [token["surface"] for token in result] == ["十一十一"]
        assert rule != "distributive-quantity"

    def test_merges_productive_nominal_zukeru_after_distributive_quantity(self):
        tokens = [
            _tok("一語", pos="名詞"),
            _tok("一", pos="名詞"),
            _tok("語", pos="名詞"),
            _tok("意味", pos="名詞"),
            _tok("づける", pos="動詞", lemma="づける"),
        ]
        result, _ = apply_suzume_merge(tokens, "一語一語意味づける")
        assert [token["surface"] for token in result] == ["一語一語", "意味づける"]
        assert result[-1]["lemma"] == "意味づける"


class TestProductiveMimeticSuru:
    def test_splits_fused_reduplicated_mimetic_progressive(self):
        result, rule = apply_suzume_merge([_tok("ぷにぷにしてる", pos="名詞")], "ぷにぷにしてる")
        assert result == [
            {"surface": "ぷにぷに", "pos": "副詞", "lemma": "ぷにぷに"},
            {"surface": "し", "pos": "動詞", "lemma": "する"},
            {"surface": "てる", "pos": "助動詞", "lemma": "てる"},
        ]
        assert rule == "productive-mimetic-suru"


class TestHonorificPredicateBoundary:
    def test_splits_lexicalized_o_noun_ni_predicate(self):
        tokens = [_tok("お目にかかっ", pos="動詞", lemma="お目にかかる")]
        result, rule = apply_suzume_merge(tokens, "お目にかかっ")
        assert result == [
            {"surface": "お", "pos": "接頭詞", "lemma": "お"},
            {"surface": "目", "pos": "名詞", "lemma": "目"},
            {"surface": "に", "pos": "助詞", "lemma": "に"},
            {"surface": "かかっ", "pos": "動詞", "lemma": "かかる"},
        ]
        assert rule == "honorific-predicate-split"

    def test_splits_predicate_after_separate_honorific_prefix(self):
        tokens = [
            _tok("お", pos="接頭詞"),
            _tok("役に立っ", pos="動詞", lemma="役に立つ"),
        ]
        result, rule = apply_suzume_merge(tokens, "お役に立っ")
        assert [token["surface"] for token in result] == ["お", "役", "に", "立っ"]
        assert result[-1]["lemma"] == "立つ"
        assert rule == "honorific-predicate-split"

    def test_keeps_nominal_o_noun_ni_expression(self):
        tokens = [_tok("お気に入り", pos="名詞")]
        result, rule = apply_suzume_merge(tokens, "お気に入り")
        assert [token["surface"] for token in result] == ["お", "気に入り"]
        assert rule == "prefix-split"
