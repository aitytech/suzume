"""Constants ported from SuzumeUtils.pm."""

# MeCab POS to Suzume POS mapping (raw mapping from MeCab)
POS_MAP: dict[str, str] = {
    "名詞": "Noun",
    "動詞": "Verb",
    "形容詞": "Adjective",
    "副詞": "Adverb",
    "助詞": "Particle",
    "助動詞": "Auxiliary",
    "接続詞": "Conjunction",
    "感動詞": "Interjection",
    "連体詞": "Adnominal",
    "接頭詞": "Prefix",
    "接尾辞": "Suffix",
    "代名詞": "Pronoun",
    "記号": "Symbol",
    "フィラー": "Filler",
    "その他": "Other",
}

# ナイ形容詞 (adjectives ending in ない that are single lexical units)
NAI_ADJECTIVES: list[str] = [
    "だらしない",
    "つまらない",
    "しょうがない",
    "もったいない",
    "くだらない",
    "せわしない",
    "やるせない",
    "いたたまれない",
    "あどけない",
    "おぼつかない",
    "はしたない",
    "みっともない",
    "ろくでもない",
    "どうしようもない",
    "ものたりない",
    "こころもとない",
]

# Lexical adjectives that MeCab merges even though Suzume preserves the
# productive nominal + independent ない boundary.
NOUN_NAI_COMPOUND_ADJECTIVES: list[str] = [
    "揺るぎない",
]

# Classical volitional auxiliary followed by a quotative particle.  MeCab
# sometimes treats the closed-class sequence as one noun token; Suzume keeps
# both grammatical search units independent.
LITERARY_VOLITIONAL_PARTICLE_COMPOUNDS: dict[str, tuple[str, str]] = {
    "むと": ("む", "と"),
}

# Counter/unit patterns (unused - kept for reference)
COUNTER_UNITS: list[str] = [
    "人",
    "個",
    "本",
    "枚",
    "台",
    "回",
    "件",
    "円",
    "年",
    "月",
    "日",
    "時",
    "分",
    "秒",
    "階",
    "番",
    "号",
    "歳",
    "才",
    "目",
    "ページ",
    "頁",
    "時間",
    "分間",
    "秒間",
    "年間",
    "日間",
    "週間",
    "か月",
    "ヶ月",
    "カ月",
]

# Productive kana quantity readings owned by Suzume's finite L1 classes.  The
# stems are NounNumber entries and the tails are quantitative Suffix entries;
# their Cartesian product is closed and can therefore repair MeCab's arbitrary
# syllable splits without enumerating open-class nouns.
KANA_NUMBER_STEMS: frozenset[str] = frozenset({"いち", "ふた", "よん"})
KANA_COUNTER_SUFFIXES: frozenset[str] = frozenset({"まい", "にん", "月"})

# Closed-class construction/composition suffixes that remain independent search
# units after a numeral+counter phrase (二階|建て, 二本|立て).
QUANTITY_BOUND_SUFFIXES: frozenset[str] = frozenset({"建て", "立て"})

# Slang adjective stems -> standard replacement for MeCab preprocessing
SLANG_ADJ_STEMS: dict[str, str] = {
    "エモ": "赤",
    "キモ": "赤",
    "ウザ": "赤",
    "ダサ": "赤",
    "イタ": "赤",
}

# Slang verb stems -> standard replacement for MeCab preprocessing
SLANG_VERB_STEMS: dict[str, str] = {
    "バズ": "走",
    "ググ": "走",
    "パク": "走",
}

# タリ活用副詞: stem + と -> Adverb
TARI_ADVERB_STEMS: list[str] = [
    "泰然",
    "堂々",
    "悠々",
    "淡々",
    "粛々",
    "颯爽",
    "毅然",
    "漫然",
    "茫然",
    "呆然",
    "唖然",
    "愕然",
    "断然",
    "俄然",
    "歴然",
    "整然",
    "雑然",
    "騒然",
    "憮然",
    "黙然",
    "昂然",
    "凛然",
    "厳然",
]

# Compound verb subsidiary verbs (V2)
COMPOUND_VERB_V2_GODAN: list[str] = [
    "込む",
    "こむ",
    "出す",
    "だす",
    "続く",
    "つづく",
    "返す",
    "かえす",
    "返る",
    "かえる",
    "合う",
    "あう",
    "消す",
    "けす",
    "直す",
    "なおす",
    "切る",
    "きる",
    "上がる",
    "あがる",
    "下がる",
    "さがる",
    "回す",
    "まわす",
    "回る",
    "まわる",
    "抜く",
    "ぬく",
    "掛かる",
    "かかる",
    "付く",
    "つく",
    "巡る",
    "めぐる",
    "飛ばす",
    "とばす",
    "交う",
    "かう",
    "潰す",
    "つぶす",
    "崩す",
    "くずす",
    "倒す",
    "たおす",
    "起こす",
    "おこす",
    "つかる",
    "入る",
    "いる",
    "こもる",
    "籠る",
    "籠もる",
    "開く",
    "ひらく",
    # Keep the oracle aligned with the core's closed V2 candidate class. These
    # are not dictionary entries for individual compounds: each V2 licenses
    # productive V1+V2 combinations such as 書き+置く and 付け+足す.
    "置く",
    "おく",
    "足す",
    "たす",
    "直る",
    "なおる",
    "下す",
    "くだす",
    "交わす",
    "かわす",
    "添う",
    "そう",
    "混じる",
    "まじる",
    "寄る",
    "よる",
    "外す",
    "はずす",
    "鳴らす",
    "ならす",
    "惜しむ",
    "おしむ",
]

COMPOUND_VERB_V2_ICHIDAN: list[str] = [
    "続ける",
    "つづける",
    "つける",
    "替える",
    "かえる",
    "換える",
    "合わせる",
    "あわせる",
    "切れる",
    "きれる",
    "出る",
    "でる",
    "上げる",
    "あげる",
    "下げる",
    "さげる",
    "抜ける",
    "ぬける",
    "落とす",
    "おとす",
    "落ちる",
    "おちる",
    "掛ける",
    "かける",
    "付ける",
    "つける",
    "入れる",
    "いれる",
    "分ける",
    "わける",
    "立てる",
    "たてる",
    "広げる",
    "ひろげる",
    "降りる",
    "おりる",
    "乱れる",
    "みだれる",
]

# Fictional/unusual proper nouns -> standard name for MeCab preprocessing
UNUSUAL_NAMES: dict[str, str] = {
    "がお": "吉田",
}

# Words that MeCab incorrectly splits but should stay together
WORD_EXCEPTIONS: dict[str, str] = {
    "小供": "供給",
    "とうきょう": "瑠璃",
    "どさり": "ゆっくり",
    "にゃー": "ねえ",
    "打ち合わせ": "会議",
    "おいで": "お出で",
    "ほんわか": "ゆっくり",
    "ありきたり": "当たり前",
    "ばたり": "ゆっくり",
    "がたり": "ゆっくり",
    "すごいいいい": "すごい",
    "すごーーい": "すごい",
    "かわいーー": "かわいい",
    "もうってば": "もう",
    "あなたったら": "あなた",
    "無意識": "意識",
    "翌営業日": "明日",
    "お疲れ様": "お願い",
    "日付け": "日付",
    "再確認": "確認",
    "ですっ": "です",
    "ますっっ": "ます",
}

# Particles that MeCab may misclassify as Noun
PARTICLE_CORRECTIONS: dict[str, str] = {
    "の": "Particle",
    "が": "Particle",
    "を": "Particle",
    "に": "Particle",
    "へ": "Particle",
    "で": "Particle",
    "と": "Particle",
    "から": "Particle",
    "まで": "Particle",
    "より": "Particle",
    "ほど": "Particle",
    "は": "Particle",
    "も": "Particle",
    "か": "Particle",
    "な": "Particle",
    "ね": "Particle",
    "よ": "Particle",
    "わ": "Particle",
    "ぞ": "Particle",
    "さ": "Particle",
    "けど": "Particle",
    "けれど": "Particle",
    "し": "Particle",
    "のに": "Particle",
    "ので": "Particle",
    "ながら": "Particle",
    "ばかり": "Particle",
    "だけ": "Particle",
    "しか": "Particle",
    "くらい": "Particle",
    "ぐらい": "Particle",
    "など": "Particle",
    "なんか": "Particle",
    "なんて": "Particle",
    "って": "Particle",
}

# Hiragana compounds that MeCab splits but should stay together
HIRAGANA_COMPOUNDS: dict[str, str] = {
    "ふともも": "名詞",
    "おもち": "名詞",
    "おかし": "名詞",
    "おととい": "名詞",
    "たまご": "名詞",
    "ひこうき": "名詞",
    "みっつ": "名詞",
    "よっつ": "名詞",
}

# Closed function words and fixed formal/search units whose internal split in
# the reference analyzer is not a Suzume morpheme boundary.  Open-class words
# do not belong here; this table is intentionally limited to finite lexical
# classes that can be normalized without inspecting the current Suzume output.
FIXED_FUNCTION_SEARCH_UNITS: dict[str, str] = {
    "然程": "副詞",
    "更に": "副詞",
    "更なる": "連体詞",
    "どのみち": "副詞",
    "ふいに": "副詞",
    "ほどなく": "副詞",
    "そんなら": "接続詞",
    "ありさま": "名詞",
    "おそれ": "名詞",
    "おかげ": "名詞",
    "おのれ": "代名詞",
    "だけ": "助詞",
    "だに": "助詞",
    "がてら": "助詞",
}

# Closed inflected function forms that the reference analyzer can split into
# pieces before a following auxiliary.  The follower gate prevents consuming
# the same prefix from a longer finite lexical form.
FIXED_INFLECTED_FUNCTION_UNITS: dict[str, tuple[str, str, tuple[str, ...]]] = {
    "いただけ": ("動詞", "いただける", ("ます", "ませ", "ない", "なかっ")),
}

# Closed units that a reference dictionary can absorb into a following noun.
# The normalizer splits only the leading unit and leaves the productive noun
# boundary intact (わが|国, ひととおり|目).
FIXED_LEADING_SEARCH_UNITS: dict[str, str] = {
    "以下": "接尾辞",
    "程度": "接尾辞",
    "ひととおり": "副詞",
    "わが": "連体詞",
}

# Search-unit compounds: kanji+okurigana words MeCab splits but Suzume keeps as one token
SEARCH_UNIT_COMPOUNDS: dict[str, str] = {}

# Kanji prefix compounds: MeCab splits kanji prefix (接頭詞) + kana-containing noun/verb.
# Maps prefix kanji → set of following token surfaces that form a valid compound.
# Used to merge 微+笑み → 微笑み, 微+笑む → 微笑む, etc.
KANJI_PREFIX_COMPOUNDS: dict[str, set[str]] = {
    "微": {"笑み", "笑む", "笑ん", "笑え", "笑っ", "笑わ", "笑い"},
}

# Family/honorific lexemes used both with and without an お prefix
_HONORIFIC_FAMILY_TERMS: set[str] = {
    "兄ちゃん",
    "姉ちゃん",
    "兄さん",
    "姉さん",
    "嬢さん",
    "嬢様",
    "父さん",
    "母さん",
    "爺さん",
    "婆さん",
    "嫁さん",
    "客様",
    "客さん",
}

# Colloquial tails that only become family terms after adding お.
_COLLOQUIAL_FAMILY_TAILS: set[str] = {
    "じさん",
    "ばさん",
    "じいさん",
    "ばあさん",
    "にいさん",
    "ねえさん",
    "とうさん",
    "かあさん",
    "もちゃ",
    "っさん",
}

# Family/honorific terms that merge with お prefix.
FAMILY_TERMS: set[str] = _HONORIFIC_FAMILY_TERMS | _COLLOQUIAL_FAMILY_TAILS
_PREFIXED_FAMILY_TERMS = {f"お{term}" for term in FAMILY_TERMS}

# Colloquial pronouns to merge
COLLOQUIAL_PRONOUNS: list[str] = ["どいつ", "こいつ", "そいつ", "あいつ"]

# Honorific suffixes regex pattern
HONORIFIC_SUFFIXES: list[str] = ["さん", "ちゃん", "様", "君", "殿", "さま"]

# Words where honorific suffix is part of the lexeme
HONORIFIC_EXCEPTIONS: set[str] = (
    _HONORIFIC_FAMILY_TERMS
    | {f"お{term}" for term in _HONORIFIC_FAMILY_TERMS}
    | {
        "皆様",
        "皆さん",
    }
)

# Words where お/ご is part of the lexeme (not separable prefix)
PREFIX_EXCEPTIONS: set[str] = _PREFIXED_FAMILY_TERMS | {
    "お出で",
    "おいで",
    "おすすめ",
    "お疲れ様",
    "お金",
    "お前",
    "おまえ",
    "おかず",
    "おでん",
    "おもち",
    "おそれ",
    "おかし",
    "おととい",
    "おかげ",
    "おいら",
    "おしっこ",
    "おもらし",
    # Fixed particle, not the honorific prefix お plus a noun.
    "おろか",
}

# User-dict registered kanji+katakana compounds (skip splitting)
USER_DICT_COMPOUNDS: set[str] = {"東京テスト"}

# POS normalization map (uppercase/variations -> canonical form)
POS_NORM_MAP: dict[str, str] = {
    "NOUN": "Noun",
    "VERB": "Verb",
    "ADJ": "Adjective",
    "ADJECTIVE": "Adjective",
    "ADV": "Adverb",
    "ADVERB": "Adverb",
    "PARTICLE": "Particle",
    "PART": "Particle",
    "AUX": "Auxiliary",
    "AUXILIARY": "Auxiliary",
    "CONJ": "Conjunction",
    "CONJUNCTION": "Conjunction",
    "INTJ": "Interjection",
    "INTERJECTION": "Interjection",
    "SYMBOL": "Symbol",
    "OTHER": "Other",
    "PREFIX": "Prefix",
    "SUFFIX": "Suffix",
    "DET": "Determiner",
    "DETERMINER": "Determiner",
    "ADNOMINAL": "Adnominal",
    "PRON": "Pronoun",
    "PRONOUN": "Pronoun",
    "FILLER": "Filler",
}

# Suzume-specific POS overrides
SUZUME_POS_OVERRIDE: dict[str, str] = {
    "Adnominal": "Determiner",
    "Filler": "Other",
}

# Emphatic sokuon patterns for preprocessing
EMPHATIC_SOKUON: dict[str, str] = {
    "行くっ": "行く",
}

# Inflected forms of the copula. Its negation takes the supplementary
# adjective, unlike a verbal auxiliary's, so the two are told apart by surface.
COPULA_SURFACES: frozenset[str] = frozenset({"だ", "だっ", "で", "です", "でし", "な", "なら"})

# Adverb overrides (words MeCab misclassifies)
ADVERB_OVERRIDES: set[str] = {
    "全く",
    "間もなく",
    "一切",
    "一切合切",
    "いっさい",
    "いま",
    "このほど",
    "たかだか",
    "むしろ",
    "いずれ",
    "いつか",
    "しどろもどろ",
    "その後",
    "なるほど",
    "たくさん",
    "かく",
    "あらまし",
    "めちゃ",
}

# Adverb/noun homographs whose nominal reading is selected by an overt case,
# topic, or genitive particle. This finite set mirrors closed lexical entries;
# ordinary adverbs such as まったく remain adverbs before の.
ADVERB_NOMINAL_HOMOGRAPHS: frozenset[str] = frozenset({"一切", "一切合切", "いっさい", "いま", "このほど", "むしろ"})

# Fixed function-word lemmas that differ from a reference analyzer's legacy
# inflectional analysis.  These are lexical entries, not productive rules.
FIXED_FUNCTION_LEMMAS: dict[str, str] = {
    "全く": "全く",
    "あるいは": "或いは",
}

# Pronoun overrides (名詞 -> Pronoun)
PRONOUN_OVERRIDES: set[str] = {
    "皆",
    "みんな",
    "みな",
    "某",
    "あなた",
    "あんた",
    "拙者",
    "我輩",
}

# Na-adjective overrides (名詞 -> Adjective)
NA_ADJ_OVERRIDES: set[str] = {
    "しんちょう",
    "しずか",
    "おだやか",
    "げんき",
    "きれい",
    "ありきたり",
    "無限",
    "滅多",
}

# Words to keep as Noun despite 形容動詞語幹 classification
KEEP_AS_NOUN_NOT_ADJ: set[str] = {
    "マジ",
    "不安",
    "不要",
    "乙",
    "不便",
    "公式",
    "可能",
    "容易",
    "積極",
    "健康",
    "傍若無人",
}

# Noun -> Pronoun overrides
NOUN_AS_PRONOUN: set[str] = {"彼氏", "彼女", "奴", "我", "わし"}

# Suffix -> Noun overrides
SUFFIX_AS_NOUN: set[str] = {"様", "末", "ごろ", "行き", "毛"}

# Valid English POS values (values of POS_MAP)
VALID_POS: set[str] = set(POS_MAP.values()) | {"Determiner", "Pronoun", "Suffix"}

# Interrogatives for でも context detection
INTERROGATIVES: set[str] = {"何", "誰", "どこ", "いつ", "どれ", "いくら", "どんな"}

# Non-自立 verb lemmas that stay as Verb (not Auxiliary)
VERB_NOT_AUX_LEMMAS: set[str] = {
    "すぎる",
    "くださる",
    "下さる",
    "いたす",
    "あげる",
    "くれる",
    "もらう",
    "始める",
    "続ける",
    "終わる",
    "終える",
    "出す",
    "直す",
    "合う",
    "込む",
    "いく",
    "いる",
    "ほしい",
    "いただく",
    "ちゃう",
    "ちまう",
    "いらっしゃる",
}

# Plural suffix split targets
PLURAL_RA_STEMS: list[str] = ["彼女", "彼", "僕", "奴", "我"]

# ったら split pronoun stems
TTARA_STEMS: set[str] = {
    "あなた",
    "おまえ",
    "きみ",
    "君",
    "彼",
    "彼女",
    "あいつ",
    "こいつ",
    "誰",
    "何",
    "これ",
    "それ",
    "あれ",
}

# ってば split stems
TTEBA_STEMS: set[str] = {"もう", "いい", "だめ", "ダメ", "嫌", "やだ"}

# Kanji codepoint ranges, mirroring kana::isKanjiCodepoint in
# src/core/kana_constants.h. The supplementary-plane ranges matter here:
# IPADIC has no entry for those characters and MeCab labels them 記号,
# which would otherwise send them through the symbol filter.
KANJI_RANGES: tuple[tuple[int, int], ...] = (
    (0x4E00, 0x9FFF),  # CJK Unified Ideographs
    (0x3400, 0x4DBF),  # CJK Extension A
    (0x20000, 0x2A6DF),  # CJK Extension B
    (0x2A700, 0x2B73F),  # CJK Extension C
    (0x2B740, 0x2B81F),  # CJK Extension D
    (0xF900, 0xFAFF),  # CJK Compatibility Ideographs
    (0x2F00, 0x2FDF),  # Kangxi Radicals
)


def is_kanji(char: str) -> bool:
    """Whether a single character is a kanji."""
    code = ord(char)
    return any(low <= code <= high for low, high in KANJI_RANGES)


def is_all_kanji(surface: str) -> bool:
    """Whether a surface is non-empty and made entirely of kanji."""
    return bool(surface) and all(is_kanji(char) for char in surface)
