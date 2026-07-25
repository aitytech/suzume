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

// Negative auxiliaries (ない/ず/ね) and noun↔short-verb-renyokei disambiguation.
float computeNegativeAndNounVerbBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float bonus{};  // value-init to 0

  // The e-row irrealis form of the polite auxiliary selects contracted
  // negation (ませ+ん). The homographic literary volitional ん requires a
  // lexical irrealis stem and cannot follow this polite inflectional form.
  // Gate on the auxiliary type and vowel row so the o-row polite volitional
  // (ましょ+う) and ordinary lexical volitionals remain available.
  const bool invalid_polite_hatsuon_volitional = prev.extended_pos == core::ExtendedPOS::AuxTenseMasu &&
                                                 grammar::endsWithERow(prev.surface) &&
                                                 next.extended_pos == core::ExtendedPOS::AuxVolitional &&
                                                 grammar::isSingleHiragana(next.surface, core::hiragana::kN);
  // The classical/contracted negative ん cannot be followed by the plain
  // copula だ. In an apparent …んだ sequence after a ma/ba/na-row verb, ん is
  // the verb's hatsuonbin and だ is the past auxiliary (膨らん+だ).
  const bool contracted_negative_before_copula =
      prev.extended_pos == core::ExtendedPOS::AuxNegativeNu && grammar::isSingleHiragana(prev.surface, U'ん') &&
      next.extended_pos == core::ExtendedPOS::AuxCopulaDa && grammar::isSingleHiragana(next.surface, U'だ');

  // A shape-verified mimetic adverb may follow a case/topic-marked nominal
  // directly (水滴が+ぽつり, 地図を+じっくり). Prefer that reading over
  // unrelated short verb and auxiliary fragments inside the mimetic.
  const bool marked_nominal_before_mimetic =
      (prev.extended_pos == core::ExtendedPOS::ParticleCase || prev.extended_pos == core::ExtendedPOS::ParticleTopic) &&
      prev.start > 0 && next.pos == core::PartOfSpeech::Adverb && next.origin == core::CandidateOrigin::Onomatopoeia;

  // A multi-character nominal compound ending in 中 can modify a predicate or
  // take a case particle as one searchable unit. Keep that reading ahead of
  // a stem-plus-suffix path; short two-character temporal forms remain split.
  const bool long_chuu_nominal =
      prev.extended_pos == core::ExtendedPOS::Noun && utf8::endsWith(prev.surface, "中") &&
      prev.surface.size() >= core::kThreeJapaneseCharBytes &&
      (next.extended_pos == core::ExtendedPOS::ParticleCase || next.extended_pos == core::ExtendedPOS::VerbShuushikei);
  if (invalid_polite_hatsuon_volitional || contracted_negative_before_copula || marked_nominal_before_mimetic ||
      long_chuu_nominal) {
    bonus += (invalid_polite_hatsuon_volitional ? cost::kAlmostNever : cost::kNeutral) +
             (contracted_negative_before_copula ? cost::kAlmostNever : cost::kNeutral) +
             (marked_nominal_before_mimetic ? cost::kDoubleVeryStrongBonus : cost::kNeutral) +
             (long_chuu_nominal ? cost::kStrongBonus + cost::kModerateBonus : cost::kNeutral);
  }

  // A topic particle cannot directly select the classical negative auxiliary.
  // In sequences such as 〜たはずだ, the apparent は+ず boundary is the formal
  // noun はず, not a predicate followed by negation.
  // Ichidan stems are both continuative and irrealis.  After the suru
  // continuative, their irrealis reading is licensed before a negative
  // auxiliary (確認し+終え+ない) rather than an unrelated unknown noun.
  const bool classical_negative_suru = prev.extended_pos == core::ExtendedPOS::AuxNegativeNu &&
                                       next.extended_pos == core::ExtendedPOS::VerbRenyokei && next.lemma == "する";
  const bool topic_before_classical_negative =
      prev.extended_pos == core::ExtendedPOS::ParticleTopic && next.extended_pos == core::ExtendedPOS::AuxNegativeNu;
  const bool sahen_ichidan_irrealis = prev.extended_pos == core::ExtendedPOS::VerbRenyokei && prev.lemma == "する" &&
                                      next.extended_pos == core::ExtendedPOS::VerbMizenkei &&
                                      next.conj_type == dictionary::ConjugationType::Ichidan;
  if (classical_negative_suru || topic_before_classical_negative || sahen_ichidan_irrealis) {
    bonus += (classical_negative_suru ? cost::kVeryStrongBonus : cost::kNeutral) +
             (topic_before_classical_negative ? cost::kProhibitive : cost::kNeutral) +
             (sahen_ichidan_irrealis ? cost::kVeryStrongBonus : cost::kNeutral);
  }

  // The copula's conjunctive form is で; an emphatic small-tsu variant cannot
  // take connective て/で. In なっ+て the onbin belongs to lexical なる, so
  // prevent a generated emphatic AuxCopulaDa edge from replacing that verb.
  if (prev.extended_pos == core::ExtendedPOS::AuxCopulaDa && utf8::endsWith(prev.surface, "っ") &&
      next.extended_pos == core::ExtendedPOS::ParticleConj && grammar::isTeDeSurface(next.surface)) {
    bonus += cost::kAlmostNever;
  }

  // Bonus for dict VERB_連用 → ない/なく/なかっ/なけれ (negative auxiliary)
  // VERB→ADJ bigram (0.8) is high, making split path lose to merged candidates
  // E.g., でき+なく should beat できなく, し+なく should beat しなく
  // Restrict to dictionary verbs (間違い+ない uses 間違い(NOUN), not 違い(VERB))
  // Exclude で (ambiguous: 出る VERB vs だ copula AUX → でない misanalysis)
  // Exclude godan mizenkei (a-dan ending): 走ら, 書か are mislabeled as VERB_連用
  // but are actually 未然形 — bonus would incorrectly boost 走ら+ない split
  if (prev.pos == core::PartOfSpeech::Verb && prev.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      prev.fromDictionary() && prev.surface != "で" && !grammar::endsWithARow(prev.surface) &&
      (next.pos == core::PartOfSpeech::Adjective || next.pos == core::PartOfSpeech::Auxiliary) &&
      utf8::equalsAny(next.surface, {"なく", "ない", "なかっ", "なけれ"})) {
    bonus += cost::kStrongBonus;
  }

  // Bonus for ば(PART_接続) → なら/なり/なる/なれ(VERB) in -なければならない pattern
  // Prevents spurious ばなら verb candidate (ばなる godan-ra) from winning
  // over correct split ば(conditional) + なら(なる mizenkei)
  if (prev.extended_pos == core::ExtendedPOS::ParticleConj && prev.surface == "ば" &&
      next.pos == core::PartOfSpeech::Verb && utf8::equalsAny(next.surface, {"なら", "なり", "なる", "なれ", "なっ"})) {
    bonus += cost::kStrongBonus;
  }

  // Contracted obligation chains retain their grammatical boundaries:
  // mizenkei + なく + ちゃ + いけ + ない, and
  // mizenkei + なきゃ/なけりゃ + なら + ない.
  if (prev.extended_pos == core::ExtendedPOS::AuxNegativeNai && utf8::equalsAny(prev.surface, {"なく"}) &&
      next.extended_pos == core::ExtendedPOS::ParticleConj && utf8::equalsAny(next.surface, {"ちゃ"})) {
    bonus += cost::kStrongBonus;
  }

  // The change-of-state construction 〜なくなる retains なく as a negative
  // auxiliary; elsewhere before a connective, the competing adjective form
  // remains appropriate (読まれなくて困る).
  if (prev.extended_pos == core::ExtendedPOS::AuxNegativeNai && utf8::equalsAny(prev.surface, {"なく"}) &&
      next.extended_pos == core::ExtendedPOS::VerbShuushikei && utf8::equalsAny(next.surface, {"なる"})) {
    bonus += cost::kExtremeBonus + cost::kMinorBonus;
  }

  // The case particle まで attaches directly to a terminal predicate
  // (食べるまで, 調べるまで). Keep this boundary before the particle's
  // leading mora can be absorbed into an unknown nominal candidate.
  if (prev.extended_pos == core::ExtendedPOS::VerbShuushikei && next.extended_pos == core::ExtendedPOS::ParticleCase &&
      utf8::equalsAny(next.surface, {"まで"})) {
    bonus += cost::kVeryStrongBonus;
  }
  if (prev.extended_pos == core::ExtendedPOS::AuxNegativeNai &&
      grammar::isColloquialConditionalNegativeSurface(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::VerbMizenkei && utf8::equalsAny(next.surface, {"なら"})) {
    bonus += cost::kStrongBonus;
  }
  if (prev.extended_pos == core::ExtendedPOS::AuxNegativeNai &&
      grammar::isColloquialConditionalNegativeSurface(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::AuxPotential && utf8::equalsAny(next.surface, {"いけ"})) {
    // The lexical いける renyokei has a low candidate cost before ない. Give
    // this closed obligation connection a small additional preference so its
    // auxiliary analysis wins only after the contracted negative conditional.
    bonus += cost::kVeryStrongBonus + cost::kMinorBonus;
  }
  // The same obligation predicate follows the formal negative conditional
  // (なけれ+ば+いけ+ない) and the te-form topic construction
  // (なく+て+は+いけ+ない).  In either case the preceding closed-class
  // particle identifies the auxiliary reading over lexical いける.
  if ((prev.extended_pos == core::ExtendedPOS::ParticleConj && utf8::equalsAny(prev.surface, {"ば"})) ||
      (prev.extended_pos == core::ExtendedPOS::ParticleTopic && utf8::equalsAny(prev.surface, {"は"}))) {
    if (next.extended_pos == core::ExtendedPOS::AuxPotential && utf8::equalsAny(next.surface, {"いけ"})) {
      bonus += cost::kDoubleVeryStrongBonus;
    }
  }
  // じゃ is the voiced member of the same contracted pair as ちゃ (読ん+じゃ+いけ+ない).
  if (prev.extended_pos == core::ExtendedPOS::ParticleConj && utf8::equalsAny(prev.surface, {"ちゃ", "じゃ"}) &&
      ((next.extended_pos == core::ExtendedPOS::AuxPotential && utf8::equalsAny(next.surface, {"いけ"})) ||
       (next.extended_pos == core::ExtendedPOS::VerbMizenkei && utf8::equalsAny(next.surface, {"なら"})))) {
    bonus += cost::kDoubleVeryStrongBonus;
  }

  // Bonus for VERB_未然 → AUX_否定古(ず/ずに/ね) connection
  // Godan mizenkei + classical negative: 書かず, 抜かず, 行かず, 行かねば
  // The split path needs help to beat merged verb candidates (書かずに as single VERB)
  // because AUX_否定古 → next token connections have default (high) cost.
  // ね is the 已然形 of the same classical negative (行かねば, 死なねば) and competes
  // with the dict VERB reading of ね (連用形 of ねる=寝る) and the sentence-final
  // particle ね; this bonus is what lets the AUX reading win after a verb mizenkei.
  // Note: lexicalized forms like 思わず(ADV) are handled by the candidate generator
  // which skips mizenkei_zu generation when verb+ず is in the dictionary.
  if (prev.extended_pos == core::ExtendedPOS::VerbMizenkei && next.extended_pos == core::ExtendedPOS::AuxNegativeNu &&
      utf8::equalsAny(next.surface, {"ず", "ずに", "ざる", "ざれ", "ね"})) {
    bonus += utf8::endsWith(next.surface, "に") ? cost::kDoubleVeryStrongBonus : cost::kStrongBonus;
  }

  // Cancel the ichidan-oriented VerbRenyokei → AuxNegativeNu(ね) bonus (消えぬ pattern)
  // when prev is not a genuine renyokei/mizenkei form (i.e., doesn't end in an
  // i-row/e-row hiragana). Some godan verbs get a spurious VerbRenyokei-tagged
  // candidate for their dictionary shuushikei form (e.g., 行く), which would
  // otherwise hijack ね away from the sentence-final particle reading (行くね).
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && next.extended_pos == core::ExtendedPOS::AuxNegativeNu &&
      next.surface == "ね" && !grammar::endsWithRenyokeiMarker(prev.surface)) {
    bonus += cost::kRare;
  }

  // Bonus for AUX_否定古(ずに) → VERB connection
  // ずに+帰る, ずに+済む etc. are natural patterns
  // Without this, split path ず+に+帰る wins due to PART_格→VERB having lower default cost
  if (prev.extended_pos == core::ExtendedPOS::AuxNegativeNu && prev.surface == "ずに" &&
      (next.pos == core::PartOfSpeech::Verb || next.pos == core::PartOfSpeech::Adjective)) {
    bonus += cost::kVeryStrongBonus;
  }

  // A nominalized predicate can attach to the continuative form of する.
  // This includes productive honorific-prefix constructions and ordinary
  // verbal-noun predicates, so prefer it over an unrelated lexical verb chain.
  if (prev.extended_pos == core::ExtendedPOS::Noun && next.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      grammar::isSuruRenyokeiSurface(next.surface)) {
    bonus += cost::kStrongBonus + cost::kMinorBonus;
  }

  // A derivational suffix can form the nominal base of する (重要+視+する,
  // 安定+化+する). Preserve the productive suffix boundary over a fused
  // unknown noun before the regular suru continuative form.
  if (prev.pos == core::PartOfSpeech::Suffix && next.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      grammar::isSuruRenyokeiSurface(next.surface)) {
    bonus += cost::kStrongBonus;
  }

  // An interrogative pronoun directly preceding the continuative し forms a
  // productive question predicate (何+し+てる). This is an exception to the
  // general ban on interrogative-pronoun-to-verb attachment.
  if (prev.extended_pos == core::ExtendedPOS::PronounInterrogative &&
      next.extended_pos == core::ExtendedPOS::VerbRenyokei && grammar::isSuruRenyokeiSurface(next.surface)) {
    bonus += cost::kDoubleVeryStrongBonus;
  }

  // Interrogative pronouns take the comparative case particle directly
  // (なに+より), rather than allowing a longer formal particle to absorb its
  // final syllable.
  if (prev.extended_pos == core::ExtendedPOS::PronounInterrogative && utf8::equalsAny(prev.surface, {"なに"}) &&
      next.extended_pos == core::ExtendedPOS::ParticleCase && utf8::equalsAny(next.surface, {"より"})) {
    bonus += cost::kDoubleVeryStrongBonus;
  }

  // Surface-based penalty for Noun → short VerbRenyokei (compound verb protection)
  // Bigram table gives bonus for Noun→VerbRenyokei (for サ変動詞: 得+し, 損+し)
  // But this should NOT apply to compound verbs like 見+つけ→見つけ
  // E.g., 勘違い should be single token, not 勘違+い
  // E.g., 見つけた should be 見つけ+た, not 見+つけ+た
  // Exception: multi-kanji noun + でき should split (外出+でき+ない)
  // Single kanji NOUN often forms compound verbs with following verb stems
  // A continuative written with its own okurigana is a complete content word,
  // so a bare noun before it is a separate token (花+散り, 飯+食べ). A bare kanji
  // carries no such evidence and stays inside the compound (出+来 in 出来事).
  const bool renyokei_has_okurigana = grammar::startsWithKanji(next.surface) && !grammar::isAllKanji(next.surface);
  if (prev.extended_pos == core::ExtendedPOS::Noun && next.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      next.surface != "し" && next.surface != "せ" && next.surface.size() <= 6 &&
      prev.surface.size() == core::kJapaneseCharBytes && !renyokei_has_okurigana) {
    bonus += cost::kRare;  // Cancel the bigram bonus
  }

  // Penalty for Noun/ナ形容詞 → い (VerbRenyokei of いる); mirrors the
  // Noun→AuxAspectIru bigram severity so both readings of a bare-noun-plus-い
  // are rejected (彼が+いる needs a particle; 間続+い beaten by 間+続い).
  // E.g., 上手いし should be 上手い+し, not 上手+い+し. Must NOT block サ変+でき (外出+でき).
  if ((prev.extended_pos == core::ExtendedPOS::AdjNaAdj || prev.extended_pos == core::ExtendedPOS::Noun) &&
      next.extended_pos == core::ExtendedPOS::VerbRenyokei && next.surface == "い") {
    bonus += cost::kSevere;
  }

  // Partial cancel for single-kanji NOUN + し pattern
  // E.g., 寒し (archaic adjective) should not split as 寒+し
  // But 得+し (suru-verb renyokei) should still split
  if (prev.extended_pos == core::ExtendedPOS::Noun && next.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      next.surface == "し" && normalize::utf8Length(prev.surface) == 1) {
    bonus += cost::kUncommon;
  }

  return bonus;
}

}  // namespace suzume::analysis::connection_rules
