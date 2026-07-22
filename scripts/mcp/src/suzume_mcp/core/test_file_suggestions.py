"""Heuristics for choosing a logical tokenization test file."""

import re


def suggest_test_files(input_text: str, tokens: list[dict]) -> list[str]:
    """Return logical test-file suggestions in preference order."""
    if not tokens:
        return []

    pos_count: dict[str, int] = {}
    for token in tokens:
        pos = token["pos"]
        pos_count[pos] = pos_count.get(pos, 0) + 1

    suggestions = []
    first_pos = tokens[0]["pos"]
    has_verb = "Verb" in pos_count
    has_aux = "Auxiliary" in pos_count
    has_adj = "Adjective" in pos_count

    if has_adj and first_pos == "Adjective":
        if input_text.endswith("そう"):
            suggestions.append("adjective_i_compound")
        elif "く" in input_text:
            suggestions.append("adjective_i_ku")
        elif "かっ" in input_text:
            suggestions.append("adjective_i_katta")
        else:
            suggestions.append("adjective_i_basic")

    if has_verb:
        for token in tokens:
            if token["pos"] == "Verb":
                lemma = token.get("lemma", "")
                if lemma.endswith("する"):
                    suggestions.append("verb_suru")
                elif re.search(r"(来る|くる)$", lemma):
                    suggestions.append("verb_irregular")
                else:
                    suggestions.append("verb_godan_misc")
                break
        if any(token["surface"] in ("て", "で") for token in tokens):
            suggestions.append("verb_te_ta")

    if has_aux and not has_verb and not has_adj:
        suggestions.append("auxiliary_modality")
    if first_pos == "Noun":
        suggestions.append("noun_general")
    if re.search(r"(です|ます)", input_text):
        suggestions.append("auxiliary_politeness")
    if input_text.endswith("ない"):
        suggestions.append("auxiliary_negation")
    if re.search(r"(だ|である)", input_text):
        suggestions.append("copula")
    if not suggestions:
        suggestions.append("basic")

    return list(dict.fromkeys(suggestions))
