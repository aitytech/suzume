"""Postprocessors ported from SuzumeUtils.pm _postprocess_* functions."""

import regex

from .constants import (
    ADVERB_NOMINAL_HOMOGRAPHS,
    COMPOUND_VERB_V2_GODAN,
    COMPOUND_VERB_V2_ICHIDAN,
    COPULA_SURFACES,
    COUNTER_UNITS,
    EMPHATIC_SOKUON,
    INTERROGATIVES,
    QUANTITY_BOUND_SUFFIXES,
    SLANG_ADJ_STEMS,
    SLANG_VERB_STEMS,
    UNUSUAL_NAMES,
    WORD_EXCEPTIONS,
)
from .mecab import mecab_analyze
from .pos_mapping import _is_katakana_onomatopoeia
from .split_rules import base_from_mizenkei, base_from_renyokei

_PRODUCTIVE_COMPOUND_V2 = frozenset(COMPOUND_VERB_V2_GODAN + COMPOUND_VERB_V2_ICHIDAN)


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


# The ない-family cells a predicate can spell: ない / なく(て) / なかっ(た) /
# なけれ(ば) / なけりゃ / なきゃ. The analyzer cuts each of them at the tail, so
# only the head is matched here.
_NAI_NEGATIVE_HEAD = regex.compile(r"^な(い|く|かっ|けれ|けりゃ|きゃ)")


def repair_kko_nominalizer(tokens: list[dict]) -> None:
    """Rebuild the bound nominalizer っこ before a ない-family predicate.

    The reference dictionary has no entry for っこ, so it reads the two morae as
    the emphatic sokuon plus the irrealis of 来る and then reconstructs a verb
    around whatever is left: 負ける becomes 負/ける with the okurigana glued to the
    sokuon (負+けっ+こ), できる becomes で+きっ+こ, and a stem whose okurigana is
    already a full continuative simply keeps a standalone っ (分かり+っ+こ). Every
    host breaks, so the suffix is restored here rather than corrected per word.

    The continuative in front of the suffix is recovered by re-analyzing the
    prefix under ます, which selects that cell and nothing else, and the ない that
    follows is left as MeCab tagged it — it is the predicate of the construction.
    Re-analysis starts after the previous repair, because feeding an already
    repaired っこ back to the analyzer would only break it the same way again.
    """
    idx = 1
    repaired_end = 0
    while idx < len(tokens) - 1:
        token = tokens[idx]
        previous = tokens[idx - 1]
        if (
            token.get("surface") != "こ"
            or not previous.get("surface", "").endswith("っ")
            or not _NAI_NEGATIVE_HEAD.match(tokens[idx + 1].get("surface", ""))
        ):
            idx += 1
            continue
        prefix = "".join(t.get("surface", "") for t in tokens[repaired_end:idx])[:-1]
        continuative = mecab_analyze(prefix + "ます")
        if not continuative or continuative[-1].get("surface") != "ます":
            idx += 1
            continue
        suffix = {"surface": "っこ", "pos": "名詞", "pos_sub1": "接尾", "lemma": "っこ"}
        tokens[repaired_end : idx + 1] = [*continuative[:-1], suffix]
        repaired_end += len(continuative)
        idx = repaired_end
    return


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
                # The explanatory な+の/ん chain takes the same reading as the
                # bare copula: after a terminal verb, そう is the hearsay
                # auxiliary in both (読む+そう+だ, 読む+そう+な+ん+だ).
                if prev_pos != "Verb" and (
                    regex.match(r"^(?:だ|です|でし|じゃ|じゃろ)", nxt)
                    or (nxt == "な" and i + 2 < len(tokens) and tokens[i + 2].get("surface") in ("の", "ん"))
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
                        nxt == "な" and i + 2 < len(tokens) and tokens[i + 2].get("surface") in ("の", "ん")
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
    adverbial_ambiguities = frozenset({"また", "やや", "およそ", "すこぶる", "おおいに", "つとめて"})
    changed = False
    for idx, token in enumerate(tokens):
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
        # MeCab can carry a suffix reading across the following content word
        # after a formal adverb (つとめて水を...).  At this proven adverbial
        # boundary the following lexical token is an ordinary noun.
        if (
            idx + 2 < len(tokens)
            and tokens[idx + 1].get("pos") == "Suffix"
            and tokens[idx + 2].get("pos") == "Particle"
        ):
            tokens[idx + 1]["pos"] = "Noun"
            tokens[idx + 1]["lemma"] = tokens[idx + 1].get("surface", "")
    return changed


def postprocess_closed_subsidiary_aux(tokens: list[dict]) -> bool:
    """Mirror the core's finite renyokei-attaching subsidiary class."""
    lemmas = {
        "かね": "かねる",
        "かねる": "かねる",
        "たまえ": "たまう",
        "そびれ": "そびれる",
        "そびれる": "そびれる",
        "あぐね": "あぐねる",
        "あぐねる": "あぐねる",
        "そこね": "そこねる",
        "そこない": "そこなう",
        "そこなう": "そこなう",
        "そこなっ": "そこなう",
        "そこなわ": "そこなう",
        "そこなえ": "そこなう",
    }
    changed = False
    for idx in range(1, len(tokens)):
        previous = tokens[idx - 1]
        token = tokens[idx]
        lemma = lemmas.get(token.get("surface", ""))
        if previous.get("pos") != "Verb" or lemma is None:
            continue
        token["pos"] = "Auxiliary"
        token["lemma"] = lemma
        changed = True
    for idx, token in enumerate(tokens):
        surface = token.get("surface", "")
        if surface == "じゃろ":
            if token.get("pos") != "Auxiliary" or token.get("lemma") != "だろ":
                token["pos"] = "Auxiliary"
                token["lemma"] = "だろ"
                changed = True
        elif surface == "ござら" and idx > 0 and tokens[idx - 1].get("surface") == "で":
            if token.get("pos") != "Auxiliary" or token.get("lemma") != "ござる":
                token["pos"] = "Auxiliary"
                token["lemma"] = "ござる"
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


def postprocess_shimau_aux(tokens: list[dict]) -> bool:
    """Normalize the closed completive auxiliary paradigm.

    Besides the kanji spelling, repair MeCab's occasional analysis of the
    voiced contraction ``じゃう`` as the copula ``じゃ`` plus an
    interjection ``う``.  A preceding verbal nasal-onbin stem makes the
    completive reading grammatical and unambiguous.
    """
    shimau_forms = frozenset({"仕舞う", "仕舞わ", "仕舞い", "仕舞っ", "仕舞え", "仕舞お"})
    contracted_forms = frozenset(
        {
            "ちゃう",
            "ちゃわ",
            "ちゃい",
            "ちゃっ",
            "ちゃえ",
            "ちゃお",
            "じゃう",
            "じゃわ",
            "じゃい",
            "じゃっ",
            "じゃえ",
            "じゃお",
        }
    )
    contracted_endings = frozenset("うわいっえお")
    changed = False
    idx = 1
    while idx < len(tokens):
        token = tokens[idx]
        previous = tokens[idx - 1]
        if token.get("surface") in shimau_forms and previous.get("surface") in ("て", "で"):
            token["pos"] = "Auxiliary"
            token["lemma"] = "しまう"
            changed = True
            idx += 1
            continue

        surface = token.get("surface", "")
        previous_is_host = previous.get("pos") == "Verb" or (
            surface.startswith("ちゃ") and previous.get("pos") == "Auxiliary"
        )
        if (
            previous_is_host
            and surface in contracted_forms
            and (not surface.startswith("じゃ") or previous.get("surface", "").endswith("ん"))
        ):
            token["pos"] = "Auxiliary"
            token["lemma"] = surface[:2] + "う"
            changed = True
            idx += 1
            continue

        if (
            idx + 1 < len(tokens)
            and previous_is_host
            and surface in ("ちゃ", "じゃ")
            and tokens[idx + 1].get("surface") in contracted_endings
            and (surface != "じゃ" or previous.get("surface", "").endswith("ん"))
        ):
            contracted = surface + tokens[idx + 1]["surface"]
            tokens[idx : idx + 2] = [{"surface": contracted, "pos": "Auxiliary", "lemma": surface + "う"}]
            changed = True
        idx += 1
    return changed


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
    verb_suffixes = frozenset({"がち", "っぱなし", "たて", "まくり"})
    changed = False
    for idx in range(len(tokens) - 1):
        stem = tokens[idx]
        suffix = tokens[idx + 1]
        if suffix.get("surface") not in verb_suffixes:
            continue
        if suffix.get("surface") == "まくり" and stem.get("pos") == "Verb":
            if suffix.get("pos") != "Suffix" or suffix.get("lemma") != "まくり":
                suffix["pos"] = "Suffix"
                suffix["lemma"] = "まくり"
                changed = True
        if suffix.get("pos") != "Suffix":
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


def postprocess_adjective_garu(tokens: list[dict]) -> bool:
    """Mirror the core's verb POS for productive adjective-stem + がる."""
    garu_forms = frozenset({"がら", "がり", "がる", "がれ", "がろ", "がっ"})
    changed = False
    for idx in range(1, len(tokens)):
        token = tokens[idx]
        if tokens[idx - 1].get("pos") != "Adjective" or token.get("surface") not in garu_forms:
            continue
        if token.get("pos") != "Verb" or token.get("lemma") != "がる":
            token["pos"] = "Verb"
            token["lemma"] = "がる"
            changed = True
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

        if idx > 0 and idx + 1 < len(tokens) and surface == "で" and tokens[idx + 1].get("surface") == "も":
            if tokens[idx - 1].get("surface") in INTERROGATIVES:
                tokens[idx : idx + 2] = [{"surface": "でも", "pos": "Particle", "lemma": "でも"}]
                changed = True
                continue
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
        previous = tokens[idx - 1]
        token = tokens[idx]
        if token.get("lemma") not in ("行く", "いく", "ゆく") and token.get("surface") not in (
            "いこ",
            "ゆこ",
            "いく",
            "ゆく",
        ):
            continue
        connective_te_de = previous.get("surface") == "て" or (
            previous.get("surface") == "で" and idx >= 2 and tokens[idx - 2].get("pos") == "Verb"
        )
        if connective_te_de and previous.get("pos") == "Particle":
            if token.get("pos") != "Auxiliary":
                token["pos"] = "Auxiliary"
                changed = True
        elif previous.get("pos") == "Verb" and token.get("pos") != "Verb":
            token["pos"] = "Verb"
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
    """Resolve honorific continuatives as predicates or nominal search units.

    Some analyzers classify a nominally homographic stem such as ``立ち`` as a
    noun even though ``ください``, ``いたす``, ``いただく``, or their potential
    benefactive supplies a verbal continuation. Productive continuative stems
    recover their dictionary form from conjugation structure without a lexical
    exception table.
    """
    changed = False
    for idx in range(1, len(tokens)):
        prefix = tokens[idx - 1]
        stem = tokens[idx]
        nominal_context = idx + 1 == len(tokens) or tokens[idx + 1].get("pos") == "Particle"
        if (
            prefix.get("pos") == "Prefix"
            and prefix.get("surface") in ("お", "ご")
            and stem.get("pos") == "Verb"
            and regex.search(r"\p{Han}", stem.get("surface", ""))
            and nominal_context
        ):
            stem["pos"] = "Noun"
            stem["lemma"] = stem.get("surface", "")
            changed = True
    for idx in range(1, len(tokens) - 1):
        prefix = tokens[idx - 1]
        stem = tokens[idx]
        continuation = tokens[idx + 1]
        surface = stem.get("surface", "")
        is_direct_honorific_continuation = continuation.get("pos") in ("Verb", "Auxiliary") and continuation.get(
            "lemma"
        ) in (
            "くださる",
            "いたす",
            "いただく",
            "いただける",
            "申し上げる",
        )
        is_honorific_naru = (
            continuation.get("surface") == "に"
            and continuation.get("pos") == "Particle"
            and idx + 2 < len(tokens)
            and tokens[idx + 2].get("pos") in ("Verb", "Auxiliary")
            and tokens[idx + 2].get("lemma") == "なる"
        )
        if (
            prefix.get("pos") == "Prefix"
            and prefix.get("surface") in ("お", "ご")
            and stem.get("pos") in ("Noun", "Suffix")
            and surface
            and (is_direct_honorific_continuation or is_honorific_naru)
        ):
            lemma = base_from_renyokei(surface)
            if lemma is not None:
                stem["pos"] = "Verb"
                stem["lemma"] = lemma
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


def postprocess_te_form_contraction(tokens: list[dict]) -> bool:
    """Tag じゃ after an onbin verb as the te-form contraction, like ちゃ.

    ちゃ (= ては) and じゃ (= では) are one paradigm; only the voicing of the
    conjunctive particle differs, selected by the onbin stem in front of it
    (書い+ちゃ, 読ん+じゃ, 泳い+じゃ). MeCab already reads ちゃ as a particle but
    reads じゃ as the copula だ, which would put a copula straight onto a verb
    continuative. The copula reading stays untouched after a nominal (本じゃない).
    """
    changed = False
    onbin_tails = ("ん", "い")
    for idx in range(1, len(tokens)):
        token = tokens[idx]
        if token.get("surface") != "じゃ" or token.get("pos") != "Auxiliary":
            continue
        previous = tokens[idx - 1]
        if previous.get("pos") != "Verb" or not previous.get("surface", "").endswith(onbin_tails):
            continue
        token["pos"] = "Particle"
        token["lemma"] = "じゃ"
        changed = True
    return changed


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


def postprocess_hiragana_yaka_adverbial(tokens: list[dict]) -> bool:
    """Repair a split hiragana na-adjective in the productive 〜やかに form."""
    for idx in range(len(tokens) - 1):
        combined = tokens[idx].get("surface", "") + tokens[idx + 1].get("surface", "")
        if len(combined) < 4 or not combined.endswith("やかに") or not regex.fullmatch(r"\p{Hiragana}+", combined):
            continue
        adjective = combined[:-1]
        tokens[idx : idx + 2] = [
            {"surface": adjective, "pos": "Adjective", "lemma": adjective},
            {"surface": "に", "pos": "Particle", "lemma": "に"},
        ]
        return True
    return False


def postprocess_dewa_aru_boundary(tokens: list[dict]) -> bool:
    """Split copular で + binding は before lexical ある."""
    for idx in range(len(tokens) - 1):
        token = tokens[idx]
        following = tokens[idx + 1]
        if token.get("surface") != "では" or following.get("pos") != "Verb" or following.get("lemma") != "ある":
            continue
        tokens[idx : idx + 1] = [
            {"surface": "で", "pos": "Auxiliary", "lemma": "だ"},
            {"surface": "は", "pos": "Particle", "lemma": "は"},
        ]
        return True
    return False


def _is_irrealis_before_negative(surface: str) -> bool:
    """Whether a surface is the irrealis stem the negative auxiliary selects.

    書か+なく+ない keeps its verb reading, because the negative attaches to the
    irrealis; 変わり+なく is the continuative that also serves as a deverbal noun.
    The reference dictionary names the difference in the probe's conjugated form.
    """
    from .mecab import mecab_analyze

    probe = mecab_analyze(surface + "ない")
    return (
        len(probe) == 2
        and probe[0].get("surface") == surface
        and probe[0].get("pos") == "動詞"
        and probe[0].get("conj_form") == "未然形"
    )


def postprocess_deverbal_noun_context(tokens: list[dict]) -> bool:
    """Normalize a continuative verb used as the head of a noun phrase.

    A non-finite verb form cannot itself take を/が/の.  When MeCab emits a
    continuative surface immediately before one of those particles, the same
    surface is the productive deverbal noun (読みを, いとなみが, 書きかけの).
    Finite verbs such as 読むの and continuative verb chains remain unchanged.
    The polite conjectural copula likewise selects a nominal predicate
    (曇りでしょう), not a bare continuative verb.
    """
    changed = False
    for idx, token in enumerate(tokens[:-1]):
        if token.get("pos") != "Verb":
            continue
        surface = token.get("surface", "")
        lemma = token.get("lemma", surface)
        if not surface or not lemma or surface == lemma:
            continue
        following = tokens[idx + 1]
        honorific_naru = (
            idx > 0
            and tokens[idx - 1].get("pos") == "Prefix"
            and tokens[idx - 1].get("surface") in {"お", "ご", "御"}
            and following.get("surface") == "に"
            and idx + 2 < len(tokens)
            and tokens[idx + 2].get("pos") in {"Verb", "Auxiliary"}
            and tokens[idx + 2].get("lemma") == "なる"
        )
        if honorific_naru:
            continue
        nominal_particle = following.get("pos") == "Particle" and following.get("surface") in {"を", "が", "の"}
        if following.get("surface") == "に":
            after_particle = tokens[idx + 2] if idx + 2 < len(tokens) else None
            motion_lemmas = {"行く", "来る", "いく", "くる", "ゆく"}
            nominal_particle = after_particle is None or after_particle.get("lemma") not in motion_lemmas
        nominal_follower = following.get("surface") in {"方", "ひとつ"}
        predicative_copula = following.get("pos") == "Auxiliary" and following.get("surface") in {"でしょ"}
        nominal_negative = (
            following.get("pos") == "Adjective"
            and following.get("surface") == "なく"
            and following.get("lemma") == "ない"
            and not _is_irrealis_before_negative(surface)
        )
        if not nominal_particle and not nominal_follower and not predicative_copula and not nominal_negative:
            continue
        token["pos"] = "Noun"
        token["lemma"] = surface
        changed = True
    return changed


def postprocess_attributive_mamonaku(tokens: list[dict]) -> bool:
    """Split temporal 間+も+なく after an attributive predicate.

    Clause-initial 間もなく is a lexical adverb, while 休む間もなく contains
    an independently modified formal noun and two closed grammatical units.
    """
    for idx in range(1, len(tokens)):
        token = tokens[idx]
        if token.get("surface") != "間もなく" or token.get("pos") not in ("Adverb", "Adjective"):
            continue
        if tokens[idx - 1].get("pos") not in ("Verb", "Adjective", "Auxiliary"):
            continue
        tokens[idx : idx + 1] = [
            {"surface": "間", "pos": "Noun", "lemma": "間"},
            {"surface": "も", "pos": "Particle", "lemma": "も"},
            {"surface": "なく", "pos": "Adjective", "lemma": "ない"},
        ]
        return True
    return False


def postprocess_adverb_nominal_context(tokens: list[dict]) -> bool:
    """Restore nominal readings of adverb homographs in particle frames."""
    changed = False
    for idx in range(len(tokens) - 1):
        token = tokens[idx]
        following = tokens[idx + 1]
        if token.get("pos") != "Adverb" or following.get("pos") != "Particle":
            continue
        particle = following.get("surface")
        is_accusative = particle == "を"
        is_lexical_homograph_frame = token.get("surface") in ADVERB_NOMINAL_HOMOGRAPHS and particle in (
            "を",
            "の",
            "は",
            "が",
            "も",
            "に",
            "で",
        )
        if not is_accusative and not is_lexical_homograph_frame:
            continue
        token["pos"] = "Noun"
        token["lemma"] = token.get("surface", "")
        changed = True
    return changed


def postprocess_temporal_nao(tokens: list[dict]) -> bool:
    """Use adverbial なお after a temporal adverb (いまなお)."""
    changed = False
    for idx in range(1, len(tokens)):
        previous = tokens[idx - 1]
        token = tokens[idx]
        if previous.get("pos") == "Adverb" and token.get("surface") == "なお" and token.get("pos") == "Conjunction":
            token["pos"] = "Adverb"
            token["lemma"] = "なお"
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
    """Treat classical けむ/らむ after a predicate as auxiliaries.

    The analyzer knows neither auxiliary. After a verb it at least keeps the two
    morae together; after a nominal predicate it splits them into a plural
    suffix plus an unknown noun (確認+ら+む), which is rejoined here.
    """
    for idx in range(len(tokens) - 1, 0, -1):
        token = tokens[idx]
        if token.get("surface") in ("けむ", "らむ") and tokens[idx - 1].get("pos") == "Verb":
            token["pos"] = "Auxiliary"
            token["lemma"] = token["surface"]
            continue
        if (
            idx >= 2
            and (tokens[idx - 1].get("surface"), token.get("surface")) in (("け", "む"), ("ら", "む"))
            and tokens[idx - 2].get("pos") in ("Noun", "Verb")
        ):
            merged = tokens[idx - 1].get("surface", "") + "む"
            tokens[idx - 1 : idx + 1] = [{"surface": merged, "pos": "Auxiliary", "lemma": merged}]


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
    """Normalize the split classical desiderative ま + ほし chain.

    ほし is the terminal cell of the same adjective the attributive ほしき
    spells, and the analyzer splits both the same way.
    """
    changed = False
    for idx, token in enumerate(tokens[:-1]):
        if token.get("surface") != "ま" or tokens[idx + 1].get("surface") not in ("ほし", "ほしき"):
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


def postprocess_classical_perfect_aux(tokens: list[dict]) -> bool:
    """Normalize terminal たり and 已然形+り as classical auxiliaries."""
    changed = False
    for idx, token in enumerate(tokens):
        if idx == 0:
            continue
        previous = tokens[idx - 1]
        if token.get("surface") == "たり" and (idx == len(tokens) - 1 or tokens[idx + 1].get("surface") == "けり"):
            if previous.get("pos") == "Noun":
                lemma = base_from_renyokei(previous.get("surface", ""))
                if lemma is not None:
                    previous["pos"] = "Verb"
                    previous["lemma"] = lemma
                    changed = True
            if previous.get("pos") == "Verb":
                if token.get("pos") != "Auxiliary" or token.get("lemma") != "たり":
                    token["pos"] = "Auxiliary"
                    token["lemma"] = "たり"
                    changed = True
        if token.get("surface") == "り" and previous.get("pos") == "Verb":
            surface = previous.get("surface", "")
            if surface.endswith("け"):
                previous["lemma"] = f"{surface[:-1]}く"
                token["pos"] = "Auxiliary"
                token["lemma"] = "り"
                changed = True
    return changed


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
        if (
            token.get("lemma") != "いただける"
            or not token.get("surface", "").startswith("いただけ")
            or token.get("pos") != "Verb"
            or idx == 0
        ):
            continue
        previous = tokens[idx - 1]
        follows_te_form = previous.get("pos") == "Particle" and previous.get("surface") in {"て", "で"}
        follows_predicate = previous.get("pos") == "Verb"
        follows_honorific_nominal = (
            previous.get("pos") == "Noun"
            and idx > 1
            and tokens[idx - 2].get("pos") == "Prefix"
            and tokens[idx - 2].get("surface") in {"お", "ご", "御"}
        )
        if follows_te_form or follows_predicate or follows_honorific_nominal:
            token["pos"] = "Auxiliary"


def postprocess_monono_conjunction(tokens: list[dict]) -> None:
    """Normalize concessive ものの as a closed connective particle."""
    for idx, token in enumerate(tokens):
        if token.get("surface") == "ものの" and idx > 0:
            if tokens[idx - 1].get("pos") in ("Verb", "Auxiliary", "Adjective"):
                token["pos"] = "Particle"
                token["lemma"] = "ものの"


def postprocess_formal_noun_lemma(tokens: list[dict]) -> bool:
    """Normalize productive formal nouns selected by closed grammar contexts."""
    canonical = {"事": "こと", "物": "もの"}
    changed = False
    for idx, token in enumerate(tokens):
        if (
            idx > 0
            and token.get("surface") == "ため"
            and tokens[idx - 1].get("surface") == "が"
            and tokens[idx - 1].get("pos") == "Particle"
        ):
            if token.get("pos") != "Noun" or token.get("lemma") != "ため":
                token["pos"] = "Noun"
                token["lemma"] = "ため"
                changed = True
            continue
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


def postprocess_shortened_causative_passive(tokens: list[dict]) -> bool:
    """Classify the bound さ in a Godan shortened causative-passive chain."""
    changed = False
    a_row_endings = frozenset("あかがさざただなはばぱまらわ")
    for idx in range(1, len(tokens) - 1):
        previous = tokens[idx - 1]
        token = tokens[idx]
        following = tokens[idx + 1]
        previous_surface = previous.get("surface", "")
        if (
            token.get("surface") != "さ"
            or token.get("pos") != "Verb"
            or token.get("lemma") != "する"
            or previous.get("pos") != "Verb"
            or not previous_surface
            or previous_surface[-1] not in a_row_endings
            or following.get("pos") != "Auxiliary"
            or not following.get("surface", "").startswith("れ")
        ):
            continue
        token["pos"] = "Auxiliary"
        token["lemma"] = "す"
        changed = True
    return changed


def postprocess_productive_search_unit_boundaries(tokens: list[dict]) -> bool:
    """Align productive boundaries without enumerating open-class hosts.

    Every branch is licensed by a closed follower class or an inflectional
    shape.  The function therefore generalizes across arbitrary noun and verb
    hosts while retaining Suzume's search-unit compounds.
    """
    changed = False
    idx = 0
    while idx < len(tokens):
        token = tokens[idx]
        surface = token.get("surface", "")

        if idx + 1 < len(tokens) and surface == "ん" and tokens[idx + 1].get("surface") == "かっ":
            tokens[idx : idx + 2] = [{"surface": "んかっ", "pos": "Auxiliary", "lemma": "ない"}]
            changed = True
            continue

        if idx + 1 < len(tokens) and surface == "づく" and tokens[idx + 1].get("surface") == "め":
            tokens[idx : idx + 2] = [{"surface": "づくめ", "pos": "Suffix", "lemma": "づくめ"}]
            changed = True
            continue

        if idx + 1 < len(tokens) and regex.fullmatch(r"([あいうえお])\1+", surface):
            following = tokens[idx + 1].get("surface", "")
            if following and set(following) == {surface[0]}:
                token["surface"] = surface + following
                token["lemma"] = token["surface"]
                token["pos"] = "Adverb"
                del tokens[idx + 1]
                changed = True
                continue

        if idx + 1 < len(tokens) and surface == "うす" and tokens[idx + 1].get("pos") == "Adjective":
            following = tokens[idx + 1]
            combined = surface + following.get("surface", "")
            tokens[idx : idx + 2] = [{"surface": combined, "pos": "Adjective", "lemma": combined}]
            changed = True
            continue

        # A compound nominal host immediately selected by the closed
        # がましい construction is one search unit (X+V-renyokei + がましい).
        if (
            idx + 3 < len(tokens)
            and tokens[idx + 2].get("surface") == "が"
            and tokens[idx + 3].get("surface") == "ましい"
        ):
            following = tokens[idx + 1]
            if token.get("pos") == "Noun" and following.get("pos") in ("Noun", "Verb"):
                combined = surface + following.get("surface", "")
                tokens[idx : idx + 2] = [{"surface": combined, "pos": "Noun", "lemma": combined}]
                changed = True
                continue

        # Calendar heads bind to the closed 末/翌+counter units while a
        # following deverbal payment stem remains its own search unit.
        if idx + 1 < len(tokens) and tokens[idx + 1].get("surface") == "末締め":
            if surface in COUNTER_UNITS:
                tokens[idx : idx + 2] = [
                    {"surface": surface + "末", "pos": "Noun", "lemma": surface + "末"},
                    {"surface": "締め", "pos": "Noun", "lemma": "締め"},
                ]
                changed = True
                idx += 2
                continue

        if surface == "翌" and idx + 1 < len(tokens):
            following_surface = tokens[idx + 1].get("surface", "")
            unit = next((candidate for candidate in COUNTER_UNITS if following_surface.startswith(candidate)), "")
            remainder = following_surface[len(unit) :]
            if unit and remainder:
                tokens[idx : idx + 2] = [
                    {"surface": surface + unit, "pos": "Noun", "lemma": surface + unit},
                    {"surface": remainder, "pos": "Noun", "lemma": remainder},
                ]
                changed = True
                idx += 2
                continue

        # MeCab can analyze productive V1+合わせる as a causative chain
        # (見合わ+せる, つめあわ+せ).  The internal 合わ/あわ boundary
        # recovers the same closed V2 class without naming V1 hosts.  A bare
        # continuative directly selected by a nominal particle is a deverbal
        # compound noun; finite せる remains a compound verb.
        compound_alignment_stem = next(
            (ending for ending in ("合わ", "あわ") if surface.endswith(ending) and len(surface) > len(ending)),
            None,
        )
        if idx + 2 < len(tokens) and compound_alignment_stem is not None:
            following = tokens[idx + 1]
            nominal_particle = tokens[idx + 2]
            if (
                following.get("surface") == "せ"
                and nominal_particle.get("pos") == "Particle"
                and nominal_particle.get("surface") in {"を", "は", "が", "の", "に", "で", "へ", "と", "も"}
            ):
                combined = surface + "せ"
                tokens[idx : idx + 2] = [{"surface": combined, "pos": "Noun", "lemma": combined}]
                changed = True
                continue

        if idx + 1 < len(tokens) and compound_alignment_stem is not None:
            following = tokens[idx + 1]
            if following.get("surface") == "せる":
                combined = surface + "せる"
                lemma = surface[: -len(compound_alignment_stem)] + "合わせる"
                tokens[idx : idx + 2] = [{"surface": combined, "pos": "Verb", "lemma": lemma}]
                changed = True
                continue

        if idx + 1 < len(tokens) and token.get("pos") in ("Verb", "Noun"):
            following = tokens[idx + 1]
            if following.get("pos") == "Verb":
                v2_base = following.get("lemma", "")
                if v2_base not in _PRODUCTIVE_COMPOUND_V2 and following.get("surface", "").endswith("せる"):
                    potential_base = following.get("surface", "")[:-2] + "す"
                    if potential_base in _PRODUCTIVE_COMPOUND_V2:
                        v2_base = potential_base
                renyokei_base = base_from_renyokei(surface)
                if (
                    v2_base in _PRODUCTIVE_COMPOUND_V2
                    and token.get("pos") == "Verb"
                    and renyokei_base == token.get("lemma")
                ):
                    combined = surface + following.get("surface", "")
                    compound_lemma = combined if following.get("surface", "").endswith("せる") else surface + v2_base
                    tokens[idx : idx + 2] = [{"surface": combined, "pos": "Verb", "lemma": compound_lemma}]
                    changed = True
                    continue

        # MeCab exposes the shortened causative mora on the host token
        # (やらさ+れ); Suzume keeps host+さ+れ as three morphemes.
        if idx + 1 < len(tokens) and token.get("pos") == "Verb" and surface.endswith("さ"):
            following = tokens[idx + 1]
            host = surface[:-1]
            host_lemma = base_from_mizenkei(host)
            is_regular_sa_row = token.get("lemma", "").endswith("す") and token.get("lemma", "")[:-1] == host
            if (
                host_lemma
                and not is_regular_sa_row
                and following.get("pos") == "Auxiliary"
                and following.get("surface", "").startswith("れ")
            ):
                tokens[idx : idx + 1] = [
                    {"surface": host, "pos": "Verb", "lemma": host_lemma},
                    {"surface": "さ", "pos": "Auxiliary", "lemma": "す"},
                ]
                changed = True
                idx += 2
                continue

        # Volitional よう is morphologically the o-row stem + auxiliary う.
        if (
            idx + 1 < len(tokens)
            and (token.get("pos") == "Verb" or (token.get("pos") == "Noun" and token.get("lemma") == "する"))
            and tokens[idx + 1].get("surface") == "よう"
            and tokens[idx + 1].get("pos") == "Auxiliary"
        ):
            token["pos"] = "Verb"
            token["surface"] = surface + "よ"
            tokens[idx + 1] = {"surface": "う", "pos": "Auxiliary", "lemma": "う"}
            changed = True
            idx += 2
            continue

        # Denominal colloquial verbs before progressive ている expose the
        # geminate on the noun in Suzume (過疎っ+て+いる).
        if idx + 2 < len(tokens) and token.get("pos") == "Noun" and regex.search(r"\p{Han}", surface):
            following = tokens[idx + 1]
            progressive = tokens[idx + 2]
            if following.get("surface") == "って" and progressive.get("lemma") == "いる":
                tokens[idx] = {"surface": surface + "っ", "pos": "Verb", "lemma": surface + "る"}
                tokens[idx + 1] = {"surface": "て", "pos": "Particle", "lemma": "て"}
                progressive["pos"] = "Auxiliary"
                changed = True

        if surface == "ましい" and idx > 0 and tokens[idx - 1].get("surface") == "が":
            token["pos"] = "Adjective"
            token["lemma"] = "ましい"
            changed = True

        if (
            idx > 0
            and idx + 1 < len(tokens)
            and surface == "あり"
            and tokens[idx - 1].get("surface") == "でも"
            and tokens[idx + 1].get("pos") == "Auxiliary"
        ):
            token["pos"] = "Noun"
            token["lemma"] = "あり"
            changed = True

        if surface == "他" and idx + 1 < len(tokens) and tokens[idx + 1].get("surface") == "の":
            token["lemma"] = "ほか"
            changed = True

        if surface == "ただ" and idx + 1 < len(tokens) and tokens[idx + 1].get("pos") == "Pronoun":
            token["pos"] = "Adverb"
            token["lemma"] = "ただ"
            changed = True

        if surface == "反し" and idx > 0 and tokens[idx - 1].get("surface") == "に":
            token["lemma"] = "反する"
            changed = True

        if (
            surface == "おいで"
            and idx + 2 < len(tokens)
            and tokens[idx + 1].get("surface") == "に"
            and tokens[idx + 2].get("lemma") == "なる"
        ):
            token["pos"] = "Noun"
            token["lemma"] = "おいで"
            changed = True

        if token.get("pos") == "Adjective" and surface.endswith("く") and idx + 1 < len(tokens):
            following = tokens[idx + 1]
            if following.get("pos") == "Adjective" and not following.get("surface", "").startswith(
                ("ない", "なく", "なかっ", "なけれ")
            ):
                token["pos"] = "Adverb"
                token["lemma"] = surface
                changed = True

        if surface == "どう" and idx + 1 < len(tokens) and tokens[idx + 1].get("surface") == "か":
            token["pos"] = "Adverb"
            token["lemma"] = "どう"
            changed = True

        if surface == "で" and idx > 0:
            previous = tokens[idx - 1]
            if token.get("pos") == "Auxiliary" and (
                previous.get("surface") == "せい" or regex.fullmatch(r"\p{Katakana}+", previous.get("surface", ""))
            ):
                token["pos"] = "Particle"
                token["lemma"] = "で"
                changed = True
            elif token.get("pos") == "Particle" and previous.get("pos") == "Adjective":
                token["pos"] = "Auxiliary"
                token["lemma"] = "だ"
                changed = True

        if surface == "どき" and idx > 0 and tokens[idx - 1].get("pos") == "Noun":
            token["pos"] = "Noun"
            token["lemma"] = "どき"
            changed = True

        if token.get("pos") == "Verb" and surface.endswith("れる") and token.get("lemma") != surface:
            token["lemma"] = surface
            changed = True

        if surface == "行っ" and idx > 0 and tokens[idx - 1].get("surface") in {"に", "へ"}:
            token["lemma"] = "行く"
            changed = True

        if (
            token.get("pos") == "Noun"
            and idx > 0
            and tokens[idx - 1].get("pos") == "Prefix"
            and idx + 1 < len(tokens)
            and tokens[idx + 1].get("surface") == "し"
        ):
            reconstructed = base_from_renyokei(surface)
            if reconstructed is not None:
                token["pos"] = "Verb"
                token["lemma"] = reconstructed
                changed = True

        if idx == len(tokens) - 1 and token.get("pos") == "Adjective" and regex.fullmatch(r"\p{Han}+よ", surface):
            token["pos"] = "Verb"
            token["lemma"] = surface[:-1] + "る"
            changed = True

        if token.get("pos") == "Adjective" and not token.get("lemma", "").endswith("い"):
            if surface.endswith("化"):
                token["pos"] = "Noun"
                token["lemma"] = surface
                changed = True

        idx += 1
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

    Also handles sentence-initial ない and the continuative なく, whose reading
    follows from what it attaches to rather than from what follows it.
    """
    for idx, tok in enumerate(tokens):
        surface = tok.get("surface", "")
        pos = tok.get("pos", "")

        if surface not in ("ない", "なく", "なかっ") or pos != "Auxiliary":
            continue

        # The negative auxiliary attaches to a predicate stem, so its
        # continuative keeps that reading after a verb or a verbal auxiliary
        # whatever follows (飲ま+なく+ちゃ, 食べ+なく+て, れ+なく+なる). The
        # copula is excluded because its negation uses the supplementary
        # adjective (本+で+なく), except when the topical particle separates
        # the two (本+で+は+なく). Everything else follows a nominal or an
        # adjective continuative and takes the adjective (休み+なく, 明るく+なく).
        if surface == "なく" and idx > 0:
            prev_pos = tokens[idx - 1].get("pos", "")
            prev_surface = tokens[idx - 1].get("surface", "")
            is_copular_topic = (
                prev_pos == "Particle"
                and prev_surface == "は"
                and idx >= 2
                and tokens[idx - 2].get("surface") == "で"
                and tokens[idx - 2].get("pos") == "Auxiliary"
            )
            is_predicate_stem = prev_pos == "Verb" or (prev_pos == "Auxiliary" and prev_surface not in COPULA_SURFACES)
            if not (is_predicate_stem or is_copular_topic):
                tok["pos"] = "Adjective"
                tok["lemma"] = "ない"
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

            # A negative auxiliary cannot attach directly to a noun, nor to a
            # suffix that derives one. In a bare nominal predicate, ない is the
            # independent adjective with an omitted nominative marker (問題ない,
            # 関係ない, 負けっこない).
            elif prev_pos in ("Noun", "Suffix"):
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


def postprocess_bound_derived_adjective(tokens: list[dict]) -> bool:
    """Rejoin the bound suffix がまし〜 when it was split at its first mora.

    がまし〜 derives an i-adjective from a nominal host (未練がましい, 恩着せがましく).
    The reference dictionary knows a few of those adjectives lexically and keeps
    them whole, but for every other host it falls back to the case particle が
    plus a remainder that is not a word at all, so the same suffix is analyzed
    two ways depending on which host it sits on.

    Only an adjective cell licenses the merge: the nominal まし takes the copula
    instead (こちらの方がましだ), and that が really is the subject marker. Runs at
    the very end of the pipeline because the compound merges that assemble the
    host come first, and they read the same が.
    """
    cells = ("ましい", "ましく", "ましかっ", "ましけれ", "ましかろ")
    nominalized_cell = "まし"
    changed = False
    idx = 1
    while idx + 1 < len(tokens):
        host = tokens[idx - 1]
        particle = tokens[idx]
        suffix = tokens[idx + 1]
        follower = tokens[idx + 2].get("surface") if idx + 2 < len(tokens) else None
        licensed = suffix.get("surface", "") in cells or (
            suffix.get("surface", "") == nominalized_cell and follower == "さ"
        )
        if host.get("pos") not in ("Noun", "Verb") or particle.get("surface") != "が" or not licensed:
            idx += 1
            continue
        host["surface"] = host.get("surface", "") + particle.get("surface", "") + suffix.get("surface", "")
        host["pos"] = "Adjective"
        host["lemma"] = host["surface"].removesuffix(suffix.get("surface", "")) + "ましい"
        del tokens[idx : idx + 2]
        changed = True
    return changed


def postprocess_quotative_determiner_spelling(tokens: list[dict]) -> bool:
    """Move the boundary of なんと+いう onto the pronoun plus quotative determiner.

    The reference dictionary reads 何という as the interrogative pronoun plus the
    quotative 連体詞, but reads its kana spelling なんという as the exclamatory
    adverb なんと plus the verb いう. The construction is the same one; only the
    script differs, so the kana spelling inherits the kanji spelling's boundary.

    Only the uninflected いう directly before a noun qualifies. An inflected form
    is the genuine adverb-plus-verb reading (なんといっても), and so is いう before
    anything other than a noun (なんというか).
    """
    changed = False
    idx = 0
    while idx + 2 < len(tokens):
        adverb, verb, head = tokens[idx], tokens[idx + 1], tokens[idx + 2]
        if adverb.get("surface") != "なんと" or verb.get("surface") != "いう" or head.get("pos") != "Noun":
            idx += 1
            continue
        adverb["surface"] = "なん"
        adverb["pos"] = "Pronoun"
        adverb["lemma"] = "なん"
        verb["surface"] = "という"
        verb["pos"] = "Determiner"
        verb["lemma"] = "という"
        changed = True
        idx += 2
    return changed


def postprocess_adverbial_na_adjective(tokens: list[dict]) -> bool:
    """Tag a degree word as an adjective in the cells its copula supplies.

    A word such as 大変 is an adverb and an adjectival noun at once. The
    reference dictionary already tags the adjectival reading before the
    attributive な, but keeps the adverb tag before the terminal だ, so one
    paradigm is split across two parts of speech by cell rather than by
    grammar. Only the copula licenses the change; a directly modified predicate
    keeps the adverb (大変おいしい).
    """
    from .constants import ADVERBIAL_NA_ADJECTIVES

    changed = False
    for idx, token in enumerate(tokens[:-1]):
        follower = tokens[idx + 1]
        if (
            token.get("surface") not in ADVERBIAL_NA_ADJECTIVES
            or token.get("pos") != "Adverb"
            or follower.get("pos") != "Auxiliary"
            or follower.get("surface") not in ("だ", "です", "な", "でし", "だっ", "なら")
        ):
            continue
        token["pos"] = "Adjective"
        changed = True
    return changed
