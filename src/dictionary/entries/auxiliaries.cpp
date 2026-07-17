#include "entries_internal.h"

namespace suzume::dictionary::entries {

EntrySpecRange getAuxiliaryEntries() {
  static constexpr EntrySpec kEntries[] = {
      // Copula/Assertion - だ (断定)
      aux("だ", "だ", EPOS::AuxCopulaDa),
      aux("だっ", "だ", EPOS::AuxCopulaDa),  // 連用タ接続形
      aux("で", "だ", EPOS::AuxCopulaDa),    // copula renyokei
      aux("だったら", "だ", EPOS::AuxCopulaDa),
      aux("な", "だ", EPOS::AuxCopulaDa),  // attributive form (連体形)

      // Copula/Assertion - です (丁寧断定)
      aux("です", "です", EPOS::AuxCopulaDesu),
      aux("でし", "です", EPOS::AuxCopulaDesu),  // renyoukei of です
      aux("でしたら", "です", EPOS::AuxCopulaDesu),
      // で+ある pattern - ある is a separate auxiliary (MeCab compatible)
      aux("ある", "ある", EPOS::AuxCopulaDa),  // で+ある (assertion)
      aux("あっ", "ある", EPOS::AuxCopulaDa),  // で+あっ+た (sokuonbin before た)
      aux("あり", "ある", EPOS::AuxCopulaDa),  // で+あり+ます
      aux("あろ", "ある", EPOS::AuxCopulaDa),  // で+あろ+う (volitional)
      // Existential ある has the same surfaces as the formal copula, but remains
      // an independent verb after a nominal predicate (本あってこそ, 本あれば).
      verb("ある", "ある", EPOS::VerbShuushikei),
      verb("あり", "ある", EPOS::VerbRenyokei),
      verb("あっ", "ある", EPOS::VerbOnbinkei),
      verb("あれ", "ある", EPOS::VerbKateikei),

      // Polite (丁寧) - ます
      aux("ます", "ます", EPOS::AuxTenseMasu),
      aux("まし", "ます", EPOS::AuxTenseMasu),    // renyoukei
      aux("ませ", "ます", EPOS::AuxTenseMasu),    // mizenkei
      aux("ましょ", "ます", EPOS::AuxTenseMasu),  // mizenkei, connects to う
      aux("ますれ", "ます", EPOS::AuxTenseMasu),  // kateikei, connects to ば

      // Negation - ない (否定)
      aux("ない", "ない", EPOS::AuxNegativeNai),
      aux("なく", "ない", EPOS::AuxNegativeNai),      // 連用形 (読め+なく)
      aux("なかっ", "ない", EPOS::AuxNegativeNai),    // 連用タ接続
      aux("なけれ", "ない", EPOS::AuxNegativeNai),    // 仮定形 (なけれ+ば)
      aux("なきゃ", "ない", EPOS::AuxNegativeNai),    // 仮定形口語縮約 (なければ→なきゃ, 標準終止)
      aux("なけりゃ", "ない", EPOS::AuxNegativeNai),  // 仮定形口語縮約 (なければ→なけりゃ)
      aux("なかろ", "ない", EPOS::AuxNegativeNai),    // 推量形 (なかろ+う)

      // The obligation predicate in 〜てはいけない. The base form いける
      // remains lexical; this negative stem is auxiliary only in the
      // conditional construction, where its connection is gated by scorer rules.
      aux("いけ", "いける", EPOS::AuxPotential),

      // Negation - ぬ/ず (文語否定)
      aux("ぬ", "ぬ", EPOS::AuxNegativeNu),
      aux("ず", "ぬ", EPOS::AuxNegativeNu),  // lemma is ぬ per MeCab
      // Contracted negative-conjunctive form. Keep its displayed lemma as ず
      // because the tokenizer emits ずに as one auxiliary token.
      aux("ずに", "ず", EPOS::AuxNegativeNu),
      aux("ざる", "ぬ", EPOS::AuxNegativeNu),     // 連体形 (せざるを得ない)
      aux("ざれ", "ぬ", EPOS::AuxNegativeNu),     // 已然形 (あらざれば)
      aux("ね", "ぬ", EPOS::AuxNegativeNu),       // 已然形 (行かねば, 死なねば, せねば)
      aux("ごとく", "ごとし", EPOS::Adverb),      // 如く (比況連用形)
      aux("ごとき", "ごとし", EPOS::Determiner),  // 如き (比況連体形)
      // じゃない: removed - split as じゃ(AuxCopulaDa) + ない(AuxNegativeNai)
      aux("ん", "ん", EPOS::AuxNegativeNu),

      // Classical assertion/past なり/けり (文語断定・過去)
      aux("なり", "なり", EPOS::AuxClassicalNari),  // 終止/連体形 断定 (春なり)
      // 連体形 なる (壮大なる, 静かなる): kept distinct from the verb なる (成る) by the higher
      // AuxClassicalNari category cost, winning only via the AdjNaAdj/Noun→なる→Noun bigram bonus.
      aux("なる", "なり", EPOS::AuxClassicalNari),
      aux("けり", "けり", EPOS::AuxClassicalKeri),  // 過去・詠嘆 (なりけり)
      // Classical タリ活用 連体形 たる (堂々たる, 確固たる). Only 連体形 is registered:
      // 終止 たり / 未然 たら / 已然命令 たれ collide with the parallel particle たり,
      // the conditional たら, and imperative forms, so they are intentionally omitted.
      aux("たる", "たり", EPOS::AuxClassicalTari),

      // Past/Completion - た (過去・完了)
      aux("た", "た", EPOS::AuxTenseTa),
      aux("たら", "た", EPOS::AuxTenseTa),  // 仮定形
      aux("だ", "だ", EPOS::AuxTenseTa),    // 連濁形 (泳いだ, 死んだ, 飛んだ, 読んだ)
      aux("だら", "だ", EPOS::AuxTenseTa),

      // Conjecture/Volitional (推量・意志) - う/よう
      aux("う", "う", EPOS::AuxVolitional),
      aux("よう", "よう", EPOS::AuxVolitional),
      // 文語の意志助動詞「む」の撥音便。否定の「ん」と同形だが、
      // 読まんとする のように引用の と が後続する文脈で区別する。
      aux("ん", "ん", EPOS::AuxVolitional),
      aux("だろ", "だ", EPOS::AuxCopulaDa),        // mizenkei, connects to う
      aux("でしょ", "です", EPOS::AuxCopulaDesu),  // mizenkei, connects to う

      // Negative conjecture (否定推量): attaches to 終止形 (godan) / 未然形 (ichidan)
      aux("まい", "まい", EPOS::AuxNegativeMai),

      // Conjecture - らしい (推定)
      aux("らしい", "らしい", EPOS::AuxConjectureRashii),
      aux("らしく", "らしい", EPOS::AuxConjectureRashii),
      aux("らしかっ", "らしい", EPOS::AuxConjectureRashii),
      // Nominalizing stem: 本らしさ → 本 + らし + さ. It remains a form
      // of the conjecture auxiliary rather than an independent adjective.
      aux("らし", "らしい", EPOS::AuxConjectureRashii),

      // Conjecture - みたい (様態推定)
      // Note: みたいだ/みたいに removed - MeCab splits as みたい+だ/に
      aux("みたい", "みたい", EPOS::AuxConjectureMitai),

      // Appearance - そう (様態)
      // Note: そうだ/そうに removed - MeCab splits as そう+だ/に
      aux("そう", "そう", EPOS::AuxAppearanceSou),

      // Demonstrative そう (指示詞/副詞的用法) - sentence-initial そうですね, etc.
      // MeCab treats "そうですね" as フィラー, but normalizes to そう(形容動詞語幹)+です+ね
      // This competes with AuxAppearanceSou; bigram rules select based on context
      na_adj("そう", "そう"),
      // MeCab: そうかもしれない → そう(副詞,助詞類接続) + かも + しれ...
      // When followed by particles (not だ/な), MeCab treats そう as adverb
      adv("そう"),
      // Note: さそう removed - MeCab splits as な+さ+そう (3 tokens)

      // Obligation (当為)
      // Classical obligation auxiliary べし - connects after verb shuushikei
      // Note: べきだ/べきで/べきでは removed - MeCab splits as べき+だ/で/では
      aux("べき", "べし", EPOS::AuxClassicalBeshi),    // 連体形: 食べるべき, 来たるべき
      aux("べく", "べし", EPOS::AuxClassicalBeshi),    // 連用形: 注意すべく, しかるべく
      aux("べし", "べし", EPOS::AuxClassicalBeshi),    // 終止形: 見るべし, 恐るべし
      aux("べから", "べし", EPOS::AuxClassicalBeshi),  // 未然形: 読むべからず

      // Passive/Potential (受身・可能)
      aux("れ", "れる", EPOS::AuxPassive),
      aux("れる", "れる", EPOS::AuxPassive),
      aux("れれ", "れる", EPOS::AuxPassive),  // 仮定形 (書か+れれ+ば)
      aux("られ", "られる", EPOS::AuxPassive),
      aux("られる", "られる", EPOS::AuxPassive),
      aux("られれ", "られる", EPOS::AuxPassive),  // 仮定形 (食べ+られれ+ば)

      // Potential auxiliary - 得る (える/うる)
      // Literary potential: し+え+ない (cannot do), し+える (can do)
      aux("え", "える", EPOS::AuxPotential),    // renyokei: 看過しえない
      aux("える", "える", EPOS::AuxPotential),  // shuushikei: 看過しえる
      aux("うる", "うる", EPOS::AuxPotential),  // alternative shuushikei: 看過しうる
      aux("得", "得る", EPOS::AuxPotential),    // kanji renyokei: 解決し得ない
      aux("得る", "得る", EPOS::AuxPotential),  // kanji shuushikei: 解決し得る

      // Modal subsidiary - かねる (inability or hesitation). Its stem and
      // terminal form follow a verb renyokei: 読みかねる, 言いかねます.
      aux("かね", "かねる", EPOS::AuxInability),
      aux("かねる", "かねる", EPOS::AuxInability),
      // Failure subsidiary - そびれる. Like かねる, it follows a verb
      // renyokei and expresses an unfulfilled action (読みそびれる).
      aux("そびれ", "そびれる", EPOS::AuxInability),
      aux("そびれる", "そびれる", EPOS::AuxInability),
      // Hesitation subsidiary - あぐねる. The stem attaches after a verb
      // renyokei and keeps the past auxiliary as a separate token.
      aux("あぐね", "あぐねる", EPOS::AuxInability),
      aux("あぐねる", "あぐねる", EPOS::AuxInability),
      // Failure subsidiary - 損なう. Its Godan forms attach after a verb
      // renyokei: 読み損なう, 読み損なった, 食べ損なわない.
      aux("損ない", "損なう", EPOS::AuxInability),
      aux("損なう", "損なう", EPOS::AuxInability),
      aux("損なっ", "損なう", EPOS::AuxInability),
      aux("損なわ", "損なう", EPOS::AuxInability),
      aux("損なえ", "損なう", EPOS::AuxInability),

      // Suru verb stem forms (サ変動詞語幹活用形) - VERB, not AUX
      verb("し", "する", EPOS::VerbRenyokei),
      verb("す", "する", EPOS::VerbShuushikei),
      // Base form: closed-class irregular sahen. Its surface lives only in the
      // L2 dictionary, so without it here core.dic-disabled (vanilla) parsing
      // has no standalone する verb token and mis-splits 管理する → 管+理する.
      verb("する", "する", EPOS::VerbShuushikei),
      verb("さ", "する", EPOS::VerbMizenkei),
      verb("せ", "する", EPOS::VerbMizenkei),  // 認識せざるを得ない

      // Kuru verb stem form (カ変動詞語幹活用形) - VERB, not AUX
      // MeCab: 来た → 来(連用形) + た(過去)
      verb("来", "来る", EPOS::VerbRenyokei),

      // Deru verb stem form (一段動詞「出る」) - VERB
      // で+たい/ます needs this to split correctly (外にでたい → 外|に|で|たい)
      verb("で", "出る", EPOS::VerbRenyokei),
      // Suru conjugation stems are separate search units before their auxiliaries.
      verb("しよ", "する", EPOS::VerbMizenkei),  // volitional base: しよ+う
      verb("すれ", "する", EPOS::VerbKateikei),  // conditional base: すれ+ば
      // Suru imperative: VERB (not AUX) - MeCab treats as 動詞
      verb("しろ", "する", EPOS::VerbMeireikei),
      verb("せよ", "する", EPOS::VerbMeireikei),
      aux("しそう", "する", EPOS::AuxAppearanceSou),

      // Causative (使役)
      aux("せ", "せる", EPOS::AuxCausative),
      aux("せる", "せる", EPOS::AuxCausative),
      aux("せれ", "せる", EPOS::AuxCausative),
      aux("せろ", "せる", EPOS::AuxCausative),  // imperative
      aux("せよ", "せる", EPOS::AuxCausative),  // imperative (literary)
      aux("させ", "させる", EPOS::AuxCausative),
      aux("させる", "させる", EPOS::AuxCausative),
      aux("させれ", "させる", EPOS::AuxCausative),
      aux("させろ", "させる", EPOS::AuxCausative),  // imperative
      aux("させよ", "させる", EPOS::AuxCausative),  // imperative (literary)

      // Desiderative - たい (願望)
      aux("たい", "たい", EPOS::AuxDesireTai),
      aux("たく", "たい", EPOS::AuxDesireTai),
      aux("たかっ", "たい", EPOS::AuxDesireTai),
      adj("たければ", "たい", EPOS::AuxDesireTai),
      // たがる (3rd-person desiderative): conjugates like a godan-ra verb
      aux("たがる", "たがる", EPOS::AuxDesireTai),  // 終止/連体
      aux("たがら", "たがる", EPOS::AuxDesireTai),  // 未然 (+ない)
      aux("たがろ", "たがる", EPOS::AuxDesireTai),  // 未然推量 (+う)
      aux("たがり", "たがる", EPOS::AuxDesireTai),  // 連用 (+ます)
      aux("たがっ", "たがる", EPOS::AuxDesireTai),  // 連用促音便 (+た/て)
      aux("たがれ", "たがる", EPOS::AuxDesireTai),  // 仮定 (+ば)

      // Irregular i-adjective よい/いい (形容詞・アウオ段)
      // MeCab: よければ → よけれ(仮定形) + ば, よかった → よかっ(連用タ接続) + た
      // いい is colloquial form of よい, shares conjugated forms (よかった, よければ, etc.)
      adj("いい", "いい", EPOS::AdjBasic),  // いい天気, いいです
      adj("よい", "よい", EPOS::AdjBasic),  // よい天気, よいです
      adj("よけれ", "よい", EPOS::AdjKeForm),
      adj("よかっ", "よい", EPOS::AdjKatt),
      adj("よく", "よい", EPOS::AdjRenyokei),
      adj("よ", "よい", EPOS::AdjStem),  // MeCab: よさ → よ(語幹/ガル接続) + さ(接尾辞)

      // Irregular i-adjective ない (形容詞・アウオ段)
      // MeCab: なさそう → な(語幹/ガル接続) + さ(名詞化接尾辞) + そう(様態)
      // 金がない → 金 + が + ない (existential negative adjective)
      // vs 食べない → 食べ + ない (negation auxiliary)
      adj("ない", "ない", EPOS::AdjBasic),
      adj("なく", "ない", EPOS::AdjRenyokei),
      adj("なかっ", "ない", EPOS::AdjKatt),
      adj("な", "ない", EPOS::AdjStem),

      // Desiderative adjective after a te-form: 読んでほしい.  Its stem
      // also connects to appearance そう (読んでほしそうだ), so retain the
      // ordinary i-adjective inflectional forms rather than fusing そう.
      adj("ほしい", "ほしい", EPOS::AdjBasic),
      adj("ほしく", "ほしい", EPOS::AdjRenyokei),
      adj("ほしかっ", "ほしい", EPOS::AdjKatt),
      adj("ほし", "ほしい", EPOS::AdjStem),

      // Literary adjective meaning absence: ことなしに, 本なしに.
      adj("なし", "ない", EPOS::AdjBasic),

      // Difficulty suffix - づらい. This is an adjective that follows a
      // verb renyokei: 読みづらい, 書きづらい.
      adj("づらい", "づらい", EPOS::AdjBasic),

      // Kanji form of ない (無い) - used in formal writing
      // MeCab: 休むこと無く → 休む + こと + 無く (形容詞連用形)
      adj("無", "無い", EPOS::AdjStem),
      adj("無く", "無い", EPOS::AdjRenyokei),

      // Honorific prefix お (お待ち, お世話, お嬢様)
      // MeCab: お待ち → お(接頭辞) + 待ち(名詞)
      prefix("お", "お"),

      // Honorific prefix ご (ご確認, ご報告, ご連絡)
      // MeCab: ご確認 → ご(接頭辞) + 確認(名詞)
      prefix("ご", "ご"),

      // Honorific prefix 御 (御尽力, 御挨拶, 御協力 - kanji form, mostly literary/formal)
      // MeCab: 御尽力 → 御(接頭辞) + 尽力(名詞)
      prefix("御", "御"),

      // Note: Negation prefixes (未, 非, 不, 無) are NOT registered
      // MeCab splits them but Suzume keeps them unified for practical tokenization
      // e.g., 未確認 → 未確認 (not 未+確認)

      // Nominalization suffix さ (高さ, 美しさ, なさ)
      // MeCab: 高さ → 高(語幹) + さ(名詞), なさそう → な + さ + そう
      suffix("さ", "さ"),

      // Construction/composition suffixes after a quantified counter
      // (二階建て, 二本立て). They retain a Suffix candidate alongside the
      // homographic verb stems, and contextual scoring selects the grammar.
      suffix("建て", "建て"),
      suffix("立て", "立て"),

      // Honorific suffixes
      suffix("さん", "さん"),
      suffix("ちゃん", "ちゃん"),
      suffix("くん", "くん"),
      suffix("さま", "さま"),
      suffix("たん", "たん"),
      suffix("にゃん", "にゃん"),
      suffix("っ娘", "っ娘"),

      // Plural suffix たち (学生たち, 私たち, 子供たち)
      // MeCab: 学生たち → 学生 + たち
      suffix("たち", "たち"),

      // Plural suffix ら (彼ら, 彼女ら, 僕ら, あいつら)
      // MeCab treats these as single tokens, but grammatically ら is a suffix
      suffix("ら", "ら"),

      // Reason/consequence suffix after a demonstrative (それゆえ, これゆえ).
      suffix("ゆえ", "ゆえ"),

      // Tendency suffix after a verb renyokei (読みがち, 食べがち).
      suffix("がち", "がち"),

      // Quantitative bound suffixes: 1kg未満, 5cm以上.
      suffix("未満", "未満"),

      // Note: 的 was previously L1 SUFFIX, but Suzume's tokenizer use case
      // prefers X+的 as one search unit (論理的, 科学的, 経済的). Merging is
      // handled by kanji-merge normalization. 的+な (na-adj formation) still
      // splits as 論理的(NOUN) + な(AuxCopula) without a 的 SUFFIX node.

      // NOTE: 中 suffix removed - MeCab treats 世界中/一日中 as single NOUN

      // Inclusive suffix ごと (皮ごと, 頭ごと)
      // MeCab: 皮ごと → 皮 + ごと (noun + suffix)
      suffix("ごと", "ごと"),

      // Coverage suffix まみれ (血まみれ, 泥まみれ, 汗まみれ)
      // MeCab: 血まみれ → 血 + まみれ (noun + suffix)
      suffix("まみれ", "まみれ"),

      // Coverage suffix だらけ (傷だらけ, 間違いだらけ)
      // MeCab: 傷だらけ → 傷 + だらけ (noun + suffix)
      suffix("だらけ", "だらけ"),

      // Tendency suffix ぎみ — hiragana spelling of 気味 (風邪ぎみ, 緊張ぎみ, 疲れぎみ)
      // MeCab: 風邪ぎみ → 風邪 + ぎみ (noun + suffix)
      suffix("ぎみ", "ぎみ"),
      suffix("気味", "気味"),

      // Audience/direction suffix: 初心者向け, 家庭向け.
      suffix("向け", "向け"),

      // Manner/conformity suffix: 予定どおり, 指示どおり.
      suffix("どおり", "どおり"),

      // Manner suffix after a verb's renyokei: 読みぶり, 食べぶり.
      suffix("ぶり", "ぶり"),

      // Exclusion suffixes: 税抜き, 水ぬき.
      suffix("抜き", "抜き"),
      suffix("ぬき", "ぬき"),

      // All-over suffix: 白ずくめ, 欠点ずくめ.
      suffix("ずくめ", "ずくめ"),

      // Interval suffix: 一日おき, 一時間おき.
      suffix("おき", "おき"),

      // Verb renyokei suffix っぱなし (出しっぱなし, 置きっぱなし)
      // MeCab: 出しっぱなし → 出し + っぱなし (verb renyokei + suffix)
      suffix("っぱなし", "っぱなし"),

      // Recent-completion suffix たて (焼きたて, 作りたて)
      // MeCab: 焼きたて → 焼き + たて (verb renyokei + suffix)
      suffix_recent_completion("たて", "たて"),

      // Adjective suffixes - connect after verb renyokei (V連用形接続)
      // MeCab: 使いにくい → 使い + にくい, 読みやすい → 読み + やすい
      adj("にくい", "にくい", EPOS::AdjBasic),
      adj("にくく", "にくい", EPOS::AdjRenyokei),
      adj("にくかっ", "にくい", EPOS::AdjKatt),
      adj("やすい", "やすい", EPOS::AdjBasic),
      adj("やすく", "やすい", EPOS::AdjRenyokei),
      adj("やすかっ", "やすい", EPOS::AdjKatt),
      // Stem form (語幹/ガル接続) for さ-nominalization, mirroring よ/な stems:
      // MeCab: 使いやすさ → 使い + やす(語幹) + さ. Only やす needs this — にく already
      // has a NOUN reading (肉/にく) in the dictionary that carries 読みにくさ, whereas
      // no やす noun exists, so 読みやすさ would otherwise fragment into や+す+さ.
      adj("やす", "やすい", EPOS::AdjStem),

      // Adjective suffix っぽい (～っぽい: 子供っぽい, 忘れっぽい)
      // MeCab: 子供っぽい → 子供 + っぽい
      adj("っぽい", "っぽい", EPOS::AdjBasic),
      adj("っぽく", "っぽい", EPOS::AdjRenyokei),
      adj("っぽかっ", "っぽい", EPOS::AdjKatt),
      adj("っぽ", "っぽい", EPOS::AdjStem),

      // Polite imperative - connect after verb renyokei
      aux("なさい", "なさる", EPOS::AuxHonorific),
      // Honorific subsidiary なさる after お+連用形.  Keep its special
      // ra-row inflection as auxiliaries so お読みなさる and its negative,
      // past, and conditional forms do not fall back to lexical verbs.
      aux("なさる", "なさる", EPOS::AuxHonorific),
      aux("なさら", "なさる", EPOS::AuxHonorific),
      aux("なさっ", "なさる", EPOS::AuxHonorific),
      aux("なされ", "なさる", EPOS::AuxHonorific),
      aux("なさろ", "なさる", EPOS::AuxHonorific),

      // Honorific subsidiary いらっしゃる has the same special ra-row
      // inflection. Keep the whole paradigm after a te-form so its initial
      // い is not detached as the progressive auxiliary.
      aux("いらっしゃる", "いらっしゃる", EPOS::AuxHonorific),
      aux("いらっしゃい", "いらっしゃる", EPOS::AuxHonorific),
      aux("いらっしゃら", "いらっしゃる", EPOS::AuxHonorific),
      aux("いらっしゃっ", "いらっしゃる", EPOS::AuxHonorific),
      aux("いらっしゃれ", "いらっしゃる", EPOS::AuxHonorific),
      aux("いらっしゃろ", "いらっしゃる", EPOS::AuxHonorific),

      // Possibility/uncertainty: かも + しれ + ない.
      // かも particle is already defined above (line 157)

      // Certainty expression: nominal unit followed by ない.
      // These are handled by noun + ない connection

      // Note: れる/られる/せる/させる (shuushikei) are registered above with the
      // Passive/Causative groups; no duplicate generic registration needed here.

      // Polite existence - ございます (丁重)
      // MeCab splits: ござい + ます (renyokei + polite)
      aux("ござい", "ござる", EPOS::AuxGozaru),
      // ござっ is onbinkei (促音便形) for ござった
      // MeCab splits: ござっ + た (onbinkei + ta)
      verb("ござっ", "ござる", EPOS::VerbOnbinkei),

      // Humble verb - いたす (謙譲語)
      // MeCab treats いたし as 動詞,非自立 (dependent verb)
      // Used in: お願いいたします, ご連絡いたします
      verb("いたし", "いたす", EPOS::VerbRenyokei),

      // Receiving verb - いただく (謙譲語)
      // Used in: いただきます, 食べていただく
      // Must be registered to prevent い+た+だき split
      verb("いただき", "いただく", EPOS::VerbRenyokei),
      // Potential form of the humble receiving auxiliary. These are closed
      // subsidiary forms after a te-form, not independent lexical verbs.
      aux("いただけ", "いただける", EPOS::AuxHonorific),
      aux("いただける", "いただける", EPOS::AuxHonorific),
      aux("いただけれ", "いただける", EPOS::AuxHonorific),

      // Potential form of the receiving benefactive. After a te-form this
      // remains a closed subsidiary paradigm, including もらえ+ない.
      aux("もらえ", "もらえる", EPOS::AuxBenefactive),
      aux("もらえる", "もらえる", EPOS::AuxBenefactive),
      aux("もらえれ", "もらえる", EPOS::AuxBenefactive),

      // Request - ください is VERB (くださる) in MeCab
      // くださる is special ra-row godan with irregular imperative form ください
      // Uses VerbRenyokei to allow connection to ます (くださいました)
      verb("ください", "くださる", EPOS::VerbRenyokei),
      verb("下さい", "下さる", EPOS::VerbRenyokei),
      verb("くださいませ", "くださる", EPOS::VerbShuushikei),

      // Special ra-row godan verbs (五段ラ行特殊) with い-form renyokei
      // These honorific/humble verbs use い instead of り for renyokei:
      // いらっしゃる → いらっしゃい+ます (not いらっしゃり)
      // ござる → ござい+ます (not ござり)
      // なさる → なさい+ます (not なさり)
      // おっしゃる → おっしゃい+ます (not おっしゃり)
      verb("いらっしゃい", "いらっしゃる", EPOS::VerbRenyokei),
      verb("ござい", "ござる", EPOS::VerbRenyokei),
      verb("なさい", "なさる", EPOS::VerbRenyokei),
      verb("おっしゃい", "おっしゃる", EPOS::VerbRenyokei),

      // Progressive/Continuous - いる (進行・継続)
      // Register い separately so aspect and following tense/conjunction remain distinct.
      aux("い", "いる", EPOS::AuxAspectIru),  // renyokei for い+た, い+ます
      aux("いる", "いる", EPOS::AuxAspectIru),
      aux("います", "いる", EPOS::AuxAspectIru),
      aux("いません", "いる", EPOS::AuxAspectIru),
      aux("いない", "いる", EPOS::AuxAspectIru),
      aux("いなかった", "いる", EPOS::AuxAspectIru),
      aux("いれば", "いる", EPOS::AuxAspectIru),

      // Progressive/Continuous - おる (humble/dialectal form of いる)
      // Used in formal polite speech: ております, おります
      // Add renyokei forms separately from following politeness auxiliaries.
      aux("おる", "おる", EPOS::AuxAspectIru),
      aux("おり", "おる", EPOS::AuxAspectIru),  // renyokei for おり+ます
      // Western-Japanese contractions of ておる / でおる. These retain the
      // progressive auxiliary's Godan-ra inflection after a verb stem or
      // onbin form (食べとる, 書いとった, 読んどらん).
      aux("とる", "とる", EPOS::AuxAspectIru),
      aux("とら", "とる", EPOS::AuxAspectIru),
      aux("とり", "とる", EPOS::AuxAspectIru),
      aux("とっ", "とる", EPOS::AuxAspectIru),
      verb("とれ", "とる", EPOS::VerbKateikei),
      aux("どる", "どる", EPOS::AuxAspectIru),
      aux("どら", "どる", EPOS::AuxAspectIru),
      aux("どり", "どる", EPOS::AuxAspectIru),
      aux("どっ", "どる", EPOS::AuxAspectIru),
      verb("どれ", "どる", EPOS::VerbKateikei),

      // Benefactive auxiliary - くれる (giving, receiving benefit)
      // Used in subsidiary verb patterns: してくれる, 買ってくれた
      // Note: MeCab treats くれる as 動詞,非自立 (dependent verb)
      aux("くれる", "くれる", EPOS::AuxAspectKuru),
      aux("くれ", "くれる", EPOS::AuxAspectKuru),  // renyokei for くれ+ます

      // Excessive degree subsidiary verb - すぎる (過度)
      // Used after adjective/verb stems: 高すぎる, 食べすぎる
      // MeCab: 動詞,非自立 (subsidiary verb, not auxiliary 助動詞)
      // MeCab splits: 高 + すぎる (終止形), 高 + すぎ + た (連用形 + た)
      // Use verb() to get POS::Verb, but keep AuxExcessive EPOS for bigram rules
      verb("すぎる", "すぎる", EPOS::AuxExcessive),
      verb("すぎ", "すぎる", EPOS::AuxExcessive),  // renyokei for すぎ+た, すぎ+て
      aux("過ぎる", "過ぎる", EPOS::AuxExcessive),
      aux("過ぎ", "過ぎる", EPOS::AuxExcessive),

      // Inceptive subsidiary verb: 読みはじめる, 食べはじめる.
      aux("はじめる", "はじめる", EPOS::AuxAspectHajimeru),

      // Adjective-stem suffix verb - がる (ガル接続)
      // Used after adjective stems: 怖がる, 嫌がる, 可愛がる
      // MeCab: 動詞,接尾 (suffix verb)
      // Godan-ra conjugation: がる, がら, がり, がっ, がれ, がろ
      verb("がる", "がる", EPOS::AuxGaru),
      verb("がら", "がる", EPOS::AuxGaru),  // mizenkei
      verb("がり", "がる", EPOS::AuxGaru),  // renyokei
      verb("がっ", "がる", EPOS::AuxGaru),  // onbinkei (がった, がって)
      verb("がれ", "がる", EPOS::AuxGaru),  // kateikei/meireikei
      verb("がろ", "がる", EPOS::AuxGaru),  // ishikei (がろう)

      // Completive/Regretful - しまう (完了・遺憾)
      // Aspectual しまう is an auxiliary rather than the lexical verb.
      aux("しまう", "しまう", EPOS::AuxAspectShimau),
      aux("しまっ", "しまう", EPOS::AuxAspectShimau),  // te-form/ta-form stem
      aux("しまい", "しまう", EPOS::AuxAspectShimau),  // negative stem
      aux("しまわ", "しまう", EPOS::AuxAspectShimau),  // irrealis before negative auxiliary
      // 仕舞う is the standard kanji spelling of the same closed-class
      // completive auxiliary. Register its full Godan-wa paradigm so all
      // following inflections retain the auxiliary boundary after a te-form.
      aux("仕舞う", "しまう", EPOS::AuxAspectShimau),
      aux("仕舞わ", "しまう", EPOS::AuxAspectShimau),
      aux("仕舞い", "しまう", EPOS::AuxAspectShimau),
      aux("仕舞っ", "しまう", EPOS::AuxAspectShimau),
      aux("仕舞え", "しまう", EPOS::AuxAspectShimau),
      aux("仕舞お", "しまう", EPOS::AuxAspectShimau),
      // Keep the lexical-verb readings alongside the auxiliary readings. The
      // te-form connection selects the latter, while a standalone transitive
      // use such as 物を仕舞う remains a verb.
      verb("仕舞う", "仕舞う", EPOS::VerbShuushikei),
      verb("仕舞わ", "仕舞う", EPOS::VerbMizenkei),
      verb("仕舞い", "仕舞う", EPOS::VerbRenyokei),
      verb("仕舞っ", "仕舞う", EPOS::VerbOnbinkei),
      verb("仕舞え", "仕舞う", EPOS::VerbKateikei),
      verb("仕舞お", "仕舞う", EPOS::VerbMizenkei),

      // Contracted forms: ちゃう/じゃう (completion)
      verb("ちゃう", "ちゃう", EPOS::AuxAspectShimau),
      verb("ちゃっ", "ちゃう", EPOS::AuxAspectShimau),
      verb("ちゃい", "ちゃう", EPOS::AuxAspectShimau),
      // じゃう is the voiced contraction after an n-onbin (読んじゃう). It
      // remains an aspect auxiliary through its Godan-wa inflection.
      aux("じゃう", "じゃう", EPOS::AuxAspectShimau),
      aux("じゃわ", "じゃう", EPOS::AuxAspectShimau),
      aux("じゃい", "じゃう", EPOS::AuxAspectShimau),
      aux("じゃっ", "じゃう", EPOS::AuxAspectShimau),
      aux("じゃえ", "じゃう", EPOS::AuxAspectShimau),
      // Volitional stems (mizenkei before う): 食べちゃおう, 読んじゃおう. ちゃお is a
      // lexicalized verb (like ちゃう) but じゃお follows the で+contraction reading and
      // is tagged as an auxiliary, mirroring MeCab's ちゃう=Verb / じゃ=Auxiliary split.
      verb("ちゃお", "ちゃう", EPOS::AuxAspectShimau),
      aux("じゃお", "じゃう", EPOS::AuxAspectShimau),

      // Contracted forms: てる/とく (progressive/preparation)
      // MeCab: 動詞,非自立 → Auxiliary (subsidiary verbs)
      aux("てる", "てる", EPOS::AuxAspectIru),
      aux("て", "てる", EPOS::AuxAspectIru),
      // Voiced contraction after an n-onbin: 読んでる = 読んでいる.
      // Its selection is restricted by the connection scorer so lexical 出る
      // remains available outside that grammatical environment.
      aux("でる", "いる", EPOS::AuxAspectIru),
      // で remains excluded: 出たい must be で(出る連用形)+たい.
      aux("とく", "とく", EPOS::AuxAspectOku),
      aux("どく", "どく", EPOS::AuxAspectOku),
      // MeCab compat: とい/どい (renyokei) + た/て instead of といた/どいた
      aux("とい", "とく", EPOS::AuxAspectOku),
      aux("どい", "どく", EPOS::AuxAspectOku),

      // Directional auxiliaries - いく/くる (方向補助動詞)
      // MeCab tags as 動詞 (Verb), not 助動詞, even in subsidiary use
      // Note: いっ (sokuonbin) is generated by hiragana verb candidate generator
      // with context-sensitive lemma (と+いっ→いう, て+いっ→いく); no L1 entry needed
      verb("いく", "いく", EPOS::VerbShuushikei),
      verb("いか", "いく", EPOS::VerbMizenkei),
      aux("いかない", "いく", EPOS::AuxAspectIku),
      // Literary form ゆく (classical 行く)
      verb("ゆく", "ゆく", EPOS::VerbShuushikei),
      verb("ゆき", "ゆく", EPOS::VerbRenyokei),
      verb("ゆか", "ゆく", EPOS::VerbMizenkei),
      verb("ゆけ", "ゆく", EPOS::VerbMeireikei),
      aux("くる", "くる", EPOS::AuxAspectKuru),
      // MeCab compat: split き+た/て/ます separately
      aux("き", "くる", EPOS::AuxAspectKuru),
      // Note: no unconditional こ (来る mizenkei) entry — the surface is far too
      // frequent as a word fragment (こと, これ, きのこ, ...). こ is generated
      // context-gated before a ない-family negative in
      // generateHiraganaVerbCandidates (こない → こ + ない).

      // Explanatory (説明) - MeCab compat: split as の/ん + だ/です/でした
      // Removed のだ/のです/のでした/んだ/んです/んでした to allow split

      // Kuruwa-kotoba (廓言葉)
      aux("ありんす", "ある", EPOS::Unknown),
      aux("ありんした", "ある", EPOS::Unknown),
      aux("ありんせん", "ある", EPOS::Unknown),
      aux("ざんす", "ある", EPOS::Unknown),
      aux("ざんせん", "ある", EPOS::Unknown),
      aux("でありんす", "だ", EPOS::Unknown),
      aux("でありんした", "だ", EPOS::Unknown),
      aux("なんし", "ます", EPOS::Unknown),
      aux("なんした", "ます", EPOS::Unknown),

      // Cat-like (猫系) - sentence-final particles (な/ね/よ variants)
      particle("にゃ", EPOS::ParticleFinal),
      particle("にゃん", EPOS::ParticleFinal),
      particle("にゃー", EPOS::ParticleFinal),
      aux("だにゃ", "だよ", EPOS::Unknown),
      aux("だにゃん", "だよ", EPOS::Unknown),
      aux("ですにゃ", "ですよ", EPOS::Unknown),
      aux("ですにゃん", "ですよ", EPOS::Unknown),

      // Squid character (イカ娘) - sentence-final particle (MeCab: Noun)
      // Note: で+ゲソ should split as で(Particle)+ゲソ(Noun)
      particle("ゲソ", EPOS::ParticleFinal),
      particle("げそ", EPOS::ParticleFinal),

      // Ojou-sama/Lady speech (お嬢様言葉)
      aux("ですわ", "です", EPOS::Unknown),
      aux("ですの", "です", EPOS::Unknown),
      aux("ますの", "ます", EPOS::Unknown),
      aux("だわ", "だ", EPOS::Unknown),

      // Youth slang (若者言葉) - っす/っすか are colloquial です, so tag them as the
      // polite copula rather than falling back to the Auxiliary default (AuxTenseTa),
      // which would wrongly reward a verb 音便形 + っす reading (つい+っす) over the
      // intended stem + っす split (きつい+っす).
      aux("っす", "です", EPOS::AuxCopulaDesu),
      aux("っした", "でした", EPOS::AuxCopulaDesu),
      aux("っすか", "ですか", EPOS::AuxCopulaDesu),

      // Rabbit-like (兎系)
      aux("ぴょん", "だ", EPOS::Unknown),
      aux("ピョン", "だ", EPOS::Unknown),

      // Ninja/Old-fashioned (忍者・古風)
      aux("ござる", "だ", EPOS::Unknown),
      aux("でござる", "だ", EPOS::Unknown),
      aux("ござった", "だった", EPOS::Unknown),
      aux("でござった", "だった", EPOS::Unknown),
      aux("ござらぬ", "ではない", EPOS::Unknown),
      aux("ござらん", "ではない", EPOS::Unknown),
      aux("でございます", "です", EPOS::Unknown),
      aux("ナリ", "だ", EPOS::Unknown),
      aux("なり", "だ", EPOS::Unknown),
      aux("でナリ", "だ", EPOS::Unknown),
      aux("でなり", "だ", EPOS::Unknown),

      // Elderly/Archaic (老人・古風)
      aux("じゃ", "だ", EPOS::AuxCopulaDa),
      aux("じゃあ", "だ", EPOS::AuxCopulaDa),
      aux("のじゃ", "のだ", EPOS::Unknown),
      aux("じゃろ", "だろ", EPOS::AuxCopulaDa),

      // Regional dialects (方言系)
      aux("ぜよ", "だ", EPOS::Unknown),
      aux("だべ", "だ", EPOS::Unknown),
      aux("やんけ", "だ", EPOS::Unknown),
      aux("や", "だ", EPOS::Unknown),
      aux("やねん", "だ", EPOS::Unknown),
      aux("だっちゃ", "だ", EPOS::Unknown),
      aux("ばい", "だ", EPOS::Unknown),

      // Robot/Mechanical (ロボット・機械)
      aux("デス", "です", EPOS::Unknown),
      aux("マス", "ます", EPOS::Unknown),
  };
  return makeEntrySpecRange(kEntries);
}

}  // namespace suzume::dictionary::entries
