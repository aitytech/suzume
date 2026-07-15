#include "entries_internal.h"

namespace suzume::dictionary::entries {

std::vector<DictionaryEntry> getFormalNounEntries() {
  return {
      // Formal nouns (形式名詞) - hiragana form is canonical in modern Japanese
      // こと/もの are grammatical function words, hiragana is preferred
      formal_noun("事", "こと"),
      formal_noun("こと", ""),
      formal_noun("物", "もの"),
      formal_noun("もの", ""),
      formal_noun("為", ""),
      // Note: 漢字「所」は削除 - 複合語（所在、場所）の一部として分割を妨げるため
      // ひらがな「ところ」のみ残す
      formal_noun("ところ", ""),
      formal_noun("時", ""),
      formal_noun("内", ""),
      formal_noun("通り", ""),
      formal_noun("限り", ""),
      // Suffix-like formal nouns
      // Lower cost for 付け to compete with verb_kanji ichidan pattern
      formal_noun("付け", ""),
      formal_noun("付", ""),
      // Hiragana-only forms
      formal_noun("よう", ""),
      formal_noun("ほう", ""),  // B49: lowered cost
      // 風 (manner/style): bound formal noun (こんなふうに, そういうふうに).
      // Registered so the u-ending verb candidate path does not fabricate a
      // ふう/VERB reading now that 2-char う stems are admitted.
      formal_noun("ふう", ""),
      formal_noun("わけ", ""),
      formal_noun("はず", "はず"),
      formal_noun("つもり", ""),
      formal_noun("まま", ""),
      formal_noun("ほか", "ほか"),
      formal_noun("他", "ほか"),
      // Abstract nouns that don't form suru-verbs
      formal_noun("仕方", ""),
      formal_noun("ありきたり", "ありきたり"),  // Low cost to prevent あり+き+たり split, na-adjective stem
      formal_noun("たたずまい", "たたずまい"),  // noun, not suru-verb
      // NOTE: 〜がち forms are split as V連用形 + がち (suffix) by the split path, not merged.
      // B35: Idiom component (eaves bracket - used in うだつが上がらない)
      formal_noun("うだつ", "うだつ"),
  };
}

}  // namespace suzume::dictionary::entries
