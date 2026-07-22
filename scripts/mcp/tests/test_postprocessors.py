"""Tests for postprocessor functions."""

from suzume_mcp.core.postprocessors import (
    postprocess_adjective_nominalizer,
    postprocess_binding_negative_aux,
    postprocess_classical_focus_namu,
    postprocess_closed_function_words,
    postprocess_copula_neg,
    postprocess_de_particle,
    postprocess_demo,
    postprocess_formal_noun_lemma,
    postprocess_fuu_formal_noun,
    postprocess_giving_aux,
    postprocess_hiragana_godan_wa_terminal,
    postprocess_hiragana_purpose_noun,
    postprocess_honorific_i_adjective,
    postprocess_honorific_request,
    postprocess_i_adjective_upper_bound,
    postprocess_ii,
    postprocess_ikaga,
    postprocess_indefinite_ka,
    postprocess_iru_aux,
    postprocess_itadakeru_aux,
    postprocess_kadouka_adverb,
    postprocess_miru_aux,
    postprocess_na_adj_noun,
    postprocess_nara_verb,
    postprocess_productive_verb_suffix_stem,
    postprocess_quantity_bound_suffix,
    postprocess_renyokei_compound_particle,
    postprocess_shimau_aux,
    postprocess_short_hiragana_onbin,
    postprocess_sou,
    postprocess_subsidiary_yuku,
    postprocess_tagaru_aux,
    postprocess_te,
    postprocess_to_areba_conditional,
    postprocess_tsuke_noun,
    postprocess_you_noun,
    preprocess_for_mecab,
)


def _tok(surface, pos, **kw):
    t = {"surface": surface, "pos": pos, "lemma": surface}
    t.update(kw)
    return t


class TestPreprocessForMecab:
    def test_slang_adj(self):
        text, reps = preprocess_for_mecab("エモい")
        assert "エモ" not in text
        assert len(reps) > 0

    def test_slang_verb(self):
        text, reps = preprocess_for_mecab("バズった")
        assert "バズ" not in text

    def test_word_exception(self):
        text, reps = preprocess_for_mecab("小供")
        assert text == "供給"

    def test_no_change(self):
        text, reps = preprocess_for_mecab("食べる")
        assert text == "食べる"
        assert len(reps) == 0


class TestPostprocessSou:
    def test_sou_before_copula(self):
        tokens = [_tok("そう", "Adverb"), _tok("だ", "Auxiliary")]
        postprocess_sou(tokens)
        assert tokens[0]["pos"] == "Adjective"

    def test_sou_standalone(self):
        tokens = [_tok("そう", "Adverb"), _tok("食べる", "Verb")]
        postprocess_sou(tokens)
        assert tokens[0]["pos"] == "Adverb"

    def test_sou_before_attributive_copula(self):
        tokens = [_tok("そう", "Adverb"), _tok("な", "Auxiliary"), _tok("の", "Particle")]
        postprocess_sou(tokens)
        assert tokens[0]["pos"] == "Adjective"

    def test_sou_before_continuative_copula(self):
        tokens = [
            _tok("そう", "Adverb"),
            _tok("で", "Auxiliary"),
            _tok("は", "Particle"),
            _tok("ない", "Auxiliary"),
        ]
        postprocess_sou(tokens)
        assert tokens[0]["pos"] == "Adjective"

    def test_katakana_stem_before_sou(self):
        tokens = [_tok("キモ", "Noun"), _tok("そう", "Auxiliary")]
        postprocess_sou(tokens)
        assert tokens[0]["pos"] == "Adjective"
        assert tokens[0]["lemma"] == "キモい"


class TestPostprocessIkaga:
    def test_ikaga_with_copula(self):
        tokens = [_tok("いかが", "Adjective"), _tok("ですか", "Auxiliary")]
        postprocess_ikaga(tokens)
        assert tokens[0]["pos"] == "Adjective"  # Keep as-is

    def test_ikaga_standalone(self):
        tokens = [_tok("いかが", "Adjective"), _tok("食べ", "Verb")]
        postprocess_ikaga(tokens)
        assert tokens[0]["pos"] == "Adverb"


class TestPostprocessDemo:
    def test_demo_after_interrogative(self):
        tokens = [_tok("何", "Noun"), _tok("でも", "Particle")]
        postprocess_demo(tokens)
        assert tokens[1]["pos"] == "Particle"

    def test_demo_after_noun(self):
        tokens = [_tok("雨", "Noun"), _tok("でも", "Conjunction")]
        postprocess_demo(tokens)
        assert tokens[1]["pos"] == "Particle"

    def test_sentence_initial_demo_uses_particle_ambiguity_policy(self):
        tokens = [_tok("でも", "Particle"), _tok("始め", "Verb")]
        postprocess_demo(tokens)
        assert tokens[0]["pos"] == "Particle"


class TestPostprocessDeParticleNoop:
    def test_copula_before_binding_particle(self):
        tokens = [_tok("本", "Noun"), _tok("で", "Particle"), _tok("しか", "Particle")]
        postprocess_de_particle(tokens)
        assert tokens[1] == _tok("で", "Auxiliary", lemma="だ")

    def test_case_particle_without_binding_particle(self):
        tokens = [_tok("本", "Noun"), _tok("で", "Particle"), _tok("学ぶ", "Verb")]
        postprocess_de_particle(tokens)
        assert tokens[1] == _tok("で", "Particle")


class TestPostprocessClosedFunctionWords:
    def test_literary_conjunction(self):
        tokens = [_tok("しかるに", "Adverb"), _tok("確認", "Noun")]
        assert postprocess_closed_function_words(tokens)
        assert tokens[0] == _tok("しかるに", "Conjunction")

    def test_distributive_pronoun(self):
        tokens = [_tok("各々", "Noun"), _tok("判断", "Noun")]
        assert postprocess_closed_function_words(tokens)
        assert tokens[0] == _tok("各々", "Pronoun")

    def test_ambiguous_function_word_uses_adverbial_policy(self):
        tokens = [_tok("また", "Conjunction"), _tok("確認", "Noun")]
        assert postprocess_closed_function_words(tokens)
        assert tokens[0] == _tok("また", "Adverb")

    def test_ordinary_noun_is_unchanged(self):
        tokens = [_tok("確認", "Noun")]
        assert not postprocess_closed_function_words(tokens)


class TestPostprocessStructuralFunctionWords:
    def test_classical_namu_before_quote(self):
        tokens = [_tok("なむ", "Interjection"), _tok("と", "Particle"), _tok("思う", "Verb")]
        assert postprocess_classical_focus_namu(tokens)
        assert tokens[0] == _tok("なむ", "Particle")

    def test_honorific_i_adjective(self):
        tokens = [_tok("お", "Prefix"), _tok("忙しい", "Noun")]
        assert postprocess_honorific_i_adjective(tokens)
        assert tokens[1] == _tok("忙しい", "Adjective")

    def test_renyokei_before_upper_bound_particle(self):
        tokens = [_tok("多く", "Noun"), _tok("とも", "Particle")]
        assert postprocess_i_adjective_upper_bound(tokens)
        assert tokens[0] == _tok("多く", "Adjective", lemma="多い")


class TestPostprocessIi:
    def test_ii_adjective(self):
        tokens = [_tok("いい", "Verb", lemma="いう")]
        postprocess_ii(tokens)
        assert tokens[0]["pos"] == "Adjective"

    def test_ii_before_verb(self):
        tokens = [_tok("いい", "Verb", lemma="いう"), _tok("出す", "Verb")]
        postprocess_ii(tokens)
        assert tokens[0]["pos"] == "Verb"  # Keep as verb


class TestPostprocessIruAux:
    def test_iru_after_te(self):
        tokens = [_tok("て", "Particle"), _tok("いる", "Verb")]
        postprocess_iru_aux(tokens)
        assert tokens[1]["pos"] == "Auxiliary"

    def test_iru_standalone(self):
        tokens = [_tok("猫", "Noun"), _tok("いる", "Verb")]
        postprocess_iru_aux(tokens)
        assert tokens[1]["pos"] == "Verb"

    def test_iru_after_focused_te_form(self):
        tokens = [_tok("で", "Particle"), _tok("しか", "Particle"), _tok("い", "Verb", lemma="いる")]
        postprocess_iru_aux(tokens)
        assert tokens[2]["pos"] == "Auxiliary"

    def test_conditional_iru_after_focus_retains_reference_pos(self):
        tokens = [_tok("で", "Particle"), _tok("さえ", "Particle"), _tok("いれ", "Verb", lemma="いる")]
        postprocess_iru_aux(tokens)
        assert tokens[2]["pos"] == "Verb"


class TestPostprocessGivingAux:
    def test_receiving_verb_after_te_is_auxiliary(self):
        tokens = [_tok("て", "Particle"), _tok("もらっ", "Verb", lemma="もらう")]
        assert postprocess_giving_aux(tokens)
        assert tokens[1]["pos"] == "Auxiliary"

    def test_lexical_giving_verb_stays_verb(self):
        tokens = [_tok("本", "Noun"), _tok("を", "Particle"), _tok("あげる", "Verb", lemma="あげる")]
        assert not postprocess_giving_aux(tokens)
        assert tokens[2]["pos"] == "Verb"


class TestPostprocessMiruAux:
    def test_miru_after_te(self):
        tokens = [_tok("て", "Particle"), _tok("み", "Verb", lemma="みる")]
        postprocess_miru_aux(tokens)
        assert tokens[1]["pos"] == "Auxiliary"

    def test_miru_after_emphatic_mo(self):
        tokens = [
            _tok("て", "Particle"),
            _tok("も", "Particle"),
            _tok("み", "Verb", lemma="みる"),
        ]
        postprocess_miru_aux(tokens)
        assert tokens[2]["pos"] == "Auxiliary"

    def test_miru_standalone(self):
        tokens = [_tok("花", "Noun"), _tok("を", "Particle"), _tok("みる", "Verb")]
        postprocess_miru_aux(tokens)
        assert tokens[2]["pos"] == "Verb"


class TestPostprocessShimauAux:
    def test_kanji_shimau_after_te(self):
        tokens = [_tok("て", "Particle"), _tok("仕舞っ", "Verb", lemma="仕舞う"), _tok("た", "Auxiliary")]
        postprocess_shimau_aux(tokens)
        assert tokens[1] == _tok("仕舞っ", "Auxiliary", lemma="しまう")

    def test_kanji_shimau_standalone(self):
        tokens = [_tok("物", "Noun"), _tok("を", "Particle"), _tok("仕舞う", "Verb")]
        postprocess_shimau_aux(tokens)
        assert tokens[2] == _tok("仕舞う", "Verb")


class TestPostprocessQuantityBoundSuffix:
    def test_quantity_counter_is_split_from_bound_suffix(self):
        tokens = [_tok("二階建て", "Noun"), _tok("三本立て", "Noun")]
        assert postprocess_quantity_bound_suffix(tokens)
        assert [(token["surface"], token["pos"]) for token in tokens] == [
            ("二階", "Noun"),
            ("建て", "Suffix"),
            ("三本", "Noun"),
            ("立て", "Suffix"),
        ]

    def test_non_quantity_verb_is_not_split(self):
        tokens = [_tok("建てる", "Verb")]
        assert not postprocess_quantity_bound_suffix(tokens)
        assert tokens == [_tok("建てる", "Verb")]

    def test_separate_homographic_verb_stem_becomes_suffix(self):
        tokens = [_tok("二本", "Noun"), _tok("立て", "Verb", lemma="立てる")]
        assert postprocess_quantity_bound_suffix(tokens)
        assert tokens[1] == _tok("立て", "Suffix")


class TestPostprocessTsureteParticle:
    def test_hiragana_compound_particle(self):
        tokens = [_tok("年", "Noun"), _tok("に", "Particle"), _tok("つれ", "Verb"), _tok("て", "Particle")]
        assert postprocess_renyokei_compound_particle(tokens)
        assert tokens == [_tok("年", "Noun"), _tok("につれて", "Particle")]

    def test_kanji_verb_is_not_compound_particle(self):
        tokens = [_tok("年", "Noun"), _tok("に", "Particle"), _tok("連れ", "Verb"), _tok("て", "Particle")]
        assert not postprocess_renyokei_compound_particle(tokens)


class TestPostprocessToArebaConditional:
    def test_splits_reference_analyzer_compound(self):
        tokens = [_tok("必要", "Noun"), _tok("と", "Particle"), _tok("あれば", "Verb", lemma="ある")]

        assert postprocess_to_areba_conditional(tokens)
        assert tokens == [
            _tok("必要", "Noun"),
            _tok("と", "Particle"),
            _tok("あれ", "Verb", lemma="ある"),
            _tok("ば", "Particle"),
        ]

    def test_keeps_existing_boundary(self):
        tokens = [
            _tok("と", "Particle"),
            _tok("あれ", "Verb", lemma="ある"),
            _tok("ば", "Particle"),
        ]

        assert not postprocess_to_areba_conditional(tokens)


class TestPostprocessProductiveVerbSuffixStem:
    def test_noun_homograph_before_gachi_becomes_godan_renyokei(self):
        tokens = [_tok("読み", "Noun"), _tok("がち", "Suffix")]
        assert postprocess_productive_verb_suffix_stem(tokens)
        assert tokens[0] == _tok("読み", "Verb", lemma="読む")

    def test_noun_homograph_before_ppanashi_becomes_godan_renyokei(self):
        tokens = [_tok("出し", "Noun"), _tok("っぱなし", "Suffix")]
        assert postprocess_productive_verb_suffix_stem(tokens)
        assert tokens[0] == _tok("出し", "Verb", lemma="出す")

    def test_ordinary_noun_before_particle_is_unchanged(self):
        tokens = [_tok("読み", "Noun"), _tok("を", "Particle")]
        assert not postprocess_productive_verb_suffix_stem(tokens)


class TestPostprocessNaraVerb:
    def test_nara_before_classical_negative(self):
        tokens = [_tok("なら", "Auxiliary"), _tok("ぬ", "Auxiliary")]
        postprocess_nara_verb(tokens)
        assert tokens[0] == _tok("なら", "Verb", lemma="なる")


class TestPostprocessHonorificRequest:
    def test_godan_renyokei_before_kudasaru(self):
        tokens = [
            _tok("お", "Prefix"),
            _tok("立ち", "Noun"),
            _tok("ください", "Verb", lemma="くださる"),
        ]
        assert postprocess_honorific_request(tokens)
        assert tokens[1] == _tok("立ち", "Verb", lemma="立つ")

    def test_ichidan_renyokei_before_kudasaru(self):
        tokens = [
            _tok("お", "Prefix"),
            _tok("答え", "Noun"),
            _tok("ください", "Verb", lemma="くださる"),
        ]
        assert postprocess_honorific_request(tokens)
        assert tokens[1] == _tok("答え", "Verb", lemma="答える")

    def test_godan_renyokei_before_itasu(self):
        tokens = [
            _tok("お", "Prefix"),
            _tok("願い", "Noun"),
            _tok("いたし", "Verb", lemma="いたす"),
        ]
        assert postprocess_honorific_request(tokens)
        assert tokens[1] == _tok("願い", "Verb", lemma="願う")

    def test_godan_renyokei_before_itadaku(self):
        tokens = [
            _tok("お", "Prefix"),
            _tok("使い", "Noun"),
            _tok("いただく", "Verb", lemma="いただく"),
        ]
        assert postprocess_honorific_request(tokens)
        assert tokens[1] == _tok("使い", "Verb", lemma="使う")

    def test_object_noun_is_not_changed(self):
        tokens = [
            _tok("お", "Prefix"),
            _tok("茶", "Noun"),
            _tok("ください", "Verb", lemma="くださる"),
        ]
        assert not postprocess_honorific_request(tokens)
        assert tokens[1]["pos"] == "Noun"


class TestTokenizerSearchUnitNormalizers:
    def test_kadouka_keeps_dou_adverbial(self):
        tokens = [
            _tok("か", "Particle"),
            _tok("どう", "Adjective"),
            _tok("か", "Particle"),
        ]
        assert postprocess_kadouka_adverb(tokens)
        assert tokens[1]["pos"] == "Adverb"

    def test_dou_outside_kadouka_is_unchanged(self):
        tokens = [_tok("どう", "Adjective"), _tok("考える", "Verb")]
        assert not postprocess_kadouka_adverb(tokens)
        assert tokens[0]["pos"] == "Adjective"

    def test_tagaru_forms_one_auxiliary(self):
        tokens = [_tok("食べ", "Verb"), _tok("た", "Auxiliary"), _tok("がる", "Verb")]
        assert postprocess_tagaru_aux(tokens)
        assert tokens == [_tok("食べ", "Verb"), _tok("たがる", "Auxiliary", lemma="たがる")]

    def test_past_auxiliary_before_case_particle_is_not_tagaru(self):
        tokens = [_tok("い", "Auxiliary", lemma="いる"), _tok("た", "Auxiliary"), _tok("が", "Particle")]
        assert not postprocess_tagaru_aux(tokens)
        assert [token["surface"] for token in tokens] == ["い", "た", "が"]

    def test_demonstrative_fuu_is_split(self):
        tokens = [_tok("そんなふうに", "Auxiliary")]
        assert postprocess_fuu_formal_noun(tokens)
        assert [token["surface"] for token in tokens] == ["そんな", "ふう", "に"]
        assert tokens[1]["pos"] == "Noun"

    def test_indefinite_pronoun_and_existential(self):
        tokens = [_tok("なにか", "Adverb"), _tok("いる", "Auxiliary")]
        assert postprocess_indefinite_ka(tokens)
        assert [(token["surface"], token["pos"]) for token in tokens] == [
            ("なに", "Pronoun"),
            ("か", "Particle"),
            ("いる", "Verb"),
        ]

    def test_subsidiary_yuku_is_verbal(self):
        tokens = [_tok("散り", "Verb", lemma="散る"), _tok("ゆく", "Auxiliary")]
        assert postprocess_subsidiary_yuku(tokens)
        assert tokens[1]["pos"] == "Verb"

    def test_hiragana_purpose_is_nominal_search_unit(self):
        tokens = [_tok("およぎ", "Verb", lemma="およぐ"), _tok("に", "Particle"), _tok("行く", "Verb")]
        assert postprocess_hiragana_purpose_noun(tokens)
        assert tokens[0] == _tok("およぎ", "Noun")

    def test_short_hiragana_onbin(self):
        tokens = [_tok("かん", "Noun"), _tok("で", "Particle")]
        assert postprocess_short_hiragana_onbin(tokens)
        assert tokens[0] == _tok("かん", "Verb", lemma="かむ")

    def test_valid_hatsuonbin_lemma_is_preserved(self):
        tokens = [_tok("とん", "Verb", lemma="とぶ"), _tok("だ", "Auxiliary")]
        assert not postprocess_short_hiragana_onbin(tokens)
        assert tokens[0] == _tok("とん", "Verb", lemma="とぶ")

    def test_suffix_before_copula_is_not_onbin(self):
        tokens = [_tok("さん", "Suffix"), _tok("で", "Auxiliary")]
        assert not postprocess_short_hiragana_onbin(tokens)

    def test_hiragana_godan_wa_terminal(self):
        tokens = [_tok("つか", "Verb"), _tok("う", "Auxiliary")]
        assert postprocess_hiragana_godan_wa_terminal(tokens)
        assert tokens == [_tok("つかう", "Verb")]

    def test_o_row_volitional_is_not_merged(self):
        tokens = [_tok("むかお", "Verb", lemma="むかう"), _tok("う", "Auxiliary")]
        assert not postprocess_hiragana_godan_wa_terminal(tokens)

    def test_adjective_volitional_is_not_merged(self):
        tokens = [_tok("うれしかろ", "Adjective", lemma="うれしい"), _tok("う", "Auxiliary")]
        assert not postprocess_hiragana_godan_wa_terminal(tokens)


class TestPostprocessDeParticle:
    def test_de_before_ha_stays_auxiliary(self):
        """で before は stays Auxiliary (no-op: MeCab distinguishes copula/particle)."""
        tokens = [_tok("で", "Auxiliary"), _tok("は", "Particle")]
        postprocess_de_particle(tokens)
        assert tokens[0]["pos"] == "Auxiliary"

    def test_de_after_adjective_stays_auxiliary(self):
        """で after Adjective stays Auxiliary (copula て-form)."""
        tokens = [_tok("好き", "Adjective"), _tok("で", "Auxiliary")]
        postprocess_de_particle(tokens)
        assert tokens[1]["pos"] == "Auxiliary"


class TestPostprocessNaAdjNoun:
    def test_adj_before_sugiru_stays_adjective(self):
        """Adjective stems before すぎる stay Adjective (no-op)."""
        tokens = [_tok("複雑", "Adjective"), _tok("すぎる", "Verb")]
        postprocess_na_adj_noun(tokens)
        assert tokens[0]["pos"] == "Adjective"

    def test_bare_na_adjective_stem_before_wo_is_nominal(self):
        tokens = [_tok("平静", "Adjective"), _tok("を", "Particle"), _tok("保っ", "Verb", lemma="保つ")]
        assert postprocess_na_adj_noun(tokens)
        assert tokens[0] == _tok("平静", "Noun")


class TestPostprocessTsukeNoun:
    def test_tsuke(self):
        tokens = [_tok("付け", "Suffix")]
        postprocess_tsuke_noun(tokens)
        assert tokens[0]["pos"] == "Noun"


class TestPostprocessYouNoun:
    def test_renyokei_you_is_formal_noun(self):
        tokens = [_tok("読み", "Verb", lemma="読む"), _tok("よう", "Suffix")]
        postprocess_you_noun(tokens)
        assert tokens[1]["pos"] == "Noun"

    def test_terminal_no_you_is_formal_noun(self):
        tokens = [_tok("の", "Particle"), _tok("よう", "Auxiliary")]
        postprocess_you_noun(tokens)
        assert tokens[1]["pos"] == "Noun"

    def test_no_you_before_copula_is_formal_noun(self):
        tokens = [_tok("の", "Particle"), _tok("よう", "Auxiliary"), _tok("だ", "Auxiliary")]
        postprocess_you_noun(tokens)
        assert tokens[1]["pos"] == "Noun"


class TestClosedGrammarNormalizers:
    def test_formal_noun_lemma_after_auxiliary(self):
        tokens = [_tok("た", "Auxiliary"), _tok("物", "Noun")]
        assert postprocess_formal_noun_lemma(tokens)
        assert tokens[1]["lemma"] == "もの"

    def test_lexical_thing_keeps_kanji_lemma(self):
        tokens = [_tok("物", "Noun")]
        assert not postprocess_formal_noun_lemma(tokens)

    def test_adjective_sa_is_suffix(self):
        tokens = [_tok("らし", "Auxiliary", lemma="らしい"), _tok("さ", "Particle")]
        assert postprocess_adjective_nominalizer(tokens)
        assert tokens[1]["pos"] == "Suffix"

    def test_causative_sa_is_not_nominalizer(self):
        tokens = [
            _tok("余儀なく", "Adjective", lemma="余儀ない"),
            _tok("さ", "Verb", lemma="する"),
            _tok("れる", "Auxiliary"),
        ]
        assert not postprocess_adjective_nominalizer(tokens)

    def test_shika_nai_is_auxiliary(self):
        tokens = [_tok("しか", "Particle"), _tok("ない", "Adjective")]
        assert postprocess_binding_negative_aux(tokens)
        assert tokens[1]["pos"] == "Auxiliary"


class TestPostprocessItadakeruAux:
    def test_after_te_particle_is_auxiliary(self):
        tokens = [_tok("て", "Particle"), _tok("いただける", "Verb")]
        postprocess_itadakeru_aux(tokens)
        assert tokens[1]["pos"] == "Auxiliary"


class TestPostprocessCopulaNeg:
    def test_naku_after_ja(self):
        tokens = [_tok("じゃ", "Auxiliary"), _tok("なく", "Auxiliary")]
        postprocess_copula_neg(tokens)
        assert tokens[1]["pos"] == "Adjective"


class TestPostprocessTe:
    def test_te_after_verb(self):
        tokens = [_tok("食べ", "Verb"), _tok("て", "Particle")]
        postprocess_te(tokens)
        assert tokens[1]["pos"] == "Auxiliary"
