#include "entries_internal.h"

namespace suzume::dictionary::entries {

std::vector<DictionaryEntry> getDeterminerEntries() {
  return {
      // Demonstrative determiners (指示連体詞) - この/その/あの/どの
      det("この", ""),
      det("その", ""),
      det("其の", ""),  // kanji variant of その
      det("あの", ""),
      det("どの", ""),
      // Demonstrative determiners (指示連体詞) - こんな/そんな/あんな/どんな
      det("こんな", ""),
      det("そんな", ""),
      det("あんな", ""),
      det("どんな", ""),

      // Other determiners (連体詞)
      det("ある", ""),
      det("あらゆる", ""),
      det("いかなる", ""),
      det("いわゆる", ""),
      det("いろんな", ""),  // colloquial variety determiner (= いろいろな), not an adjective
      det("おかしな", ""),
      det("同じ", ""),      // same - prevent VERB confusion
      det("たいした", ""),  // 大した - prevent 願望たい+し+た split (たいした問題)

      // Demonstrative manner determiners (指示様態連体詞)
      // Lower cost to compete with X + いう (VERB cost 0.3) splits
      det("こういう", ""),
      det("そういう", ""),
      det("ああいう", ""),
      det("どういう", ""),

      // Quotative determiners (引用連体詞) - prevents incorrect split like 病+とい+う
      // Lower cost to beat と(PARTICLE,-0.4)+いった(VERB,-0.034)+conn(0.2)=-0.232
      det("という", ""),
      det("といった", ""),
      det("っていう", ""),  // colloquial

      // Note: Quotative verb forms (といって, こういって, etc.) removed for MeCab compatibility
      // MeCab splits as と+いっ+て, こう+いっ+て, etc.

      // Determiners with kanji - B51: lowered cost to prioritize over NOUN unknown
      det("大きな", ""),
      det("小さな", ""),
      det("おっきな", ""),  // colloquial variant of 大きな

      // Classical possessive determiner (我が家, 我が子, 我が国)
      det("我が", ""),

      // Classical/literary determiner (斯かる = such, this kind of)
      // Note: shares hiragana surface with godan-ra verb 掛かる/懸かる (L2: かかる).
      // L1 Determiner competes with VERB in determiner+NOUN contexts (かかる事態).
      det("かかる", ""),

      // Classical/literary determiner (彼の = that, the aforementioned)
      // Without L1, over-splits to か(unknown)+の(particle).
      // Same pattern as かかる above.
      det("かの", ""),
  };
}

}  // namespace suzume::dictionary::entries
