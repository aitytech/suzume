#include "analysis/bigram_table.h"
#include "analysis/category_cost.h"
#include "analysis/scorer.h"
#include "analysis/scorer_connection_rules.h"
#include "analysis/scorer_connection_rules_internal.h"
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

float computeProgressiveHonorificBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float bonus{};  // value-init to 0

  // The continuative construction V連用形+つつ+ある uses the existential verb,
  // not the homographic copula or determiner. The same finite verb remains
  // correct before an attributive noun (読みつつある本).
  if (prev.extended_pos == core::ExtendedPOS::ParticleConj && utf8::equalsAny(prev.surface, {"つつ"}) &&
      next.extended_pos == core::ExtendedPOS::VerbShuushikei && next.lemma == "ある") {
    bonus += cost::kVeryStrongBonus;
  }

  // A conjunctive particle cannot host the contracted negative auxiliary.
  // This preserves the productive hatsuonbin verb path (つつん+で) while
  // leaving classical negative ず after a conjunction (のみなら+ず) intact.
  if (prev.extended_pos == core::ExtendedPOS::ParticleConj && next.extended_pos == core::ExtendedPOS::AuxNegativeNu &&
      grammar::isContractedNegativeAuxiliaryLemma(next.lemma)) {
    bonus += cost::kAlmostNever;
  }

  // The conjunctive negative ずに is a single closed auxiliary form. Its
  // registered edge must outrank the otherwise valid classical-negative plus
  // case-particle sequence, while other case-marked forms (ざるを, ぬを)
  // retain their ordinary boundary.
  if (prev.extended_pos == core::ExtendedPOS::AuxNegativeNu && utf8::equalsAny(prev.surface, {"ず"}) &&
      next.extended_pos == core::ExtendedPOS::ParticleCase && utf8::equalsAny(next.surface, {"に"})) {
    bonus += cost::kStrong;
  }

  // A nominal phrase followed by の+ある uses the existential verb in
  // attributive form (意味のある文), not the homographic copula or determiner.
  // Keep the condition on the preceding nominalizer so ordinary ある本 remains
  // a determiner.
  if (prev.extended_pos == core::ExtendedPOS::ParticleNo && next.extended_pos == core::ExtendedPOS::VerbShuushikei &&
      next.lemma == "ある") {
    bonus += cost::kDoubleVeryStrongBonus;
  }

  // The nominalizer の can introduce conditional particles such as なら and
  // で, but it cannot be followed by the conjunctive particle し or the
  // renyokei of する. This guards against a fabricated copula-plus-nominalizer
  // sequence hiding a registered closed auxiliary.
  if (prev.extended_pos == core::ExtendedPOS::ParticleNo &&
      ((next.extended_pos == core::ExtendedPOS::ParticleConj && grammar::isConjunctiveParticleShi(next.surface)) ||
       (next.extended_pos == core::ExtendedPOS::VerbRenyokei && grammar::isSuruRenyokeiSurface(next.surface)))) {
    bonus += cost::kStrong;
  }

  // The honorific potential construction Noun+に+なれ+ます keeps the
  // potential form of なる intact. Without this connection, the homographic
  // adjective stem plus passive auxiliary path can win before polite ます.
  if (prev.extended_pos == core::ExtendedPOS::ParticleCase && grammar::isSingleHiragana(prev.surface, U'に') &&
      next.extended_pos == core::ExtendedPOS::VerbKateikei && next.lemma == "なる") {
    bonus += cost::kStrongBonus;
  }

  // A dictionary-backed godan renyokei of the progressive subsidiary after
  // connective て/で should beat a homographic unknown lexical verb. Restrict
  // this to a true renyokei (e.g. おり from おる) so fused inflected forms such
  // as います and いない retain their grammatical internal split.
  if (prev.extended_pos == core::ExtendedPOS::ParticleConj && grammar::isTeDeSurface(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::AuxAspectIru && next.fromDictionary() &&
      isGodanRenyokeiOfLemma(next.surface, next.lemma)) {
    bonus += cost::kVeryStrongBonus;
  }

  // Demonstrative そう is a na-adjective before the polite copula.  The
  // dedicated connection keeps そう+です intact instead of taking the
  // homographic adverb plus で+す path.
  if (prev.extended_pos == core::ExtendedPOS::AdjNaAdj && prev.lemma == "そう" &&
      next.extended_pos == core::ExtendedPOS::AuxCopulaDesu) {
    bonus += cost::kDoubleVeryStrongBonus;
  }

  // みたい immediately after a connective て/で is not the conjecture
  // auxiliary: 〜てみたい consists of the trial subsidiary み + desiderative
  // たい. This applies whether the competing connective edge was classified as a
  // particle or as a contracted aspect auxiliary.
  if (grammar::isTeDeSurface(prev.surface) && next.extended_pos == core::ExtendedPOS::AuxConjectureMitai) {
    bonus += cost::kAlmostNever;
  }

  // Progressive で+い+ます should use the auxiliary い, not the standalone verb いる.
  // The preceding で can be tagged as either a conjunction particle or a te-form
  // particle depending on the onbin path, so match by surface here.
  if (prev.surface == "で" && next.surface == "い" && next.extended_pos == core::ExtendedPOS::VerbRenyokei) {
    bonus += cost::kAlmostNever;
  }

  // Excessive-degree すぎ + て is ordinary connective て. Do not reinterpret it
  // as contracted progressive てる.
  if (prev.extended_pos == core::ExtendedPOS::AuxExcessive && next.surface == "て" &&
      next.extended_pos == core::ExtendedPOS::AuxAspectIru) {
    bonus += cost::kAlmostNever;
  }

  // The excessive subsidiary inflects before the past auxiliary. Restrict the
  // rule to the actual past surface so a homographic character-speech edge for
  // て cannot be selected as a past form.
  if (prev.extended_pos == core::ExtendedPOS::AuxExcessive && next.extended_pos == core::ExtendedPOS::AuxTenseTa &&
      next.surface == "た") {
    bonus += cost::kVeryStrongBonus;
  }

  // If contracted-progressive て is followed by ordinary particles or content
  // words, prefer the connective particle て instead.
  if (prev.surface == "て" && prev.extended_pos == core::ExtendedPOS::AuxAspectIru &&
      (next.pos == core::PartOfSpeech::Particle || next.pos == core::PartOfSpeech::Noun ||
       next.pos == core::PartOfSpeech::Pronoun || next.pos == core::PartOfSpeech::Determiner ||
       next.pos == core::PartOfSpeech::Adverb || next.pos == core::PartOfSpeech::Conjunction ||
       next.pos == core::PartOfSpeech::Verb)) {
    bonus += cost::kAlmostNever;
  }

  // The colloquial progressive contraction permits the nominalizer directly
  // after its contracted て/で form (食べ+て+ん+の, 読ん+で+ん+の). This is
  // distinct from an independent progressive auxiliary such as いる or い,
  // whose nominalization must not absorb a following clause boundary.
  if (prev.extended_pos == core::ExtendedPOS::AuxAspectIru && grammar::isTeDeSurface(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::ParticleNo) {
    bonus += cost::kDoubleVeryStrongBonus;
  }

  // The ん in a contracted progressive (〜てんだ) is the nominalizer の, not
  // the negative auxiliary. Exclude only that impossible immediate auxiliary
  // attachment, leaving genuine negative forms after mizenkei untouched.
  if (prev.extended_pos == core::ExtendedPOS::AuxAspectIru && grammar::isTeDeSurface(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::AuxNegativeNu &&
      grammar::isContractedNegativeAuxiliaryLemma(next.lemma)) {
    bonus += cost::kAlmostNever;
  }

  // The regional ておる/でおる contractions retain Godan-ra inflection, so
  // their conditional form attaches to a conjunctive particle (食べ+とれ+ば).
  // Keep this lemma-scoped to avoid broadly favoring a finite progressive
  // before compound conjunction candidates such as のに.
  const bool is_dialectal_oru_contraction = grammar::isDialectalOruContractionLemma(prev.lemma);
  if ((prev.extended_pos == core::ExtendedPOS::AuxAspectIru || prev.extended_pos == core::ExtendedPOS::VerbKateikei) &&
      is_dialectal_oru_contraction && next.extended_pos == core::ExtendedPOS::ParticleConj) {
    bonus += cost::kStrongBonus;
  }

  // The negative dialectal contraction uses the mizenkei plus ん
  // (食べ+とら+ん). Prefer the negative auxiliary over the homographic
  // nominalizer only for the contracted おる paradigm.
  if (prev.extended_pos == core::ExtendedPOS::AuxAspectIru && is_dialectal_oru_contraction &&
      next.extended_pos == core::ExtendedPOS::AuxNegativeNu) {
    bonus += cost::kVeryStrongBonus;
  }

  // Dialectal/character-speech やで is particle + particle in the regression
  // corpus, not copula で.
  if (prev.surface == "や" && next.surface == "で" && next.extended_pos == core::ExtendedPOS::AuxCopulaDa) {
    bonus += cost::kAlmostNever;
  }

  // The attributive copula な cannot introduce the progressive/aspectual いる.
  // This rules out the fabricated な+い+ん+だ chain and leaves the independent
  // adjective plus nominalizer in ないんだ.
  if (prev.extended_pos == core::ExtendedPOS::AuxCopulaDa && grammar::isAttributiveCopulaNa(prev.surface)) {
    if (next.extended_pos == core::ExtendedPOS::AuxAspectIru || next.extended_pos == core::ExtendedPOS::AuxNegativeNu ||
        next.pos == core::PartOfSpeech::Verb) {
      bonus += cost::kAlmostNever;
    }
  }

  // The independent adjective ない is commonly nominalized before a copula
  // (ない+ん+だ). Favor that complete adjective phrase over the unrelated
  // negative-auxiliary homograph.
  if (prev.extended_pos == core::ExtendedPOS::AdjBasic && grammar::isIndependentNegativeAdjective(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::ParticleNo) {
    bonus += cost::kStrongBonus;
  }

  // The complete negative ない cannot itself take another negative ん. This
  // applies regardless of the provisional POS chosen for the homograph and
  // retains the nominalizer analysis in ないんだ.
  if (grammar::isIndependentNegativeAdjective(prev.surface) && next.extended_pos == core::ExtendedPOS::AuxNegativeNu) {
    bonus += cost::kAlmostNever;
  }

  // An onbin candidate that already includes the complete negative ない is
  // not an irrealis form for another negative ん. This removes only the
  // whole-verb competitor in 知らないんだ, not ordinary onbin inflections.
  if (prev.extended_pos == core::ExtendedPOS::VerbOnbinkei && grammar::endsWithNegativeNai(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::AuxNegativeNu) {
    bonus += cost::kAlmostNever;
  }

  // である is the formal copula sequence. A case/conjunctive で followed by
  // the determiner ある is not a grammatical alternative, so preserve the
  // copula candidate before formal nouns such as 以上 and 場合.
  if (utf8::equalsAny(prev.surface, {"で"}) && utf8::equalsAny(next.surface, {"ある"}) &&
      next.extended_pos == core::ExtendedPOS::Determiner) {
    bonus += cost::kAlmostNever;
  }

  // In the copular negative でなく, なく is the adverbial adjective form of
  // ない. Keep the auxiliary reading for verbal 〜なく separate.
  if (utf8::equalsAny(prev.surface, {"で"}) && utf8::equalsAny(next.surface, {"なく"}) &&
      next.extended_pos == core::ExtendedPOS::AuxNegativeNai) {
    bonus += cost::kAlmostNever;
  }

  // Date + 付け + で is a formal-noun construction ("as of ..."), not the
  // verb 付ける in renyokei.
  if (prev.pos == core::PartOfSpeech::Noun && next.surface == "付け" &&
      next.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      (utf8::endsWith(prev.surface, "日") || utf8::endsWith(prev.surface, "月") ||
       utf8::endsWith(prev.surface, "年"))) {
    bonus += cost::kAlmostNever;
  }

  // Keep the common na-adjective 複雑 together; the split 複 + 雑い is a false
  // i-adjective path.
  if (prev.surface == "複" && next.surface == "雑" && next.pos == core::PartOfSpeech::Adjective) {
    bonus += cost::kAlmostNever;
  }

  // Penalty for VerbOnbinkei/VerbRenyokei ending in いい → AuxTenseTa pattern
  // E.g., 願いい+た should be 願い+いたし+ます, not 願いい (願いく) + た
  // Valid forms are: 書い, 泳い, etc. (single い after kanji)
  // Invalid: 願いい (連用形い + さらにい) - this suggests wrong verb base
  // Include VerbRenyokei since 願いい is sometimes assigned as renyokei of 願いう
  if ((prev.extended_pos == core::ExtendedPOS::VerbOnbinkei || prev.extended_pos == core::ExtendedPOS::VerbRenyokei) &&
      next.extended_pos == core::ExtendedPOS::AuxTenseTa && utf8::endsWith(prev.surface, "いい")) {
    bonus += cost::kAlmostNever;
  }

  // Penalty for ParticleAdverbial → single-mora hiragana VerbRenyokei.
  // A subsidiary/adverbial particle (しか, だけ, ばかり…) directly followed by a
  // one-mora hiragana renyokei is a fabricated over-split: the particle path
  // severs a real verb compound (し+かね) and reads the trailing mora as a bare
  // ichidan renyokei (ね ← 寝る). Genuine ParticleAdverbial→VerbRenyokei
  // sequences (かも+しれ, など+あり, でも+あり) always carry a ≥2-mora renyokei
  // stem, and a one-kanji renyokei (だけ+寝) keeps its self-standing stem, so
  // gating on a single hiragana character leaves those untouched while
  // countering the ParticleAdverbial→VerbRenyokei bonus that the false path
  // would otherwise receive.
  if (prev.extended_pos == core::ExtendedPOS::ParticleAdverbial &&
      next.extended_pos == core::ExtendedPOS::VerbRenyokei && next.surface.size() == core::kJapaneseCharBytes &&
      normalize::classifyChar(utf8::decodeFirstChar(next.surface)) == normalize::CharType::Hiragana) {
    bonus += cost::kStrong;
  }

  // A final particle followed immediately by a one-mora hiragana renyokei
  // is likewise an implausible no-boundary split. It fabricates chains such
  // as 扱い+か+ね+た from an adjective/question fragment, instead of preserving
  // the preceding verb-renyokei plus its subsidiary. Real sentence-final
  // questions end here; a following lexical verb begins a new clause and is
  // normally separated by punctuation or has a multi-mora stem.
  if (prev.extended_pos == core::ExtendedPOS::ParticleFinal && next.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      next.surface.size() == core::kJapaneseCharBytes &&
      normalize::classifyChar(utf8::decodeFirstChar(next.surface)) == normalize::CharType::Hiragana) {
    bonus += cost::kStrong;
  }

  // Bonus for VerbRenyokei/VerbOnbinkei → VerbRenyokei (subsidiary verb patterns)
  // E.g., 願い+いたし (お願いいたします), 報告+いたし (ご報告いたします),
  //       し+かね (賛成しかねます), 沿い+かね (ご期待に沿いかねます)
  // Include VerbOnbinkei since 願い is often recognized as onbin form of 願う
  if ((prev.extended_pos == core::ExtendedPOS::VerbRenyokei || prev.extended_pos == core::ExtendedPOS::VerbOnbinkei) &&
      next.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      (grammar::isSubsidiaryHonorificRenyokei(next.surface) || grammar::isModalSubsidiaryRenyokei(next.surface))) {
    bonus += cost::kVeryStrongBonus;
  }

  // A humble auxiliary in renyokei productively takes the connective て
  // (いたし+ております). This preserves the lexical honorific verb against a
  // coincidental auxiliary sequence at the beginning of a sentence.
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && grammar::isHumbleHonorificRenyokei(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::ParticleConj && utf8::equalsAny(next.surface, {"て"})) {
    bonus += cost::kModerateBonus;
  }

  // Bonus for honorific verb renyokei → AuxTenseMasu (ます)
  // E.g., いただき+ます (いただきます), いたし+ます (いたします)
  // This helps いただき beat い+た+だき pattern
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && next.extended_pos == core::ExtendedPOS::AuxTenseMasu &&
      grammar::isHumbleHonorificRenyokei(prev.surface)) {
    bonus += cost::kVeryStrongBonus;
  }

  // Penalty for single い → AuxTenseTa pattern (いただきます problem)
  // い+た+だき should lose to いただき+ます
  // But て+い+た is valid (食べていた)
  // We penalize い→た only when prev is OTHER (sentence start) or NOUN
  // NOT when prev comes from て-form (VerbTeForm)
  if (prev.surface == "い" && prev.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      next.extended_pos == core::ExtendedPOS::AuxTenseTa) {
    bonus += cost::kVeryRare;
  }

  // Penalty for AuxTenseTa → い pattern (たい over-split prevention)
  // 行きたい should be 行き+たい, not 行き+た+い
  // た (AuxTenseTa) should not be followed by standalone い
  // This fixes the issue where VerbRenyokei→た bonus (-1.6) beats たい (-0.8)
  if (prev.extended_pos == core::ExtendedPOS::AuxTenseTa && next.surface == "い") {
    bonus += cost::kAlmostNever;
  }

  // Penalty for だ(AuxTenseTa) after non-ん/non-い words
  // だ as past tense follows ん-onbin (読んだ, 飲んだ) or い-onbin (泳いだ, 注いだ)
  // Without this, てる+だ(past) beats てる+だけ(adverbial particle)
  // because AuxAspectIru→AuxTenseTa bonus applies to both た and だ
  if (next.surface == "だ" && next.extended_pos == core::ExtendedPOS::AuxTenseTa &&
      !utf8::endsWith(prev.surface, "ん") && !utf8::endsWith(prev.surface, "い")) {
    bonus += cost::kAlmostNever;
  }

  return bonus;
}

// Contracted negative past (かっ), progressive でる, できる, て→いた, ござい,
// すぎ intensifier attachment, and final-particle guards.

}  // namespace suzume::analysis::connection_rules
