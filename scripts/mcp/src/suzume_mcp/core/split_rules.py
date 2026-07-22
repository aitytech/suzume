"""Split rules ported from SuzumeUtils.pm apply_suzume_split()."""

import regex

from .constants import (
    FIXED_LEADING_SEARCH_UNITS,
    LITERARY_VOLITIONAL_PARTICLE_COMPOUNDS,
    TTARA_STEMS,
    TTEBA_STEMS,
    USER_DICT_COMPOUNDS,
    VERB_NAI_COMPOUND_ADJECTIVES,
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
_ICHIDAN_RENYOKEI_ENDINGS = frozenset("えけげせぜてでねへべめれ")


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


def apply_suzume_split(tokens: list[dict]) -> tuple[list[dict], str | None]:
    """Apply Suzume split rules to MeCab tokens.

    Returns:
        Tuple of (split tokens, applied rule name or None).
    """
    result: list[dict] = []
    applied_rule: str | None = None

    for t in tokens:
        surface = t.get("surface", "")

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

        # 0a. Split MeCab single-token kanji adverbs ending in に
        # e.g., 次に → 次+に, 滅多に → 滅多+に
        if t.get("pos") == "副詞":
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
                result.append({"surface": adv_part, "pos": "副詞", "lemma": adv_part})
                result.append({"surface": "し", "pos": "動詞", "lemma": "する"})
                result.append({"surface": "て", "pos": "助詞", "lemma": "て"})
                if applied_rule is None:
                    applied_rule = "kango-toshite-split"
                continue

        # 8. Copula negation: じゃない -> じゃ|ない
        if surface == "じゃない" and t.get("pos") == "助動詞":
            result.append({"surface": "じゃ", "pos": "助動詞", "lemma": "だ"})
            result.append({"surface": "ない", "pos": "助動詞", "lemma": "ない"})
            if applied_rule is None:
                applied_rule = "copula-negation-split"
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

        # 10. Verb+ない compound adjective split
        # MeCab merges verb renyokei + ない as single adjective token
        # (e.g., 揺るぎない, 何気ない), but Suzume correctly splits them
        if t.get("pos") == "形容詞" and surface in VERB_NAI_COMPOUND_ADJECTIVES:
            verb_part = surface[: -len("ない")]
            result.append({"surface": verb_part, "pos": "動詞", "lemma": verb_part})
            result.append({"surface": "ない", "pos": "助動詞", "lemma": "ない"})
            if applied_rule is None:
                applied_rule = "verb-nai-compound-split"
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
            result.append({"surface": auxiliary, "pos": "助動詞", "lemma": auxiliary})
            result.append({"surface": particle, "pos": "助詞", "lemma": particle})
            if applied_rule is None:
                applied_rule = "literary-volitional-particle-split"
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
