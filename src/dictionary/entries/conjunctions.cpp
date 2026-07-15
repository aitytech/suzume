#include "entries_internal.h"

namespace suzume::dictionary::entries {

std::vector<DictionaryEntry> getConjunctionEntries() {
  return {
      // Sequential (順接)
      conj("従って", ""), conj("故に", ""), conj("ゆえに", ""), conj("そして", ""), conj("そうして", ""),
      conj("それから", ""), conj("それで", ""), conj("だから", ""), conj("そのため", ""), conj("したがって", "従って"),

      // Adversative (逆接)
      conj("しかし", ""), conj("しかしながら", ""), conj("だが", ""), conj("けれども", ""),
      conj("だけど", ""),  // colloquial variant
      conj("ところが", ""), conj("それでも", ""), conj("でも", ""),
      conj("だって", ""),  // にもかかわらず removed for MeCab compat
      conj("どころか", ""), conj("ものの", ""), conj("されど", ""), conj("もっとも", ""),

      // Parallel/Addition (並列・添加)
      conj("又", ""), conj("及び", ""), conj("および", ""), conj("並びに", ""), conj("ならびに", ""), conj("且つ", ""),
      conj("かつ", "且つ"), conj("更に", ""),
      // 次に is 次(noun)+に(particle), not a closed-class conjunction — the oracle
      // splits it, so keep it out of L1 to avoid a spurious single-token merge.
      conj("しかも", ""), conj("そのうえ", ""),

      // Alternative (選択)
      conj("或いは", ""), conj("若しくは", ""), conj("または", ""), conj("それとも", ""), conj("あるいは", "或いは"),
      conj("もしくは", "若しくは"),

      // Explanation/Supplement (説明・補足)
      conj("即ち", ""), conj("すなわち", ""), conj("例えば", ""), conj("但し", ""), conj("ただし", ""), conj("尚", ""),
      conj("つまり", ""), conj("たとえば", ""), conj("なぜなら", ""), conj("ちなみに", ""), conj("まして", ""),

      // Topic change (転換)
      conj("さて", ""), conj("ところで", ""),
      // Note: では removed to allow で+は splitting in ではない patterns
      // MeCab splits 彼女ではない as 彼女+で+は+ない, not 彼女+では+ない
      conj("それでは", ""),

      // Addition/Emphasis
      conj("のみならず", ""),

      // Additional conjunctions
      conj("いわば", "言わば"), conj("言わば", ""), conj("さもないと", ""), conj("さもなければ", ""),
      // そんなら removed: MeCab splits as そん+なら
      conj("それにしても", ""), adv("ともかく", ""), conj("いずれにしても", ""), conj("いずれにせよ", ""),

      // Closed-class function adverbs that over-split into non-word verbs without an L1 anchor
      // (呼応副詞 めったに requires a 否定; 陳述副詞 どうぞ). Like the ともかく entry above, these
      // are function adverbs kept in L1 to beat the spurious verb decompositions. Kanji-initial
      // 決して is intentionally NOT registered here: it would swallow the 決 of 解決して
      // (解決|し|て → 解|決して); its 決し(非語 VERB)+て over-split needs a candidate-side fix.
      adv("めったに", ""),  // 滅多に〜ない - prevent めった(非語 VERB める)+に split
      adv("どうぞ", ""),    // 陳述副詞 - prevent どう(ADJ)+ぞ split
  };
}

}  // namespace suzume::dictionary::entries
