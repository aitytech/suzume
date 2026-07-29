"""Split rules ported from SuzumeUtils.pm apply_suzume_split()."""

import regex

from .constants import (
    COMPOUND_VERB_V2_ICHIDAN,
    COPULAR_PREDICATE_HEADS,
    FIXED_FUNCTION_SEARCH_UNITS,
    FIXED_LEADING_SEARCH_UNITS,
    LITERARY_VOLITIONAL_PARTICLE_COMPOUNDS,
    NOUN_NAI_COMPOUND_ADJECTIVES,
    STATE_NOUN_SUFFIXES,
    TTARA_STEMS,
    TTEBA_STEMS,
    USER_DICT_COMPOUNDS,
)

_GODAN_RENYOKEI_TO_BASE: dict[str, str] = {
    "い": "う",
    "き": "く",
    "ぎ": "ぐ",
    "し": "す",
    "ち": "つ",
    "に": "ぬ",
    "び": "ぶ",
    "み": "む",
    "り": "る",
}
_GODAN_MIZENKEI_TO_BASE: dict[str, str] = {
    "わ": "う",
    "か": "く",
    "が": "ぐ",
    "さ": "す",
    "た": "つ",
    "な": "ぬ",
    "ば": "ぶ",
    "ま": "む",
    "ら": "る",
}
_ICHIDAN_RENYOKEI_ENDINGS = frozenset("えけげせぜてでねへべめれ")

# A case particle followed by an inflected lexical predicate that a reference
# dictionary lexicalizes as one 連語, inconsistently: the same surface is split
# in some contexts and kept whole in others.  The internal boundary is a real
# inflection boundary (もっ = 持つ continuative, すれ = する conditional), so it is
# always restored.
_LEXICALIZED_PREDICATE_COMPOUNDS: dict[str, tuple[dict, ...]] = {
    "をもって": (
        {"surface": "を", "pos": "助詞", "lemma": "を"},
        {"surface": "もっ", "pos": "動詞", "lemma": "もつ"},
        {"surface": "て", "pos": "助詞", "lemma": "て"},
    ),
    "とすれば": (
        {"surface": "と", "pos": "助詞", "lemma": "と"},
        {"surface": "すれ", "pos": "動詞", "lemma": "する"},
        {"surface": "ば", "pos": "助詞", "lemma": "ば"},
    ),
}
_COMPLETIVE_TSUKUSU_FORMS = frozenset({"尽くさ", "尽くし", "尽くす", "尽くせ", "尽くそ"})


def _emit_split(
    result: list[dict],
    split_tokens: tuple[dict, ...],
    applied_rule: str | None,
    rule: str,
) -> str:
    """Emit a complete split and retain the first rule name for reporting."""
    result.extend(split_tokens)
    return applied_rule or rule


def base_from_renyokei(stem: str) -> str | None:
    """Reconstruct a dictionary form from a productive renyokei surface."""
    if not stem:
        return None
    ending = stem[-1]
    if ending in _GODAN_RENYOKEI_TO_BASE:
        return stem[:-1] + _GODAN_RENYOKEI_TO_BASE[ending]
    if ending in _ICHIDAN_RENYOKEI_ENDINGS:
        return stem + "る"
    return None


def bases_from_renyokei(stem: str) -> tuple[str, ...]:
    """Every dictionary form a renyokei surface can reconstruct to.

    An i-row ending belongs to both conjugation classes (落ち is 落つ or 落ちる,
    起き is 起く or 起きる), so a caller that resolves the reading against a closed
    verb class needs both readings rather than the Godan one alone.
    """
    godan = base_from_renyokei(stem)
    if godan is None:
        return ()
    if stem[-1] in _GODAN_RENYOKEI_TO_BASE:
        return (godan, stem + "る")
    return (godan,)


def base_from_mizenkei(stem: str) -> str | None:
    """Reconstruct a Godan dictionary form from an a-row irrealis stem."""
    if not stem:
        return None
    ending = _GODAN_MIZENKEI_TO_BASE.get(stem[-1])
    return stem[:-1] + ending if ending is not None else None


def apply_suzume_split(tokens: list[dict]) -> tuple[list[dict], str | None]:
    """Apply Suzume split rules to MeCab tokens.

    Returns:
        Tuple of (split tokens, applied rule name or None).
    """
    result: list[dict] = []
    applied_rule: str | None = None

    for token_index, t in enumerate(tokens):
        surface = t.get("surface", "")

        # IPADIC lexicalizes this entire interrogative nominal phrase as an
        # adverb. Suzume keeps its productive pronoun/particle/noun boundaries;
        # the final か is the indefinite adverbial particle.
        if t.get("pos") == "副詞" and surface == "いつの間にか":
            result.extend(
                [
                    {"surface": "いつ", "pos": "名詞", "pos_sub1": "代名詞", "lemma": "いつ"},
                    {"surface": "の", "pos": "助詞", "lemma": "の"},
                    {"surface": "間", "pos": "名詞", "lemma": "間"},
                    {"surface": "に", "pos": "助詞", "lemma": "に"},
                    {"surface": "か", "pos": "助詞", "lemma": "か"},
                ]
            )
            if applied_rule is None:
                applied_rule = "interrogative-nominal-adverb-boundary"
            continue

        # 従う is intransitive and reaches its complement through に, so a bare
        # noun sitting directly in front of it cannot be that complement. What
        # the span actually spells is the sa-hen continuative plus the
        # desiderative (確認+し+たがっ+て+いる). The reference analyzer already
        # reads it that way in every cell whose surface does not collide with
        # an onbin form of 従う (確認+し+たがる).
        if (
            t.get("pos") == "動詞"
            and t.get("lemma") in ("従う", "したがう")
            and surface.startswith("し")
            and len(surface) > 1
            and token_index > 0
            and tokens[token_index - 1].get("pos") == "名詞"
        ):
            result.extend(
                [
                    {"surface": "し", "pos": "動詞", "lemma": "する"},
                    {"surface": surface[1:], "pos": "助動詞", "lemma": "たがる"},
                ]
            )
            if applied_rule is None:
                applied_rule = "sahen-desiderative-boundary"
            continue

        # ます is an auxiliary, so it is never part of a particle. A compound
        # particle lexicalized together with its polite form (に関しまして,
        # に際しまして) hides the auxiliary boundary that the plain form
        # (に関し, に際し) keeps, which makes the same closed unit tokenize two
        # different ways depending only on politeness.
        if t.get("pos") == "助詞" and surface.endswith("まして") and len(surface) > 3:
            result.extend(
                [
                    {"surface": surface[:-3], "pos": "助詞", "lemma": surface[:-3]},
                    {"surface": "まし", "pos": "助動詞", "lemma": "ます"},
                    {"surface": "て", "pos": "助詞", "lemma": "て"},
                ]
            )
            if applied_rule is None:
                applied_rule = "polite-compound-particle-boundary"
            continue

        lexicalized_compound = _LEXICALIZED_PREDICATE_COMPOUNDS.get(surface)
        if lexicalized_compound is not None and t.get("pos") in ("助詞", "接続詞"):
            result.extend(dict(part) for part in lexicalized_compound)
            if applied_rule is None:
                applied_rule = "lexicalized-particle-predicate-boundary"
            continue

        # Productive negative auxiliaries keep their boundary even when a
        # reference dictionary lexicalizes the entire compound.  Restrict the
        # reconstruction to the closed V2 class, leaving ordinary lexical
        # adjectives ending in ない untouched.
        if t.get("pos") == "形容詞" and surface.endswith("ない"):
            negative_stem = surface[:-2]
            split_compound_negative = False
            for v2_base in COMPOUND_VERB_V2_ICHIDAN:
                v2_stem = v2_base[:-1] if v2_base.endswith("る") else ""
                if v2_stem and negative_stem.endswith(v2_stem) and len(negative_stem) > len(v2_stem):
                    result.append({"surface": negative_stem, "pos": "動詞", "lemma": negative_stem + "る"})
                    result.append({"surface": "ない", "pos": "助動詞", "lemma": "ない"})
                    if applied_rule is None:
                        applied_rule = "productive-compound-negative"
                    split_compound_negative = True
                    break
            if split_compound_negative:
                continue

        # Classical negative ぬ is a separate auxiliary after a derivable
        # Godan irrealis stem, including lexicalized attributive spellings.
        if surface.endswith("ぬ") and len(surface) > 1:
            negative_stem = surface[:-1]
            base = base_from_mizenkei(negative_stem)
            if base is not None:
                result.append({"surface": negative_stem, "pos": "動詞", "lemma": base})
                result.append({"surface": "ぬ", "pos": "助動詞", "lemma": "ぬ"})
                if applied_rule is None:
                    applied_rule = "classical-negative-boundary"
                continue

        # In the closed ずに+は frame, a reference adjective ending in ない is
        # the productive verb mizenkei + negative auxiliary chain.  Derive the
        # host from its inflection instead of naming the open-class verb.
        if (
            t.get("pos") == "形容詞"
            and surface.endswith("ない")
            and token_index >= 2
            and tokens[token_index - 1].get("surface") == "は"
            and tokens[token_index - 2].get("surface") == "ずに"
        ):
            stem = surface[: -len("ない")]
            lemma = base_from_mizenkei(stem)
            if lemma is not None:
                result.append({"surface": stem, "pos": "動詞", "lemma": lemma})
                result.append({"surface": "ない", "pos": "助動詞", "lemma": "ない"})
                if applied_rule is None:
                    applied_rule = "zu-ni-wa-negative-auxiliary"
                continue

        # Productive renyokei + 尽くす keeps the subsidiary-verb boundary.
        # Reference dictionaries may lexicalize the whole expression, but the
        # same closed completive paradigm attaches to arbitrary verb stems.
        completive_form = next(
            (form for form in _COMPLETIVE_TSUKUSU_FORMS if surface.endswith(form) and len(surface) > len(form)),
            "",
        )
        if completive_form:
            stem = surface[: -len(completive_form)]
            lemma = base_from_renyokei(stem)
            if lemma is not None:
                result.append({"surface": stem, "pos": "動詞", "lemma": lemma})
                result.append({"surface": completive_form, "pos": "助動詞", "lemma": "尽くす"})
                if applied_rule is None:
                    applied_rule = "productive-completive-tsukusu"
                continue

        # Productive renyokei + たて (freshly completed).  Reference
        # dictionaries inconsistently lexicalize the whole expression as a
        # noun or as a compound verb.  Recover the same grammatical boundary
        # for any derivable continuative stem instead of listing host verbs.
        following_surface = tokens[token_index + 1].get("surface", "") if token_index + 1 < len(tokens) else ""
        verb_inflection_followers = frozenset({"て", "た", "たり", "ない", "なかっ", "ぬ", "ます", "まし"})
        if (
            surface.endswith("たて")
            and len(surface) > len("たて")
            and following_surface not in verb_inflection_followers
        ):
            stem = surface[: -len("たて")]
            lemma = base_from_renyokei(stem)
            if lemma is not None:
                result.append({"surface": stem, "pos": "動詞", "lemma": lemma})
                result.append({"surface": "たて", "pos": "接尾辞", "pos_sub1": "接尾", "lemma": "たて"})
                if applied_rule is None:
                    applied_rule = "productive-tate-suffix"
                continue

        # Noun-forming state suffix (泥/まみれ, 開け/っぱなし). The reference
        # dictionary holds the lexicalized hosts as one token and splits every
        # other host, but the suffix is productive and nothing else ends in it,
        # so the boundary is always there.
        state_suffix = next((suf for suf in STATE_NOUN_SUFFIXES if surface.endswith(suf)), "")
        if t.get("pos") == "名詞" and state_suffix and len(surface) > len(state_suffix):
            stem = surface[: -len(state_suffix)]
            result.append({"surface": stem, "pos": "名詞", "lemma": stem})
            result.append({"surface": state_suffix, "pos": "名詞", "pos_sub1": "接尾", "lemma": state_suffix})
            if applied_rule is None:
                applied_rule = "state-noun-suffix"
            continue

        # Degree suffix げ over an adjective stem or an adjectival noun
        # (楽し/げ, おぼろ/げ). Lexicalized hosts (誇らしげ, 得意げ) reach us as
        # one 形容動詞語幹 token; the suffix is the same productive one, so the
        # host keeps its own search boundary.
        if (
            t.get("pos") == "名詞"
            and t.get("pos_sub1") == "形容動詞語幹"
            and surface.endswith("げ")
            and len(surface) > 1
        ):
            stem = surface[:-1]
            if stem.endswith("し"):
                result.append({"surface": stem, "pos": "形容詞", "lemma": stem + "い"})
            else:
                result.append({"surface": stem, "pos": "名詞", "lemma": stem})
            result.append({"surface": "げ", "pos": "名詞", "pos_sub1": "接尾", "lemma": "げ"})
            if applied_rule is None:
                applied_rule = "degree-suffix-ge"
            continue

        # A closed leading modifier/adverb can be swallowed by a following
        # noun in the reference dictionary. Restore the grammatical search
        # boundary without enumerating the open-class noun on the right.
        leading_unit = next(
            (unit for unit in sorted(FIXED_LEADING_SEARCH_UNITS, key=len, reverse=True) if surface.startswith(unit)),
            "",
        )
        if leading_unit and len(surface) > len(leading_unit):
            remainder = surface[len(leading_unit) :]
            result.append(
                {
                    "surface": leading_unit,
                    "pos": FIXED_LEADING_SEARCH_UNITS[leading_unit],
                    "lemma": leading_unit,
                }
            )
            result.append({"surface": remainder, "pos": "名詞", "lemma": remainder})
            if applied_rule is None:
                applied_rule = "fixed-leading-search-unit"
            continue

        # 0a. Split a kanji nominal head from adverbial に regardless of the
        # reference dictionary's POS coverage (次に, 滅多に).
        if surface not in FIXED_FUNCTION_SEARCH_UNITS:
            m = regex.match(r"^([\p{Han}]+)(に)$", surface)
            if m:
                base = m.group(1)
                result.append({"surface": base, "pos": "名詞", "lemma": base})
                result.append({"surface": "に", "pos": "助詞", "lemma": "に"})
                if applied_rule is None:
                    applied_rule = "adverb-ni-split"
                continue

        # 0. Plural suffix ら
        m = regex.match(r"^(彼女|彼|僕|奴|我)ら$", surface)
        if m:
            result.append({"surface": m.group(1), "pos": "名詞", "pos_sub1": "代名詞", "lemma": m.group(1)})
            result.append({"surface": "ら", "pos": "名詞", "pos_sub1": "接尾", "lemma": "ら"})
            if applied_rule is None:
                applied_rule = "ra-suffix-split"
            continue

        # 1. ったら topic particle
        m = regex.match(r"^(.+)(ったら)$", surface)
        if m and len(m.group(1)) >= 3:
            stem = m.group(1)
            if stem in TTARA_STEMS:
                result.append({"surface": stem, "pos": "名詞", "lemma": stem})
                result.append({"surface": "ったら", "pos": "助詞", "lemma": "ったら"})
                if applied_rule is None:
                    applied_rule = "ttara-split"
                continue

        # 2. ってば emphatic particle
        m = regex.match(r"^(.+)(ってば)$", surface)
        if m and len(m.group(1)) >= 2:
            stem = m.group(1)
            if stem in TTEBA_STEMS:
                result.append({"surface": stem, "pos": "副詞", "lemma": stem})
                result.append({"surface": "ってば", "pos": "助詞", "lemma": "ってば"})
                if applied_rule is None:
                    applied_rule = "tteba-split"
                continue

        # 3. Unnatural kanji compounds
        m = regex.match(r"^(時分)(学校)$", surface)
        if m:
            result.append({"surface": m.group(1), "pos": "名詞", "lemma": m.group(1)})
            result.append({"surface": m.group(2), "pos": "名詞", "lemma": m.group(2)})
            if applied_rule is None:
                applied_rule = "compound-split"
            continue

        # 4. ねたい adjective -> ね|たい
        if surface == "ねたい" and t.get("pos") == "形容詞":
            result.append({"surface": "ね", "pos": "動詞", "lemma": "ねる"})
            result.append({"surface": "たい", "pos": "助動詞", "lemma": "たい"})
            if applied_rule is None:
                applied_rule = "netai-split"
            continue

        # 5. Compound nouns with dictionary words at start
        m = regex.match(r"^(自然)(言語処理.+)$", surface)
        if m:
            result.append({"surface": m.group(1), "pos": "名詞", "lemma": m.group(1)})
            result.append({"surface": m.group(2), "pos": "名詞", "lemma": m.group(2)})
            if applied_rule is None:
                applied_rule = "compound-dict-split"
            continue

        # 6. Prefecture + city compounds
        m = regex.match(r"^(.+県)(.+市)$", surface)
        if m:
            result.append({"surface": m.group(1), "pos": "名詞", "lemma": m.group(1)})
            result.append({"surface": m.group(2), "pos": "名詞", "lemma": m.group(2)})
            if applied_rule is None:
                applied_rule = "prefecture-city-split"
            continue

        # 7. Kanji + Katakana compound nouns
        if t.get("pos") == "名詞" and surface not in USER_DICT_COMPOUNDS:
            m = regex.match(r"^([\p{Han}]+)([\u30A0-\u30FFー]+)$", surface)
            if m:
                result.append({"surface": m.group(1), "pos": "名詞", "lemma": m.group(1)})
                result.append({"surface": m.group(2), "pos": "名詞", "lemma": m.group(2)})
                if applied_rule is None:
                    applied_rule = "kanji-katakana-split"
                continue

        # 8a. Kango + として adverbs: 依然として → 依然と|し|て
        # MeCab treats these as single adverbs, but they are taru-adjective
        # adverb forms (漢語 + と) + する conjugation (し + て)
        if t.get("pos") == "副詞":
            m = regex.match(r"^([\p{Han}]{2,}と)(して)$", surface)
            if m:
                adv_part = m.group(1)
                result.append({"surface": adv_part, "pos": "副詞", "lemma": adv_part[:-1]})
                result.append({"surface": "し", "pos": "動詞", "lemma": "する"})
                result.append({"surface": "て", "pos": "助詞", "lemma": "て"})
                if applied_rule is None:
                    applied_rule = "kango-toshite-split"
                continue

        # The productive quotative + する te-form keeps all grammatical
        # boundaries even when the reference dictionary emits として as one
        # particle token (考えよう+と+し+て+も).
        if surface == "として" and token_index > 0 and tokens[token_index - 1].get("surface") in ("う", "よう", "まい"):
            result.append({"surface": "と", "pos": "助詞", "lemma": "と"})
            result.append({"surface": "し", "pos": "動詞", "lemma": "する"})
            result.append({"surface": "て", "pos": "助詞", "lemma": "て"})
            if applied_rule is None:
                applied_rule = "quotative-suru-te-split"
            continue

        # The exemplification particle でも is the copula's continuative で plus
        # the binding particle も, and the reference dictionary carries only the
        # fused spelling — では has no entry and is therefore always emitted in
        # its parts. That lexicalization decides the boundary for hosts that
        # cannot take exemplification at all: after a formal noun or the
        # nominalizer の, in front of the copula's own supporting verb, ではない
        # comes back decomposed while でもない does not (ほか+で+は+ない against
        # ほか+でも+ない). The same reference splits はず+で+も+ない, so the
        # fused reading is an artifact of the entry rather than a reading of the
        # construction. A referential host keeps the fusion (学生でもない), which
        # is why the host set stays closed here.
        if (
            surface == "でも"
            and t.get("pos") == "助詞"
            and token_index > 0
            and tokens[token_index - 1].get("surface") in COPULAR_PREDICATE_HEADS
            and token_index + 1 < len(tokens)
            and tokens[token_index + 1].get("lemma") in ("ある", "ない")
        ):
            result.append({"surface": "で", "pos": "助動詞", "lemma": "だ"})
            result.append({"surface": "も", "pos": "助詞", "pos_sub1": "係助詞", "lemma": "も"})
            if applied_rule is None:
                applied_rule = "copular-head-demo-split"
            continue

        # 8. Copula negation: じゃない -> じゃ|ない
        if surface == "じゃない" and t.get("pos") == "助動詞":
            result.append({"surface": "じゃ", "pos": "助動詞", "lemma": "だ"})
            result.append({"surface": "ない", "pos": "助動詞", "lemma": "ない"})
            if applied_rule is None:
                applied_rule = "copula-negation-split"
            continue

        # Productive na-adjective nominalization.  The reference dictionary
        # may lexicalize the result as a noun, but the suffix boundary remains
        # visible for arbitrary stems with the characteristic -か ending.
        if t.get("pos") == "名詞" and surface.endswith("かさ") and len(surface) > len("かさ"):
            stem = surface[:-1]
            result.append({"surface": stem, "pos": "形容詞", "lemma": stem})
            result.append({"surface": "さ", "pos": "接尾辞", "pos_sub1": "接尾", "lemma": "さ"})
            if applied_rule is None:
                applied_rule = "na-adjective-sa-suffix"
            continue

        # 9. Onomatopoeia + っと + する conjugation (single MeCab token)
        # MeCab may treat ぷるんっとした as one token; Suzume splits as ぷるんっと+し+た
        m = regex.match(
            r"^([\p{Hiragana}\p{Katakana}ー]{1,6}っと)(し|した|して|する|すれ|しろ|せ|さ|される|された|させ|させる)$",
            surface,
        )
        if m:
            adv_part = m.group(1)
            verb_part = m.group(2)
            result.append({"surface": adv_part, "pos": "副詞", "lemma": adv_part})
            # Split する conjugation further: した→し+た, して→し+て, etc.
            if verb_part in ("した", "して", "しろ"):
                result.append({"surface": verb_part[:1], "pos": "動詞", "lemma": "する"})
                result.append(
                    {
                        "surface": verb_part[1:],
                        "pos": "助動詞" if verb_part[1:] == "た" else "助詞",
                        "lemma": verb_part[1:],
                    }
                )
            elif verb_part in ("される", "された", "させ", "させる"):
                result.append({"surface": "さ", "pos": "動詞", "lemma": "する"})
                rest = verb_part[1:]
                result.append(
                    {"surface": rest, "pos": "動詞", "lemma": rest + ("る" if not rest.endswith("る") else "")}
                )
            else:
                result.append({"surface": verb_part, "pos": "動詞", "lemma": "する"})
            if applied_rule is None:
                applied_rule = "onomatopoeia-tto-suru-split"
            continue

        # 10. Nominal + ない lexical adjective split. A Godan negative would
        # require the a-row mizenkei, so the i-row surface is a deverbal noun.
        if t.get("pos") == "形容詞" and surface in NOUN_NAI_COMPOUND_ADJECTIVES:
            noun_part = surface[: -len("ない")]
            result.append({"surface": noun_part, "pos": "名詞", "lemma": noun_part})
            result.append({"surface": "ない", "pos": "形容詞", "lemma": "ない"})
            if applied_rule is None:
                applied_rule = "noun-nai-compound-split"
            continue

        # 11. Literary volitional ん: verb+ん → verb + ん
        # MeCab merges ichidan verb + ん (literary volitional =む/よう)
        # as single token with conj_form 体言接続特殊
        # e.g., 乗り越えん → 乗り越え + ん, 越えん → 越え + ん
        if (
            t.get("pos") == "動詞"
            and surface.endswith("ん")
            and len(surface) >= 2
            and t.get("conj_form") == "体言接続特殊"
        ):
            verb_part = surface[: -len("ん")]
            lemma = t.get("lemma", "")
            result.append({"surface": verb_part, "pos": "動詞", "lemma": lemma})
            result.append({"surface": "ん", "pos": "助動詞", "lemma": "ん"})
            if applied_rule is None:
                applied_rule = "literary-volitional-n-split"
            continue

        # 12. Literary volitional auxiliary + quotative particle.  Keep the
        # two closed-class grammatical units searchable even when MeCab emits
        # a fused noun token (見むと → 見 + む + と).
        compound = LITERARY_VOLITIONAL_PARTICLE_COMPOUNDS.get(surface)
        if compound is not None:
            auxiliary, particle = compound
            applied_rule = _emit_split(
                result,
                (
                    {"surface": auxiliary, "pos": "助動詞", "lemma": auxiliary},
                    {"surface": particle, "pos": "助詞", "lemma": particle},
                ),
                applied_rule,
                "literary-volitional-particle-split",
            )
            continue

        # 13. An excessive auxiliary remains a separate search unit. MeCab can
        # lexicalize a kanji V1 plus 過ぎ into one verb token (行き過ぎ), while
        # Suzume consistently exposes the productive V1 + 過ぎ boundary.
        if t.get("pos") == "動詞" and surface.endswith("過ぎ") and t.get("lemma", "").endswith("過ぎる"):
            verb_part = surface[: -len("過ぎ")]
            verb_lemma = base_from_renyokei(verb_part)
            if verb_lemma is not None:
                result.append({"surface": verb_part, "pos": "動詞", "lemma": verb_lemma})
                result.append({"surface": "過ぎ", "pos": "助動詞", "lemma": "過ぎる"})
                if applied_rule is None:
                    applied_rule = "excessive-auxiliary-split"
                continue

        # 14. The failure subsidiary 損なう remains searchable after its V1.
        # MeCab may lexicalize the whole compound, including the bare one-kanji
        # ichidan stem used before a kanji-written subsidiary.
        if t.get("pos") == "動詞" and surface.endswith("損なう"):
            verb_part = surface[: -len("損なう")]
            verb_lemma = base_from_renyokei(verb_part)
            if verb_lemma is None and regex.fullmatch(r"\p{Han}", verb_part):
                verb_lemma = verb_part + "る"
            if verb_lemma is not None:
                result.append({"surface": verb_part, "pos": "動詞", "lemma": verb_lemma})
                result.append({"surface": "損なう", "pos": "動詞", "lemma": "損なう"})
                if applied_rule is None:
                    applied_rule = "failure-subsidiary-split"
                continue

        if t.get("pos") == "動詞" and surface.endswith("そびれる"):
            verb_part = surface[: -len("そびれる")]
            verb_lemma = base_from_renyokei(verb_part)
            if verb_lemma is None and regex.fullmatch(r"\p{Han}", verb_part):
                verb_lemma = verb_part + "る"
            if verb_lemma is not None:
                result.append({"surface": verb_part, "pos": "動詞", "lemma": verb_lemma})
                result.append({"surface": "そびれる", "pos": "助動詞", "lemma": "そびれる"})
                if applied_rule is None:
                    applied_rule = "failure-subsidiary-split"
                continue

        if t.get("pos") == "動詞" and surface.endswith("かねる"):
            verb_part = surface[: -len("かねる")]
            verb_lemma = base_from_renyokei(verb_part)
            if verb_lemma is not None:
                result.append({"surface": verb_part, "pos": "動詞", "lemma": verb_lemma})
                result.append({"surface": "かねる", "pos": "助動詞", "lemma": "かねる"})
                if applied_rule is None:
                    applied_rule = "inability-subsidiary-split"
                continue

        # No split needed
        result.append(t)

    return result, applied_rule
