"""Postprocessors ported from SuzumeUtils.pm _postprocess_* functions."""

import regex

from .constants import (
    EMPHATIC_SOKUON,
    INTERROGATIVES,
    SLANG_ADJ_STEMS,
    SLANG_VERB_STEMS,
    UNUSUAL_NAMES,
    WORD_EXCEPTIONS,
)
from .pos_mapping import _is_katakana_onomatopoeia


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
                if regex.match(r"^(?:だ|です|でし|じゃ|じゃろ)", nxt) and prev_pos != "Verb":
                    t["pos"] = "Adjective"
                # そう before で+ある (copula である)
                elif nxt == "で" and i < len(tokens) - 2:
                    nxt2 = tokens[i + 2].get("surface", "")
                    if nxt2 in ("ある", "あり", "あれ", "あっ"):
                        t["pos"] = "Adjective"
            elif i == 0:  # Sentence-initial そう before copula
                if i < len(tokens) - 1:
                    nxt = tokens[i + 1].get("surface", "")
                    if regex.match(r"^(?:だ|です|でし|じゃ|じゃろ)", nxt):
                        t["pos"] = "Adjective"
                    # そう before で+ある (copula である)
                    elif nxt == "で" and i < len(tokens) - 2:
                        nxt2 = tokens[i + 2].get("surface", "")
                        if nxt2 in ("ある", "あり", "あれ", "あっ"):
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
    """Context-dependent でも normalization."""
    for i, t in enumerate(tokens):
        if t.get("surface") != "でも" or t.get("pos") != "Particle":
            continue
        if i > 0 and tokens[i - 1].get("surface", "") in INTERROGATIVES:
            continue  # Keep as Particle
        t["pos"] = "Conjunction"


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
    """Fix い/いる after て: Verb -> Auxiliary."""
    for i in range(1, len(tokens)):
        t = tokens[i]
        surface = t.get("surface", "")
        if surface not in ("い", "いる", "いれ", "いた", "いない"):
            continue
        if t.get("pos") != "Verb":
            continue
        prev_surface = tokens[i - 1].get("surface", "")
        if prev_surface in ("て", "で"):
            t["pos"] = "Auxiliary"
            t["lemma"] = "いる"


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
        if (
            prefix.get("pos") == "Prefix"
            and prefix.get("surface") in ("お", "ご")
            and stem.get("pos") == "Noun"
            and surface
            and continuation.get("pos") == "Verb"
            and continuation.get("lemma") in ("くださる", "いたす", "いただく")
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


def postprocess_de_particle(tokens: list[dict]) -> None:
    """Keep the upstream copula/particle distinction for で unchanged."""
    pass


def postprocess_na_adj_noun(tokens: list[dict]) -> None:
    """No-op: kept for import compatibility. Adjective stems before すぎる stay Adjective."""
    pass


def postprocess_tsuke_noun(tokens: list[dict]) -> None:
    """Fix 付け: Suffix -> Noun."""
    for t in tokens:
        if t.get("surface") == "付け" and t.get("pos") == "Suffix":
            t["pos"] = "Noun"
            t["lemma"] = "付け"


def postprocess_copula_neg(tokens: list[dict]) -> None:
    """Fix なく after じゃ/で: Auxiliary -> Adjective."""
    for i in range(1, len(tokens)):
        t = tokens[i]
        if t.get("surface") != "なく" or t.get("pos") != "Auxiliary":
            continue
        prev = tokens[i - 1].get("surface", "")
        if prev in ("じゃ", "で"):
            t["pos"] = "Adjective"
            t["lemma"] = "ない"


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


def postprocess_gozai_verb(tokens: list[dict]) -> None:
    """Fix ござい after おめでとう: Auxiliary -> Verb."""
    for i in range(1, len(tokens)):
        t = tokens[i]
        if t.get("surface") == "ござい" and t.get("pos") == "Auxiliary":
            if tokens[i - 1].get("surface") == "おめでとう":
                t["pos"] = "Verb"
                t["lemma"] = "ござる"


def postprocess_you_noun(tokens: list[dict]) -> None:
    """Fix よう: Noun -> Auxiliary (Suzume treats よう as Auxiliary consistently)."""
    for t in tokens:
        if t.get("surface") == "よう" and t.get("pos") == "Noun":
            t["pos"] = "Auxiliary"


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
    """Fix ん in kuruwa dialect (after あり): Particle -> Auxiliary."""
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
                should_fix = True

            # なく before て → Adjective (renyokei of ない-adjective)
            elif surface == "なく" and idx + 1 < len(tokens):
                next_surface = tokens[idx + 1].get("surface", "")
                if next_surface == "て":
                    should_fix = True

        if should_fix:
            tok["pos"] = "Adjective"
            tok["lemma"] = "ない"


def postprocess_nara_verb(tokens: list[dict]) -> None:
    """Fix なら before negative auxiliaries: Auxiliary -> Verb(なる)."""
    for i in range(len(tokens) - 1):
        t = tokens[i]
        if t.get("surface") != "なら":
            continue
        if t.get("pos") not in ("Auxiliary", "Particle"):
            continue
        nxt_surface = tokens[i + 1].get("surface", "")
        if nxt_surface in ("ない", "なく", "なかっ", "ぬ"):
            t["pos"] = "Verb"
            t["lemma"] = "なる"
