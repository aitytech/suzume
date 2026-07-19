#include "entries_internal.h"

namespace suzume::dictionary::entries {

EntrySpecRange getPronounEntries() {
  static constexpr EntrySpec kEntries[] = {
      // First person (一人称) - kanji with reading
      // Note: 私/俺 have lower cost to encourage splits (第一私は → 第一+私+は)
      // But 僕 keeps higher cost due to compounds like 下僕
      pronoun("私", ""),
      pronoun("僕", ""),
      pronoun("俺", ""),
      // First person - hiragana/colloquial
      pronoun("わたくし", ""),
      pronoun("あたし", ""),
      pronoun("あたい", ""),
      pronoun("あちき", ""),
      pronoun("わし", ""),
      pronoun("おいら", ""),

      // First person plural: 僕ら/俺ら handled as pronoun + ら suffix
      pronoun("我々", ""),

      // Second person (二人称) - kanji with reading
      pronoun("貴方", ""),
      pronoun("君", ""),
      // Second person - hiragana/mixed only
      pronoun("あなた", ""),
      // B39: お前 needs low cost to beat PREFIX(お)+NOUN(前) split (connection bonus -1.5)
      // PREFIX→NOUN path has cost ~-1.2, so お前 needs cost < -1.2 to win
      pronoun("お前", ""),
      pronoun("おまえ", ""),

      // Second person plural removed - use pronoun + たち suffix

      // Third person (三人称) - kanji with reading
      pronoun("彼", ""),
      pronoun("彼女", ""),
      pronoun("彼氏", ""),
      pronoun("奴", ""),
      // 彼ら/彼女ら removed - handled as pronoun + ら suffix

      // Archaic/Samurai (武家・古風)
      pronoun("我", ""),
      pronoun("われ", ""),  // 我/吾 classical first-person pronoun
      pronoun("己", ""),
      pronoun("拙者", ""),
      pronoun("貴殿", ""),
      pronoun("某", ""),
      pronoun("我輩", ""),
      pronoun("吾輩", ""),

      // Collective pronouns (集合代名詞)
      // Keep the collective pronoun and honorific suffix as separate search units.
      pronoun("皆", ""),
      pronoun("みんな", ""),

      // Distributive pronoun (分配代名詞)
      pronoun("各々", ""),

      // Demonstrative - proximal (近称)
      pronoun("これ", ""),
      pronoun("ここ", ""),
      pronoun("こちら", ""),
      // Colloquial demonstratives - prevent っち split
      pronoun("こっち", ""),
      pronoun("そっち", ""),
      pronoun("あっち", ""),
      pronoun("どっち", ""),
      pronoun("あちこち", ""),

      // Demonstrative - medial (中称)
      pronoun("それ", ""),
      pronoun("そこ", ""),
      pronoun("そちら", ""),
      // Fixed medial locative compound (そこかしこ): retain its two
      // demonstrative search units rather than splitting かしこ internally.
      pronoun("かしこ", ""),

      // Demonstrative - distal (遠称)
      pronoun("あれ", ""),
      pronoun("あそこ", ""),
      pronoun("あちら", ""),
      // Fixed demonstrative collection: keep the closed expression as one
      // search unit instead of two independent demonstratives.
      pronoun("あれこれ", ""),

      // Demonstrative - person reference (こそあど+いつ)
      pronoun("こいつ", ""),
      pronoun("そいつ", ""),
      pronoun("あいつ", ""),
      pronoun("どいつ", ""),

      // Demonstrative - interrogative (不定称)
      pronoun_interrogative("どれ", ""),
      pronoun_interrogative("どこ", ""),
      pronoun_interrogative("どちら", ""),
      pronoun_interrogative("どなた", ""),

      // Indefinite (不定代名詞) - kanji with reading
      // Low cost to act as strong anchors against prefix compounds (今何 → 今+何)
      // Cost -1.5 to beat: 今(1.4) + 何(-1.9) + conn(0.5) = 0.0 < 今何(0.5)
      pronoun_interrogative("誰", ""),
      pronoun_interrogative("何", ""),
      // Hiragana interrogatives (だれ=誰, なに=何): closed class, mirror the
      // kanji forms so 誰か/何か boundaries resolve in pure-hiragana text
      pronoun_interrogative("だれ", ""),
      pronoun_interrogative("なに", ""),
      // Closed indefinite pronoun, not an interrogative plus final particle.
      pronoun_interrogative("何かしら", ""),
      pronoun("よそ", ""),  // 外所: place/other-party pronoun

      // Interrogatives (疑問詞)
      pronoun_interrogative("いつ", ""),
      pronoun_interrogative("いくつ", ""),
      pronoun_interrogative("いくら", ""),
      // どう/いかが can take だ/です (どうですか, いかがですか)
      // Register as both adverb and na-adjective for correct copula connection
      adv("どう", ""),
      na_adj("どう", "どう"),
      adv("いかが", ""),
      na_adj("いかが", "いかが"),
      na_adj("さまざま", "さまざま"),
      na_adj("同じ", "同じ"),
      // Note: どうして needs very low cost to prevent split when followed by verb
      // The te-form bonus makes どう+して+VERB cheaper than どうして+VERB
      adv("どうして", ""),
      adv("なぜ", ""),

      // Classical/literary adverbs (古語・文語副詞)
      adv("かく", ""),        // 斯く - classical demonstrative adverb (=こう/such)
      adv("なんと", ""),      // exclamatory adverb (感嘆副詞)
      adv("なんとか", ""),    // indefinite adverb (somehow/one way or another)
      adv("悪しからず", ""),  // fixed formal acknowledgment phrase
      adv("何とか", ""),      // kanji-mixed spelling of なんとか
      adv("何とも", ""),      // degree/evaluative adverb (何とも言えない)
      adv("なんとも", ""),    // hiragana orthography

      // Degree adverbs (程度副詞) - very common, prevent misparse
      // とても could be split as と+て+も without this entry
      adv("とても", ""),
      adv("かなり", ""),
      adv("多少なりとも", ""),
      adv("たいそう", ""),
      adv("たいして", ""),
      adv("しいて", ""),
      adv("さほど", ""),
      adv("然程", ""),
      adv("たいへん", ""),
      adv("すごく", ""),
      adv("ちょっと", ""),
      adv("もっと", ""),
      adv("ずっと", ""),
      adv("やっと", ""),
      adv("きっと", ""),
      adv("必ず", ""),
      adv("ちょうど", ""),
      // Temporal adverbs - common, prevent misclassification
      adv("まだ", ""),
      adv("まだしも", ""),

      // Fixed temporal and frequency adverbs. These are closed lexical
      // function words, not productive pronoun-plus-particle sequences.
      adv("いつか", ""),
      adv("まもなく", ""),
      adv("ときどき", ""),

      // Fixed degree and discourse adverbs. Their surface endings otherwise
      // attract unrelated verb, particle, and auxiliary candidates.
      adv("いっそう", ""),
      adv("最も", ""),
      adv("あらためて", ""),
      adv("とくに", ""),
      adv("まったく", ""),
      adv("全く", ""),
      adv("概して", ""),
      adv("あらかた", ""),
      adv("おおかた", ""),
      adv("あらまし", ""),
      adv("きわめて", ""),
      // Additional fixed adverbial function words. These are closed lexical
      // expressions; treating them as a unit prevents unrelated inflectional
      // fragments from winning in hiragana or mixed-script input.
      adv("全然", ""),
      adv("ずいぶん", ""),
      adv("なにぶん", ""),
      adv("大抵", ""),
      adv("凡そ", ""),
      adv("むしろ", ""),
      adv("いったん", ""),
      adv("いまだに", ""),
      adv("未だに", ""),
      adv("どうしても", ""),
      adv("できるだけ", ""),
      adv("ひとまず", ""),
      adv("ことごとく", ""),
      adv("おおむね", ""),
      adv("せいぜい", ""),
      adv("とうてい", ""),
      adv("いかに", ""),
      adv("いかで", ""),
      adv("いかほど", ""),
      adv("あまつさえ", ""),
      adv("たかだか", ""),
      adv("わずか", ""),
      adv("何しろ", ""),
      adj("やむを得ない", "やむを得ない", EPOS::AdjBasic),
      adj("ろくでもない", "ろくでもない", EPOS::AdjBasic),
      adv("おおよそ", ""),
      adv("よほど", ""),
      adv("ろくすっぽ", ""),
      adv("さして", ""),
      adv("さも", ""),
      adv("つとに", ""),
      adv("つぶさに", ""),
      adv("おのずと", ""),
      adv("しきりに", ""),
      adv("いまだ", ""),
      adv("このほど", ""),
      adv("概ね", ""),
      adv("今さら", ""),
      adv("一応", ""),
      adv("一切", ""),
      adv("一切合切", ""),
      adv("以上", ""),
      adv("何一つ", ""),
      // Fixed adverbs with productive-looking endings. They form a closed
      // lexical class, while their internal moras otherwise attract prefix,
      // particle, or formal-noun candidates.
      adv("おのずから", ""),
      adv("おもむろに", ""),
      adv("ことさら", ""),
      adv("およそ", ""),
      adv("もっぱら", ""),
      adv("ひとしお", ""),
      adv("ひとたび", ""),
      adv("なにとぞ", ""),
      adv("ぜひとも", ""),
      adv("何もかも", ""),
      adv("何より", ""),
      adv("取りあえず", ""),
      adv("予め", ""),
      adv("予てから", ""),
      adv("ひとしきり", ""),
      adj("余儀なく", "余儀ない", EPOS::AdjRenyokei),

      // Compound adverb (改めて = anew/once more) - 動詞「改める」連用形+て の語彙化
      // MeCab: 改めて → 改めて(副詞)
      adv("改めて", ""),
  };
  return makeEntrySpecRange(kEntries);
}

}  // namespace suzume::dictionary::entries
