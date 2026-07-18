#include "entries_internal.h"

namespace suzume::dictionary::entries {

EntrySpecRange getConjunctionEntries() {
  static constexpr EntrySpec kEntries[] = {
      // Sequential (順接)
      conj("従って", ""), conj("故に", ""), conj("ゆえに", ""), conj("そして", ""), conj("そうして", ""),
      conj("そうすると", ""), conj("それから", ""), conj("それで", ""), conj("だから", ""), conj("そのため", ""),
      conj("したがって", ""),

      // Adversative (逆接)
      conj("しかし", ""), conj("しかしながら", ""), conj("だが", ""), conj("けれども", ""),
      conj("だけど", ""),  // colloquial variant
      conj("ところが", ""), conj("それでも", ""), conj("それなのに", ""), conj("でも", ""),
      conj("だって", ""),  // にもかかわらず removed for MeCab compat
      conj("どころか", ""), conj("それどころか", ""), conj("されど", ""), conj("もっとも", ""),

      // Parallel/Addition (並列・添加)
      conj("又", ""), conj("及び", ""), conj("および", ""), conj("並びに", ""), conj("ならびに", ""), conj("且つ", ""),
      conj("かつ", "かつ"), conj("更に", ""), conj("次いで", ""),
      // 次に is 次(noun)+に(particle), not a closed-class conjunction — the oracle
      // splits it, so keep it out of L1 to avoid a spurious single-token merge.
      conj("しかも", ""), conj("そのうえ", ""),

      // Alternative (選択)
      conj("或いは", ""), conj("又は", ""), conj("若しくは", ""), conj("または", ""), conj("ないしは", ""),
      conj("それとも", ""), conj("あるいは", "或いは"), conj("もしくは", ""),

      // Explanation/Supplement (説明・補足)
      conj("即ち", ""), conj("すなわち", ""), conj("例えば", ""), conj("但し", ""), conj("ただし", ""), conj("尚", ""),
      conj("なお", ""), conj("つまり", ""), conj("たとえば", ""), conj("なぜなら", ""), conj("ちなみに", ""),
      conj("まして", ""), conj("ましてや", ""),

      // Topic change (転換)
      conj("さて", ""), adv("さては", ""), verb("さておき", "さておく", EPOS::VerbRenyokei), conj("ところで", ""),
      // Note: では removed to allow で+は splitting in ではない patterns
      // MeCab splits 彼女ではない as 彼女+で+は+ない, not 彼女+では+ない
      conj("それでは", ""),

      // Additional conjunctions
      conj("いわば", "言わば"), conj("言わば", ""), conj("さもないと", ""), conj("さもなければ", ""),
      conj("とすれば", ""),
      // そんなら removed: MeCab splits as そん+なら
      conj("それにしても", ""), adv("ともかく", ""), conj("いずれにしても", ""),

      // Closed-class function adverbs that over-split into non-word verbs without an L1 anchor
      // (呼応副詞 めったに requires a 否定; 陳述副詞 どうぞ). Like the ともかく entry above, these
      // are function adverbs kept in L1 to beat the spurious verb decompositions. Kanji-initial
      // 決して is intentionally NOT registered here: it would swallow the 決 of 解決して
      // (解決|し|て → 解|決して); its 決し(非語 VERB)+て over-split needs a candidate-side fix.
      adv("もとより", ""),   // 追加・強調: 本はもとより水を読む
      conj("ともあれ", ""),  // 譲歩・話題転換: ともあれ始める
      adv("めったに", ""),   // 滅多に〜ない - prevent めった(非語 VERB める)+に split
      adv("どうぞ", ""),     // 陳述副詞 - prevent どう(ADJ)+ぞ split
      adv("あえて", ""),     // 意図的選択: あえ(非語一段動詞)+て を防ぐ
      adv("あくまで", ""),   // 限定・強調: あく(動詞)+まで を防ぐ
      adv("いたって", ""),   // 程度: いたっ(動詞音便)+て を防ぐ
      adv("すこぶる", ""),   // 程度: す+こぶる の非語分解を防ぐ
      adv("おおいに", ""),   // 程度: おお+い+に の分解を防ぐ
      adv("つとめて", ""),   // 努力: つ+とめ+て の非語分解を防ぐ
      adv("ひいては", ""),   // 帰結・拡張: ひい+て+は を防ぐ
      adv("かえって", ""),   // 逆接・予想外: かえっ(動詞音便)+て を防ぐ
  };
  return makeEntrySpecRange(kEntries);
}

}  // namespace suzume::dictionary::entries
