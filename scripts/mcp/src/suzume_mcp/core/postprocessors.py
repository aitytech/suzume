"""Postprocessors ported from SuzumeUtils.pm _postprocess_* functions."""

import regex

from .constants import (
    COUNTER_UNITS,
    EMPHATIC_SOKUON,
    QUANTITY_BOUND_SUFFIXES,
    SLANG_ADJ_STEMS,
    SLANG_VERB_STEMS,
    UNUSUAL_NAMES,
    WORD_EXCEPTIONS,
)
from .pos_mapping import _is_katakana_onomatopoeia
from .split_rules import base_from_renyokei


def preprocess_for_mecab(text: str) -> tuple[str, dict[tuple[int, str], dict]]:
    """Replace slang stems with standard ones before MeCab analysis.

    Returns:
        Tuple of (processed text, replacements dict keyed by (start position,
        category)). Keying on the category as well as the start position keeps
        replacements from different categories that happen to match at the same
        offset from silently overwriting one another.
    """
    replacements: dict[tuple[int, str], dict] = {}

    # Slang adjectives
    for slang, standard in SLANG_ADJ_STEMS.items():
        for m in regex.finditer(regex.escape(slang) + r"[いかくけさ]", text):
            replacements[(m.start(), "slang_adj")] = {
                "original": slang,
                "replacement": standard,
                "length": len(slang),
            }

    # Slang verbs
    for slang, standard in SLANG_VERB_STEMS.items():
        for m in regex.finditer(regex.escape(slang) + r"[らりるれろっ]", text):
            replacements[(m.start(), "slang_verb")] = {
                "original": slang,
                "replacement": standard,
                "length": len(slang),
            }

    # Unusual names
    for name, standard in UNUSUAL_NAMES.items():
        for m in regex.finditer(regex.escape(name) + r"(さん|ちゃん|様|君|殿)", text):
            replacements[(m.start(), "unusual_name")] = {
                "original": name,
                "replacement": standard,
                "length": len(name),
            }

    # Word exceptions
    for word, standard in WORD_EXCEPTIONS.items():
        for m in regex.finditer(regex.escape(word), text):
            replacements[(m.start(), "word_exception")] = {
                "original": word,
                "replacement": standard,
                "length": len(word),
            }

    # Emphatic sokuon
    for pattern, standard in EMPHATIC_SOKUON.items():
        for m in regex.finditer(regex.escape(pattern) + r"(?!て)", text):
            replacements[(m.start(), "emphatic_sokuon")] = {
                "original": pattern,
                "replacement": standard,
                "length": len(pattern),
            }

    # Apply replacements in reverse position order
    for key in sorted(replacements, key=lambda k: k[0], reverse=True):
        pos = key[0]
        r = replacements[key]
        text = text[:pos] + r["replacement"] + text[pos + r["length"] :]

    return text, replacements


def postprocess_mecab_tokens(
    tokens: list[dict], original_text: str, replacements: dict[tuple[int, str], dict]
) -> list[dict]:
    """Restore slang terms in tokens after MeCab processing."""
    if not replacements:
        return tokens

    # Pattern-based restoration
    for t in tokens:
        surface = t.get("surface", "")
        for r in replacements.values():
            if r["replacement"] in surface:
                surface = surface.replace(r["replacement"], r["original"])
                t["surface"] = surface
            lemma = t.get("lemma", "")
            if lemma and r["replacement"] in lemma:
                t["lemma"] = lemma.replace(r["replacement"], r["original"])

    # Surface realignment
    total_surface = sum(len(t.get("surface", "")) for t in tokens)
    if total_surface != len(original_text):
        pos = 0
        for idx, t in enumerate(tokens):
            orig_at_pos = original_text[pos:] if pos < len(original_text) else ""
            if not orig_at_pos.startswith(t.get("surface", "")):
                if idx > 0:
                    prev = tokens[idx - 1]
                    prev_pos = pos - len(prev.get("surface", ""))
                    for ext in range(1, 6):
                        try_len = len(prev.get("surface", "")) + ext
                        after = original_text[prev_pos + try_len :]
                        if after.startswith(t.get("surface", "")):
                            prev["surface"] = original_text[prev_pos : prev_pos + try_len]
                            pos = prev_pos + try_len
                            break
            pos += len(t.get("surface", ""))

    return tokens


def postprocess_sou(tokens: list[dict]) -> None:
    """Context-dependent そう normalization."""
    for i, t in enumerate(tokens):
        if t.get("surface") != "そう":
            continue

        # 伝聞そう before copula -> Adjective
        # Only when preceded by Auxiliary (だ/た etc.), not by Verb (様態そう)
        pos = t.get("pos", "")
        if pos in ("Adverb", "Auxiliary"):
            if i < len(tokens) - 1 and i > 0:
                nxt = tokens[i + 1].get("surface", "")
                prev_pos = tokens[i - 1].get("pos", "")
                if (regex.match(r"^(?:だ|です|でし|じゃ|じゃろ)", nxt) and prev_pos != "Verb") or (
                    nxt == "な" and i + 2 < len(tokens) and tokens[i + 2].get("surface") == "の"
                ):
                    t["pos"] = "Adjective"
                elif nxt == "で" and i + 2 < len(tokens):
                    following = tokens[i + 2].get("surface", "")
                    after_topic = tokens[i + 3].get("surface", "") if i + 3 < len(tokens) else ""
                    if following in ("ある", "あり", "あれ", "あっ", "ない", "なく", "なかっ", "なけれ", "なかろ") or (
                        following == "は" and after_topic in ("ない", "なく", "なかっ", "なけれ", "なかろ")
                    ):
                        t["pos"] = "Adjective"
            elif i == 0:  # Sentence-initial そう before copula
                if i < len(tokens) - 1:
                    nxt = tokens[i + 1].get("surface", "")
                    if regex.match(r"^(?:だ|です|でし|じゃ|じゃろ)", nxt) or (
                        nxt == "な" and i + 2 < len(tokens) and tokens[i + 2].get("surface") == "の"
                    ):
                        t["pos"] = "Adjective"
                    elif nxt == "で" and i + 2 < len(tokens):
                        following = tokens[i + 2].get("surface", "")
                        after_topic = tokens[i + 3].get("surface", "") if i + 3 < len(tokens) else ""
                        if following in (
                            "ある",
                            "あり",
                            "あれ",
                            "あっ",
                            "ない",
                            "なく",
                            "なかっ",
                            "なけれ",
                            "なかろ",
                        ) or (following == "は" and after_topic in ("ない", "なく", "なかっ", "なけれ", "なかろ")):
                            t["pos"] = "Adjective"

        # Katakana adjective stem + そう: Noun -> Adjective
        if i > 0:
            prev = tokens[i - 1]
            prev_surface = prev.get("surface", "")
            if (
                prev.get("pos") == "Noun"
                and regex.match(r"^[\u30A0-\u30FF]+$", prev_surface)
                and not _is_katakana_onomatopoeia(prev_surface)
            ):
                prev["pos"] = "Adjective"
                prev["lemma"] = prev_surface + "い"


def postprocess_ikaga(tokens: list[dict]) -> None:
    """Context-dependent いかが normalization."""
    for i, t in enumerate(tokens):
        if t.get("surface") != "いかが":
            continue
        has_copula = False
        if i < len(tokens) - 1:
            nxt = tokens[i + 1].get("surface", "")
            if regex.match(r"^(?:です|でし|だ|だっ|でしょ)", nxt):
                has_copula = True
        if not has_copula:
            t["pos"] = "Adverb"


def postprocess_demo(tokens: list[dict]) -> None:
    """Use Suzume's ambiguity policy: homographic でも is a bound particle."""
    for t in tokens:
        if t.get("surface") != "でも":
            continue
        t["pos"] = "Particle"
        t["lemma"] = "でも"


def postprocess_closed_function_words(tokens: list[dict]) -> bool:
    """Normalize finite conjunction and pronoun classes mislabelled by MeCab."""
    conjunctions = frozenset({"しかるに", "もって"})
    pronouns = frozenset({"各々", "各自", "あれこれ", "何かしら"})
    adverbial_ambiguities = frozenset({"また"})
    changed = False
    for token in tokens:
        surface = token.get("surface", "")
        target_pos = (
            "Conjunction"
            if surface in conjunctions
            else "Pronoun"
            if surface in pronouns
            else "Adverb"
            if surface in adverbial_ambiguities
            else ""
        )
        if not target_pos or token.get("pos") == target_pos:
            continue
        token["pos"] = target_pos
        token["lemma"] = surface
        changed = True
    return changed


def postprocess_classical_focus_namu(tokens: list[dict]) -> bool:
    """Classify classical なむ before a quotation boundary as a particle."""
    changed = False
    for idx, token in enumerate(tokens[:-1]):
        if token.get("surface") != "なむ" or tokens[idx + 1].get("surface") != "と":
            continue
        if token.get("pos") != "Particle":
            token["pos"] = "Particle"
            token["lemma"] = "なむ"
            changed = True
    return changed


def postprocess_honorific_i_adjective(tokens: list[dict]) -> bool:
    """Restore an i-adjective ending in -しい after honorific prefix お."""
    changed = False
    for idx in range(1, len(tokens)):
        token = tokens[idx]
        if (
            tokens[idx - 1].get("surface") == "お"
            and tokens[idx - 1].get("pos") == "Prefix"
            and token.get("pos") == "Noun"
            and token.get("surface", "").endswith("しい")
        ):
            token["pos"] = "Adjective"
            token["lemma"] = token["surface"]
            changed = True
    return changed


def postprocess_i_adjective_upper_bound(tokens: list[dict]) -> bool:
    """Restore an i-adjective continuative before the upper-bound particle とも."""
    changed = False
    for idx, token in enumerate(tokens[:-1]):
        surface = token.get("surface", "")
        if token.get("pos") != "Noun" or not surface.endswith("く"):
            continue
        if tokens[idx + 1].get("surface") != "とも":
            continue
        token["pos"] = "Adjective"
        token["lemma"] = surface[:-1] + "い"
        changed = True
    return changed


def postprocess_kadouka_adverb(tokens: list[dict]) -> bool:
    """Keep どう adverbial in the closed interrogative frame か+どう+か."""
    changed = False
    for idx in range(1, len(tokens) - 1):
        token = tokens[idx]
        if (
            token.get("surface") == "どう"
            and token.get("pos") == "Adjective"
            and tokens[idx - 1].get("surface") == "か"
            and tokens[idx + 1].get("surface") == "か"
        ):
            token["pos"] = "Adverb"
            changed = True
    return changed


def postprocess_ii(tokens: list[dict]) -> None:
    """Fix いい: Verb(いう) -> Adjective when not followed by verb."""
    for i, t in enumerate(tokens):
        if t.get("surface") != "いい":
            continue
        if t.get("pos") != "Verb" or t.get("lemma") != "いう":
            continue
        next_is_verb = False
        if i < len(tokens) - 1:
            next_is_verb = tokens[i + 1].get("pos") == "Verb"
        if not next_is_verb:
            t["pos"] = "Adjective"
            t["lemma"] = "いい"


def postprocess_iru_aux(tokens: list[dict]) -> None:
    """Fix dependent い/いる after a te-form: Verb -> Auxiliary."""
    focus_particles = frozenset({"さえ", "しか", "こそ", "も"})
    for i in range(1, len(tokens)):
        t = tokens[i]
        surface = t.get("surface", "")
        if surface not in ("い", "いる", "いれ", "いた", "いない"):
            continue
        if t.get("pos") != "Verb":
            continue
        prev_surface = tokens[i - 1].get("surface", "")
        direct_te_form = prev_surface in ("て", "で")
        focused_te_form = (
            surface in ("い", "いる")
            and prev_surface in focus_particles
            and i >= 2
            and tokens[i - 2].get("surface") in ("て", "で")
        )
        if direct_te_form or focused_te_form:
            t["pos"] = "Auxiliary"
            t["lemma"] = "いる"


def postprocess_giving_aux(tokens: list[dict]) -> bool:
    """Classify productive て/で + giving/receiving verbs as auxiliaries."""
    auxiliary_lemmas = frozenset({"あげる", "くれる", "もらう"})
    changed = False
    for idx in range(1, len(tokens)):
        token = tokens[idx]
        if token.get("lemma") not in auxiliary_lemmas:
            continue
        if tokens[idx - 1].get("surface") not in ("て", "で"):
            continue
        if token.get("pos") != "Auxiliary":
            token["pos"] = "Auxiliary"
            changed = True
    return changed


def postprocess_contracted_progressive_aux(tokens: list[dict]) -> bool:
    """Normalize the voiced progressive contraction after an onbin stem."""
    changed = False
    for idx in range(1, len(tokens)):
        token = tokens[idx]
        previous = tokens[idx - 1]
        if token.get("surface") != "でる" or previous.get("pos") != "Verb":
            continue
        if not previous.get("surface", "").endswith("ん"):
            continue
        token["pos"] = "Auxiliary"
        token["lemma"] = "いる"
        changed = True
    return changed


def postprocess_miru_aux(tokens: list[dict]) -> None:
    """Classify trial みる after a te-form boundary as Auxiliary."""
    trial_surfaces = {"み", "みる", "みれ", "みろ", "みよ"}
    for idx in range(1, len(tokens)):
        token = tokens[idx]
        if token.get("surface") not in trial_surfaces:
            continue
        prev_idx = idx - 1
        if tokens[prev_idx].get("surface") == "も" and prev_idx > 0:
            prev_idx -= 1
        if tokens[prev_idx].get("surface") not in ("て", "で"):
            continue
        token["pos"] = "Auxiliary"
        token["lemma"] = "みる"


def postprocess_shimau_aux(tokens: list[dict]) -> None:
    """Classify the completive 仕舞う paradigm after a te-form as Auxiliary."""
    shimau_forms = frozenset({"仕舞う", "仕舞わ", "仕舞い", "仕舞っ", "仕舞え", "仕舞お"})
    for idx in range(1, len(tokens)):
        token = tokens[idx]
        if token.get("surface") not in shimau_forms:
            continue
        if tokens[idx - 1].get("surface") not in ("て", "で"):
            continue
        token["pos"] = "Auxiliary"
        token["lemma"] = "しまう"


def postprocess_quantity_bound_suffix(tokens: list[dict]) -> bool:
    """Split a numeral+counter phrase from its closed-class bound suffix."""
    counter_pattern = "|".join(regex.escape(unit) for unit in sorted(COUNTER_UNITS, key=len, reverse=True))
    suffix_pattern = "|".join(regex.escape(suffix) for suffix in QUANTITY_BOUND_SUFFIXES)
    quantity_only_pattern = regex.compile(rf"^[0-9０-９〇零一二三四五六七八九十百千万億兆]+(?:{counter_pattern})$")
    quantity_pattern = regex.compile(
        rf"^(?P<quantity>[0-9０-９〇零一二三四五六七八九十百千万億兆]+(?:{counter_pattern}))"
        rf"(?P<suffix>{suffix_pattern})$"
    )
    changed = False
    index = 0
    while index < len(tokens):
        token = tokens[index]
        # MeCab sometimes already supplies the quantity and suffix as separate
        # tokens but tags the homographic suffix as a verb stem (二本|立て).
        # The preceding quantity fixes the closed-class suffix reading.
        if (
            index > 0
            and token.get("surface") in QUANTITY_BOUND_SUFFIXES
            and quantity_only_pattern.fullmatch(tokens[index - 1].get("surface", ""))
        ):
            token["pos"] = "Suffix"
            token["lemma"] = token["surface"]
            changed = True
            index += 1
            continue
        match = quantity_pattern.fullmatch(token.get("surface", ""))
        if match is None:
            index += 1
            continue
        quantity = match.group("quantity")
        suffix = match.group("suffix")
        tokens[index : index + 1] = [
            {"surface": quantity, "pos": "Noun", "lemma": quantity},
            {"surface": suffix, "pos": "Suffix", "lemma": suffix},
        ]
        changed = True
        index += 2
    return changed


def postprocess_exclusion_suffix(tokens: list[dict]) -> bool:
    """Classify nominal X+抜き/ぬき constructions as exclusion suffixes."""
    changed = False
    for idx, token in enumerate(tokens):
        if idx == 0 or token.get("surface") not in ("抜き", "ぬき"):
            continue
        previous = tokens[idx - 1]
        if previous.get("pos") not in ("Noun", "Prefix"):
            continue
        if previous.get("surface") == "中" and previous.get("pos") == "Prefix":
            previous["pos"] = "Noun"
            changed = True
        if token.get("pos") != "Suffix":
            token["pos"] = "Suffix"
            changed = True
    return changed


def postprocess_state_suffix(tokens: list[dict]) -> bool:
    """Classify nominal X+中 as a state suffix before a following particle."""
    changed = False
    for idx, token in enumerate(tokens[1:], start=1):
        if token.get("surface") != "中" or token.get("pos") != "Noun":
            continue
        if tokens[idx - 1].get("pos") != "Noun":
            continue
        if idx + 1 < len(tokens) and tokens[idx + 1].get("pos") == "Particle":
            token["pos"] = "Suffix"
            changed = True
    return changed


def postprocess_productive_verb_suffix_stem(tokens: list[dict]) -> bool:
    """Restore a verb continuative before a productive derivational suffix."""
    verb_suffixes = frozenset({"がち", "っぱなし"})
    changed = False
    for idx in range(len(tokens) - 1):
        stem = tokens[idx]
        suffix = tokens[idx + 1]
        if suffix.get("surface") not in verb_suffixes or suffix.get("pos") != "Suffix":
            continue
        if stem.get("pos") == "Verb" and stem.get("lemma") != stem.get("surface"):
            continue
        lemma = base_from_renyokei(stem.get("surface", ""))
        if lemma is None:
            continue
        stem["pos"] = "Verb"
        stem["lemma"] = lemma
        changed = True
    return changed


def postprocess_teki_na_adjective(tokens: list[dict]) -> bool:
    """Classify X的 before な/に as a derived na-adjective."""
    changed = False
    for idx, token in enumerate(tokens[:-1]):
        if token.get("pos") != "Noun" or not token.get("surface", "").endswith("的"):
            continue
        if tokens[idx + 1].get("surface") not in ("な", "に"):
            continue
        token["pos"] = "Adjective"
        changed = True
    return changed


def postprocess_chigai_negative_adjective(tokens: list[dict]) -> bool:
    """Keep deverbal 〜違い before ない in its nominal-adjective reading."""
    changed = False
    for idx, token in enumerate(tokens[1:], start=1):
        previous = tokens[idx - 1]
        if token.get("surface") != "ない" or not previous.get("surface", "").endswith("違い"):
            continue
        previous["pos"] = "Noun"
        if previous.get("surface") == "違い":
            previous["lemma"] = "ちがい"
        token["pos"] = "Adjective"
        changed = True
    return changed


def postprocess_renyokei_compound_particle(tokens: list[dict]) -> bool:
    """Keep closed particle plus continuative-form expressions as search units."""
    compound_particles = {
        ("に", "つれ", "て"): "につれて",
        ("に", "かけ", "て"): "にかけて",
    }
    changed = False
    index = 0
    while index + 2 < len(tokens):
        surfaces = tuple(tokens[index + offset].get("surface") for offset in range(3))
        compound = compound_particles.get(surfaces)
        if compound is None:
            index += 1
            continue
        tokens[index : index + 3] = [{"surface": compound, "pos": "Particle", "lemma": compound}]
        if (
            compound == "にかけて"
            and index + 1 < len(tokens)
            and tokens[index + 1].get("surface") == "続く"
            and tokens[index + 1].get("pos") == "Auxiliary"
        ):
            tokens[index + 1]["pos"] = "Verb"
            tokens[index + 1]["lemma"] = "続く"
        changed = True
        index += 1
    return changed


def postprocess_to_areba_conditional(tokens: list[dict]) -> bool:
    """Preserve the verb inflection and conditional-particle boundary in とあれば."""
    changed = False
    index = 0
    while index + 1 < len(tokens):
        if tokens[index].get("surface") == "と" and tokens[index + 1].get("surface") == "あれば":
            tokens[index + 1 : index + 2] = [
                {"surface": "あれ", "pos": "Verb", "lemma": "ある"},
                {"surface": "ば", "pos": "Particle", "lemma": "ば"},
            ]
            changed = True
            index += 3
            continue
        index += 1
    return changed


def postprocess_tagaru_aux(tokens: list[dict]) -> bool:
    """Keep the desiderative-observation auxiliary たがる as one search unit."""
    tagaru_forms = frozenset({"がら", "がり", "がる", "がれ", "がろ", "がっ"})
    changed = False
    idx = 0
    while idx + 1 < len(tokens):
        if tokens[idx].get("surface") == "た" and tokens[idx + 1].get("surface") in tagaru_forms:
            surface = "た" + tokens[idx + 1].get("surface", "")
            tokens[idx : idx + 2] = [{"surface": surface, "pos": "Auxiliary", "lemma": "たがる"}]
            changed = True
        idx += 1
    return changed


def postprocess_fuu_formal_noun(tokens: list[dict]) -> bool:
    """Normalize demonstrative + ふう + に as grammatical search units."""
    joined = "".join(token.get("surface", "") for token in tokens)
    match = regex.fullmatch(r"([こそあど]んな)ふうに", joined)
    if match:
        tokens[:] = [
            {"surface": match.group(1), "pos": "Determiner", "lemma": match.group(1)},
            {"surface": "ふう", "pos": "Noun", "lemma": "ふう"},
            {"surface": "に", "pos": "Particle", "lemma": "に"},
        ]
        return True
    for token in tokens:
        if token.get("surface") == "ふう" and token.get("pos") != "Noun":
            token["pos"] = "Noun"
            token["lemma"] = "ふう"
            return True
    return False


def postprocess_indefinite_ka(tokens: list[dict]) -> bool:
    """Separate indefinite か from a pronoun and restore existential いる."""
    indefinite_pronoun_stems = frozenset({"なに", "何", "だれ", "誰", "どこ", "どちら", "どれ", "どなた"})
    changed = False
    idx = 0
    while idx < len(tokens):
        token = tokens[idx]
        surface = token.get("surface", "")
        stem = surface[:-1] if surface.endswith("か") else ""
        if stem in indefinite_pronoun_stems:
            tokens[idx : idx + 1] = [
                {"surface": stem, "pos": "Pronoun", "lemma": stem},
                {"surface": "か", "pos": "Particle", "lemma": "か"},
            ]
            changed = True
            idx += 1
        elif idx > 0 and tokens[idx - 1].get("pos") == "Pronoun" and surface == "かい":
            tokens[idx : idx + 1] = [
                {"surface": "か", "pos": "Particle", "lemma": "か"},
                {"surface": "い", "pos": "Verb", "lemma": "いる"},
            ]
            changed = True
            idx += 1
        idx += 1

    for idx in range(1, len(tokens)):
        if tokens[idx - 1].get("surface") == "か" and tokens[idx].get("surface") in ("い", "いる"):
            tokens[idx]["pos"] = "Verb"
            tokens[idx]["lemma"] = "いる"
            changed = True
    return changed


def postprocess_subsidiary_yuku(tokens: list[dict]) -> bool:
    """Treat literary 連用形 + ゆく/いく as a subsidiary verb."""
    changed = False
    for idx in range(1, len(tokens)):
        if tokens[idx - 1].get("pos") == "Verb" and tokens[idx].get("surface") in ("ゆく", "いく"):
            tokens[idx]["pos"] = "Verb"
            tokens[idx]["lemma"] = tokens[idx].get("surface")
            changed = True
    return changed


def postprocess_hiragana_purpose_noun(tokens: list[dict]) -> bool:
    """Use a nominal search unit for hiragana activity + に + motion verb."""
    changed = False
    motion_lemmas = {"行く", "来る", "帰る"}
    for idx in range(len(tokens) - 2):
        token = tokens[idx]
        if (
            token.get("pos") == "Verb"
            and regex.fullmatch(r"\p{Hiragana}+", token.get("surface", ""))
            and tokens[idx + 1].get("surface") == "に"
            and tokens[idx + 2].get("lemma") in motion_lemmas
        ):
            token["pos"] = "Noun"
            token["lemma"] = token.get("surface")
            changed = True
    return changed


def postprocess_short_hiragana_onbin(tokens: list[dict]) -> bool:
    """Normalize a short pure-hiragana 撥音便 immediately before だ/で."""
    changed = False
    for idx in range(len(tokens) - 1):
        token = tokens[idx]
        surface = token.get("surface", "")
        if (
            len(surface) == 2
            and surface.endswith("ん")
            and regex.fullmatch(r"\p{Hiragana}+", surface)
            and tokens[idx].get("pos") in ("Noun", "Verb")
            and tokens[idx + 1].get("surface") in ("だ", "で")
        ):
            lemma = token.get("lemma", "")
            has_valid_onbin_lemma = (
                token.get("pos") == "Verb" and lemma[:-1] == surface[:-1] and lemma.endswith(("む", "ぶ", "ぬ"))
            )
            if has_valid_onbin_lemma:
                continue
            token["pos"] = "Verb"
            token["lemma"] = surface[:-1] + "む"
            changed = True
    return changed


def postprocess_hiragana_godan_wa_terminal(tokens: list[dict]) -> bool:
    """Merge a pure-hiragana Godan-wa base split from final auxiliary う."""
    if len(tokens) != 2 or tokens[0].get("pos") != "Verb" or tokens[1].get("surface") != "う":
        return False
    stem = tokens[0].get("surface", "")
    # An o-row stem followed by う is normally a volitional form (e.g. 書こう),
    # not a dictionary-form Godan-wa verb split at its final vowel.
    if stem and stem[-1] in "おこそとのほもよろをごぞどぼぽょ":
        return False
    surface = stem + "う"
    if len(surface) < 3 or not regex.fullmatch(r"\p{Hiragana}+", surface):
        return False
    tokens[:] = [{"surface": surface, "pos": "Verb", "lemma": surface}]
    return True


def postprocess_honorific_request(tokens: list[dict]) -> bool:
    """Restore a verbal stem in an honorific or humble construction.

    Some analyzers classify a nominally homographic stem such as ``立ち`` as a
    noun even though ``ください``, ``いたす``, or ``いただく`` supplies a verbal
    continuation. I-row Godan stems and e-row Ichidan stems can both recover
    their dictionary form from conjugation structure without a lexical exception
    table.
    """
    godan_renyokei_to_base = {
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
    changed = False
    for idx in range(1, len(tokens) - 1):
        prefix = tokens[idx - 1]
        stem = tokens[idx]
        continuation = tokens[idx + 1]
        surface = stem.get("surface", "")
        is_direct_honorific_continuation = continuation.get("pos") == "Verb" and continuation.get("lemma") in (
            "くださる",
            "いたす",
            "いただく",
        )
        is_honorific_naru = (
            continuation.get("surface") == "に"
            and continuation.get("pos") == "Particle"
            and idx + 2 < len(tokens)
            and tokens[idx + 2].get("pos") == "Verb"
            and tokens[idx + 2].get("lemma") == "なる"
        )
        if (
            prefix.get("pos") == "Prefix"
            and prefix.get("surface") in ("お", "ご")
            and stem.get("pos") in ("Noun", "Suffix")
            and surface
            and (is_direct_honorific_continuation or is_honorific_naru)
        ):
            final = surface[-1]
            if final in godan_renyokei_to_base:
                stem["pos"] = "Verb"
                stem["lemma"] = surface[:-1] + godan_renyokei_to_base[final]
                changed = True
            elif final in "えけげせぜてでねへべめれ":
                stem["pos"] = "Verb"
                stem["lemma"] = surface + "る"
                changed = True
    return changed


def postprocess_honorific_oki_aux(tokens: list[dict]) -> bool:
    """Normalize the preparatory auxiliary in a polite request.

    In a noun + おき + ください request, おき is the renyokei of the
    subsidiary verb おく, not the independent ichidan verb おきる.  The rule
    is structural so other dictionary-form uses of おきる remain untouched.
    """
    changed = False
    for idx in range(1, len(tokens) - 1):
        previous = tokens[idx - 1]
        token = tokens[idx]
        following = tokens[idx + 1]
        if (
            previous.get("pos") == "Noun"
            and token.get("surface") == "おき"
            and token.get("pos") == "Verb"
            and token.get("lemma") == "おきる"
            and following.get("pos") == "Verb"
            and following.get("lemma") == "くださる"
        ):
            token["lemma"] = "おく"
            changed = True
    return changed


def postprocess_de_particle(tokens: list[dict]) -> None:
    """Normalize copular で before a binding particle."""
    binding_surfaces = frozenset({"こそ", "さえ", "すら", "しか"})
    for idx in range(1, len(tokens) - 1):
        token = tokens[idx]
        if token.get("surface") != "で" or token.get("pos") != "Particle":
            continue
        if tokens[idx + 1].get("surface") not in binding_surfaces:
            continue
        if tokens[idx - 1].get("pos") not in ("Noun", "Pronoun", "Adjective"):
            continue
        token["pos"] = "Auxiliary"
        token["lemma"] = "だ"


def postprocess_dai_final_particle(tokens: list[dict]) -> None:
    """Normalize closed sentence-final particles that MeCab labels as nouns."""
    if not tokens:
        return
    token = tokens[-1]
    final_particle_surfaces = frozenset({"だい", "ゲソ", "げそ"})
    if token.get("surface") in final_particle_surfaces and token.get("pos") == "Noun":
        token["pos"] = "Particle"
        token["lemma"] = token["surface"]


def postprocess_nanka_particle(tokens: list[dict]) -> bool:
    """Normalize the colloquial adverbial particle なんか from MeCab's filler tag."""
    changed = False
    for token in tokens:
        if token.get("surface") == "なんか" and token.get("pos") == "Other":
            token["pos"] = "Particle"
            token["lemma"] = "なんか"
            changed = True
    return changed


def postprocess_kiri_limited_particle(tokens: list[dict]) -> bool:
    """Normalize hiragana きり as the closed limiting particle."""
    changed = False
    for idx, token in enumerate(tokens):
        previous = tokens[idx - 1] if idx > 0 else None
        if token.get("surface") == "きり" and token.get("pos") == "Noun":
            if previous is not None and previous.get("pos") == "Adverb":
                previous["pos"] = "Noun"
            token["pos"] = "Particle"
            token["lemma"] = "きり"
            changed = True
    return changed


def postprocess_kuru_causative(tokens: list[dict]) -> bool:
    """Restore the irregular Kuru lemma in its split causative connection."""
    changed = False
    for idx, token in enumerate(tokens[:-1]):
        following = tokens[idx + 1]
        if (
            token.get("surface") == "来さ"
            and token.get("pos") == "Verb"
            and token.get("lemma") == "来す"
            and following.get("surface", "").startswith("せ")
            and following.get("pos") == "Auxiliary"
        ):
            token["lemma"] = "来る"
            changed = True
    return changed


def postprocess_onaji_predicate(tokens: list[dict]) -> bool:
    """Normalize 同じ before the copula from a determiner to a na-adjective."""
    changed = False
    for idx in range(len(tokens) - 1):
        token = tokens[idx]
        following = tokens[idx + 1]
        if (
            token.get("surface") == "同じ"
            and token.get("pos") == "Determiner"
            and following.get("surface") in ("だ", "で")
        ):
            token["pos"] = "Adjective"
            token["lemma"] = "同じ"
            changed = True
    return changed


def postprocess_na_adj_noun(tokens: list[dict]) -> bool:
    """Treat a bare na-adjective stem before accusative を as a noun use.

    An i-adjective cannot directly take を, while a na-adjective stem can be
    used nominally (for example, 平静を保つ).  This is a syntactic correction,
    not a lexical exception; adjective readings before な/に/すぎる remain
    untouched.
    """
    changed = False
    for idx, token in enumerate(tokens[:-1]):
        if token.get("pos") != "Adjective" or tokens[idx + 1].get("surface") != "を":
            continue
        lemma = token.get("lemma", token.get("surface", ""))
        if lemma.endswith("い"):
            continue
        token["pos"] = "Noun"
        token["lemma"] = token.get("surface", "")
        changed = True
    return changed


def postprocess_tsuke_noun(tokens: list[dict]) -> None:
    """Fix 付け: Suffix -> Noun."""
    for t in tokens:
        if t.get("surface") == "付け" and t.get("pos") == "Suffix":
            t["pos"] = "Noun"
            t["lemma"] = "付け"


def postprocess_copula_neg(tokens: list[dict]) -> bool:
    """Normalize copular negative chains and their continuative adjective."""
    changed = False
    for idx in range(1, len(tokens) - 2):
        predicate = tokens[idx - 1]
        copula = tokens[idx]
        topic = tokens[idx + 1]
        negative = tokens[idx + 2]
        if (
            predicate.get("pos") in ("Noun", "Pronoun", "Adjective", "Particle", "Suffix")
            and copula.get("surface") == "で"
            and topic.get("surface") == "は"
            and negative.get("surface") == "ない"
        ):
            copula["pos"] = "Auxiliary"
            copula["lemma"] = "だ"
            negative["pos"] = "Auxiliary"
            negative["lemma"] = "ない"
            changed = True

    for i in range(1, len(tokens)):
        t = tokens[i]
        if t.get("surface") != "なく" or t.get("pos") != "Auxiliary":
            continue
        prev = tokens[i - 1].get("surface", "")
        if prev in ("じゃ", "で"):
            t["pos"] = "Adjective"
            t["lemma"] = "ない"
            changed = True
    return changed


def postprocess_te(tokens: list[dict]) -> None:
    """Fix て/で after verb: Particle -> Auxiliary."""
    for i in range(1, len(tokens)):
        t = tokens[i]
        surface = t.get("surface", "")
        if surface not in ("て", "で") or t.get("pos") != "Particle":
            continue
        prev_pos = tokens[i - 1].get("pos", "")
        if prev_pos in ("Verb", "Auxiliary", "Adjective"):
            t["pos"] = "Auxiliary"
            t["lemma"] = "てる" if surface == "て" else "でる"


def postprocess_de_aru(tokens: list[dict]) -> None:
    """Fix copula で+ある/あり/あっ pattern based on context."""
    for i in range(len(tokens)):
        t = tokens[i]
        if t.get("surface") != "で":
            continue
        if i >= len(tokens) - 1:
            continue

        nxt = tokens[i + 1]
        nxt_surface = nxt.get("surface", "")

        if nxt_surface not in ("ある", "あり", "あれ", "あっ"):
            continue

        prev_pos = tokens[i - 1].get("pos", "") if i > 0 else ""
        is_past = nxt_surface == "あっ"

        if is_past and prev_pos in ("Noun", "Pronoun", "Suffix"):
            # N+であった: で→Particle, あっ→Verb
            t["pos"] = "Particle"
            t["lemma"] = "で"
            if nxt.get("pos") == "Auxiliary":
                nxt["pos"] = "Verb"
                nxt["lemma"] = "ある"
        elif is_past:
            # Na-adj+であった or other: keep as-is (copula chain)
            pass
        else:
            # Present/continuous forms: で→Auxiliary(だ), ある→Verb
            t["pos"] = "Auxiliary"
            t["lemma"] = "だ"
            if nxt.get("pos") == "Auxiliary":
                nxt["pos"] = "Verb"
                nxt["lemma"] = "ある"


def postprocess_taihen(tokens: list[dict]) -> None:
    """Fix 大変 before な: Adverb -> Adjective (na-adjective use)."""
    for i, t in enumerate(tokens):
        if t.get("surface") == "大変" and t.get("pos") == "Adverb":
            if i < len(tokens) - 1 and tokens[i + 1].get("surface") == "な":
                t["pos"] = "Adjective"


def postprocess_you_noun(tokens: list[dict]) -> None:
    """Distinguish formal-noun よう from the true volitional auxiliary."""
    for idx, t in enumerate(tokens):
        if t.get("surface") != "よう":
            continue
        if idx > 0 and tokens[idx - 1].get("pos") == "Verb" and t.get("pos") == "Suffix":
            # A mizenkei immediately followed by よう is the ichidan
            # volitional auxiliary (見+よう, 着+よう), not the formal noun.
            # Other verb+よう sequences retain the formal-noun reading
            # (見る+ように, 読む+ようだ).
            if "未然" in (tokens[idx - 1].get("conj_form") or "") or (
                len(tokens[idx - 1].get("surface", "")) == 1
                and idx + 1 < len(tokens)
                and tokens[idx + 1].get("surface") == "に"
            ):
                t["pos"] = "Auxiliary"
            else:
                t["pos"] = "Noun"
        else:
            previous_surface = tokens[idx - 1].get("surface", "") if idx > 0 else ""
            following_surface = tokens[idx + 1].get("surface", "") if idx + 1 < len(tokens) else ""
            formal_context = previous_surface == "の" or following_surface in ("だ", "です", "で", "な", "に")
            if formal_context:
                t["pos"] = "Noun"
                t["lemma"] = "よう"


def postprocess_classical_conjecture_aux(tokens: list[dict]) -> None:
    """Treat classical けむ/らむ after a verb as auxiliaries."""
    for idx, token in enumerate(tokens):
        if idx == 0 or token.get("surface") not in ("けむ", "らむ"):
            continue
        if tokens[idx - 1].get("pos") == "Verb":
            token["pos"] = "Auxiliary"
            token["lemma"] = token["surface"]


def postprocess_classical_kere_aux(tokens: list[dict]) -> bool:
    """Normalize the classical past 已然形 in a verb+けれ+ば chain."""
    for idx in range(1, len(tokens) - 1):
        token = tokens[idx]
        if (
            tokens[idx - 1].get("pos") == "Verb"
            and token.get("surface") == "けれ"
            and tokens[idx + 1].get("surface") == "ば"
        ):
            token["pos"] = "Auxiliary"
            token["lemma"] = "けり"
            return True
    return False


def postprocess_classical_ramu_boundary(tokens: list[dict]) -> None:
    """Repair MeCab's one-kanji godan-ka plus らむ boundary."""
    for idx, token in enumerate(tokens):
        if idx == 0 or token.get("surface") != "くらむ" or token.get("pos") != "Verb":
            continue
        previous = tokens[idx - 1]
        stem = previous.get("surface", "")
        if previous.get("pos") != "Noun" or len(stem) != 1:
            continue
        previous["surface"] = f"{stem}く"
        previous["pos"] = "Verb"
        previous["lemma"] = f"{stem}く"
        token["surface"] = "らむ"
        token["pos"] = "Auxiliary"
        token["lemma"] = "らむ"


def postprocess_classical_desiderative_aux(tokens: list[dict]) -> bool:
    """Normalize the split classical desiderative ま + ほしき chain."""
    changed = False
    for idx, token in enumerate(tokens[:-1]):
        if token.get("surface") != "ま" or tokens[idx + 1].get("surface") != "ほしき":
            continue
        token["pos"] = "Auxiliary"
        token["lemma"] = "まほし"
        changed = True
    return changed


def postprocess_classical_honorific_aux(tokens: list[dict]) -> bool:
    """Normalize the split classical honorific auxiliary た + ま + ふ."""
    changed = False
    for idx in range(len(tokens) - 2):
        first, second, third = tokens[idx : idx + 3]
        if (first.get("surface"), second.get("surface"), third.get("surface")) != ("た", "ま", "ふ"):
            continue
        for token in (second, third):
            token["pos"] = "Auxiliary"
            token["lemma"] = "たまふ"
        changed = True
    return changed


def postprocess_classical_perfect_aux(tokens: list[dict]) -> None:
    """Normalize terminal たり and 已然形+り as classical auxiliaries."""
    for idx, token in enumerate(tokens):
        if idx == 0:
            continue
        previous = tokens[idx - 1]
        if token.get("surface") == "たり" and idx == len(tokens) - 1 and previous.get("pos") == "Verb":
            token["pos"] = "Auxiliary"
            token["lemma"] = "たり"
        if token.get("surface") == "り" and previous.get("pos") == "Verb":
            surface = previous.get("surface", "")
            if surface.endswith("け"):
                previous["lemma"] = f"{surface[:-1]}く"
                token["pos"] = "Auxiliary"
                token["lemma"] = "り"


def postprocess_ka_suru_noun(tokens: list[dict]) -> None:
    """Keep 化-derived suru-verb nouns out of the na-adjective class."""
    for idx, token in enumerate(tokens[:-1]):
        if token.get("pos") != "Adjective" or not token.get("surface", "").endswith("化"):
            continue
        following = tokens[idx + 1]
        if following.get("surface") == "し" and following.get("pos") == "Verb":
            token["pos"] = "Noun"
            token["lemma"] = token.get("surface")


def postprocess_prolonged_sound_noun(tokens: list[dict]) -> None:
    """Keep a single-kanji lexical word after a prolonged mark out of suffix POS."""
    for idx, token in enumerate(tokens):
        if idx == 0 or token.get("pos") != "Suffix" or len(token.get("surface", "")) != 1:
            continue
        if tokens[idx - 1].get("surface", "").endswith("ー"):
            token["pos"] = "Noun"


def postprocess_yoshi_formal_noun(tokens: list[dict]) -> None:
    """Normalize よし as a formal noun in the negative knowledge construction."""
    for idx, token in enumerate(tokens):
        if token.get("surface") != "よし" or token.get("pos") != "Adjective" or idx == 0:
            continue
        if tokens[idx - 1].get("pos") != "Verb" or idx + 2 >= len(tokens):
            continue
        if tokens[idx + 1].get("surface") == "も" and tokens[idx + 2].get("surface") == "ない":
            token["pos"] = "Noun"
            token["lemma"] = "よし"


def postprocess_itadakeru_aux(tokens: list[dict]) -> None:
    """Treat potential いただける as a humble subsidiary verb after a predicate."""
    for idx, token in enumerate(tokens):
        if token.get("surface") != "いただける" or token.get("pos") != "Verb" or idx == 0:
            continue
        previous_pos = tokens[idx - 1].get("pos")
        if previous_pos in ("Verb", "Particle") or (
            previous_pos == "Noun" and idx > 1 and tokens[idx - 2].get("pos") == "Prefix"
        ):
            token["pos"] = "Auxiliary"


def postprocess_monono_conjunction(tokens: list[dict]) -> None:
    """Normalize concessive ものの as a closed connective particle."""
    for idx, token in enumerate(tokens):
        if token.get("surface") == "ものの" and idx > 0:
            if tokens[idx - 1].get("pos") in ("Verb", "Auxiliary", "Adjective"):
                token["pos"] = "Particle"
                token["lemma"] = "ものの"


def postprocess_formal_noun_lemma(tokens: list[dict]) -> bool:
    """Use canonical kana lemmas for 事/物 in productive formal-noun contexts."""
    canonical = {"事": "こと", "物": "もの"}
    changed = False
    for idx, token in enumerate(tokens):
        lemma = canonical.get(token.get("surface"))
        if lemma is None or token.get("pos") != "Noun" or idx == 0:
            continue
        previous = tokens[idx - 1]
        if previous.get("surface") != "の" and previous.get("pos") not in (
            "Verb",
            "Auxiliary",
            "Adjective",
            "Determiner",
        ):
            continue
        if token.get("lemma") != lemma:
            token["lemma"] = lemma
            changed = True
    return changed


def postprocess_adjective_nominalizer(tokens: list[dict]) -> bool:
    """Classify productive adjective + さ nominalization as a suffix."""
    changed = False
    for idx in range(1, len(tokens)):
        token = tokens[idx]
        previous = tokens[idx - 1]
        following_surface = tokens[idx + 1].get("surface", "") if idx + 1 < len(tokens) else ""
        if (
            token.get("surface") != "さ"
            or previous.get("pos") not in ("Adjective", "Auxiliary")
            or not previous.get("lemma", "").endswith("い")
            or following_surface.startswith(("れ", "せ"))
        ):
            continue
        if token.get("pos") != "Suffix":
            token["pos"] = "Suffix"
            token["lemma"] = "さ"
            changed = True
    return changed


def postprocess_binding_negative_aux(tokens: list[dict]) -> bool:
    """Keep the closed しか + negative predicate chain in auxiliary POS."""
    changed = False
    for idx in range(1, len(tokens)):
        token = tokens[idx]
        if token.get("surface") not in ("ない", "なく", "なかっ"):
            continue
        if tokens[idx - 1].get("surface") != "しか":
            continue
        if token.get("pos") != "Auxiliary":
            token["pos"] = "Auxiliary"
            token["lemma"] = "ない"
            changed = True
    return changed


def postprocess_difficulty_adjective_stem(tokens: list[dict]) -> bool:
    """Normalize にく before さ as the productive difficulty adjective stem."""
    changed = False
    for idx, token in enumerate(tokens[:-1]):
        if token.get("surface") != "にく" or tokens[idx + 1].get("surface") != "さ":
            continue
        token["pos"] = "Adjective"
        token["lemma"] = "にくい"
        changed = True
    return changed


def postprocess_sou_aux(tokens: list[dict]) -> None:
    """Fix そう after Auxiliary (しまい etc.): Adverb -> Auxiliary (様態)."""
    for i in range(1, len(tokens)):
        t = tokens[i]
        if t.get("surface") != "そう" or t.get("pos") != "Adverb":
            continue
        prev_pos = tokens[i - 1].get("pos", "")
        if prev_pos == "Auxiliary":
            t["pos"] = "Auxiliary"


def postprocess_n_kuruwa(tokens: list[dict]) -> None:
    """Normalize closed kuruwa polite auxiliaries."""
    index = 0
    while index + 1 < len(tokens):
        if tokens[index].get("surface") == "なん" and tokens[index + 1].get("surface") == "し":
            tokens[index : index + 2] = [{"surface": "なんし", "pos": "Auxiliary", "lemma": "ます"}]
            continue
        index += 1

    for i in range(1, len(tokens)):
        t = tokens[i]
        if t.get("surface") != "ん" or t.get("pos") != "Particle":
            continue
        prev = tokens[i - 1]
        if prev.get("surface") in ("あり", "あっ"):
            t["pos"] = "Auxiliary"
            t["lemma"] = "ん"


def postprocess_nai_context(tokens: list[dict]) -> None:
    """Correct ない/なく/なかっ POS: Auxiliary → Adjective after particles.

    Suzume treats standalone ない (doesn't exist) as Adjective, not Auxiliary.
    MeCab classifies it as 助動詞 in all contexts, but when ない follows
    particles like が/は/も, it's an existence adjective, not a negative auxiliary.

    Also handles sentence-initial ない and なく before て (adjective renyokei).
    """
    for idx, tok in enumerate(tokens):
        surface = tok.get("surface", "")
        pos = tok.get("pos", "")

        if surface not in ("ない", "なく", "なかっ") or pos != "Auxiliary":
            continue

        should_fix = False

        if idx == 0:
            # Sentence-initial ない/なく/なかっ → Adjective
            should_fix = True
        else:
            prev_pos = tokens[idx - 1].get("pos", "")
            prev_surface = tokens[idx - 1].get("surface", "")

            # After particle が/は/も → Adjective (existence negation)
            if prev_pos == "Particle" and prev_surface in ("が", "は", "も"):
                is_copular_negative = (
                    prev_surface == "は"
                    and idx >= 2
                    and tokens[idx - 2].get("surface") == "で"
                    and tokens[idx - 2].get("pos") == "Auxiliary"
                )
                should_fix = not is_copular_negative

            # なく before て → Adjective (renyokei of ない-adjective)
            elif surface == "なく" and idx + 1 < len(tokens):
                next_surface = tokens[idx + 1].get("surface", "")
                if next_surface == "て":
                    should_fix = True

        if should_fix:
            tok["pos"] = "Adjective"
            tok["lemma"] = "ない"


def postprocess_nara_verb(tokens: list[dict]) -> None:
    """Normalize なら in negative predicates and the limiting 〜のみならず chain."""
    for i in range(len(tokens) - 1):
        t = tokens[i]
        if t.get("surface") != "なら":
            continue
        prev_surface = tokens[i - 1].get("surface", "") if i > 0 else ""
        if t.get("pos") not in ("Auxiliary", "Particle"):
            if not (prev_surface == "のみ" and tokens[i + 1].get("surface") == "ず"):
                continue
        nxt_surface = tokens[i + 1].get("surface", "")
        if prev_surface == "のみ" and nxt_surface == "ず":
            t["pos"] = "Particle"
            t["lemma"] = "なら"
            continue
        if nxt_surface in ("ない", "なく", "なかっ", "ぬ"):
            t["pos"] = "Verb"
            t["lemma"] = "なる"
