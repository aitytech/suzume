#include "entries_internal.h"

namespace suzume::dictionary::entries {

EntrySpecRange getConjunctionEntries() {
  static constexpr EntrySpec kEntries[] = {
      // Sequential (順接)
      conj("従って", ""), conj("故に", ""), conj("ゆえに", ""), conj("そして", ""), conj("そうして", ""),
      conj("そうすると", ""), conj("それから", ""), conj("それで", ""), conj("だから", ""), conj("そのため", ""),
      conj("したがって", ""), conj("ついては", ""), conj("もって", ""), conj("よって", ""), conj("だからといって", ""),
      conj("だからこそ", ""),

      // Adversative (逆接)
      conj("しかし", ""), conj("然し", ""), conj("しかしながら", ""), conj("だが", ""), conj("けれども", ""),
      conj("だけど", ""),  // colloquial variant
      conj("ところが", ""), conj("それでも", ""), conj("それなのに", ""), conj("でも", ""),
      conj("だって", ""),  // にもかかわらず removed for MeCab compat
      conj("どころか", ""), conj("それどころか", ""), conj("されど", ""), conj("さりとて", ""), conj("しかるに", ""),
      conj("もっとも", ""), conj("尤も", ""),

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
      adv("もとより", ""),                      // 追加・強調: 本はもとより水を読む
      conj("ともあれ", ""),                     // 譲歩・話題転換: ともあれ始める
      adv("とりわけ", ""),                      // Focus adverb
      adv("取り分け", ""),                      // Orthographic variant
      adv("目の当たり", ""),                    // Fixed evidential adverb
      adv("めったに", ""),                      // 滅多に〜ない - prevent めった(非語 VERB める)+に split
      adv("めちゃ", ""),                        // Colloquial degree adverb
      na_adj("めった", ""),                     // めったなことではない
      adv("どうぞ", ""),                        // 陳述副詞 - prevent どう(ADJ)+ぞ split
      adv("あえて", ""),                        // 意図的選択: あえ(非語一段動詞)+て を防ぐ
      adv("あくまで", ""),                      // 限定・強調: あく(動詞)+まで を防ぐ
      adv("飽くまで", ""),                      // Orthographic variant
      adv("いたって", ""),                      // 程度: いたっ(動詞音便)+て を防ぐ
      adv("すこぶる", ""),                      // 程度: す+こぶる の非語分解を防ぐ
      adv("おおいに", ""),                      // 程度: おお+い+に の分解を防ぐ
      adv("また", ""),                          // 追加・反復副詞
      adv("たいてい", ""),                      // 頻度副詞
      adv("ふたたび", ""),                      // 反復副詞
      adv("どのみち", ""),                      // 結論副詞
      adv("どうにか", ""),                      // 様態副詞
      adv("ふいに", ""),                        // 突発の様態副詞
      adv("いま", ""),                          // 時間副詞（いまなお は いま + なお）
      adv("それなり", ""),                      // 程度を表す固定表現
      adv("いっさい", ""),                      // 否定呼応の限定副詞
      adv("いっこうに", ""),                    // 否定呼応の程度副詞
      adv("ゆっくり", ""),                      // 様態副詞
      adv("とうに", ""),                        // 時間副詞
      adv("さしも", ""),                        // 強調副詞
      adv("つとめて", ""),                      // 努力: つ+とめ+て の非語分解を防ぐ
      adv("ひいては", ""),                      // 帰結・拡張: ひい+て+は を防ぐ
      adv("かえって", ""),                      // 逆接・予想外: かえっ(動詞音便)+て を防ぐ
      adv("直ちに", ""),                        // 即時: 直ち+に の分解を防ぐ
      adv("いかにも", ""),                      // 強意: いかに+も の分解を防ぐ
      adv("まさしく", ""),                      // 強意: OTHER フォールバックを防ぐ
      adv("至って", ""),                        // 程度: 至+って の分解を防ぐ
      adv("案外", ""),                          // 評価副詞: 後続ナ形容詞との未知語併合を防ぐ
      adv("いかんせん", ""),                    // 評価・譲歩の定型副詞
      adv("おしなべて", ""),                    // 総括副詞
      adv("総じて", ""),                        // 総括副詞
      adv("さしあたり", ""),                    // 当面の時間副詞
      adv("かたがた", ""),                      // 目的併記の定型副詞
      adv("かねて", ""),                        // Fixed temporal adverb
      adv("予て", ""),                          // Orthographic variant
      adv("なんら", ""),                        // 否定呼応の総称副詞
      adv("互いに", ""),                        // 相互副詞
      adv("なにせ", ""),                        // 理由強調副詞
      adv("何せ", ""),                          // Orthographic variant
      adv("あいかわらず", ""),                  // 継続副詞
      adv("あいにく", ""),                      // 逆接副詞
      adv("生憎", ""),                          // Orthographic variant
      adv("つねに", ""),                        // 恒常副詞
      adv("思いがけず", ""),                    // Fixed adverbial expression
      adv("おそらくは", ""),                    // Fixed probability adverb
      particle("ものの", EPOS::ParticleConj),   // 譲歩接続助詞
      particle("がてら", EPOS::ParticleConj),   // purpose-combining conjunctive expression
      particle("ていう", EPOS::ParticleQuote),  // 口語引用表現
      particle("やら", EPOS::ParticleAdverbial),  // 列挙助詞
  };
  return makeEntrySpecRange(kEntries);
}

}  // namespace suzume::dictionary::entries
