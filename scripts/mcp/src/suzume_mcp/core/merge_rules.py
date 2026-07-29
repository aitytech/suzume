"""Merge rules ported from SuzumeUtils.pm apply_suzume_merge()."""

import regex

from .constants import (
    COLLOQUIAL_PRONOUNS,
    COMPOUND_VERB_V2_GODAN,
    COMPOUND_VERB_V2_ICHIDAN,
    COMPOUND_VERB_V2_NOT_AFTER_SURU,
    COMPOUND_VERB_V2_SURU_ONLY,
    DERIVED_ADJECTIVE_SUFFIX_LEMMAS,
    DERIVED_VERB_SUFFIX_FORMS,
    FAMILY_TERMS,
    FIXED_FUNCTION_LEMMAS,
    FIXED_FUNCTION_SEARCH_UNITS,
    FIXED_INFLECTED_FUNCTION_UNITS,
    HIRAGANA_COMPOUNDS,
    KANA_COUNTER_SUFFIXES,
    KANA_NUMBER_STEMS,
    NAI_ADJECTIVES,
    TARI_ADVERB_STEMS,
    TEMPORAL_COMPOUND_UNITS,
    TEMPORAL_PREFIX_KANJI,
)
from .mecab import mecab_analyze
from .merge_postprocessors import (
    _postprocess_adj_bungo,
    _postprocess_adj_kari,
    _postprocess_ascii_joiner_merge,
    _postprocess_atode,
    _postprocess_bound_suffix_noun_cell,
    _postprocess_bound_voiced_suffix,
    _postprocess_classical_mu,
    _postprocess_classical_shimu,
    _postprocess_demo_copula,
    _postprocess_dialectal,
    _postprocess_distributive_quantity,
    _postprocess_epenthetic_sa,
    _postprocess_filler_split,
    _postprocess_gamashii,
    _postprocess_ha_row_godan,
    _postprocess_honorific_split,
    _postprocess_izenkei_concessive,
    _postprocess_kamo,
    _postprocess_kanji_merge,
    _postprocess_ku_nominalization,
    _postprocess_kuruwa,
    _postprocess_nde_split,
    _postprocess_nickname_merge,
    _postprocess_nominal_zukeru,
    _postprocess_noni,
    _postprocess_onomatopoeia_tto_merge,
    _postprocess_prefix_split,
    _postprocess_productive_mimetics,
    _postprocess_search_unit_split,
    _postprocess_small_kana_head_merge,
    _postprocess_tomo_particle,
    _postprocess_totomoni,
)
from .split_rules import base_from_renyokei, bases_from_renyokei


def _reads_as_one_verb(lemma: str) -> bool:
    """Whether the reference dictionary reads `lemma` as a single verb."""
    if not lemma:
        return False
    tokens = mecab_analyze(lemma)
    return len(tokens) == 1 and tokens[0].get("pos") == "動詞"


# Numeric-approximation/aggregation prefixes that modify a whole quantity and split
# off the following number+counter (約|二時間, 計|五名), unlike ordinal 第 which binds
# to its number (第三十四|回). Mirrors normalize::isNumericApproxPrefixKanji in the core.
_APPROX_NUMERIC_PREFIXES = {"約", "計", "総"}
_PRODUCTIVE_COMPOUND_V2 = frozenset(COMPOUND_VERB_V2_GODAN + COMPOUND_VERB_V2_ICHIDAN)
_PRODUCTIVE_ICHIDAN_COMPOUND_V2 = frozenset(COMPOUND_VERB_V2_ICHIDAN)
_NOMINALIZING_PARTICLES = frozenset({"を", "は", "が", "の", "に", "で", "へ", "と", "も"})
_KANA_NUMBER_COUNTERS = tuple(
    sorted((stem + suffix for stem in KANA_NUMBER_STEMS for suffix in KANA_COUNTER_SUFFIXES), key=len, reverse=True)
)
_FIXED_FUNCTION_SEARCH_UNITS = tuple(sorted(FIXED_FUNCTION_SEARCH_UNITS, key=len, reverse=True))
_FIXED_INFLECTED_FUNCTION_UNITS = tuple(sorted(FIXED_INFLECTED_FUNCTION_UNITS, key=len, reverse=True))


def apply_suzume_merge(tokens: list[dict], text: str) -> tuple[list[dict], str | None]:
    """Apply Suzume merge rules to MeCab tokens.

    Returns:
        Tuple of (merged tokens, applied rule name or None).
    """
    result: list[dict] = []
    i = 0
    applied_rule: str | None = None

    while i < len(tokens):
        t = tokens[i]
        merged = False

        # Calculate position in text
        pos_in_text = sum(len(tokens[k].get("surface", "")) for k in range(i))
        remaining = text[pos_in_text:] if pos_in_text < len(text) else ""

        # A closed subsidiary inflection may be split into arbitrary pieces
        # by the reference dictionary (い+た+だけ+ませ).  Consume the exact
        # source span only when its next token is a licensed auxiliary follower,
        # before the generic V1+V2 compound rule can absorb the initial piece.
        if not merged:
            fixed_form = next((form for form in _FIXED_INFLECTED_FUNCTION_UNITS if remaining.startswith(form)), "")
            if fixed_form:
                consumed = ""
                j = i
                while j < len(tokens) and len(consumed) < len(fixed_form):
                    consumed += tokens[j].get("surface", "")
                    j += 1
                pos, lemma, followers = FIXED_INFLECTED_FUNCTION_UNITS[fixed_form]
                following = tokens[j].get("surface", "") if j < len(tokens) else ""
                if consumed == fixed_form and any(following.startswith(follower) for follower in followers):
                    result.append({"surface": fixed_form, "pos": pos, "lemma": lemma})
                    i = j
                    merged = True
                    if applied_rule is None:
                        applied_rule = "fixed-inflected-function-unit"

        # Closed function words and formal nouns remain one search unit even
        # when the reference dictionary splits them into homographic pieces
        # (そん+なら, お+それ, が+てら). Consume an exact source-text span so the
        # rule never absorbs a partial token or crosses the fixed word's end.
        if not merged:
            fixed_word = next((word for word in _FIXED_FUNCTION_SEARCH_UNITS if remaining.startswith(word)), "")
            if fixed_word:
                consumed = ""
                j = i
                while j < len(tokens) and len(consumed) < len(fixed_word):
                    consumed += tokens[j].get("surface", "")
                    j += 1
                if consumed == fixed_word:
                    result.append(
                        {
                            "surface": fixed_word,
                            "pos": FIXED_FUNCTION_SEARCH_UNITS[fixed_word],
                            "lemma": FIXED_FUNCTION_LEMMAS.get(fixed_word, fixed_word),
                        }
                    )
                    i = j
                    merged = True
                    if applied_rule is None:
                        applied_rule = "fixed-function-search-unit"

        # 0. Kana number + counter.  Raw MeCab can split these closed quantity
        # readings at arbitrary syllables (い|ちまい, よ|ん|に|ん), so consume
        # exactly one finite L1 composition by source-text length.
        if not merged:
            kana_quantity = next((quantity for quantity in _KANA_NUMBER_COUNTERS if remaining.startswith(quantity)), "")
            if kana_quantity:
                consumed = ""
                j = i
                while j < len(tokens) and len(consumed) < len(kana_quantity):
                    consumed += tokens[j].get("surface", "")
                    j += 1
                if consumed == kana_quantity:
                    result.append({"surface": kana_quantity, "pos": "名詞", "pos_sub1": "数", "lemma": kana_quantity})
                    i = j
                    merged = True
                    if applied_rule is None:
                        applied_rule = "kana-number+unit"

        # 1. Full date pattern
        if not merged:
            m = regex.match(r"^(\d+年\d+月\d+日)", remaining)
            if m:
                date = m.group(1)
                length = 0
                j = i
                while j < len(tokens) and length < len(date):
                    length += len(tokens[j].get("surface", ""))
                    j += 1
                if length == len(date):
                    result.append({"surface": date, "pos": "名詞", "lemma": date})
                    i = j
                    merged = True
                    if applied_rule is None:
                        applied_rule = "date"

        # 1.3. お + family/honorific terms
        if not merged and t.get("surface") == "お" and "接頭詞" in t.get("pos", "") and i + 1 < len(tokens):
            next_surface = tokens[i + 1].get("surface", "")
            if next_surface in FAMILY_TERMS:
                combined = "お" + next_surface
                result.append({"surface": combined, "pos": "名詞", "lemma": combined})
                i += 2
                merged = True
                if applied_rule is None:
                    applied_rule = "family-merge"

        # 1.4. Fixed temporal adverb split by the reference analyzer.
        if not merged and t.get("surface") == "かね" and i + 1 < len(tokens):
            if tokens[i + 1].get("surface") == "て":
                result.append({"surface": "かねて", "pos": "副詞", "lemma": "かねて"})
                i += 2
                merged = True
                if applied_rule is None:
                    applied_rule = "kanete-merge"

        if not merged and t.get("surface") == "より" and result:
            if result[-1].get("surface") == "かねて":
                result.append({"surface": "より", "pos": "助詞", "lemma": "より"})
                i += 1
                merged = True

        # 1.5. URL pattern
        if not merged:
            m = regex.match(r"^(https?://[a-zA-Z0-9\-._~:/?#\[\]@!$&'()*+,;=%]+)", remaining)
            if m:
                url = m.group(1)
                url = regex.sub(r"[.,)\]']+$", "", url)
                length = 0
                j = i
                while j < len(tokens) and length < len(url):
                    length += len(tokens[j].get("surface", ""))
                    j += 1
                if length == len(url):
                    result.append({"surface": url, "pos": "名詞", "lemma": url})
                    i = j
                    merged = True
                    if applied_rule is None:
                        applied_rule = "url"

        # 1c. Mixed-script reduplication (一つひとつ, 一人ひとり). Writing the same
        # word twice in two scripts is how the distributive adverbial is spelled,
        # and the two halves are one search unit. The reference analyzer merges
        # only the pairs its lexicon happens to list, which is why 一つひとつ確認する
        # comes back whole while 一つひとつ調べる comes back split. Matching on the
        # reading is what makes the pattern general; requiring the surfaces to
        # differ is what keeps an identical repetition (二つ二つに分ける) out, where
        # two separate quantities are a live reading. A reduplication written
        # entirely in kanji (一人一人, 一件一件) is already covered by the
        # number+counter rule below.
        if not merged and t.get("pos") == "名詞" and i + 1 < len(tokens):
            nxt = tokens[i + 1]
            reading = t.get("reading", "")
            surface = t.get("surface", "")
            if reading and nxt.get("pos") == "名詞" and nxt.get("reading") == reading and nxt.get("surface") != surface:
                combined = surface + nxt.get("surface", "")
                result.append({"surface": combined, "pos": "名詞", "pos_sub1": "数", "lemma": combined})
                i += 2
                merged = True
                if applied_rule is None:
                    applied_rule = "mixed-script-reduplication"

        # 2. Number + counter/katakana
        if not merged and t.get("pos") == "名詞" and t.get("pos_sub1") == "数":
            j = i + 1
            combined = t.get("surface", "")
            while j < len(tokens):
                nxt = tokens[j]
                ns = nxt.get("surface", "")
                np = nxt.get("pos", "")
                ns1 = nxt.get("pos_sub1", "")
                ns2 = nxt.get("pos_sub2", "")

                is_counter = np == "名詞" and ns1 == "接尾" and ns2 == "助数詞"
                # The span marker 間 (名詞/接尾/一般) closes a month-counter duration
                # (三ヶ月+間 → 三ヶ月間), matching the single-token 年間/時間/週間/分間 that
                # MeCab already emits whole. Without this ヶ月 splits from 間, which then
                # folds rightward into a following noun (三ヶ月|間勉強 non-word).
                is_span_kan = (
                    ns == "間"
                    and np == "名詞"
                    and ns1 == "接尾"
                    and regex.search(r"(?:ヶ月|ケ月|カ月|ヵ月|箇月|か月)$", combined)
                )
                is_calendar_month = ns == "月" and regex.match(r"^(?:1[0-2]|[1-9])$", combined)
                is_katakana_noun = np == "名詞" and regex.match(r"^[\u30A0-\u30FF]+$", ns)
                is_chuu_suffix = ns == "中" and np == "名詞" and ns1 == "接尾"
                is_me_suffix = ns == "目" and np == "名詞" and ns1 == "接尾"
                is_large_unit = np == "名詞" and ns1 == "数" and ns in ("万", "億", "兆")
                is_number_after_large = combined.endswith(("万", "億", "兆")) and np == "名詞" and ns1 == "数"
                is_number_after_decimal = combined.endswith(".") and np == "名詞" and ns1 == "数"
                is_counter_aux = ns == "つ" and np in ("助動詞", "動詞")
                is_percent = ns == "%"
                is_decimal = ns == "."
                is_consecutive_number = np == "名詞" and ns1 == "数" and regex.match(r"^[0-9０-９]+$", ns)
                is_kanji_number_run = (
                    np == "名詞" and ns1 == "数" and regex.match(r"^[一二三四五六七八九十百千万億兆〇零]+$", ns)
                )
                is_alpha_unit = regex.match(r"^[A-Za-z]+$", ns) and np == "名詞"

                if any(
                    [
                        is_counter,
                        is_span_kan,
                        is_calendar_month,
                        is_katakana_noun,
                        is_chuu_suffix,
                        is_me_suffix,
                        is_large_unit,
                        is_number_after_large,
                        is_number_after_decimal,
                        is_counter_aux,
                        is_percent,
                        is_decimal,
                        is_alpha_unit,
                        is_consecutive_number,
                        is_kanji_number_run,
                    ]
                ):
                    combined += ns
                    j += 1
                    if any([is_katakana_noun, is_chuu_suffix, is_me_suffix, is_counter_aux, is_percent, is_alpha_unit]):
                        break
                else:
                    break
            if j > i + 1:
                result.append({"surface": combined, "pos": "名詞", "pos_sub1": "数", "lemma": combined})
                i = j
                merged = True
                if applied_rule is None:
                    applied_rule = "number+unit"

        # 2a2. Address number pattern
        if not merged and t.get("pos") == "名詞" and t.get("pos_sub1") == "数":
            j = i + 1
            combined = t.get("surface", "")
            has_hyphen = False
            while j + 1 < len(tokens):
                hyphen = tokens[j]
                next_num = tokens[j + 1]
                if hyphen.get("surface") == "-" and next_num.get("pos") == "名詞" and next_num.get("pos_sub1") == "数":
                    combined += "-" + next_num.get("surface", "")
                    j += 2
                    has_hyphen = True
                else:
                    break
            if has_hyphen:
                result.append({"surface": combined, "pos": "名詞", "pos_sub1": "数", "lemma": combined})
                i = j
                merged = True
                if applied_rule is None:
                    applied_rule = "address-number"

        # 2b. Prefix + number (第一, 第二, etc.)
        # Only merge numbers, not counters — 第一+毛 should stay split
        # An approximation prefix (約/計/総) modifies the whole quantity and splits off
        # the number+counter (約|二時間, 計|五名), unlike an ordinal prefix (第) that binds
        # to its number (第三十四|回). Skip approximation prefixes here so the number binds
        # right to its counter via the number+counter rule.
        if (
            not merged
            and t.get("pos") == "接頭詞"
            and t.get("pos_sub1") == "数接続"
            and t.get("surface", "") not in _APPROX_NUMERIC_PREFIXES
        ):
            j = i + 1
            combined = t.get("surface", "")
            while j < len(tokens):
                nxt = tokens[j]
                is_number = nxt.get("pos") == "名詞" and nxt.get("pos_sub1") == "数"
                if is_number:
                    combined += nxt.get("surface", "")
                    j += 1
                else:
                    break
            if j > i + 1:
                result.append({"surface": combined, "pos": "名詞", "pos_sub1": "数", "lemma": combined})
                i = j
                merged = True
                if applied_rule is None:
                    applied_rule = "number+unit"

        # 2c. Noun + 書/誌 suffix
        if not merged and t.get("pos") == "名詞" and i + 1 < len(tokens):
            nxt = tokens[i + 1]
            if nxt.get("surface", "") in ("書", "誌") and nxt.get("pos") == "名詞" and nxt.get("pos_sub1") == "接尾":
                combined = t.get("surface", "") + nxt["surface"]
                result.append({"surface": combined, "pos": "名詞", "lemma": combined})
                i += 2
                merged = True
                if applied_rule is None:
                    applied_rule = "noun+suffix-char"

        # 2c2. Noun + productive search-unit suffix
        if not merged and t.get("pos") == "名詞" and i + 1 < len(tokens):
            nxt = tokens[i + 1]
            if (
                nxt.get("surface", "") in ("時", "率", "性", "長")
                and nxt.get("pos") == "名詞"
                and nxt.get("pos_sub1") == "接尾"
            ):
                combined = t.get("surface", "") + nxt["surface"]
                result.append({"surface": combined, "pos": "名詞", "lemma": combined})
                i += 2
                merged = True
                if applied_rule is None:
                    applied_rule = "noun+suffix"

        # 2c3. Version number
        if not merged and t.get("surface", "") in ("v", "V") and i + 1 < len(tokens):
            j = i + 1
            combined = t["surface"]
            while j < len(tokens):
                ns = tokens[j].get("surface", "")
                if regex.match(r"^\d+$", ns) or ns == ".":
                    combined += ns
                    j += 1
                else:
                    break
            if j > i + 1:
                result.append({"surface": combined, "pos": "名詞", "lemma": combined})
                i = j
                merged = True
                if applied_rule is None:
                    applied_rule = "version"

        # 2c4. Brand + number
        if not merged and regex.match(r"^[A-Za-z]+$", t.get("surface", "")) and t.get("pos") == "名詞":
            if i + 1 < len(tokens):
                nxt = tokens[i + 1]
                ns = nxt.get("surface", "")
                if regex.match(r"^\d+$", ns) and nxt.get("pos") == "名詞":
                    combined = t["surface"] + ns
                    result.append({"surface": combined, "pos": "名詞", "lemma": combined})
                    i += 2
                    merged = True
                    if applied_rule is None:
                        applied_rule = "brand+number"

        # 2d. Prefix + Noun (kanji only)
        # Suzume design: 御 is a productive prefix that always splits off
        # (御 + 尽力, 御 + 挨拶, 御 + 協力). Skip merge for 御 prefix.
        if (
            not merged
            and t.get("pos") == "接頭詞"
            and t.get("pos_sub1") == "名詞接続"
            and t.get("surface", "") != "御"
            and i + 1 < len(tokens)
        ):
            nxt = tokens[i + 1]
            # A na-adjective stem heads a predicate rather than joining a
            # compound, so the prefix stays a separate modifier there (超|簡単,
            # 超|重要) while a plain noun host still yields one search unit
            # (超高速, 超大型).
            if nxt.get("pos") == "名詞" and nxt.get("pos_sub1") != "形容動詞語幹":
                combined = t.get("surface", "") + nxt.get("surface", "")
                # A temporal prefix heads a temporal noun, so only a temporal unit
                # continues it (今週, 今度, 毎時). Before an ordinary noun the prefix
                # is the adverbial 今 and the noun is its own word (今|紙, 今|水).
                temporal_break = (
                    t.get("surface", "") in TEMPORAL_PREFIX_KANJI
                    and nxt.get("surface", "")[:1] not in TEMPORAL_COMPOUND_UNITS
                )
                if not temporal_break and regex.match(r"^[\p{Han}]+$", combined):
                    result.append({"surface": combined, "pos": "名詞", "lemma": combined})
                    i += 2
                    merged = True
                    if applied_rule is None:
                        applied_rule = "prefix+noun"

        # 3. Nai-adjective merge
        if not merged:
            for adj in NAI_ADJECTIVES:
                if remaining.startswith(adj):
                    length = 0
                    j = i
                    while j < len(tokens) and length < len(adj):
                        length += len(tokens[j].get("surface", ""))
                        j += 1
                    if length == len(adj):
                        result.append({"surface": adj, "pos": "形容詞", "lemma": adj})
                        i = j
                        merged = True
                        if applied_rule is None:
                            applied_rule = "nai-adjective"
                        break

        # 4. Elongated adjective
        if not merged and t.get("pos") == "形容詞" and t.get("conj_form") == "ガル接続":
            j = i + 1
            if j < len(tokens) and tokens[j].get("surface") == "ー":
                combined = t.get("surface", "") + "ー"
                lemma = t.get("lemma") or (t.get("surface", "") + "い")
                j += 1
                if j < len(tokens):
                    ns = tokens[j].get("surface", "")
                    if ns == "い":
                        combined += "い"
                        j += 1
                    elif regex.match(r"^い(ね|よ|な|わ|ぞ|さ|か|の|けど)$", ns):
                        particle = ns[1:]
                        combined += "い"
                        result.append({"surface": combined, "pos": "形容詞", "lemma": lemma})
                        result.append({"surface": particle, "pos": "助詞", "lemma": particle})
                        i = j + 1
                        merged = True
                        if applied_rule is None:
                            applied_rule = "elongated-adjective"
                if not merged:
                    result.append({"surface": combined, "pos": "形容詞", "lemma": lemma})
                    i = j
                    merged = True
                    if applied_rule is None:
                        applied_rule = "elongated-adjective"

        # 4b. Vowel repetition: verb + repeated う (2+)
        if not merged and t.get("pos") == "動詞":
            j = i + 1
            combined = t.get("surface", "")
            lemma = t.get("lemma") or t.get("surface", "")
            u_count = 0
            while j < len(tokens):
                nxt = tokens[j]
                if nxt.get("surface") == "う" and nxt.get("pos") == "助動詞":
                    combined += "う"
                    u_count += 1
                    j += 1
                else:
                    break
            if u_count >= 2:
                result.append({"surface": combined, "pos": "動詞", "lemma": lemma})
                i = j
                merged = True
                if applied_rule is None:
                    applied_rule = "vowel-repeat"

        # 4c. Emphatic sokuon in past tense
        if not merged and t.get("pos") == "動詞" and "連用" in (t.get("conj_form") or ""):
            j = i + 1
            if j < len(tokens) and tokens[j].get("surface") == "たっ":
                combined = t.get("surface", "") + "たっ"
                lemma = t.get("lemma") or t.get("surface", "")
                result.append({"surface": combined, "pos": "動詞", "lemma": lemma})
                i = j + 1
                merged = True
                if applied_rule is None:
                    applied_rule = "emphatic-sokuon"

        # 4d. Adjective vowel repetition
        if not merged and t.get("pos") == "形容詞" and t.get("surface", "").endswith("い"):
            j = i + 1
            if j < len(tokens):
                nxt = tokens[j]
                if nxt.get("surface") == "いい" and nxt.get("pos") == "形容詞":
                    combined = t.get("surface", "") + "いい"
                    lemma = t.get("lemma") or t.get("surface", "")
                    result.append({"surface": combined, "pos": "形容詞", "lemma": lemma})
                    i = j + 1
                    merged = True
                    if applied_rule is None:
                        applied_rule = "vowel-repeat"

        # 4d-2. Nominal host + productive adjective suffix
        # くさい derives an adjective from its host instead of predicating over
        # a separate preceding word, so the two form one search unit. Only a
        # free nominal can be a host: after a particle, an adverb or a
        # determiner the adjective is the predicate and keeps its own token
        # (この魚は|くさい, ちょっと|くさい, その|くさい匂い).
        if (
            not merged
            and t.get("pos") == "名詞"
            and t.get("pos_sub1") not in ("非自立", "代名詞")
            and i + 1 < len(tokens)
        ):
            nxt = tokens[i + 1]
            if nxt.get("pos") == "形容詞" and nxt.get("lemma") in DERIVED_ADJECTIVE_SUFFIX_LEMMAS:
                host = t.get("surface", "")
                result.append(
                    {
                        "surface": host + nxt.get("surface", ""),
                        "pos": "形容詞",
                        "lemma": host + (nxt.get("lemma") or ""),
                    }
                )
                i += 2
                merged = True
                if applied_rule is None:
                    applied_rule = "nominal+derived-adjective"

        # 4e. Prolonged sound mark (ー) merge
        # Merge ー with preceding token, consecutive ーs reduce to one
        # e.g., あの + ー → あのー, あの + ーー → あのー
        if not merged and i + 1 < len(tokens):
            next_surface = tokens[i + 1].get("surface", "")
            if regex.match(r"^ー+$", next_surface):
                combined = t.get("surface", "") + "ー"
                lemma = t.get("lemma") or t.get("surface", "")
                j = i + 2
                # Skip any additional ー-only tokens
                while j < len(tokens) and regex.match(r"^ー+$", tokens[j].get("surface", "")):
                    j += 1
                result.append({"surface": combined, "pos": t.get("pos", ""), "lemma": lemma})
                i = j
                merged = True
                if applied_rule is None:
                    applied_rule = "prolonged-sound-merge"

        # 5. タリ活用副詞
        if not merged:
            derived_tari = regex.match(r"^\p{Han}+然と", remaining)
            if derived_tari is not None:
                adverb = derived_tari.group(0)
                previous_surface = result[-1].get("surface", "") if result else ""
                following_surface = remaining[len(adverb) : len(adverb) + 1]
                if previous_surface == "の" and following_surface == "は":
                    derived_tari = None
            if derived_tari is not None:
                adverb = derived_tari.group(0)
                length = 0
                j = i
                while j < len(tokens) and length < len(adverb):
                    length += len(tokens[j].get("surface", ""))
                    j += 1
                if length == len(adverb):
                    result.append({"surface": adverb, "pos": "副詞", "lemma": adverb[:-1]})
                    i = j
                    merged = True
                    if applied_rule is None:
                        applied_rule = "tari-adverb"

        if not merged:
            for stem in TARI_ADVERB_STEMS:
                adverb = stem + "と"
                if remaining.startswith(adverb):
                    length = 0
                    j = i
                    while j < len(tokens) and length < len(adverb):
                        length += len(tokens[j].get("surface", ""))
                        j += 1
                    if length == len(adverb):
                        result.append({"surface": adverb, "pos": "副詞", "lemma": stem})
                        i = j
                        merged = True
                        if applied_rule is None:
                            applied_rule = "tari-adverb"
                        break

        # 5a. Verb renyokei + 会
        if not merged and t.get("pos") == "動詞" and t.get("conj_form") == "連用形":
            j = i + 1
            if j < len(tokens):
                nxt = tokens[j]
                if nxt.get("surface") == "会" and nxt.get("pos") == "名詞" and nxt.get("pos_sub1") == "接尾":
                    combined = t.get("surface", "") + "会"
                    result.append({"surface": combined, "pos": "名詞", "lemma": combined})
                    i = j + 1
                    merged = True
                    if applied_rule is None:
                        applied_rule = "verb-renyokei+kai"

        # 5a'. Short simple verb renyokei + 方 (歩き方, やり方, 読み方, 言い方)
        # remains a lexical search unit. Longer compound continuatives retain
        # the productive suffix boundary (打ち合わせ + 方, 組み合わせ + 方).
        if not merged and t.get("pos") == "動詞" and t.get("conj_form") == "連用形":
            j = i + 1
            if j < len(tokens):
                nxt = tokens[j]
                if (
                    len(t.get("surface", "")) <= 2
                    and nxt.get("surface") == "方"
                    and nxt.get("pos") == "名詞"
                    and nxt.get("pos_sub1") == "接尾"
                ):
                    combined = t.get("surface", "") + "方"
                    result.append({"surface": combined, "pos": "名詞", "lemma": combined})
                    i = j + 1
                    merged = True
                    if applied_rule is None:
                        applied_rule = "verb-renyokei+kata"

        # 5a''. Noun + verb-forming derivational suffix (謎めく, 冗談めかす).
        # The suffix builds one godan paradigm with its host, so the whole
        # derived verb inflects as a unit and carries no internal boundary.
        # The reference dictionary merges only the entries it happens to hold
        # (春めい) and splits the rest.
        if not merged and t.get("pos") == "名詞" and t.get("pos_sub1") != "接尾" and i + 1 < len(tokens):
            nxt = tokens[i + 1]
            suffix_lemma = DERIVED_VERB_SUFFIX_FORMS.get(nxt.get("surface", ""))
            if suffix_lemma is not None and nxt.get("pos") == "動詞":
                combined = t.get("surface", "") + nxt.get("surface", "")
                result.append(
                    {
                        "surface": combined,
                        "pos": "動詞",
                        "lemma": t.get("surface", "") + suffix_lemma,
                    }
                )
                i += 2
                merged = True
                if applied_rule is None:
                    applied_rule = "noun+derived-verb-suffix"

        # 5b. Proper noun + region suffix
        # A destination suffix is one productive search unit with its nominal
        # host (東京行き, 学校行き).  The 接尾 feature supplies the boundary
        # evidence; no place-name or ordinary-noun list is needed.
        if not merged and t.get("pos") == "名詞" and i + 1 < len(tokens):
            nxt = tokens[i + 1]
            if nxt.get("surface") == "行き" and nxt.get("pos") == "名詞" and nxt.get("pos_sub1") == "接尾":
                combined = t.get("surface", "") + "行き"
                result.append({"surface": combined, "pos": "名詞", "lemma": combined})
                i += 2
                merged = True
                if applied_rule is None:
                    applied_rule = "destination-suffix"

        # 5c. Proper noun + region suffix
        if not merged and t.get("pos") == "名詞" and t.get("pos_sub1") == "固有名詞" and t.get("pos_sub2") == "地域":
            j = i + 1
            combined = t.get("surface", "")
            while j < len(tokens):
                nxt = tokens[j]
                ns = nxt.get("surface", "")
                if ns == "行き":
                    break
                is_proper_region = (
                    nxt.get("pos") == "名詞" and nxt.get("pos_sub1") == "固有名詞" and nxt.get("pos_sub2") == "地域"
                )
                is_region_suffix = (
                    nxt.get("pos") == "名詞" and nxt.get("pos_sub1") == "接尾" and nxt.get("pos_sub2") == "地域"
                )
                is_kanji_noun = (
                    nxt.get("pos") == "名詞" and regex.match(r"^[\p{Han}]+$", ns) and nxt.get("pos_sub1") != "接尾"
                )
                if is_proper_region or is_region_suffix or is_kanji_noun:
                    combined += ns
                    j += 1
                else:
                    break
            if j > i + 1:
                result.append({"surface": combined, "pos": "名詞", "pos_sub1": "固有名詞", "lemma": combined})
                i = j
                merged = True
                if applied_rule is None:
                    applied_rule = "proper-noun"

        # 6. Kanji compound
        is_mergeable_kanji = (
            regex.match(r"^[\p{Han}]+$", t.get("surface", ""))
            and t.get("pos") == "名詞"
            and t.get("pos_sub1", "") not in ("接尾", "固有名詞", "副詞可能")
        )
        if not merged and is_mergeable_kanji:
            j = i + 1
            combined = t.get("surface", "")
            while j < len(tokens):
                nxt = tokens[j]
                ns = nxt.get("surface", "")
                next_mergeable = (
                    regex.match(r"^[\p{Han}]+$", ns)
                    and nxt.get("pos") == "名詞"
                    and nxt.get("pos_sub1", "") not in ("接尾", "固有名詞", "形容動詞語幹", "副詞可能")
                )
                if not next_mergeable:
                    break
                # A number after a plain noun is a quantity boundary, not part of a kanji
                # compound (徒歩|五分, 定員|五名, 気温|三十度 — never 徒歩五|分). The number
                # binds right to its counter via the number+counter rule.
                if nxt.get("pos_sub1") == "数":
                    break
                combined += ns
                j += 1
            # Merge with common noun-forming suffixes
            if j < len(tokens):
                ns = tokens[j].get("surface", "")
                if ns in ("付け", "者", "人"):
                    combined += ns
                    j += 1
            if j > i + 1:
                result.append({"surface": combined, "pos": "名詞", "lemma": combined})
                i = j
                merged = True
                if applied_rule is None:
                    applied_rule = "kanji-compound"

        # 7. Katakana compound
        if not merged and regex.match(r"^[\u30A0-\u30FF]+$", t.get("surface", "")) and t.get("pos") == "名詞":
            j = i + 1
            combined = t.get("surface", "")
            while (
                j < len(tokens)
                and regex.match(r"^[\u30A0-\u30FF]+$", tokens[j].get("surface", ""))
                and tokens[j].get("pos") == "名詞"
            ):
                combined += tokens[j].get("surface", "")
                j += 1
            if j > i + 1:
                result.append({"surface": combined, "pos": "名詞", "lemma": combined})
                i = j
                merged = True
                if applied_rule is None:
                    applied_rule = "katakana-compound"

        # 7b. Alphabet + Katakana/Kanji compound
        if not merged and regex.match(r"^[A-Za-z]+$", t.get("surface", "")) and t.get("pos") == "名詞":
            j = i + 1
            combined = t.get("surface", "")
            while j < len(tokens):
                nxt = tokens[j]
                ns = nxt.get("surface", "")
                np = nxt.get("pos", "")
                is_katakana = regex.match(r"^[\u30A0-\u30FF]+$", ns) and np == "名詞"
                is_kanji = regex.match(r"^[\p{Han}]+$", ns) and np == "名詞"
                if is_katakana or is_kanji:
                    combined += ns
                    j += 1
                    break  # Only merge one following token
                else:
                    break
            if j > i + 1:
                result.append({"surface": combined, "pos": "名詞", "lemma": combined})
                i = j
                merged = True
                if applied_rule is None:
                    applied_rule = "alphabet-compound"

        # 7c. Snake_case identifier
        if not merged and regex.match(r"^[A-Za-z0-9]+$", t.get("surface", "")) and t.get("pos") == "名詞":
            j = i + 1
            combined = t.get("surface", "")
            found_underscore = False
            while j < len(tokens):
                nxt = tokens[j]
                if nxt.get("surface") == "_":
                    if j + 1 < len(tokens):
                        after = tokens[j + 1]
                        if regex.match(r"^[A-Za-z0-9]+$", after.get("surface", "")):
                            combined += "_" + after["surface"]
                            j += 2
                            found_underscore = True
                            continue
                    break
                else:
                    break
            if found_underscore:
                result.append({"surface": combined, "pos": "名詞", "lemma": combined})
                i = j
                merged = True
                if applied_rule is None:
                    applied_rule = "snake-case"

        # 7b. Mention pattern
        if not merged and t.get("surface") == "@":
            j = i + 1
            combined = "@"
            found_mention = False
            while j < len(tokens):
                ns = tokens[j].get("surface", "")
                if regex.match(r"^[A-Za-z0-9]+$", ns):
                    combined += ns
                    j += 1
                    found_mention = True
                elif ns == "_" and found_mention:
                    if j + 1 < len(tokens) and regex.match(r"^[A-Za-z0-9]+$", tokens[j + 1].get("surface", "")):
                        combined += "_"
                        j += 1
                    else:
                        break
                else:
                    break
            if found_mention:
                result.append({"surface": combined, "pos": "名詞", "lemma": combined})
                i = j
                merged = True
                if applied_rule is None:
                    applied_rule = "mention"

        # 7c. Hashtag pattern
        if not merged and t.get("surface") in ("#", "＃"):
            if i + 1 < len(tokens):
                ns = tokens[i + 1].get("surface", "")
                if regex.match(r"^[\p{Katakana}\p{Han}A-Za-z0-9_]+$", ns):
                    combined = t["surface"] + ns
                    result.append({"surface": combined, "pos": "名詞", "lemma": combined})
                    i += 2
                    merged = True
                    if applied_rule is None:
                        applied_rule = "hashtag"

        # 8. Colloquial pronouns
        if not merged:
            for pronoun in COLLOQUIAL_PRONOUNS:
                if remaining.startswith(pronoun):
                    length = 0
                    j = i
                    while j < len(tokens) and length < len(pronoun):
                        length += len(tokens[j].get("surface", ""))
                        j += 1
                    if length == len(pronoun):
                        result.append({"surface": pronoun, "pos": "代名詞", "lemma": pronoun})
                        i = j
                        merged = True
                        if applied_rule is None:
                            applied_rule = "colloquial-pronoun"
                        break

        # 8b. Character speech: にゃ+ん -> にゃん
        if not merged and t.get("surface") == "にゃ":
            if i + 1 < len(tokens) and tokens[i + 1].get("surface") == "ん":
                result.append({"surface": "にゃん", "pos": "助詞", "lemma": "にゃん"})
                i += 2
                merged = True
                if applied_rule is None:
                    applied_rule = "character-speech"

        # 9. Compound verbs
        following_source = remaining[len(t.get("surface", "")) :]
        begins_fixed_subsidiary = any(following_source.startswith(form) for form in _FIXED_INFLECTED_FUNCTION_UNITS)
        v1_surface = t.get("surface", "")
        v1_verb_renyokei = t.get("pos") == "動詞" and "連用" in (t.get("conj_form") or "")
        # MeCab frequently lexicalizes a bare renyokei as a noun (座り, 入り).
        # Reconstructing its base is a productive morphology check; the closed
        # V2 class below prevents this from becoming an unrestricted noun+verb
        # merge rule.
        # A dependent noun keeps its own boundary: 〜たきり is the formal noun, not
        # a nominalized 切り, however well it reconstructs as one.
        v1_nominal_renyokei = (
            t.get("pos") == "名詞" and t.get("pos_sub1") != "非自立" and base_from_renyokei(v1_surface) is not None
        )
        if not merged and not begins_fixed_subsidiary and (v1_verb_renyokei or v1_nominal_renyokei):
            j = i + 1
            if j < len(tokens):
                nxt = tokens[j]
                if nxt.get("pos") == "動詞" and (nxt.get("lemma") or nxt.get("surface", "")) != "でる":
                    next_lemma = nxt.get("lemma") or nxt.get("surface", "")
                    v2_base = ""
                    for v2 in COMPOUND_VERB_V2_GODAN + COMPOUND_VERB_V2_ICHIDAN:
                        if next_lemma == v2:
                            v2_base = v2
                            break
                    v1_is_suru = (t.get("lemma") or v1_surface) == "する"
                    restricted = COMPOUND_VERB_V2_NOT_AFTER_SURU if v1_is_suru else COMPOUND_VERB_V2_SURU_ONLY
                    if v2_base in restricted:
                        v2_base = ""
                    if v2_base:
                        combined = t.get("surface", "") + nxt.get("surface", "")
                        compound_lemma = t.get("surface", "") + v2_base
                        result.append({"surface": combined, "pos": "動詞", "lemma": compound_lemma})
                        i = j + 1
                        merged = True
                        if applied_rule is None:
                            applied_rule = "compound-verb"

        # 9a. A productive V1+V2 continuative directly nominalized by a
        # particle is one deverbal compound search unit. MeCab often tags V2
        # as a noun in this context (押し/下げ/を), so the finite-verb rule above
        # cannot see it. Reconstruct V2 through the conjugation table and
        # require both the closed V2 class and the nominalizing follower.
        v1_renyokei = t.get("pos") == "動詞" and "連用" in (t.get("conj_form") or "")
        v1_nominal_renyokei = t.get("pos") == "名詞" and base_from_renyokei(v1_surface) is not None
        if not merged and (v1_renyokei or v1_nominal_renyokei):
            if i + 2 < len(tokens):
                nxt = tokens[i + 1]
                follower = tokens[i + 2]
                v2_readings = bases_from_renyokei(nxt.get("surface", ""))
                v2_base = next((base for base in v2_readings if base in _PRODUCTIVE_COMPOUND_V2), None)
                nominalizing_particle = (
                    follower.get("pos") == "助詞" and follower.get("surface") in _NOMINALIZING_PARTICLES
                )
                # A bound or verbal-noun V2 reading attaches to whatever stem
                # precedes it (仕立て+直し, 引き+寄せ); a free nominal V2 attaches
                # only behind an unambiguous verb continuative (送り+届け). Two free
                # nominals side by side are coordinated, not compounded, and keep
                # their boundary (上がり + 下がり).
                v2_is_bound_reading = nxt.get("pos") == "接尾辞" or nxt.get("pos_sub1") in {"接尾", "サ変接続"}
                # A し-final stem is the continuative of a Godan-sa verb (出し, 押し)
                # however MeCab tags it, so it counts as a verbal head as well.
                v1_is_verbal = v1_renyokei or v1_surface.endswith("し")
                if (
                    nxt.get("pos") in {"名詞", "接尾辞"}
                    and v2_base is not None
                    and (v2_is_bound_reading or v1_is_verbal)
                    and nominalizing_particle
                ):
                    combined = v1_surface + nxt.get("surface", "")
                    result.append({"surface": combined, "pos": "名詞", "lemma": combined})
                    i += 2
                    merged = True
                    if applied_rule is None:
                        applied_rule = "compound-renyokei-nominal"

        # 9b. Lexicalized こもる compounds MeCab fails to merge because the
        # renyokei prefix (引き) is highly productive and tagged as a noun.
        # As a tokenizer, 引きこもり/引きこもる is a single search unit; treat こもる
        # as V2 even when MeCab tags the preceding renyokei form as a noun.
        if not merged and t.get("pos") == "名詞":
            j = i + 1
            if j < len(tokens):
                nxt = tokens[j]
                next_lemma = nxt.get("lemma") or nxt.get("surface", "")
                if nxt.get("pos") == "動詞" and next_lemma in ("こもる", "籠る", "籠もる"):
                    combined = t.get("surface", "") + nxt.get("surface", "")
                    result.append({"surface": combined, "pos": "動詞", "lemma": t.get("surface", "") + "こもる"})
                    i = j + 1
                    merged = True
                    if applied_rule is None:
                        applied_rule = "komoru-compound"

        # 10. Lexicalized hiragana words
        if not merged:
            for word in sorted(HIRAGANA_COMPOUNDS.keys(), key=len, reverse=True):
                if remaining.startswith(word):
                    length = 0
                    j = i
                    consumed = ""
                    while j < len(tokens) and length < len(word):
                        token_surface = tokens[j].get("surface", "")
                        consumed += token_surface
                        length += len(token_surface)
                        j += 1
                    residual = consumed[len(word) :]
                    if consumed.startswith(word) and (not residual or residual in _NOMINALIZING_PARTICLES):
                        result.append({"surface": word, "pos": HIRAGANA_COMPOUNDS[word], "lemma": word})
                        if residual:
                            result.append({"surface": residual, "pos": "助詞", "lemma": residual})
                        i = j
                        merged = True
                        if applied_rule is None:
                            applied_rule = "hiragana-compound"
                        break

        # 11. Colloquial intensifier めちゃ
        if not merged and t.get("surface") == "め" and t.get("pos") == "名詞":
            if i + 1 < len(tokens) and tokens[i + 1].get("surface") == "ちゃ":
                result.append({"surface": "めちゃ", "pos": "その他", "lemma": "めちゃ"})
                i += 2
                merged = True
                if applied_rule is None:
                    applied_rule = "mecha-merge"

        # AだのBだの coordinates with one particle repeated. The reference
        # analyzer lexicalizes it inside the sentence but falls back to the
        # copula plus の at the end, so the same morpheme comes out two
        # different ways in a single coordination. An earlier だの in the same
        # sentence is what identifies the frame.
        if (
            not merged
            and t.get("surface") == "だ"
            and i + 1 < len(tokens)
            and tokens[i + 1].get("surface") == "の"
            and any(prior.get("surface") == "だの" for prior in result)
        ):
            result.append({"surface": "だの", "pos": "助詞", "lemma": "だの"})
            i += 2
            merged = True
            if applied_rule is None:
                applied_rule = "dano-coordination"

        # 11b. ず+に -> ずに
        if not merged and t.get("surface") == "ず" and t.get("pos") == "助動詞":
            if i + 1 < len(tokens) and tokens[i + 1].get("surface") == "に":
                result.append({"surface": "ずに", "pos": "助動詞", "lemma": "ず"})
                i += 2
                merged = True
                if applied_rule is None:
                    applied_rule = "zu-ni-merge"

        # 11bb. Productive renyokei + たて suffix.  MeCab sometimes reads
        # the closed freshness suffix as the unrelated past auxiliary + te
        # particle (e.g. でき+た+て).  The raw token stream still contains
        # punctuation, so doing this before symbol filtering cannot join across
        # sentence boundaries.
        if (
            not merged
            and t.get("pos") == "動詞"
            and i + 2 < len(tokens)
            and tokens[i + 1].get("surface") == "た"
            and tokens[i + 1].get("pos") == "助動詞"
            and tokens[i + 2].get("surface") == "て"
            and tokens[i + 2].get("pos") == "助詞"
        ):
            result.append(
                {
                    "surface": t.get("surface", ""),
                    "pos": "動詞",
                    "lemma": t.get("lemma") or base_from_renyokei(t.get("surface", "")) or t.get("surface", ""),
                }
            )
            result.append({"surface": "たて", "pos": "接尾辞", "pos_sub1": "接尾", "lemma": "たて"})
            i += 3
            merged = True
            if applied_rule is None:
                applied_rule = "productive-tate-suffix"

        # 11c. Resultative 〜てある retains the te-particle boundary.  MeCab
        # may emit an ichidan te-form as one token (並べて), while Suzume keeps
        # the productive verb stem + て + ある chain for its grammar model.
        #
        # Ending in て is not on its own evidence of a te-form: compound case
        # particles and lexical adverbs end the same way (について, 全て).  The
        # split is therefore only taken when the stem it would leave behind
        # actually names a verb, which is what a te-form always decomposes into.
        # Without that check the lemma is fabricated by appending る to whatever
        # precedes the て (についる, 全る).
        if (
            not merged
            and t.get("surface", "").endswith("て")
            and len(t.get("surface", "")) > 1
            and i + 1 < len(tokens)
            and tokens[i + 1].get("surface") in ("ある", "あっ", "あり")
        ):
            stem = t["surface"][:-1]
            lemma = t.get("lemma") or stem
            if lemma == t["surface"]:
                lemma = stem + "る"
            if _reads_as_one_verb(lemma):
                result.append({"surface": stem, "pos": "動詞", "lemma": lemma})
                result.append({"surface": "て", "pos": "助詞", "lemma": "て"})
                i += 1
                merged = True
                if applied_rule is None:
                    applied_rule = "te-aru-split"

        # No merge: pass through
        if not merged:
            lemma = t.get("lemma") or t.get("surface", "")
            lemma = FIXED_FUNCTION_LEMMAS.get(t.get("surface", ""), lemma)
            result.append(
                {
                    "surface": t.get("surface", ""),
                    "pos": t.get("pos", ""),
                    "pos_sub1": t.get("pos_sub1"),
                    "pos_sub2": t.get("pos_sub2"),
                    "conj_type": t.get("conj_type"),
                    "conj_form": t.get("conj_form"),
                    "lemma": lemma,
                }
            )
            i += 1

    # Post-process passes
    result = _postprocess_kamo(result, applied_rule)
    result, applied_rule = _postprocess_totomoni(result, applied_rule)
    result, applied_rule = _postprocess_noni(result, applied_rule)
    result, applied_rule = _postprocess_atode(result, applied_rule)
    _postprocess_epenthetic_sa(result)
    result, applied_rule = _postprocess_honorific_split(result, applied_rule)
    result, applied_rule = _postprocess_prefix_split(result, applied_rule)
    result, applied_rule = _postprocess_nde_split(result, applied_rule)
    result, applied_rule = _postprocess_filler_split(result, applied_rule)
    result, applied_rule = _postprocess_kuruwa(result, applied_rule)
    result, applied_rule = _postprocess_demo_copula(result, applied_rule)
    result, applied_rule = _postprocess_gamashii(result, applied_rule)
    result, applied_rule = _postprocess_adj_bungo(result, applied_rule)
    result, applied_rule = _postprocess_adj_kari(result, applied_rule)
    result, applied_rule = _postprocess_ha_row_godan(result, applied_rule)
    result, applied_rule = _postprocess_classical_mu(result, applied_rule)
    result, applied_rule = _postprocess_ku_nominalization(result, applied_rule)
    result, applied_rule = _postprocess_classical_shimu(result, applied_rule)
    result, applied_rule = _postprocess_izenkei_concessive(result, applied_rule)
    result, applied_rule = _postprocess_tomo_particle(result, applied_rule)
    result, applied_rule = _postprocess_bound_voiced_suffix(result, applied_rule)
    result, applied_rule = _postprocess_bound_suffix_noun_cell(result, applied_rule)
    result, applied_rule = _postprocess_kanji_merge(result, applied_rule)
    result, applied_rule = _postprocess_nickname_merge(result, applied_rule)
    result, applied_rule = _postprocess_search_unit_split(result, applied_rule)
    result, applied_rule = _postprocess_onomatopoeia_tto_merge(result, applied_rule)
    result, applied_rule = _postprocess_productive_mimetics(result, applied_rule)
    result, applied_rule = _postprocess_distributive_quantity(result, applied_rule)
    result, applied_rule = _postprocess_nominal_zukeru(result, applied_rule)
    result, applied_rule = _postprocess_ascii_joiner_merge(result, applied_rule)
    result, applied_rule = _postprocess_small_kana_head_merge(result, applied_rule)
    _postprocess_dialectal(result)

    return result, applied_rule
