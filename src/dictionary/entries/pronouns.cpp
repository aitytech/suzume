#include "entries_internal.h"

namespace suzume::dictionary::entries {

std::vector<DictionaryEntry> getPronounEntries() {
  return {
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
      pronoun("拙者", ""),
      pronoun("貴殿", ""),
      pronoun("某", ""),
      pronoun("我輩", ""),
      pronoun("吾輩", ""),

      // Collective pronouns (集合代名詞)
      // Note: 皆さん is split as 皆+さん for MeCab compatibility
      pronoun("皆", ""),
      pronoun("みんな", ""),

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

      // Demonstrative - distal (遠称)
      pronoun("あれ", ""),
      pronoun("あそこ", ""),
      pronoun("あちら", ""),

      // Demonstrative - person reference (こそあど+いつ)
      pronoun("こいつ", ""),
      pronoun("そいつ", ""),
      pronoun("あいつ", ""),
      pronoun("どいつ", ""),

      // Demonstrative - interrogative (不定称)
      pronoun("どれ", ""),
      pronoun("どこ", ""),
      pronoun("どちら", ""),

      // Indefinite (不定代名詞) - kanji with reading
      // Low cost to act as strong anchors against prefix compounds (今何 → 今+何)
      // Cost -1.5 to beat: 今(1.4) + 何(-1.9) + conn(0.5) = 0.0 < 今何(0.5)
      pronoun("誰", ""),
      pronoun("何", ""),
      // Hiragana interrogatives (だれ=誰, なに=何): closed class, mirror the
      // kanji forms so 誰か/何か boundaries resolve in pure-hiragana text
      pronoun("だれ", ""),
      pronoun("なに", ""),

      // Interrogatives (疑問詞)
      pronoun("いつ", ""),
      pronoun("いくつ", ""),
      pronoun("いくら", ""),
      // どう/いかが can take だ/です (どうですか, いかがですか)
      // Register as both adverb and na-adjective for correct copula connection
      adv("どう", ""),
      na_adj("どう", "どう"),
      adv("いかが", ""),
      na_adj("いかが", "いかが"),
      // Note: どうして needs very low cost to prevent split when followed by verb
      // The te-form bonus makes どう+して+VERB cheaper than どうして+VERB
      adv("どうして", ""),
      adv("なぜ", ""),

      // Classical/literary adverbs (古語・文語副詞)
      adv("かく", ""),      // 斯く - classical demonstrative adverb (=こう/such)
      adv("なんと", ""),    // exclamatory adverb (感嘆副詞)
      adv("なんとか", ""),  // indefinite adverb (somehow/one way or another)

      // Degree adverbs (程度副詞) - very common, prevent misparse
      // とても could be split as と+て+も without this entry
      adv("とても", ""),
      adv("かなり", ""),
      adv("すごく", ""),
      adv("ちょっと", ""),
      adv("もっと", ""),
      adv("ずっと", ""),
      adv("やっと", ""),
      adv("きっと", ""),
      adv("ちょうど", ""),
      // Temporal adverbs - common, prevent misclassification
      adv("まだ", ""),

      // Compound adverb (改めて = anew/once more) - 動詞「改める」連用形+て の語彙化
      // MeCab: 改めて → 改めて(副詞)
      adv("改めて", ""),
  };
}

}  // namespace suzume::dictionary::entries
