#include "entries_internal.h"

namespace suzume::dictionary::entries {

EntrySpecRange getFormalNounEntries() {
  static constexpr EntrySpec kEntries[] = {
      // Formal nouns (形式名詞) - hiragana form is canonical in modern Japanese
      // こと/もの are grammatical function words, hiragana is preferred
      formal_noun("事", "こと"),
      formal_noun("こと", ""),
      formal_noun("物", "もの"),
      formal_noun("もの", ""),
      formal_noun("為", ""),
      formal_noun("ため", ""),
      // Note: 漢字「所」は削除 - 複合語（所在、場所）の一部として分割を妨げるため
      // ひらがな「ところ」のみ残す
      formal_noun("ところ", ""),
      formal_noun("どころ", ""),
      formal_noun("時", ""),
      formal_noun("内", ""),
      formal_noun("あいだ", ""),
      formal_noun("うち", ""),
      formal_noun("たび", ""),
      formal_noun("きり", ""),
      formal_noun("通り", ""),
      formal_noun("とおり", ""),
      formal_noun("限り", ""),
      formal_noun("かぎり", ""),
      // Suffix-like formal nouns
      // Lower cost for 付け to compete with verb_kanji ichidan pattern
      formal_noun("付け", ""),
      formal_noun("付", ""),
      // Per-unit distributive formal noun (一人当たり, 利用者当たり).
      formal_noun("当たり", ""),
      // Hiragana-only forms
      formal_noun("よう", ""),
      formal_noun("ほう", ""),  // B49: lowered cost
      // Formal noun for conditions and prerequisites (読むうえで, 読んだうえで).
      formal_noun("うえ", ""),
      // Formal noun for comparison and degree (読むわりに, 食べるわりだ).
      formal_noun("わり", ""),
      // Concessive formal noun after an attributive clause (読むくせに,
      // 高いくせに, 静かなくせに).
      formal_noun("くせ", ""),
      // Formal noun for substitution or contrast (読むかわりに, 本のかわりに).
      formal_noun("かわり", ""),
      // 風 (manner/style): bound formal noun (こんなふうに, そういうふうに).
      // Registered so the u-ending verb candidate path does not fabricate a
      // ふう/VERB reading now that 2-char う stems are admitted.
      formal_noun("ふう", ""),
      formal_noun("わけ", ""),
      // Causal formal noun followed by a copula (読むゆえだ, 本のゆえだ).
      // The demonstrative-bound suffix reading remains available separately.
      formal_noun("ゆえ", ""),
      // Formal noun for a basis or surrounding condition (読むもとで,
      // 食べるもとに).
      formal_noun("もと", ""),
      // Formal noun in the certainty predicate (本にちがいない,
      // 読むに違いない). Keep its nominal reading over the homographic
      // Godan verb renyokei.
      formal_noun("ちがい", ""),
      formal_noun("違い", "ちがい"),
      // Causal formal noun after an attributive clause or nominalizer:
      // 本のせいで, 読むせいで.
      formal_noun("せい", ""),
      // Formal noun expressing a risk after an attributive clause
      // (遅れるおそれがある, 欠けるおそれはない).
      formal_noun("おそれ", ""),
      formal_noun("はず", "はず"),
      // Formal noun for conditional cases (読む場合、必要な場合).
      formal_noun("場合", ""),
      formal_noun("つもり", ""),
      // Formal noun for an incidental accompanying action (書いたついでに).
      formal_noun("ついで", ""),
      // Formal noun for a simultaneous/parallel action (読むかたわら書く).
      formal_noun("かたわら", ""),
      // Temporal formal noun after a past clause (読んだとたん書く).
      formal_noun("とたん", ""),
      // Temporal formal noun after a past clause (書いたそばから読む).
      formal_noun("そば", ""),
      // Immediate-sequence expression (読むや否や書く).
      formal_noun("否や", ""),
      // Resulting-state formal noun after a past clause (読んだあげく書く).
      formal_noun("あげく", ""),
      // Result formal noun after a past clause (考えすぎたあまり眠れない).
      formal_noun("あまり", ""),
      formal_noun("まま", ""),
      formal_noun("ほか", "ほか"),
      formal_noun("他", "ほか"),
      // Fixed negative predicate: ほかなら+ない (none other than).
      verb("ほかなら", "ほかなる", EPOS::VerbMizenkei),
      // Abstract nouns that don't form suru-verbs
      formal_noun("仕方", ""),
      formal_noun("しかた", ""),
      formal_noun("ありきたり", "ありきたり"),  // Low cost to prevent あり+き+たり split, na-adjective stem
      formal_noun("たたずまい", "たたずまい"),  // noun, not suru-verb
      // NOTE: 〜がち forms are split as V連用形 + がち (suffix) by the split path, not merged.
      // B35: Idiom component (eaves bracket - used in うだつが上がらない)
      formal_noun("うだつ", "うだつ"),
  };
  return makeEntrySpecRange(kEntries);
}

}  // namespace suzume::dictionary::entries
