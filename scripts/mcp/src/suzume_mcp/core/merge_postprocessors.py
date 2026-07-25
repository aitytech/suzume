"""Focused post-processing passes used by the MeCab merge pipeline."""

import regex

from .constants import (
    HONORIFIC_EXCEPTIONS,
    HONORIFIC_SUFFIXES,
    PREFIX_EXCEPTIONS,
    SEARCH_UNIT_COMPOUNDS,
)


def _postprocess_kamo(result: list[dict], applied_rule: str | None) -> list[dict]:
    """Merge か+も -> かも (compound particle)."""
    merged = []
    skip_next = False
    for j, curr in enumerate(result):
        if skip_next:
            skip_next = False
            continue
        if (
            j < len(result) - 1
            and curr.get("surface") == "か"
            and curr.get("pos") == "助詞"
            and result[j + 1].get("surface") == "も"
            and result[j + 1].get("pos") == "助詞"
        ):
            merged.append({"surface": "かも", "pos": "助詞", "lemma": "かも"})
            skip_next = True
        else:
            merged.append(curr)
    return merged


def _postprocess_totomoni(result: list[dict], applied_rule: str | None) -> tuple[list[dict], str | None]:
    """Merge the kanji spelling of the parallel compound particle と共に."""
    merged = []
    idx = 0
    while idx < len(result):
        if (
            idx + 1 < len(result)
            and result[idx].get("surface") == "と"
            and result[idx].get("pos") == "助詞"
            and result[idx + 1].get("surface") == "共に"
            and result[idx + 1].get("pos") == "副詞"
        ):
            merged.append({"surface": "と共に", "pos": "助詞", "lemma": "と共に"})
            idx += 2
            if applied_rule is None:
                applied_rule = "totomoni-compound-particle"
            continue
        if (
            idx + 2 < len(result)
            and result[idx].get("surface") == "と"
            and result[idx].get("pos") == "助詞"
            and result[idx + 1].get("surface") == "共"
            and result[idx + 2].get("surface") == "に"
            and result[idx + 2].get("pos") == "助詞"
        ):
            merged.append({"surface": "と共に", "pos": "助詞", "lemma": "と共に"})
            idx += 3
            if applied_rule is None:
                applied_rule = "totomoni-compound-particle"
            continue
        merged.append(result[idx])
        idx += 1
    return merged, applied_rule


def _postprocess_noni(result: list[dict], applied_rule: str | None) -> tuple[list[dict], str | None]:
    """Merge の+に -> のに after past tense."""
    merged = []
    skip_next = False
    for j, curr in enumerate(result):
        if skip_next:
            skip_next = False
            continue
        if (
            j >= 1
            and j < len(result) - 1
            and curr.get("surface") == "の"
            and result[j + 1].get("surface") == "に"
            and (result[j - 1].get("surface", "") in ("た", "っ", "だ") or result[j - 1].get("pos") == "助動詞")
        ):
            merged.append({"surface": "のに", "pos": "助詞", "lemma": "のに"})
            skip_next = True
            if applied_rule is None:
                applied_rule = "noni-merge"
        else:
            merged.append(curr)
    return merged, applied_rule


def _postprocess_atode(result: list[dict], applied_rule: str | None) -> tuple[list[dict], str | None]:
    """Split 後で(副詞) -> 後+で."""
    new_result = []
    for t in result:
        if t.get("surface") == "後で" and t.get("pos") == "副詞":
            new_result.append({"surface": "後", "pos": "名詞", "lemma": "後"})
            new_result.append({"surface": "で", "pos": "助詞", "lemma": "で"})
            if applied_rule is None:
                applied_rule = "atode-split"
        else:
            new_result.append(t)
    return new_result, applied_rule


def _postprocess_epenthetic_sa(result: list[dict]) -> None:
    """Fix epenthetic さ in adjective+さ+そう pattern."""
    for j in range(1, len(result) - 1):
        prev = result[j - 1]
        curr = result[j]
        nxt = result[j + 1]
        if (
            prev.get("pos") == "形容詞"
            and curr.get("surface") == "さ"
            and curr.get("pos_sub1") == "接尾"
            and nxt.get("surface") == "そう"
        ):
            curr["pos"] = "Suffix"
            curr.pop("pos_sub1", None)
            curr.pop("pos_sub2", None)


def _postprocess_honorific_split(result: list[dict], applied_rule: str | None) -> tuple[list[dict], str | None]:
    """Restore productive honorific boundaries hidden by lexicalized tokens."""
    honorific_re = "|".join(regex.escape(s) for s in HONORIFIC_SUFFIXES)

    new_result = []
    for t in result:
        surface = t.get("surface", "")
        if t.get("pos") == "動詞":
            predicate = regex.match(r"^(お|ご)([\p{Han}]+)(に)([\p{Hiragana}]+)$", surface)
            if predicate:
                prefix, noun, particle, verb_surface = predicate.groups()
                lemma_match = regex.match(
                    rf"^{regex.escape(prefix + noun + particle)}([\p{{Hiragana}}]+)$",
                    t.get("lemma", ""),
                )
                verb_lemma = lemma_match.group(1) if lemma_match else verb_surface
                new_result.extend(
                    [
                        {"surface": prefix, "pos": "接頭詞", "lemma": prefix},
                        {"surface": noun, "pos": "名詞", "lemma": noun},
                        {"surface": particle, "pos": "助詞", "lemma": particle},
                        {"surface": verb_surface, "pos": "動詞", "lemma": verb_lemma},
                    ]
                )
                if applied_rule is None:
                    applied_rule = "honorific-predicate-split"
                continue
            separated_predicate = regex.match(r"^([\p{Han}]+)(に)([\p{Han}\p{Hiragana}]+)$", surface)
            has_honorific_prefix = (
                new_result and new_result[-1].get("pos") == "接頭詞" and new_result[-1].get("surface") in {"お", "ご"}
            )
            if separated_predicate and has_honorific_prefix:
                noun, particle, verb_surface = separated_predicate.groups()
                lemma_match = regex.match(
                    rf"^{regex.escape(noun + particle)}([\p{{Han}}\p{{Hiragana}}]+)$",
                    t.get("lemma", ""),
                )
                verb_lemma = lemma_match.group(1) if lemma_match else verb_surface
                new_result.extend(
                    [
                        {"surface": noun, "pos": "名詞", "lemma": noun},
                        {"surface": particle, "pos": "助詞", "lemma": particle},
                        {"surface": verb_surface, "pos": "動詞", "lemma": verb_lemma},
                    ]
                )
                if applied_rule is None:
                    applied_rule = "honorific-predicate-split"
                continue
        if surface not in HONORIFIC_EXCEPTIONS:
            m = regex.match(rf"^(お)?([\p{{Han}}]+)({honorific_re})$", surface)
            if m:
                prefix, kanji, suffix = m.group(1), m.group(2), m.group(3)
                if prefix:
                    new_result.append({"surface": prefix, "pos": "接頭詞", "lemma": prefix})
                new_result.append({"surface": kanji, "pos": "名詞", "lemma": kanji})
                new_result.append({"surface": suffix, "pos": "名詞", "pos_sub1": "接尾", "lemma": suffix})
                if applied_rule is None:
                    applied_rule = "honorific-split"
                continue
        new_result.append(t)
    return new_result, applied_rule


def _postprocess_prefix_split(result: list[dict], applied_rule: str | None) -> tuple[list[dict], str | None]:
    """Split お/ご+noun patterns."""
    new_result = []
    for t in result:
        surface = t.get("surface", "")
        pos = t.get("pos", "")
        pos_sub1 = t.get("pos_sub1", "")
        if pos == "名詞" and pos_sub1 != "接尾" and surface not in PREFIX_EXCEPTIONS:
            m = regex.match(r"^(お|ご)([\p{Han}\p{Hiragana}]+)$", surface)
            if m:
                prefix, noun = m.group(1), m.group(2)
                new_result.append({"surface": prefix, "pos": "接頭詞", "lemma": prefix})
                new_result.append({"surface": noun, "pos": "名詞", "lemma": noun})
                if applied_rule is None:
                    applied_rule = "prefix-split"
                continue
        new_result.append(t)
    return new_result, applied_rule


def _postprocess_nde_split(result: list[dict], applied_rule: str | None) -> tuple[list[dict], str | None]:
    """Split contracted んでる verb forms."""
    new_result = []
    for t in result:
        surface = t.get("surface", "")
        pos = t.get("pos", "")
        m = regex.match(r"^(.+ん)(で)(る)$", surface)
        if pos == "動詞" and m:
            stem, de, ru = m.group(1), m.group(2), m.group(3)
            new_result.append({"surface": stem, "pos": "動詞", "lemma": t.get("lemma") or stem})
            new_result.append({"surface": de, "pos": "助詞", "lemma": de})
            new_result.append({"surface": ru, "pos": "動詞", "lemma": "いる"})
            if applied_rule is None:
                applied_rule = "nde-contract-split"
        else:
            new_result.append(t)
    return new_result, applied_rule


def _postprocess_filler_split(result: list[dict], applied_rule: str | None) -> tuple[list[dict], str | None]:
    """Split filler tokens like そうですね -> そう+です+ね."""
    new_result = []
    for t in result:
        surface = t.get("surface", "")
        pos = t.get("pos", "")
        m = regex.match(r"^(そう)(です)(ね|か|よ|よね)?$", surface)
        if pos == "フィラー" and m:
            sou, desu, particle = m.group(1), m.group(2), m.group(3)
            new_result.append({"surface": sou, "pos": "名詞", "pos_sub1": "形容動詞語幹", "lemma": sou})
            new_result.append({"surface": desu, "pos": "助動詞", "lemma": "です"})
            if particle:
                new_result.append({"surface": particle, "pos": "助詞", "lemma": particle})
            if applied_rule is None:
                applied_rule = "filler-split"
        else:
            new_result.append(t)
    return new_result, applied_rule


def _postprocess_kuruwa(result: list[dict], applied_rule: str | None) -> tuple[list[dict], str | None]:
    """Fix kuruwa kotoba segmentation: あ+りん -> あり+ん."""
    new_result = []
    skip_next = False
    for j, curr in enumerate(result):
        if skip_next:
            skip_next = False
            continue
        if curr.get("surface") == "あ" and j + 1 < len(result) and result[j + 1].get("surface") == "りん":
            new_result.append({"surface": "あり", "pos": "動詞", "lemma": "ある"})
            new_result.append({"surface": "ん", "pos": "助動詞", "lemma": "ん"})
            skip_next = True
            if applied_rule is None:
                applied_rule = "kuruwa-fix"
        else:
            new_result.append(curr)
    return new_result, applied_rule


def _postprocess_adj_bungo(result: list[dict], applied_rule: str | None) -> tuple[list[dict], str | None]:
    """Fix archaic adjective form: 恐し + いとも → 恐しい + と + も.

    When 形容詞 in 文語基本形 is followed by a token starting with い + particles,
    merge い back into the adjective and split out the particles.
    """
    new_result: list[dict] = []
    skip_next = False
    for j, curr in enumerate(result):
        if skip_next:
            skip_next = False
            continue
        if curr.get("pos") == "形容詞" and curr.get("conj_form") == "文語基本形" and j + 1 < len(result):
            nxt = result[j + 1]
            ns = nxt.get("surface", "")
            if ns.startswith("い"):
                adj_surface = curr["surface"] + "い"
                # The reference analyzer may normalize the lemma to another
                # modern spelling.  Suzume preserves the observed productive
                # -しい base, so the oracle must do the same after restoring
                # the split terminal い.
                new_result.append({"surface": adj_surface, "pos": "形容詞", "lemma": adj_surface})
                rest = ns[1:]
                for ch in rest:
                    new_result.append({"surface": ch, "pos": "助詞", "lemma": ch})
                skip_next = True
                if applied_rule is None:
                    applied_rule = "adj-bungo-fix"
                continue
        new_result.append(curr)
    return new_result, applied_rule


_KARI_TAILS = ("かり", "かる", "かれ")
_KARI_MIZENKEI_TAIL = "から"
_KARI_MAX_TOKEN_RUN = 4


def _kari_adjective_lemma(surface: str) -> str | None:
    """Return the modern lemma when a surface is a カリ-conjugation adjective form.

    The reference dictionary carries only the 未然形 cell of the supplementary
    conjugation (高から, 大きから), so that cell is used as the probe: a surface
    whose カリ ending can be swapped for から and still analyze as one 形容詞 is
    itself a cell of the same adjective's paradigm.
    """
    from .mecab import mecab_analyze

    probe = surface[: -len(_KARI_MIZENKEI_TAIL)] + _KARI_MIZENKEI_TAIL
    tokens = mecab_analyze(probe)
    if len(tokens) != 1:
        return None
    token = tokens[0]
    if token.get("pos") != "形容詞" or token.get("surface") != probe:
        return None
    return token.get("lemma")


def _postprocess_adj_kari(result: list[dict], applied_rule: str | None) -> tuple[list[dict], str | None]:
    """Rebuild the classical supplementary (カリ) conjugation of an i-adjective.

    から/かり/かる/かれ are cells of the adjective's own inflection table, not a
    stem plus an auxiliary: there is no 助動詞 かり in the classical inventory.
    The reference dictionary only carries the 未然形 cell, so the others fall
    back to unrelated verbs (高+かり as かりる, 冷+たかる as たかる, 小+さかり as
    さかる). Restore them as one 形容詞 token with the adjective's own lemma,
    which is what the 未然形 cell already yields.
    """
    merged: list[dict] = []
    idx = 0
    while idx < len(result):
        run = ""
        matched_end = 0
        matched_lemma = None
        for end in range(idx, min(idx + _KARI_MAX_TOKEN_RUN, len(result))):
            run += result[end].get("surface", "")
            # The rule repairs a split the dictionary got wrong, so a surface it
            # already analyzes as one word needs no repair (明かり stays a noun).
            if end == idx:
                continue
            if len(run) <= len(_KARI_MIZENKEI_TAIL) or not run.endswith(_KARI_TAILS):
                continue
            lemma = _kari_adjective_lemma(run)
            if lemma is not None:
                matched_end = end + 1
                matched_lemma = lemma
                break
        if matched_lemma is not None:
            surface = "".join(result[pos].get("surface", "") for pos in range(idx, matched_end))
            merged.append({"surface": surface, "pos": "形容詞", "lemma": matched_lemma})
            idx = matched_end
            if applied_rule is None:
                applied_rule = "adj-kari-conjugation"
            # The whole point of the かり cell is to carry a classical auxiliary,
            # so a lone し behind it is the 連体形 of the past き. The dictionary
            # never saw the かり token, so it read that し as the サ変
            # continuative it shares its spelling with.
            if (
                surface.endswith("かり")
                and idx < len(result)
                and result[idx].get("surface") == "し"
                and result[idx].get("pos") == "動詞"
            ):
                merged.append({"surface": "し", "pos": "助動詞", "lemma": "き"})
                idx += 1
            continue
        merged.append(result[idx])
        idx += 1
    return merged, applied_rule


_A_ROW_TO_U_ROW = {
    "か": "く",
    "が": "ぐ",
    "さ": "す",
    "た": "つ",
    "な": "ぬ",
    "ば": "ぶ",
    "ま": "む",
    "ら": "る",
    "わ": "う",
}
_CLASSICAL_IRREALIS_AUX = "む"


def _is_single_verb(surface: str) -> bool:
    """Whether the reference dictionary reads a surface as exactly one verb."""
    from .mecab import mecab_analyze

    tokens = mecab_analyze(surface)
    return len(tokens) == 1 and tokens[0].get("pos") == "動詞" and tokens[0].get("surface") == surface


def _postprocess_classical_mu(result: list[dict], applied_rule: str | None) -> tuple[list[dict], str | None]:
    """Restore the boundary of the classical conjectural む.

    む attaches to a verb's 未然形 (読ま+む, 書か+む), but the reference dictionary
    carries no such auxiliary. It therefore either hands the irrealis kana to a
    lexical verb spanning the boundary (書+かむ, read as 噛む) or swallows む into
    a longer idiom (読ま+むとする). Rebuild 未然形 + む and re-analyze the rest.
    """
    from .mecab import mecab_analyze

    merged: list[dict] = []
    idx = 0
    while idx < len(result):
        token = result[idx]
        surface = token.get("surface", "")
        previous = merged[-1] if merged else None
        # The irrealis kana was handed to the following verb (書 + かむ).
        if (
            previous is not None
            and token.get("pos") == "動詞"
            and len(surface) == 2
            and surface[0] in _A_ROW_TO_U_ROW
            and surface[1] == _CLASSICAL_IRREALIS_AUX
        ):
            stem = previous.get("surface", "")
            lemma = stem + _A_ROW_TO_U_ROW[surface[0]]
            if stem and _is_single_verb(lemma):
                merged[-1] = {"surface": stem + surface[0], "pos": "動詞", "lemma": lemma}
                merged.append({"surface": surface[1], "pos": "助動詞", "lemma": surface[1]})
                idx += 1
                if applied_rule is None:
                    applied_rule = "classical-mu-boundary"
                continue
        # む opened a longer idiom the dictionary lists as one word (むとする).
        if (
            previous is not None
            and previous.get("pos") == "動詞"
            and previous.get("surface", "")[-1:] in _A_ROW_TO_U_ROW
            and len(surface) > 1
            and surface[0] == _CLASSICAL_IRREALIS_AUX
        ):
            merged.append({"surface": surface[0], "pos": "助動詞", "lemma": surface[0]})
            merged.extend(mecab_analyze(surface[1:]))
            idx += 1
            if applied_rule is None:
                applied_rule = "classical-mu-boundary"
            continue
        merged.append(token)
        idx += 1
    return merged, applied_rule


_E_ROW_TO_U_ROW = {
    "え": "う",
    "け": "く",
    "げ": "ぐ",
    "せ": "す",
    "て": "つ",
    "ね": "ぬ",
    "べ": "ぶ",
    "め": "む",
    "れ": "る",
}
_CONCESSIVE_PARTICLES = ("ど", "ども")


def _postprocess_izenkei_concessive(result: list[dict], applied_rule: str | None) -> tuple[list[dict], str | None]:
    """Give the 已然形 before a concessive conjunction its plain verb lemma.

    ど/ども select the 已然形, which the modern paradigm spells like the
    hypothetical (書け+ど, 飲め+ど). The potential verb of the same stem reaches
    that conjunction only through its own 已然形 (書けれ+ど), so a bare e-row form
    here belongs to the plain verb. The reference dictionary splits the two
    readings by row, giving 飲む for one and 書ける for the other.
    """
    tagged: list[dict] = []
    for idx, token in enumerate(result):
        following = result[idx + 1] if idx + 1 < len(result) else None
        surface = token.get("surface", "")
        if (
            following is not None
            and following.get("surface") in _CONCESSIVE_PARTICLES
            and following.get("pos") == "助詞"
            and token.get("pos") == "動詞"
            and token.get("lemma") == surface + "る"
            and surface[-1:] in _E_ROW_TO_U_ROW
        ):
            plain = surface[:-1] + _E_ROW_TO_U_ROW[surface[-1]]
            if _is_single_verb(plain):
                tagged.append({**token, "lemma": plain})
                if applied_rule is None:
                    applied_rule = "izenkei-concessive-lemma"
                continue
        tagged.append(token)
    return tagged, applied_rule


_CLASSICAL_CAUSATIVE_FORMS = ("しむ", "しめ", "しむる", "しむれ")


def _postprocess_classical_shimu(result: list[dict], applied_rule: str | None) -> tuple[list[dict], str | None]:
    """Tag the classical causative しむ as an auxiliary.

    しむ conjugates 下二段 and attaches to the same 未然形 as the modern せる, but
    the reference dictionary has no such auxiliary and falls back to a lexical
    verb of the same spelling (書か+しむ). After an irrealis there is no verb
    reading available, so the cell is the auxiliary.
    """
    tagged: list[dict] = []
    for idx, token in enumerate(result):
        previous = result[idx - 1] if idx > 0 else None
        if (
            previous is not None
            and previous.get("pos") == "動詞"
            and previous.get("surface", "")[-1:] in _A_ROW_TO_U_ROW
            and token.get("surface") in _CLASSICAL_CAUSATIVE_FORMS
        ):
            tagged.append({**token, "pos": "助動詞", "lemma": "しむ"})
            if applied_rule is None:
                applied_rule = "classical-shimu-auxiliary"
            continue
        tagged.append(token)
    return tagged, applied_rule


_HA_ROW_TAILS = ("は", "ひ", "ふ", "へ")
_HA_ROW_DETACHED_TAILS = ("ひ", "ふ", "へ")
_HA_ROW_STEM_POS = ("名詞", "動詞", "形容詞", "副詞", "接尾辞")


def _ha_row_fabricated_ichidan(token: dict) -> bool:
    """Whether a verb token's lemma is the 一段 reading invented for a ハ行 cell."""
    surface = token.get("surface", "")
    return token.get("pos") == "動詞" and token.get("lemma") == surface + "る"


def _postprocess_ha_row_godan(result: list[dict], applied_rule: str | None) -> tuple[list[dict], str | None]:
    """Rebuild the classical ハ行四段 conjugation (候ふ, 移ろひ, 思へ).

    ハ行四段 is the historical-kana spelling of the modern ワ行五段 row, so
    は/ひ/ふ/へ are cells of one verb whose terminal form is the lemma. The
    reference dictionary carries the row only for the handful of verbs it
    happens to list (思ふ); everywhere else the cell kana falls out as a separate
    one-mora verb with an invented 一段 lemma (候+ふ as ふる, 移ろ+ひ as ひる), or
    the whole cell keeps such a lemma (思へ as 思へる). Reattach the detached
    kana to its stem and give both the row's own terminal ふ.
    """
    merged: list[dict] = []
    idx = 0
    while idx < len(result):
        token = result[idx]
        following = result[idx + 1] if idx + 1 < len(result) else None
        if (
            following is not None
            and following.get("surface") in _HA_ROW_DETACHED_TAILS
            and _ha_row_fabricated_ichidan(following)
            and token.get("pos") in _HA_ROW_STEM_POS
        ):
            stem = token.get("surface", "")
            merged.append({"surface": stem + following["surface"], "pos": "動詞", "lemma": stem + "ふ"})
            idx += 2
            if applied_rule is None:
                applied_rule = "ha-row-godan-conjugation"
            continue
        # The classical honorific stem is tagged as a suffix, and its imperative
        # cell then falls out as the direction particle (給+へ). A suffix never
        # takes that particle, so the pair is one 命令形 of the ハ行四段 verb.
        if (
            following is not None
            and token.get("pos") == "名詞"
            and token.get("pos_sub1") == "接尾"
            and following.get("surface") in _HA_ROW_DETACHED_TAILS
            and following.get("pos") == "助詞"
        ):
            stem = token.get("surface", "")
            merged.append({"surface": stem + following["surface"], "pos": "動詞", "lemma": stem + "ふ"})
            idx += 2
            if applied_rule is None:
                applied_rule = "ha-row-godan-conjugation"
            continue
        surface = token.get("surface", "")
        if len(surface) > 1 and surface.endswith(_HA_ROW_TAILS) and _ha_row_fabricated_ichidan(token):
            merged.append({**token, "lemma": surface[:-1] + "ふ"})
            idx += 1
            if applied_rule is None:
                applied_rule = "ha-row-godan-conjugation"
            continue
        merged.append(token)
        idx += 1
    return merged, applied_rule


def _postprocess_nickname_merge(result: list[dict], applied_rule: str | None) -> tuple[list[dict], str | None]:
    """Merge hiragana nickname + honorific into a single token.

    Tokenizer use case: short hiragana nicknames like たっちゃん / ゆうちゃん /
    けんちゃん / わんちゃん should be one search unit, not split as stem+suffix.
    Kanji or katakana names (太郎+ちゃん, ピー+ちゃん) keep splitting.

    Also handles MeCab's misparses where the nickname spans 3+ tokens
    (e.g., たっちゃん → たっ + ちゃ + ん). The scan greedily concatenates
    consecutive hiragana tokens (preceding `prev_is_prefix == False`) and
    merges when the concatenated surface is short hiragana stem + honorific.
    """
    honorifics = ("ちゃん", "くん", "さん")
    hira_re = regex.compile(r"^[\p{Hiragana}っー]+$")

    merged: list[dict] = []
    i = 0
    while i < len(result):
        # Hiragana run starting at i that ends with a honorific. Skip if prev
        # is a prefix (お/ご) — let family-merge handle those.
        prev_is_prefix = merged and merged[-1].get("pos", "") == "接頭詞"
        if not prev_is_prefix and i < len(result) and hira_re.match(result[i].get("surface", "")):
            j = i
            run = ""
            while j < len(result) and hira_re.match(result[j].get("surface", "")):
                run += result[j].get("surface", "")
                j += 1
            matched = False
            for h in honorifics:
                if run.endswith(h):
                    stem = run[: len(run) - len(h)]
                    # Stem 2-3 hiragana chars. 1-char stems (e.g., おさん) are
                    # too short and risk false merges (がおさん → が+おさん bad).
                    if 2 <= len(stem) <= 3:
                        merged.append({"surface": run, "pos": "名詞", "lemma": run})
                        i = j
                        if applied_rule is None:
                            applied_rule = "nickname-merge"
                        matched = True
                        break
            if matched:
                continue
            # No nickname match — append the current token and continue
            merged.append(result[i])
            i += 1
            continue
        merged.append(result[i])
        i += 1
    return merged, applied_rule


def _postprocess_kanji_merge(result: list[dict], applied_rule: str | None) -> tuple[list[dict], str | None]:
    """Merge consecutive all-kanji tokens.

    Also merges single-kanji + kanji-starting tokens when MeCab incorrectly
    splits compound words (e.g., 微+笑み → 微笑み).
    """
    # Specific kanji+hiragana compounds that MeCab splits incorrectly
    _KANJI_HIRA_MERGES = {"微": {"笑み"}}

    merged = []
    for index, curr in enumerate(result):
        surface = curr.get("surface", "")
        # Suzume design: tokenizer use case prefers X+suffix as a single search
        # unit. These suffixes are not treated as token boundaries; X+SUFFIX
        # merges via kanji-merge.
        #   家/力/化/法/論/員/式/感/的 — productive but one search unit
        # 様/氏 keep splitting (honorific separates from name).
        is_merge_allowed_suffix = surface in ("家", "力", "化", "法", "論", "員", "式", "感", "的", "風")
        # Suzume design: 御 is a productive prefix that always splits off
        # (御 + 尽力, 御 + 挨拶, 御 + 協力). Skip kanji-merge after 御 prefix tokens.
        prev_is_go_prefix = merged and merged[-1].get("surface", "") == "御" and merged[-1].get("pos", "") == "接頭詞"
        # A non-independent noun followed by an attributive na-adjective is a
        # grammatical boundary (時 + 不思議 + な), not a kanji compound. MeCab
        # tags the adjective stem as a noun, so the generic kanji merge would
        # otherwise erase that boundary before POS normalization.
        next_is_attributive_na = (
            index + 1 < len(result)
            and result[index + 1].get("surface", "") == "な"
            and result[index + 1].get("pos", "") == "助動詞"
        )
        formal_noun_na_adjective_boundary = (
            merged
            and merged[-1].get("pos", "") == "名詞"
            and merged[-1].get("pos_sub1", "") == "非自立"
            and curr.get("pos", "") == "名詞"
            and curr.get("pos_sub1", "") == "形容動詞語幹"
            and next_is_attributive_na
        )
        if merged and (
            (
                regex.match(r"^[\p{Han}]+$", surface)
                and regex.match(r"^[\p{Han}]+$", merged[-1].get("surface", ""))
                # Kanji adjacency alone is not a compound boundary.  In
                # particular, a temporal noun followed by a one-kanji verb
                # stem (the pattern 日+見+た) must retain the predicate
                # boundary.  Restrict this recovery pass to nominal pieces.
                and curr.get("pos", "") == "名詞"
                and merged[-1].get("pos", "") == "名詞"
                and "々" not in merged[-1].get("surface", "")
                and (merged[-1].get("pos_sub1", "") not in ("副詞可能", "固有名詞", "数") or is_merge_allowed_suffix)
                and merged[-1].get("pos", "") != "副詞"
                and (curr.get("pos_sub1", "") != "接尾" or is_merge_allowed_suffix)
                # A number+counter unit (五分, 二時間, 五名) is its own search unit and
                # must not fold into a preceding noun/prefix (徒歩|五分, 約|二時間).
                and curr.get("pos_sub1", "") != "数"
                and not prev_is_go_prefix
                and not formal_noun_na_adjective_boundary
            )
            or (surface == "々" and regex.match(r"^[\p{Han}]+$", merged[-1].get("surface", "")))
            or (
                merged[-1].get("surface", "") in _KANJI_HIRA_MERGES
                and surface in _KANJI_HIRA_MERGES[merged[-1]["surface"]]
            )
        ):
            merged[-1]["surface"] += surface
            merged[-1]["lemma"] = merged[-1]["surface"]
            merged[-1]["pos"] = "名詞"
            if applied_rule is None:
                applied_rule = "kanji-merge"
        else:
            merged.append(curr)
    return merged, applied_rule


def _postprocess_search_unit_split(result: list[dict], applied_rule: str | None) -> tuple[list[dict], str | None]:
    """Re-split kanji-merged tokens that absorbed part of a search-unit compound.

    Example: kanji-merge produces AB+C, but BC should be one token.
    This splits AB → A+B, then merges B+C → BC.
    """
    new_result: list[dict] = []
    skip_next = False
    for j, curr in enumerate(result):
        if skip_next:
            skip_next = False
            continue
        if j < len(result) - 1:
            nxt = result[j + 1]
            curr_surface = curr.get("surface", "")
            nxt_surface = nxt.get("surface", "")
            for word, word_pos in SEARCH_UNIT_COMPOUNDS.items():
                # Check if word spans across curr (ending) + nxt (beginning)
                for split_pos in range(1, len(word)):
                    prefix = word[:split_pos]
                    suffix = word[split_pos:]
                    if curr_surface.endswith(prefix) and nxt_surface == suffix:
                        head = curr_surface[: -len(prefix)]
                        if head:
                            new_result.append({"surface": head, "pos": curr.get("pos", ""), "lemma": head})
                        new_result.append({"surface": word, "pos": word_pos, "lemma": word})
                        skip_next = True
                        if applied_rule is None:
                            applied_rule = "search-unit-split"
                        break
                if skip_next:
                    break
        if not skip_next or j < len(result) - 1:
            if not skip_next:
                new_result.append(curr)
    # Handle last token if not skipped
    if not skip_next and len(result) > 0:
        pass  # Already appended in the loop
    return new_result, applied_rule


def _postprocess_ascii_dot_merge(result: list[dict], applied_rule: str | None) -> tuple[list[dict], str | None]:
    """Merge ASCII + dot + ASCII/number tokens."""
    merged = []
    for j, curr in enumerate(result):
        surface = curr.get("surface", "")
        if (
            surface == "."
            and merged
            and regex.match(r"^[a-zA-Z]+$", merged[-1].get("surface", ""))
            and j + 1 < len(result)
            and regex.match(r"^[a-zA-Z0-9]+$", result[j + 1].get("surface", ""))
        ):
            merged[-1]["surface"] += "."
            merged[-1]["lemma"] = merged[-1]["surface"]
            if applied_rule is None:
                applied_rule = "ascii-dot-merge"
        elif merged and merged[-1].get("surface", "").endswith(".") and regex.match(r"^[a-zA-Z0-9]+$", surface):
            merged[-1]["surface"] += surface
            merged[-1]["lemma"] = merged[-1]["surface"]
        else:
            merged.append(curr)
    return merged, applied_rule


def _postprocess_onomatopoeia_tto_merge(result: list[dict], applied_rule: str | None) -> tuple[list[dict], str | None]:
    """Merge onomatopoeia stem + っと → Xっと (adverb).

    MeCab splits: どき+っと, ぱっ+と, etc.
    Suzume treats Xっと as a single adverb unit.
    Pattern: short hiragana/katakana token + っと where stem is 1-4 chars.
    """
    merged: list[dict] = []
    skip_next = False
    for j, curr in enumerate(result):
        if skip_next:
            skip_next = False
            continue
        if (
            j < len(result) - 1
            and result[j + 1].get("surface") == "っと"
            and regex.match(r"^[\p{Hiragana}\p{Katakana}ー]{1,4}$", curr.get("surface", ""))
        ):
            combined = curr.get("surface", "") + "っと"
            merged.append({"surface": combined, "pos": "副詞", "lemma": combined})
            skip_next = True
            if applied_rule is None:
                applied_rule = "onomatopoeia-tto-merge"
        else:
            merged.append(curr)
    return merged, applied_rule


def _is_productive_mimetic_stem(surface: str) -> bool:
    """Recognize productive hiragana mimetic shapes without a word list."""
    if not regex.fullmatch(r"[\p{Hiragana}ー]{3,12}", surface):
        return False
    length = len(surface)
    if length % 2 == 0 and surface[: length // 2] == surface[length // 2 :]:
        return True
    if regex.fullmatch(r".{2,4}ん.{2,4}ん", surface):
        return True
    if regex.fullmatch(r".っ.[らり]", surface):
        return True
    # Alternating two-mora mimetics such as ちくたく share their closing
    # mora even when the two halves are not identical.
    return length == 4 and surface[1] == surface[3]


def _postprocess_productive_mimetics(result: list[dict], applied_rule: str | None) -> tuple[list[dict], str | None]:
    """Rebuild productive mimetic search units from arbitrary MeCab splits.

    Repetition and fixed phonological shapes are lexical content; a following
    adverbial と remains its own particle.  The productive Xっと shape instead
    includes と in the mimetic itself.
    """
    normalized: list[dict] = []
    idx = 0
    while idx < len(result):
        matched = False
        max_end = min(len(result), idx + 4)
        for end in range(max_end, idx, -1):
            combined = "".join(token.get("surface", "") for token in result[idx:end])
            starts_at_real_boundary = (
                result[idx].get("pos") != "助詞" or idx == 0 or result[idx - 1].get("pos") == "記号"
            )
            if (
                starts_at_real_boundary
                and combined.endswith("っと")
                and regex.fullmatch(r"[\p{Hiragana}ー]{3,12}", combined)
            ):
                normalized.append({"surface": combined, "pos": "副詞", "lemma": combined})
                idx = end
                matched = True
            elif (
                result[idx].get("pos") != "助詞"
                and combined.endswith("と")
                and _is_productive_mimetic_stem(combined[:-1])
            ):
                stem = combined[:-1]
                normalized.append({"surface": stem, "pos": "副詞", "lemma": stem})
                normalized.append({"surface": "と", "pos": "助詞", "lemma": "と"})
                idx = end
                matched = True
            elif (
                end == idx + 1
                and result[idx].get("pos") in {"その他", "副詞", "感動詞"}
                and _is_productive_mimetic_stem(combined)
            ):
                normalized.append({"surface": combined, "pos": "副詞", "lemma": combined})
                idx = end
                matched = True
            if matched:
                if applied_rule is None:
                    applied_rule = "productive-mimetic"
                break
        if not matched:
            normalized.append(result[idx])
            idx += 1
    return normalized, applied_rule


def _is_quantity_unit(surface: str) -> bool:
    """Return whether surface is a productive numeral+kanji unit."""
    numeric = regex.match(r"^[0-9０-９一二三四五六七八九十百千万億兆]+", surface)
    if numeric is None:
        return False
    unit = surface[numeric.end() :]
    return bool(unit and regex.fullmatch(r"[\p{Han}]+", unit))


def _postprocess_distributive_quantity(result: list[dict], applied_rule: str | None) -> tuple[list[dict], str | None]:
    """Merge repeated numeral+unit phrases such as 一語一語 structurally."""
    normalized: list[dict] = []
    idx = 0
    while idx < len(result):
        surface = result[idx].get("surface", "")
        split_prefix = ""
        for width in range(2, len(surface) // 2 + 1):
            unit = surface[:width]
            if surface.startswith(unit + unit) and _is_quantity_unit(unit):
                split_prefix = unit + unit
                break
        if result[idx].get("pos") == "名詞" and split_prefix:
            normalized.append({"surface": split_prefix, "pos": "名詞", "lemma": split_prefix})
            remainder = surface[len(split_prefix) :]
            if remainder:
                normalized.append({"surface": remainder, "pos": "名詞", "lemma": remainder})
            idx += 1
            if applied_rule is None:
                applied_rule = "distributive-quantity"
            continue
        if (
            idx + 1 < len(result)
            and result[idx].get("pos") == "名詞"
            and result[idx + 1].get("pos") == "名詞"
            and result[idx + 1].get("surface") == surface
            and _is_quantity_unit(surface)
        ):
            combined = surface + surface
            normalized.append({"surface": combined, "pos": "名詞", "lemma": combined})
            idx += 2
            if applied_rule is None:
                applied_rule = "distributive-quantity"
            continue
        normalized.append(result[idx])
        idx += 1
    return normalized, applied_rule


def _postprocess_dialectal(result: list[dict]) -> None:
    """Fix POS/lemmas for dialectal/special patterns."""
    for j, curr in enumerate(result):
        surface = curr.get("surface", "")
        if surface in ("おいで", "お出で"):
            curr["pos"] = "副詞"
            curr["lemma"] = "おいで"
        if j < len(result) - 1:
            nxt = result[j + 1]
            if surface == "なん" and nxt.get("surface") == "し":
                curr["pos"] = "名詞"
                curr["lemma"] = "なん"
                nxt["pos"] = "動詞"
                nxt["lemma"] = "する"
