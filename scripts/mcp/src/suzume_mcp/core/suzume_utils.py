"""Main orchestration module ported from SuzumeUtils.pm get_expected_tokens() etc."""

import unicodedata

import regex

from .constants import SLANG_ADJ_STEMS
from .mecab import mecab_analyze
from .merge_rules import apply_suzume_merge
from .pos_mapping import correct_mecab_pos, map_mecab_pos, normalize_pos
from .postprocessors import (
    postprocess_adjective_garu,
    postprocess_adjective_nominalizer,
    postprocess_adverb_nominal_context,
    postprocess_adverbial_na_adjective,
    postprocess_attributive_mamonaku,
    postprocess_binding_negative_aux,
    postprocess_bound_derived_adjective,
    postprocess_chigai_negative_adjective,
    postprocess_classical_conjecture_aux,
    postprocess_classical_desiderative_aux,
    postprocess_classical_focus_namu,
    postprocess_classical_honorific_aux,
    postprocess_classical_kere_aux,
    postprocess_classical_perfect_aux,
    postprocess_classical_ramu_boundary,
    postprocess_closed_function_words,
    postprocess_closed_subsidiary_aux,
    postprocess_contracted_progressive_aux,
    postprocess_copula_neg,
    postprocess_dai_final_particle,
    postprocess_de_aru,
    postprocess_de_particle,
    postprocess_demo,
    postprocess_deverbal_noun_context,
    postprocess_dewa_aru_boundary,
    postprocess_difficulty_adjective_stem,
    postprocess_exclusion_suffix,
    postprocess_formal_noun_lemma,
    postprocess_fuu_formal_noun,
    postprocess_giving_aux,
    postprocess_hiragana_godan_wa_terminal,
    postprocess_hiragana_purpose_noun,
    postprocess_hiragana_yaka_adverbial,
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
    postprocess_productive_search_unit_boundaries,
    postprocess_productive_verb_suffix_stem,
    postprocess_prolonged_sound_noun,
    postprocess_quantity_bound_suffix,
    postprocess_quotative_determiner_spelling,
    postprocess_renyokei_compound_particle,
    postprocess_shimau_aux,
    postprocess_short_hiragana_onbin,
    postprocess_shortened_causative_passive,
    postprocess_sou,
    postprocess_sou_aux,
    postprocess_state_suffix,
    postprocess_subsidiary_yuku,
    postprocess_tada,
    postprocess_tagaru_aux,
    postprocess_taihen,
    postprocess_te_form_contraction,
    postprocess_teki_na_adjective,
    postprocess_temporal_nao,
    postprocess_to_areba_conditional,
    postprocess_tsuke_noun,
    postprocess_yoshi_formal_noun,
    postprocess_you_noun,
    preprocess_for_mecab,
    repair_kko_nominalizer,
    split_transparent_suru_te_adverb,
)
from .split_rules import apply_suzume_split


def _oracle_text(text: str) -> str:
    """Return the single coordinate space shared by MeCab and merge rules."""
    compact = "".join(char for char in text if not char.isspace())
    normalized = []
    for char in compact:
        char = char.translate(_FULLWIDTH_TABLE)
        if "\uff66" <= char <= "\uff9d":
            # Mirror Normalizer::halfwidthKatakanaToFullwidth. The two
            # half-width voiced marks (FF9E/FF9F) deliberately remain outside
            # this range because the native table does not map them.
            char = unicodedata.normalize("NFKC", char)
        if char in ("\u309b", "\u309c") and normalized:
            combining = "\u3099" if char == "\u309b" else "\u309a"
            combined = unicodedata.normalize("NFC", normalized[-1] + combining)
            if len(combined) == 1:
                normalized[-1] = combined
                continue
        normalized.append(char)
    return unicodedata.normalize("NFC", "".join(normalized))


def _is_deliberately_removed_symbol(surface: str) -> bool:
    """Mirror the punctuation/emoji classes removed by the C++ default."""

    def is_cpp_emoji(codepoint: int) -> bool:
        return (
            0x1F300 <= codepoint <= 0x1F64F
            or 0x1F680 <= codepoint <= 0x1FBFF
            or 0x2300 <= codepoint <= 0x23FF
            or 0x25A0 <= codepoint <= 0x27BF
            or 0x2B50 <= codepoint <= 0x2B55
            or 0x2934 <= codepoint <= 0x2935
            or 0x1F1E6 <= codepoint <= 0x1F1FF
            or codepoint == 0x200D
            or 0xFE0E <= codepoint <= 0xFE0F
            or 0x1F3FB <= codepoint <= 0x1F3FF
            or codepoint == 0x20E3
            or 0xE0020 <= codepoint <= 0xE007F
        )

    return bool(surface) and all(
        unicodedata.category(char).startswith("P") or is_cpp_emoji(ord(char)) for char in surface
    )


def get_mecab_tokens(text: str) -> list[dict]:
    """Get MeCab tokens with slang handling and POS mapping."""
    normalized_text = _oracle_text(text)
    processed_text, replacements = preprocess_for_mecab(normalized_text)
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

    postprocess_mecab_tokens(tokens, normalized_text, replacements)
    return tokens


def _reject_lossy_symbol_drop(surface: str, text: str) -> None:
    """Fail when the symbol filter is about to discard real text.

    Dropping punctuation is intended; dropping a word is not. MeCab labels
    anything IPADIC does not know as 記号, so a character outside its dictionary
    would silently vanish from the expected tokens and leave a test asserting a
    segmentation of text that is not the input. Correct the token's POS in
    correct_mecab_pos instead of letting it reach this filter.
    """
    if _is_deliberately_removed_symbol(surface):
        return
    carries_text = [char for char in surface if not char.isspace()]
    if carries_text:
        raise RuntimeError(
            f"symbol filter would drop {''.join(carries_text)!r} from {text!r} "
            f"(token {surface!r}): MeCab labelled real text as 記号. "
            "Add a correct_mecab_pos rule for it rather than losing the surface."
        )


def _reject_surface_mismatch(
    tokens: list[dict],
    text: str,
    surface_rule: str | None,
    *,
    normalize_fullwidth: bool = False,
) -> None:
    """Fail when normalization duplicates or loses an unexplained surface."""
    reconstructed = "".join(token.get("surface", "") for token in tokens)
    expected = "".join(char for char in text if not char.isspace())
    if surface_rule == "prolonged-sound-merge":
        # This pre-existing oracle rule deliberately canonicalizes a run of
        # long-vowel marks. Every other merge/split rule must preserve input.
        expected = regex.sub(r"ー+", "ー", expected)
    if normalize_fullwidth:
        expected = expected.translate(_FULLWIDTH_TABLE)
    if reconstructed != expected:
        raise RuntimeError(
            f"normalized token surfaces do not reconstruct the input: "
            f"expected {expected!r}, got {reconstructed!r} for {text!r}"
        )


def get_expected_tokens(text: str, suzume_tokens: list[dict] | None = None) -> tuple[list[dict], str, str]:
    """Get expected tokens: MeCab + Suzume rule corrections.

    Returns:
        Tuple of (tokens, source_label, applied_rule).
    """
    # Get raw MeCab tokens
    normalized_text = _oracle_text(text)
    processed_text, replacements = preprocess_for_mecab(normalized_text)
    raw_tokens = mecab_analyze(processed_text)
    postprocess_mecab_tokens(raw_tokens, normalized_text, replacements)
    repair_kko_nominalizer(raw_tokens)

    # Fix MeCab POS errors (before POS mapping)
    correct_mecab_pos(raw_tokens)
    # Restore inflection boundaries the dictionary lexicalized away.  Runs here
    # because the decision needs the reading, which the merge pass drops.
    split_transparent_suru_te_adverb(raw_tokens)

    # Apply Suzume merge rules
    merged, merge_rule = apply_suzume_merge(raw_tokens, normalized_text)

    # Apply Suzume split rules
    split_tokens, split_rule = apply_suzume_split(merged)
    _reject_surface_mismatch(split_tokens, normalized_text, merge_rule)

    # Combine rule names
    applied_rule = merge_rule or split_rule
    if merge_rule and split_rule:
        applied_rule = f"{merge_rule}+{split_rule}"

    # Map POS and filter symbols
    tokens = []
    removed_symbol = False
    retained_surface_parts = []
    for t in split_tokens:
        pos = normalize_pos(map_mecab_pos(t))
        surface = t.get("surface", "")
        if pos == "Symbol" or _is_deliberately_removed_symbol(surface):
            _reject_lossy_symbol_drop(t.get("surface", ""), text)
            removed_symbol = True
            continue
        retained_surface_parts.append(surface)
        tokens.append(
            {
                "surface": t.get("surface", ""),
                "pos": pos,
                "lemma": t["lemma"] if t.get("lemma") and t["lemma"] != "*" else t.get("surface", ""),
            }
        )
    if removed_symbol and applied_rule is None:
        applied_rule = "symbol-filter"

    # Post-processing: context-dependent POS normalization
    if postprocess_sou(tokens) and applied_rule is None:
        applied_rule = "sou-context"
    if postprocess_ikaga(tokens) and applied_rule is None:
        applied_rule = "ikaga-adverb"
    if postprocess_tada(tokens) and applied_rule is None:
        applied_rule = "tada-context"
    if postprocess_demo(tokens) and applied_rule is None:
        applied_rule = "demo-particle"
    if postprocess_hiragana_yaka_adverbial(tokens) and applied_rule is None:
        applied_rule = "hiragana-yaka-adverbial"
    if postprocess_closed_function_words(tokens) and applied_rule is None:
        applied_rule = "closed-function-word-pos"
    if postprocess_closed_subsidiary_aux(tokens) and applied_rule is None:
        applied_rule = "closed-subsidiary-aux"
    if postprocess_classical_focus_namu(tokens) and applied_rule is None:
        applied_rule = "classical-focus-namu"
    if postprocess_honorific_i_adjective(tokens) and applied_rule is None:
        applied_rule = "honorific-i-adjective"
    if postprocess_i_adjective_upper_bound(tokens) and applied_rule is None:
        applied_rule = "i-adjective-upper-bound"
    if postprocess_kadouka_adverb(tokens) and applied_rule is None:
        applied_rule = "kadouka-adverb"
    if postprocess_ii(tokens) and applied_rule is None:
        applied_rule = "ii-adjective"
    if postprocess_iru_aux(tokens) and applied_rule is None:
        applied_rule = "iru-aux"
    if postprocess_giving_aux(tokens) and applied_rule is None:
        applied_rule = "giving-receiving-aux"
    if postprocess_contracted_progressive_aux(tokens) and applied_rule is None:
        applied_rule = "contracted-progressive-aux"
    if postprocess_itadakeru_aux(tokens) and applied_rule is None:
        applied_rule = "itadakeru-aux"
    if postprocess_miru_aux(tokens) and applied_rule is None:
        applied_rule = "miru-aux"
    if postprocess_monono_conjunction(tokens) and applied_rule is None:
        applied_rule = "monono-conjunction"
    if postprocess_formal_noun_lemma(tokens) and applied_rule is None:
        applied_rule = "formal-noun-lemma"
    if postprocess_adjective_nominalizer(tokens) and applied_rule is None:
        applied_rule = "adjective-nominalizer"
    if postprocess_shortened_causative_passive(tokens) and applied_rule is None:
        applied_rule = "shortened-causative-passive"
    if postprocess_shimau_aux(tokens) and applied_rule is None:
        applied_rule = "contracted-shimau-aux"
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
    if postprocess_adjective_garu(tokens) and applied_rule is None:
        applied_rule = "adjective-garu-pos"
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
    if postprocess_de_particle(tokens) and applied_rule is None:
        applied_rule = "de-particle"
    if postprocess_te_form_contraction(tokens) and applied_rule is None:
        applied_rule = "te-form-contraction-particle"
    if postprocess_dai_final_particle(tokens) and applied_rule is None:
        applied_rule = "dai-final-particle"
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
    if postprocess_de_aru(tokens) and applied_rule is None:
        applied_rule = "de-aru"
    if postprocess_dewa_aru_boundary(tokens) and applied_rule is None:
        applied_rule = "dewa-aru-boundary"
    if postprocess_ka_suru_noun(tokens) and applied_rule is None:
        applied_rule = "ka-suru-noun"
    if postprocess_taihen(tokens) and applied_rule is None:
        applied_rule = "taihen-context"
    if postprocess_na_adj_noun(tokens) and applied_rule is None:
        applied_rule = "na-adjective-noun-use"
    if postprocess_deverbal_noun_context(tokens) and applied_rule is None:
        applied_rule = "deverbal-noun-context"
    if postprocess_attributive_mamonaku(tokens) and applied_rule is None:
        applied_rule = "attributive-mamonaku"
    if postprocess_adverb_nominal_context(tokens) and applied_rule is None:
        applied_rule = "adverb-nominal-context"
    if postprocess_temporal_nao(tokens) and applied_rule is None:
        applied_rule = "temporal-nao-adverb"
    if postprocess_tsuke_noun(tokens) and applied_rule is None:
        applied_rule = "tsuke-noun"
    if postprocess_copula_neg(tokens) and applied_rule is None:
        applied_rule = "copular-negative-pos"
    if postprocess_you_noun(tokens) and applied_rule is None:
        applied_rule = "you-noun"
    if postprocess_classical_ramu_boundary(tokens) and applied_rule is None:
        applied_rule = "classical-ramu-boundary"
    if postprocess_classical_desiderative_aux(tokens) and applied_rule is None:
        applied_rule = "classical-desiderative-aux"
    if postprocess_classical_honorific_aux(tokens) and applied_rule is None:
        applied_rule = "classical-honorific-aux"
    if postprocess_classical_conjecture_aux(tokens) and applied_rule is None:
        applied_rule = "classical-conjecture-aux"
    if postprocess_classical_kere_aux(tokens) and applied_rule is None:
        applied_rule = "classical-kere-aux"
    if postprocess_classical_perfect_aux(tokens) and applied_rule is None:
        applied_rule = "classical-perfect-aux"
    if postprocess_prolonged_sound_noun(tokens) and applied_rule is None:
        applied_rule = "prolonged-sound-noun"
    if postprocess_yoshi_formal_noun(tokens) and applied_rule is None:
        applied_rule = "yoshi-formal-noun"
    if postprocess_sou_aux(tokens) and applied_rule is None:
        applied_rule = "sou-aux"
    if postprocess_nara_verb(tokens) and applied_rule is None:
        applied_rule = "nara-verb"
    if postprocess_n_kuruwa(tokens) and applied_rule is None:
        applied_rule = "n-kuruwa"
    if postprocess_nai_context(tokens) and applied_rule is None:
        applied_rule = "nai-context"
    if postprocess_binding_negative_aux(tokens) and applied_rule is None:
        applied_rule = "binding-negative-aux"
    if postprocess_productive_search_unit_boundaries(tokens) and applied_rule is None:
        applied_rule = "productive-search-unit-boundaries"
    if postprocess_bound_derived_adjective(tokens) and applied_rule is None:
        applied_rule = "bound-derived-adjective"
    if postprocess_quotative_determiner_spelling(tokens) and applied_rule is None:
        applied_rule = "quotative-determiner-spelling"
    if postprocess_adverbial_na_adjective(tokens) and applied_rule is None:
        applied_rule = "adverbial-na-adjective"

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

    # Merge/split validation above catches structural rules, while this second
    # gate protects every context-dependent postprocessor as well. The only
    # permitted surface changes after that first gate are the public symbol
    # filter and full-width alphanumeric normalization.
    _reject_surface_mismatch(
        tokens,
        "".join(retained_surface_parts),
        merge_rule,
        normalize_fullwidth=True,
    )

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

    if regex.search(r"\p{Han}+然と", text):
        return "tari-adverb"
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
