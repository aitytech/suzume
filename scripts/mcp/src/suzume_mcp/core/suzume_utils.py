"""Main orchestration module ported from SuzumeUtils.pm get_expected_tokens() etc."""

import regex

from .constants import SLANG_ADJ_STEMS
from .mecab import mecab_analyze
from .merge_rules import apply_suzume_merge
from .pos_mapping import correct_mecab_pos, map_mecab_pos, normalize_pos
from .postprocessors import (
    postprocess_adjective_nominalizer,
    postprocess_binding_negative_aux,
    postprocess_chigai_negative_adjective,
    postprocess_classical_conjecture_aux,
    postprocess_classical_desiderative_aux,
    postprocess_classical_focus_namu,
    postprocess_classical_honorific_aux,
    postprocess_classical_kere_aux,
    postprocess_classical_perfect_aux,
    postprocess_classical_ramu_boundary,
    postprocess_closed_function_words,
    postprocess_contracted_progressive_aux,
    postprocess_copula_neg,
    postprocess_dai_final_particle,
    postprocess_de_aru,
    postprocess_de_particle,
    postprocess_demo,
    postprocess_difficulty_adjective_stem,
    postprocess_exclusion_suffix,
    postprocess_formal_noun_lemma,
    postprocess_fuu_formal_noun,
    postprocess_giving_aux,
    postprocess_hiragana_godan_wa_terminal,
    postprocess_hiragana_purpose_noun,
    postprocess_honorific_i_adjective,
    postprocess_honorific_oki_aux,
    postprocess_honorific_request,
    postprocess_i_adjective_upper_bound,
    postprocess_ii,
    postprocess_ikaga,
    postprocess_indefinite_ka,
    postprocess_iru_aux,
    postprocess_itadakeru_aux,
    postprocess_ka_suru_noun,
    postprocess_kadouka_adverb,
    postprocess_kiri_limited_particle,
    postprocess_kuru_causative,
    postprocess_mecab_tokens,
    postprocess_miru_aux,
    postprocess_monono_conjunction,
    postprocess_n_kuruwa,
    postprocess_na_adj_noun,
    postprocess_nai_context,
    postprocess_nanka_particle,
    postprocess_nara_verb,
    postprocess_onaji_predicate,
    postprocess_productive_verb_suffix_stem,
    postprocess_prolonged_sound_noun,
    postprocess_quantity_bound_suffix,
    postprocess_renyokei_compound_particle,
    postprocess_shimau_aux,
    postprocess_short_hiragana_onbin,
    postprocess_sou,
    postprocess_sou_aux,
    postprocess_state_suffix,
    postprocess_subsidiary_yuku,
    postprocess_tagaru_aux,
    postprocess_taihen,
    postprocess_teki_na_adjective,
    postprocess_to_areba_conditional,
    postprocess_tsuke_noun,
    postprocess_yoshi_formal_noun,
    postprocess_you_noun,
    preprocess_for_mecab,
)
from .split_rules import apply_suzume_split


def get_mecab_tokens(text: str) -> list[dict]:
    """Get MeCab tokens with slang handling and POS mapping."""
    processed_text, replacements = preprocess_for_mecab(text)
    raw_tokens = mecab_analyze(processed_text)

    tokens = []
    for t in raw_tokens:
        tokens.append(
            {
                "surface": t["surface"],
                "pos": map_mecab_pos(t),
                "lemma": t["lemma"] if t.get("lemma") and t["lemma"] != "*" else t["surface"],
            }
        )

    postprocess_mecab_tokens(tokens, text, replacements)
    return tokens


def get_expected_tokens(text: str, suzume_tokens: list[dict] | None = None) -> tuple[list[dict], str, str]:
    """Get expected tokens: MeCab + Suzume rule corrections.

    Returns:
        Tuple of (tokens, source_label, applied_rule).
    """
    # Get raw MeCab tokens
    processed_text, replacements = preprocess_for_mecab(text)
    raw_tokens = mecab_analyze(processed_text)
    postprocess_mecab_tokens(raw_tokens, text, replacements)

    # Fix MeCab POS errors (before POS mapping)
    correct_mecab_pos(raw_tokens)

    # Apply Suzume merge rules
    merged, merge_rule = apply_suzume_merge(raw_tokens, text)

    # Apply Suzume split rules
    split_tokens, split_rule = apply_suzume_split(merged)

    # Combine rule names
    applied_rule = merge_rule or split_rule
    if merge_rule and split_rule:
        applied_rule = f"{merge_rule}+{split_rule}"

    # Map POS and filter symbols
    tokens = []
    for t in split_tokens:
        pos = normalize_pos(map_mecab_pos(t))
        if pos == "Symbol":
            continue
        tokens.append(
            {
                "surface": t.get("surface", ""),
                "pos": pos,
                "lemma": t["lemma"] if t.get("lemma") and t["lemma"] != "*" else t.get("surface", ""),
            }
        )

    # Post-processing: context-dependent POS normalization
    postprocess_sou(tokens)
    postprocess_ikaga(tokens)
    postprocess_demo(tokens)
    if postprocess_closed_function_words(tokens) and applied_rule is None:
        applied_rule = "closed-function-word-pos"
    if postprocess_classical_focus_namu(tokens) and applied_rule is None:
        applied_rule = "classical-focus-namu"
    if postprocess_honorific_i_adjective(tokens) and applied_rule is None:
        applied_rule = "honorific-i-adjective"
    if postprocess_i_adjective_upper_bound(tokens) and applied_rule is None:
        applied_rule = "i-adjective-upper-bound"
    if postprocess_kadouka_adverb(tokens) and applied_rule is None:
        applied_rule = "kadouka-adverb"
    postprocess_ii(tokens)
    postprocess_iru_aux(tokens)
    if postprocess_giving_aux(tokens) and applied_rule is None:
        applied_rule = "giving-receiving-aux"
    if postprocess_contracted_progressive_aux(tokens) and applied_rule is None:
        applied_rule = "contracted-progressive-aux"
    postprocess_itadakeru_aux(tokens)
    postprocess_miru_aux(tokens)
    postprocess_monono_conjunction(tokens)
    if postprocess_formal_noun_lemma(tokens) and applied_rule is None:
        applied_rule = "formal-noun-lemma"
    if postprocess_adjective_nominalizer(tokens) and applied_rule is None:
        applied_rule = "adjective-nominalizer"
    postprocess_shimau_aux(tokens)
    if postprocess_quantity_bound_suffix(tokens) and applied_rule is None:
        applied_rule = "quantity-bound-suffix"
    if postprocess_exclusion_suffix(tokens) and applied_rule is None:
        applied_rule = "exclusion-suffix"
    if postprocess_state_suffix(tokens) and applied_rule is None:
        applied_rule = "state-suffix"
    if postprocess_productive_verb_suffix_stem(tokens) and applied_rule is None:
        applied_rule = "productive-verb-suffix-stem"
    if postprocess_teki_na_adjective(tokens) and applied_rule is None:
        applied_rule = "teki-na-adjective"
    if postprocess_difficulty_adjective_stem(tokens) and applied_rule is None:
        applied_rule = "difficulty-adjective-stem"
    if postprocess_renyokei_compound_particle(tokens) and applied_rule is None:
        applied_rule = "renyokei-compound-particle"
    if postprocess_to_areba_conditional(tokens) and applied_rule is None:
        applied_rule = "to-areba-conditional"
    if postprocess_tagaru_aux(tokens) and applied_rule is None:
        applied_rule = "tagaru-search-unit"
    if postprocess_fuu_formal_noun(tokens) and applied_rule is None:
        applied_rule = "fuu-formal-noun"
    if postprocess_indefinite_ka(tokens) and applied_rule is None:
        applied_rule = "indefinite-ka"
    if postprocess_subsidiary_yuku(tokens) and applied_rule is None:
        applied_rule = "subsidiary-yuku"
    if postprocess_hiragana_purpose_noun(tokens) and applied_rule is None:
        applied_rule = "hiragana-purpose-noun"
    if postprocess_short_hiragana_onbin(tokens) and applied_rule is None:
        applied_rule = "short-hiragana-onbin"
    if postprocess_hiragana_godan_wa_terminal(tokens) and applied_rule is None:
        applied_rule = "hiragana-godan-wa-terminal"
    if postprocess_honorific_request(tokens) and applied_rule is None:
        applied_rule = "honorific-request-renyokei"
    if postprocess_honorific_oki_aux(tokens) and applied_rule is None:
        applied_rule = "honorific-oki-aux"
    postprocess_de_particle(tokens)
    postprocess_dai_final_particle(tokens)
    if postprocess_chigai_negative_adjective(tokens) and applied_rule is None:
        applied_rule = "chigai-negative-adjective"
    if postprocess_nanka_particle(tokens) and applied_rule is None:
        applied_rule = "nanka-colloquial-particle"
    if postprocess_kiri_limited_particle(tokens) and applied_rule is None:
        applied_rule = "kiri-limiting-particle"
    if postprocess_kuru_causative(tokens) and applied_rule is None:
        applied_rule = "kuru-causative-lemma"
    if postprocess_onaji_predicate(tokens) and applied_rule is None:
        applied_rule = "onaji-predicative-na-adjective"
    postprocess_de_aru(tokens)
    postprocess_ka_suru_noun(tokens)
    postprocess_taihen(tokens)
    if postprocess_na_adj_noun(tokens) and applied_rule is None:
        applied_rule = "na-adjective-noun-use"
    postprocess_tsuke_noun(tokens)
    if postprocess_copula_neg(tokens) and applied_rule is None:
        applied_rule = "copular-negative-pos"
    postprocess_you_noun(tokens)
    postprocess_classical_ramu_boundary(tokens)
    if postprocess_classical_desiderative_aux(tokens) and applied_rule is None:
        applied_rule = "classical-desiderative-aux"
    if postprocess_classical_honorific_aux(tokens) and applied_rule is None:
        applied_rule = "classical-honorific-aux"
    postprocess_classical_conjecture_aux(tokens)
    if postprocess_classical_kere_aux(tokens) and applied_rule is None:
        applied_rule = "classical-kere-aux"
    postprocess_classical_perfect_aux(tokens)
    postprocess_prolonged_sound_noun(tokens)
    postprocess_yoshi_formal_noun(tokens)
    postprocess_sou_aux(tokens)
    postprocess_nara_verb(tokens)
    postprocess_n_kuruwa(tokens)
    postprocess_nai_context(tokens)
    if postprocess_binding_negative_aux(tokens) and applied_rule is None:
        applied_rule = "binding-negative-aux"

    # Normalize full-width alphanumeric to half-width
    fullwidth_applied = False
    for t in tokens:
        for key in ("surface", "lemma"):
            val = t.get(key)
            if val is None:
                continue
            new_val = val.translate(_FULLWIDTH_TABLE)
            if new_val != val:
                t[key] = new_val
                fullwidth_applied = True

    if fullwidth_applied and applied_rule is None:
        applied_rule = "fullwidth-normalize"

    if applied_rule:
        return tokens, "MeCab+SuzumeRules", applied_rule or ""

    # Check for slang adjective rule
    for slang in SLANG_ADJ_STEMS:
        if regex.search(regex.escape(slang) + r"[いかくけさ]", text):
            return tokens, "MeCab", "slang-adjective"

    return tokens, "MeCab", ""


def tokens_match(a: list[dict], b: list[dict], *, compare_lemma: bool = True) -> bool:
    """Compare two token arrays for equality (surface, pos, and lemma by default)."""
    if len(a) != len(b):
        return False
    for ta, tb in zip(a, b, strict=True):
        if ta.get("surface", "") != tb.get("surface", ""):
            return False
        pos_a = normalize_pos(ta.get("pos", ""))
        pos_b = normalize_pos(tb.get("pos", ""))
        if pos_a != pos_b:
            return False
        if compare_lemma:
            lemma_a = ta.get("lemma") or ta.get("surface", "")
            lemma_b = tb.get("lemma") or tb.get("surface", "")
            if lemma_a != lemma_b:
                return False
    return True


def format_expected(tokens: list[dict]) -> list[dict]:
    """Format tokens for JSON output. Always includes lemma."""
    result = []
    for t in tokens:
        entry: dict = {"surface": t["surface"], "pos": t["pos"]}
        lemma = t.get("lemma", "")
        entry["lemma"] = lemma if lemma else t["surface"]
        result.append(entry)
    return result


def get_char_types(s: str) -> list[str]:
    """Get character types present in a string."""
    types: set[str] = set()
    for ch in s:
        code = ord(ch)
        if 0x3040 <= code <= 0x309F:
            types.add("hiragana")
        elif 0x30A0 <= code <= 0x30FF:
            types.add("katakana")
        elif 0x4E00 <= code <= 0x9FFF:
            types.add("kanji")
        elif 0x0041 <= code <= 0x007A:
            types.add("alpha")
        elif 0x0030 <= code <= 0x0039:
            types.add("digit")
    return list(types)


def get_suzume_rule(text: str) -> str:
    """Check if text matches Suzume normalization rules."""
    from .constants import NAI_ADJECTIVES, TARI_ADVERB_STEMS

    for adj in NAI_ADJECTIVES:
        if adj in text:
            return "nai-adjective"

    if regex.search(r"\d+[^\d\s]", text):
        return "number+unit"

    if regex.search(r"\d+年\d+月\d+日", text):
        return "date"

    for stem in SLANG_ADJ_STEMS:
        if regex.search(regex.escape(stem) + r"[いかくけさ]", text):
            return "slang-adjective"
    if regex.search(r"やばい|やばかっ|やばく", text):
        return "slang-adjective"

    for stem in TARI_ADVERB_STEMS:
        if stem + "と" in text:
            return "tari-adverb"

    if regex.search(r"\p{Han}{2,}", text):
        return "kanji-compound"

    if regex.search(r"[\u30A0-\u30FF]{4,}", text):
        return "katakana-compound"

    return ""


# Full-width to half-width translation table
_FULLWIDTH_TABLE = str.maketrans(
    "０１２３４５６７８９ＡＢＣＤＥＦＧＨＩＪＫＬＭＮＯＰＱＲＳＴＵＶＷＸＹＺａｂｃｄｅｆｇｈｉｊｋｌｍｎｏｐｑｒｓｔｕｖｗｘｙｚ",
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
)
