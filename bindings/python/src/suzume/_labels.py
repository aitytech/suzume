"""Decode compact numeric C-ABI labels into the public Python strings."""

from __future__ import annotations

POS_ENGLISH = (
    "OTHER",
    "NOUN",
    "VERB",
    "ADJ",
    "ADV",
    "PARTICLE",
    "AUX",
    "CONJ",
    "DET",
    "PRON",
    "PREFIX",
    "SUFFIX",
    "INTJ",
    "SYMBOL",
    "OTHER",
)
POS_JAPANESE = (
    "その他",
    "名詞",
    "動詞",
    "形容詞",
    "副詞",
    "助詞",
    "助動詞",
    "接続詞",
    "連体詞",
    "代名詞",
    "接頭辞",
    "接尾辞",
    "感動詞",
    "記号",
    "その他",
)
EXTENDED_POS = (
    "UNKNOWN",
    "VERB_終止",
    "VERB_連用",
    "VERB_未然",
    "VERB_音便",
    "VERB_て形",
    "VERB_仮定",
    "VERB_命令",
    "VERB_連体",
    "VERB_た形",
    "VERB_たら形",
    "ADJ_終止",
    "ADJ_連用",
    "ADJ_語幹",
    "ADJ_かっ",
    "ADJ_け形",
    "ADJ_NA",
    "AUX_過去",
    "AUX_丁寧",
    "AUX_否定",
    "AUX_否定古",
    "AUX_願望",
    "AUX_意志",
    "AUX_受身",
    "AUX_使役",
    "AUX_可能",
    "AUX_継続",
    "AUX_完了",
    "AUX_準備",
    "AUX_試行",
    "AUX_進行",
    "AUX_接近",
    "AUX_開始",
    "AUX_様態",
    "AUX_推定",
    "AUX_みたい",
    "AUX_断定",
    "AUX_丁寧断定",
    "AUX_尊敬",
    "AUX_丁重",
    "AUX_過度",
    "AUX_ガル",
    "PART_格",
    "PART_係",
    "PART_終",
    "PART_接続",
    "PART_引用",
    "PART_副",
    "PART_準体",
    "PART_係結",
    "NOUN",
    "NOUN_形式",
    "NOUN_転成",
    "NOUN_固有",
    "NOUN_姓",
    "NOUN_名",
    "NOUN_数",
    "PRON",
    "PRON_疑問",
    "ADV",
    "ADV_引用",
    "CONJ",
    "DET",
    "PREFIX",
    "SUFFIX",
    "SYMBOL",
    "INTJ",
    "OTHER",
    "ADJ_未然",
    "AUX_打消推量",
    "AUX_文語断定",
    "AUX_文語過去",
    "AUX_文語断定連体",
    "AUX_文語完了",
    "AUX_文語当為",
    "AUX_不可能",
    "AUX_授受",
    "SUFFIX_直後",
    "SUFFIX_傾向",
    "DET_引用",
    "AUX_よう",
    "AUX_KURUWA_POLITE",
    "AUX_文語過去キ",
)
CONJUGATION_TYPES: tuple[str | None, ...] = (
    None,
    "一段",
    "五段・カ行",
    "五段・ガ行",
    "五段・サ行",
    "五段・タ行",
    "五段・ナ行",
    "五段・バ行",
    "五段・マ行",
    "五段・ラ行",
    "五段・ワ行",
    "サ変",
    "カ変",
    "形容詞",
)
CONJUGATION_FORMS = ("終止形", "未然形", "連用形", "連用形", "仮定形", "命令形", "意志形")

FLAG_USER_DICT = 1 << 0
FLAG_FORMAL_NOUN = 1 << 1
FLAG_LOW_INFO = 1 << 2
FLAG_UNKNOWN = 1 << 3
FLAG_FROM_DICTIONARY = 1 << 4


def _lookup(values: tuple[str, ...], code: int, fallback: str) -> str:
    return values[code] if 0 <= code < len(values) else fallback


def pos_english(code: int) -> str:
    return _lookup(POS_ENGLISH, code, "OTHER")


def pos_japanese(code: int) -> str:
    return _lookup(POS_JAPANESE, code, "その他")


def extended_pos(code: int) -> str:
    return _lookup(EXTENDED_POS, code, "UNKNOWN")


def conjugation_type(code: int) -> str | None:
    return CONJUGATION_TYPES[code] if 0 <= code < len(CONJUGATION_TYPES) else None


def conjugation_form(code: int) -> str | None:
    return CONJUGATION_FORMS[code] if 0 <= code < len(CONJUGATION_FORMS) else None
