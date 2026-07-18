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
    """Split honorific patterns like 皆様 -> 皆+様."""
    honorific_re = "|".join(regex.escape(s) for s in HONORIFIC_SUFFIXES)

    new_result = []
    for t in result:
        surface = t.get("surface", "")
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
            if ns.startswith("い") and len(ns) > 1:
                adj_surface = curr["surface"] + "い"
                lemma = curr.get("lemma", "")
                if not lemma or lemma == curr["surface"]:
                    lemma = adj_surface
                new_result.append({"surface": adj_surface, "pos": "形容詞", "lemma": lemma})
                rest = ns[1:]
                for ch in rest:
                    new_result.append({"surface": ch, "pos": "助詞", "lemma": ch})
                skip_next = True
                if applied_rule is None:
                    applied_rule = "adj-bungo-fix"
                continue
        new_result.append(curr)
    return new_result, applied_rule


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
        is_merge_allowed_suffix = surface in ("家", "力", "化", "法", "論", "員", "式", "感", "的")
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
                and "々" not in merged[-1].get("surface", "")
                and merged[-1].get("pos_sub1", "") not in ("副詞可能", "固有名詞", "数")
                and merged[-1].get("pos", "") != "副詞"
                and (curr.get("pos_sub1", "") != "接尾" or is_merge_allowed_suffix)
                # A number+counter unit (五分, 二時間, 五名) is its own search unit and
                # must not fold into a preceding noun/prefix (徒歩|五分, 約|二時間).
                and curr.get("pos_sub1", "") != "数"
                and not prev_is_go_prefix
                and not formal_noun_na_adjective_boundary
            )
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
