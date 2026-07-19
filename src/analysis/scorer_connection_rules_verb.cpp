#include <cmath>

#include "analysis/bigram_table.h"
#include "analysis/category_cost.h"
#include "analysis/scorer.h"
#include "analysis/scorer_connection_rules.h"
#include "analysis/scorer_constants.h"
#include "analysis/verb_candidates_helpers.h"
#include "core/debug.h"
#include "core/kana_constants.h"
#include "core/types.h"
#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "grammar/honorific_verbs.h"
#include "normalize/char_type.h"
#include "normalize/utf8.h"

namespace cost = suzume::analysis::bigram_cost;
namespace sc = suzume::analysis::scorer;

// Surface-based adjustments use cost:: namespace directly from bigram_cost.
// See bigram_table.h and scorer_constants.h for constant values.

namespace suzume::analysis::connection_rules {

// Surface-based connection rules, extracted from connectionCost for readability.
// Each helper accumulates the `surface_bonus +=` contributions of a thematically
// related group of rules and returns their sum. Helpers are self-contained: they
// recompute any needed locals from prev/next and never read caller state. Because
// every contribution is additive, the order among these helpers does not affect the
// total; call sites are kept at their original positions for readability.

// Past-tense た/たり, でした copula, た→ら, and volitional う rules.
float computeTaFormVolitionalBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float bonus{};  // value-init to 0

  // Surface-based bonus for VerbRenyokei → た/たら (ichidan/irregular
  // past and conditional-past forms). E.g., 食べ+た, 見+たら.
  // Guard: require kanji, dictionary, or a context-validated ひらがな一段
  // candidate.  The latter is created only for its immediately following
  // て/た, so it can retain a real split (混雑を+さけ+た) without allowing
  // unbounded fragments such as まし(ましる) to steal the past auxiliary.
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && utf8::equalsAny(next.surface, {"た", "たら"}) &&
      next.extended_pos == core::ExtendedPOS::AuxTenseTa &&
      (grammar::containsKanji(prev.surface) || prev.fromDictionary() ||
       prev.origin == core::CandidateOrigin::VerbHiraganaInflectedRenyokei)) {
    bonus += cost::kVeryStrongBonus;
  }

  // Bonus for VerbRenyokei/VerbOnbinkei → たり/だり (parallel listing particle)
  // E.g., 食べ+たり+する, 飲ん+だり+食べ+たり+する
  // Without this, た(AuxTenseTa) wins over たり(ParticleConj) due to strong た bonus
  // Accept dictionary-backed hiragana forms too (なっ+たり), while excluding
  // unknown pure-hiragana sound-symbolic forms (まっ+たり).
  if ((prev.extended_pos == core::ExtendedPOS::VerbRenyokei || prev.extended_pos == core::ExtendedPOS::VerbOnbinkei) &&
      (next.surface == "たり" || next.surface == "だり") && next.extended_pos == core::ExtendedPOS::ParticleConj &&
      (grammar::containsKanji(prev.surface) || prev.fromDictionary())) {
    bonus += cost::kVeryStrongBonus;
  }

  // Penalty for godan passive/causative-passive renyokei (～Aれ for A-row) → た
  // MeCab splits these as 言わ+れ+た, not 言われ+た
  // E.g., 言われた → 言わ+れ+た, 売られた → 売ら+れ+た, 書かれた → 書か+れ+た
  // A godan passive stem is a godan mizenkei a-row mora (か/が/さ/た/な/ば/ま/ら/わ)
  // followed by れ; verbTypeFromARowCodepoint recognizes exactly that set (and
  // rejects non-godan a-row like は so 晴れた stays 晴れ+た). This cancels the
  // VerbRenyokei→た bonus for godan passive forms.
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      prev.surface.size() >= core::kTwoJapaneseCharBytes &&  // At least 2 chars (Aれ, e.g. かれ)
      utf8::endsWith(prev.surface, "れ") && next.surface == "た" &&
      next.extended_pos == core::ExtendedPOS::AuxTenseTa) {
    const std::string_view before_re = utf8::dropLastChar(prev.surface);
    if (grammar::verbTypeFromARowCodepoint(utf8::decodeLastChar(before_re)) != grammar::VerbType::Unknown) {
      bonus += cost::kSevere;  // Cancel VerbRenyokei→た bonus
    }
  }

  // Surface-based bonus for でし → た/たら (polite past copula / conditional)
  // 本でした should be 本+でし+た, not 本+で+し+た
  // でしたら should be でし+たら (conditional), not でし+た+ら
  // The competing path is Noun→で(PARTICLE)→し(VERB)→た with VerbRenyokei→た bonus
  if (prev.surface == "でし" && prev.extended_pos == core::ExtendedPOS::AuxCopulaDesu &&
      (next.surface == "た" || next.surface == "たら") && next.extended_pos == core::ExtendedPOS::AuxTenseTa) {
    bonus += cost::kVeryStrongBonus;
  }

  // Penalty for た(AuxTenseTa) → ら(Suffix) pattern
  // This discourages splitting たら into た+ら
  // たら is a conditional form of た and should stay together
  if (prev.surface == "た" && prev.extended_pos == core::ExtendedPOS::AuxTenseTa && next.surface == "ら" &&
      next.extended_pos == core::ExtendedPOS::Suffix) {
    bonus += cost::kSevere;  // Penalty to discourage た+ら split
  }

  // The volitional auxiliary is realized as bare う only after an o-row mizenkei
  // (書こ+う, 泳ご+う, しよ+う; the ichidan form is the 2-mora 食べ+よう). A bare
  // う after any non-o-row verb ending is impossible Japanese — an a-row mizenkei
  // (つか of つく) or a u-row shuushikei (す of する) never takes the volitional う
  // — yet the spurious split would otherwise beat the real godan-wa verb
  // (つかう/使う, あらう/洗う, すう/吸う). Penalize so the whole-verb reading wins.
  // This applies only to the bare う surface. The literary volitional ん is
  // likewise one mora, but follows an a-row irrealis stem in 読まんとする.
  if (next.extended_pos == core::ExtendedPOS::AuxVolitional &&
      grammar::isSingleHiragana(next.surface, core::hiragana::kU) && prev.pos == core::PartOfSpeech::Verb &&
      !grammar::endsWithORow(prev.surface)) {
    bonus += cost::kSevere;
  }

  // An o-row irrealis form followed by bare う is the productive modern
  // volitional (書こ+う, 泳ご+う, しよ+う). Prefer it over a homographic
  // continuative-form plus formal-noun path.
  if (next.extended_pos == core::ExtendedPOS::AuxVolitional &&
      grammar::isSingleHiragana(next.surface, core::hiragana::kU) &&
      prev.extended_pos == core::ExtendedPOS::VerbMizenkei && grammar::endsWithORow(prev.surface) &&
      prev.lemma != "いく" &&
      (grammar::isGodanVerbType(grammar::conjTypeToVerbType(prev.conj_type)) ||
       prev.conj_type == dictionary::ConjugationType::Suru || prev.lemma == "する")) {
    bonus += cost::kVeryStrongBonus + cost::kStrongBonus;
  }

  // The directional subsidiary retains the same volitional boundary after a
  // connective form (読んで+いこ+う), rather than yielding to the
  // demonstrative adverb こう.
  if (prev.extended_pos == core::ExtendedPOS::AuxAspectIku && next.extended_pos == core::ExtendedPOS::AuxVolitional &&
      grammar::isSingleHiragana(next.surface, core::hiragana::kU)) {
    bonus += cost::kVeryStrongBonus;
  }

  return bonus;
}

// Suffix (さ/kanji-suffix) splits, ADV→でも/だけど, and short verb-renyokei→aux rules.

}  // namespace suzume::analysis::connection_rules
