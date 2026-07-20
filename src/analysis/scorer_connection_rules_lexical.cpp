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

float computeParticleQuoteBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float bonus{};

  // The comparative case particle follows an adverbial reference point
  // (かねて+より, 以前+より). Keep this relation ahead of its homographic
  // continuative verb without changing other adverb-to-case boundaries.
  if (prev.pos == core::PartOfSpeech::Adverb && next.extended_pos == core::ExtendedPOS::ParticleCase &&
      utf8::equalsAny(next.surface, {"より"})) {
    bonus += cost::kStrongBonus;
  }

  // A final particle can be quoted as a complete utterance (かしら+と
  // 思う, かな+と考える). This relation is specific to the quotative case
  // particle; applying it to every case particle incorrectly favors paths
  // such as ADV+わ+から over an ordinary following predicate.
  if (prev.extended_pos == core::ExtendedPOS::ParticleFinal && next.extended_pos == core::ExtendedPOS::ParticleCase &&
      utf8::equalsAny(next.surface, {"と"})) {
    bonus += cost::kStrongBonus;
  }

  // A small closed set of sentence-final particles stacks productively
  // (か+な, よ+ね, わ+ね, ぜ+よ). Restore these grammatical sequences against
  // the general final-particle-to-final-particle penalty.
  if (prev.extended_pos == core::ExtendedPOS::ParticleFinal && next.extended_pos == core::ExtendedPOS::ParticleFinal &&
      grammar::isFinalParticleStack(prev.surface, next.surface)) {
    bonus += cost::kExtremeBonus;
  }

  return bonus;
}

float computeCompoundNominalizationBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  // A verb's continuative followed by its particle-marked deverbal noun
  // reading is a productive serial nominalization (打ち+鳴らしを). Prefer
  // that relation over reclassifying the preceding continuative as an
  // unrelated noun. The generated noun is restricted at creation time to a
  // direct particle continuation, so finite and derivational uses remain out
  // of scope.
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && next.extended_pos == core::ExtendedPOS::NounVerbal &&
      next.origin == core::CandidateOrigin::NominalizedNoun) {
    return cost::kModerateBonus;
  }

  // A verified compound in renyokei can be nominalized by の (書きかけの紙,
  // 書きたての文). Its single-word candidate must remain available against a
  // competing decomposition into a verb stem and suffix.
  if (prev.origin == core::CandidateOrigin::VerbCompound && prev.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      next.extended_pos == core::ExtendedPOS::ParticleNo && utf8::equalsAny(next.surface, {"の"})) {
    return cost::kTripleVeryStrongBonus + cost::kModerateBonus;
  }
  return cost::kNeutral;
}

// Progressive/contracted て, dialectal やで, 付け-で formal noun, honorific
// renyokei (いたし/いただき), and い/た/だ auxiliary attachment rules.
float computeSugiFinalParticleBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float bonus{};  // value-init to 0

  // A generated renyokei ending in ず is a fused classical-negative path.
  // Before an independent finite predicate, the productive analysis is
  // mizenkei + ず + predicate (種類を問わ|ず|進む), not a fabricated verb
  // connection. Dictionary words retain their lexical reading.
  if (!prev.fromDictionary() && prev.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      utf8::endsWith(prev.surface, "ず") && next.extended_pos == core::ExtendedPOS::VerbShuushikei) {
    bonus += cost::kAlmostNever;
  }

  // A dictionary noun that carries a particle-like extended POS is a surface
  // homograph, not a grammatical binding particle. Do not let it replace the
  // topic-particle boundary in nominal predicates such as 本|は|ない.
  if (prev.pos == core::PartOfSpeech::Noun && next.pos == core::PartOfSpeech::Noun &&
      next.extended_pos == core::ExtendedPOS::ParticleBinding) {
    bonus += cost::kStrong;
  }

  // A contracted negative ん cannot be followed by an independent かっ verb.
  // The colloquial past is represented by the closed auxiliary んかっ, so
  // reject the fabricated ん + かっ verb chain.
  if (prev.extended_pos == core::ExtendedPOS::AuxNegativeNu && next.extended_pos == core::ExtendedPOS::VerbOnbinkei &&
      next.surface == "かっ") {
    bonus += cost::kAlmostNever;
  }

  // A generated verb-onbin candidate whose reconstructed lemma ends in ぬ
  // is a contracted negative (読まん, 書かん), not a lexical onbin form.
  // Before connective で, keep the productive mizenkei + ん + でも chain.
  if (prev.extended_pos == core::ExtendedPOS::VerbOnbinkei && utf8::endsWith(prev.lemma, "ぬ") &&
      utf8::startsWith(next.surface, "で") &&
      (next.extended_pos == core::ExtendedPOS::ParticleConj ||
       next.extended_pos == core::ExtendedPOS::ParticleAdverbial ||
       next.extended_pos == core::ExtendedPOS::Conjunction)) {
    bonus += cost::kStrong;
  }

  // Penalty for VerbOnbinkei(ん) → Verb(でる) pattern
  // After ん音便, でる is almost always the contracted ている, not the verb 出る
  // E.g., 並んでる = 並んでいる (progressive), やんでる = 病んでいる
  // Force the で(PART_接続) + る path instead
  if (prev.extended_pos == core::ExtendedPOS::VerbOnbinkei && utf8::endsWith(prev.surface, "ん") &&
      next.pos == core::PartOfSpeech::Verb && next.surface == "でる") {
    bonus += cost::kStrong;
  }

  // The completion auxiliary after the renyokei homograph of 出る belongs
  // to the voiced te-form chain (読ん+で+しまう), not to a lexical verb.
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && utf8::equalsAny(prev.lemma, {"出る", "でる"}) &&
      next.extended_pos == core::ExtendedPOS::AuxAspectShimau) {
    bonus += cost::kStrong;
  }

  // The voiced progressive contraction follows an n-onbin: 読んでる,
  // 飲んでる. Outside this environment でる retains its lexical-verb reading.
  if (prev.extended_pos == core::ExtendedPOS::VerbOnbinkei && utf8::endsWith(prev.surface, "ん") &&
      next.extended_pos == core::ExtendedPOS::AuxAspectIru && next.surface == "でる") {
    bonus += cost::kStrongBonus;
  }

  // The irregular potential できる can follow either a verbal noun or a
  // compound adverbial particle.  Keep it ahead of an accidental で + きる
  // split while preserving the stronger particle boundary evidence.
  if (next.pos == core::PartOfSpeech::Verb && next.surface == "できる") {
    if (prev.pos == core::PartOfSpeech::Noun) {
      bonus += cost::kModerateBonus;
    } else if (prev.extended_pos == core::ExtendedPOS::ParticleAdverbial) {
      bonus += cost::kStrongBonus;
    }
  }

  // Penalty for PARTICLE て → VerbTaForm いた pattern
  // MeCab splits て+い+た, not て+いた
  // いた as verb た-form should not follow て directly
  if (prev.extended_pos == core::ExtendedPOS::ParticleConj && prev.surface == "て" &&
      next.extended_pos == core::ExtendedPOS::VerbTaForm && next.surface == "いた") {
    bonus += cost::kAlmostNever;
  }

  // Penalty for PREFIX ご → VerbRenyokei ざい pattern
  // E.g., ございます should be ござい+ます, not ご+ざい+ます
  // The prefix ご is for nouns (ご報告), not for splitting ござる
  if (prev.extended_pos == core::ExtendedPOS::Prefix && prev.surface == "ご" &&
      next.extended_pos == core::ExtendedPOS::VerbRenyokei && utf8::startsWith(next.surface, "ざい")) {
    bonus += cost::kAlmostNever;
  }

  // Surface-based bonus for AdjStem → すぎ pattern
  // E.g., 高+すぎる, 美味し+すぎた: adjective stem plus excessive auxiliary.
  // AdjStem→Verb has prohibitive penalty to prevent な+い splits
  // But AdjStem+すぎ is valid grammar (i-adjective stem + すぎる)
  // Exclude VerbTeForm (すぎて) - should split as すぎ+て
  if (prev.extended_pos == core::ExtendedPOS::AdjStem && next.extended_pos != core::ExtendedPOS::VerbTeForm &&
      utf8::startsWith(next.surface, "すぎ")) {
    // Strong bonus to overcome AdjStem→Verb prohibitive penalty
    bonus += sc::kBonusDoubleVeryStrong;
  }

  // Surface-based bonus for AdjNaAdj → すぎ pattern
  // E.g., シンプル+すぎない, 静か+すぎる (na-adjective + sugiru)
  // NOUN→VERB_連用 has bonus from bigram table, which can beat ADJ_NA path
  // This helps dictionary ADJ_NA entries beat unknown NOUN candidates
  if (prev.extended_pos == core::ExtendedPOS::AdjNaAdj && utf8::startsWith(next.surface, "すぎ")) {
    bonus += cost::kStrongBonus;
  }

  // Surface-based bonus for all-kanji NOUN → すぎ pattern
  // E.g., 最高+すぎ, 贅沢+すぎ, 美人+すぎ (kanji compound + sugiru "too much")
  // Without this, multi-kanji nouns get split: 最高→最+高(ADJ_語幹)+すぎ
  // because ADJ_語幹→すぎ has a very strong surface bonus (-3.2)
  // Only apply to all-kanji surfaces (not katakana/verb renyokei)
  if (prev.pos == core::PartOfSpeech::Noun && prev.surface.size() >= 6 &&  // 2+ chars (6+ bytes)
      grammar::isAllKanji(prev.surface) && utf8::startsWith(next.surface, "すぎ")) {
    bonus += sc::kBonusDoubleVeryStrong;
  }

  // A sokuonbin copula followed by たら uses the hypothetical form of the
  // past auxiliary: 静か+だっ+たら. The homographic conjunctive particle
  // cannot attach directly to the copula's だっ form.
  if (prev.extended_pos == core::ExtendedPOS::AuxCopulaDa && utf8::endsWith(prev.surface, "っ") &&
      next.extended_pos == core::ExtendedPOS::ParticleConj && utf8::equalsAny(next.surface, {"たら"})) {
    bonus += cost::kVeryStrongBonus;
  }

  // Penalty for AuxCopulaDa(だ/な) → ParticleFinal(ったら) pattern.
  // The final particle is valid after a noun (あなた+ったら), but after the
  // copula these surfaces belong to a different inflectional boundary:
  // だっ+たら (copula conditional) or なっ+たら (なる conditional).
  if (prev.extended_pos == core::ExtendedPOS::AuxCopulaDa && utf8::equalsAny(prev.surface, {"だ", "な"}) &&
      next.extended_pos == core::ExtendedPOS::ParticleFinal && utf8::startsWith(next.surface, "った")) {
    bonus += cost::kStrong;
  }

  // Penalty for ParticleFinal → VerbRenyokei pattern
  // E.g., いいよね should be いい+よ+ね(PART), not いい+よ+ね(VERB 寝る)
  // Final particles (よ, な, ね, わ) are rarely followed by verb renyokei
  // The short hiragana verb ね (寝る renyokei) competes with final particle ね
  // This penalty ensures particle interpretation wins in よね, なね, etc. patterns
  if (prev.extended_pos == core::ExtendedPOS::ParticleFinal && next.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      grammar::isPureHiragana(next.surface) && next.surface.size() <= 3) {  // Single hiragana (3 bytes)
    bonus += cost::kRare;
  }

  // The surface か also marks an indefinite phrase (誰か来る, 何かいる).
  // If a verb actually follows, the edge is internal and therefore cannot be
  // sentence-final. Cancel the generic final-particle-to-verb penalty for this
  // homograph while retaining it for genuine final particles よ/ね/な/わ.
  if (prev.extended_pos == core::ExtendedPOS::ParticleFinal && utf8::equalsAny(prev.surface, {"か"}) &&
      next.pos == core::PartOfSpeech::Verb && next.fromDictionary() &&
      next.extended_pos != core::ExtendedPOS::VerbMizenkei) {
    bonus += cost::kVeryStrongBonus;
  }

  // Penalty for pure-hiragana Conjunction → bare single-hiragana non-particle
  // A conjunction is a complete word; a following lone hiragana verb/aux/unknown
  // is never a natural continuation. When the conjunction surface is a proper
  // prefix of a longer i-adjective, this is the fragment path that must lose:
  // ただしい → ただし(CONJ)+い must lose to the ただしい adjective.
  // Particles are exempt: they legitimately form compound conjunctions
  // (されど+も, だけど+も).
  if (prev.pos == core::PartOfSpeech::Conjunction && grammar::isPureHiragana(prev.surface) &&
      next.pos != core::PartOfSpeech::Particle && grammar::isPureHiragana(next.surface) &&
      next.surface.size() <= 3) {  // Single hiragana (3 bytes)
    bonus += cost::kNever;
  }

  return bonus;
}

/// Penalty for a bare single-character potential auxiliary (え/得, renyokei of
/// える/得る) followed by anything other than a continuation morpheme.
/// The renyokei form only occurs in chains like あり+え+ない / 解決し+得+ない /
/// あり+え+て, so a following noun/verb/symbol means the え is a fragment of a
/// longer word (いいえ → いい+え, ねえ → ね+え). The multi-character
/// shuushikei える/うる legitimately ends a clause and is exempt.
float computeBarePotentialRenyokeiPenalty(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float penalty{};
  if (prev.extended_pos == core::ExtendedPOS::AuxPotential && prev.surface.size() <= 3 &&  // Single character (3 bytes)
      next.pos != core::PartOfSpeech::Auxiliary && next.pos != core::PartOfSpeech::Particle &&
      next.pos != core::PartOfSpeech::Suffix) {
    penalty += cost::kSevere;
  }
  return penalty;
}

float computeCopulaConditionalBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  // The literary concessive/conditional construction であれ(ば) is the
  // continuative copula followed by the hypothetical form of ある. Favor this
  // grammatical chain over the homographic case-particle + pronoun sequence.
  if (prev.extended_pos == core::ExtendedPOS::AuxCopulaDa && next.extended_pos == core::ExtendedPOS::VerbKateikei &&
      utf8::equalsAny(next.surface, {"あれ"}) && utf8::equalsAny(next.lemma, {"ある"})) {
    return cost::kVeryStrongBonus;
  }
  // A copula cannot directly take an unrelated lexical hypothetical form.
  // This also keeps the known で+あれ+ば chain split rather than selecting a
  // fabricated one-token verb candidate for あれば.
  if (prev.extended_pos == core::ExtendedPOS::AuxCopulaDa && next.extended_pos == core::ExtendedPOS::VerbKateikei) {
    return cost::kStrong;
  }
  // In であれ, the hypothetical ある can coordinate another nominal
  // predicate (本であれ水であれ) or introduce the following predicate
  // (本であれ読む). These continuations distinguish it from the pronoun あれ.
  if (prev.extended_pos == core::ExtendedPOS::VerbKateikei && prev.lemma == "ある" &&
      (next.pos == core::PartOfSpeech::Noun || next.extended_pos == core::ExtendedPOS::VerbShuushikei)) {
    return cost::kStrongBonus;
  }
  return {};
}

float computePastConditionalVerbBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  // The sahen renyokei takes the past conditional directly in 〜としたら.
  // This is a true inflectional chain, unlike a general renyokei followed by
  // a past auxiliary, and keeps たら as AUX rather than a conjunction.
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && prev.lemma == "する" &&
      next.extended_pos == core::ExtendedPOS::AuxTenseTa && utf8::equalsAny(next.surface, {"たら"})) {
    return cost::kVeryStrongBonus;
  }

  // The conditional forms of the past auxiliary introduce a following main
  // predicate (読ん+たら+進む, 読ま+せ+ん+でし+たら+進む). They are unlike a
  // completed-past た, which must not be followed by a bare verb.
  if (prev.extended_pos == core::ExtendedPOS::AuxTenseTa && utf8::equalsAny(prev.surface, {"たら", "だら"}) &&
      next.extended_pos == core::ExtendedPOS::VerbShuushikei) {
    return cost::kDoubleVeryStrongBonus;
  }
  return {};
}

float computeExistentialAruNominalPredicateBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  // A bare noun can directly predicate the existential verb in literary and
  // fixed constructions (本あってこそ, 本あれば, 本ある限り). This is distinct
  // from the copular である sequence, whose preceding morpheme is auxiliary.
  if (prev.extended_pos == core::ExtendedPOS::Noun && next.pos == core::PartOfSpeech::Verb && next.fromDictionary() &&
      next.lemma == "ある") {
    // The generic noun-to-short-verb guards intentionally resist an unmarked
    // object+verb split. A dictionary-backed existential form is the
    // grammatical exception, so cancel those guards only for this paradigm.
    return cost::kDoubleVeryStrongBonus + cost::kStrongBonus;
  }
  return {};
}

float computeCompletionAuxiliaryBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  // A lexical renyokei can take the completion subsidiary 終わる in its
  // irrealis form before negation (読み+終わら+ない). This is distinct from
  // an arbitrary renyokei-to-verb sequence, which remains discouraged.
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && next.extended_pos == core::ExtendedPOS::VerbMizenkei &&
      utf8::equalsAny(next.lemma, {"終わる"})) {
    return cost::kDoubleVeryStrongBonus;
  }
  return {};
}

float computeAdjectiveTePredicatePenalty(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  // An i-adjective candidate ending in て/で cannot directly govern a new
  // lexical predicate. The final mora is the connective particle and must be
  // separated (嬉しく|て|なら|ない, 高く|て|なる). Auxiliary continuations are
  // represented by their own extended POS and are intentionally unaffected.
  if (prev.extended_pos == core::ExtendedPOS::AdjRenyokei &&
      (utf8::endsWith(prev.surface, "て") || utf8::endsWith(prev.surface, "で")) &&
      (next.extended_pos == core::ExtendedPOS::VerbMizenkei || next.extended_pos == core::ExtendedPOS::ParticleConj)) {
    return cost::kAlmostNever;
  }
  return {};
}

float computeClassicalNegativeBoundaryPenalty(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  // A renyokei candidate ending in ぬ before a formal noun is a false fused
  // negative. The productive analysis is mizenkei + ぬ + formal noun.
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && utf8::endsWith(prev.surface, "ぬ") &&
      next.extended_pos == core::ExtendedPOS::NounFormal) {
    return cost::kStrong;
  }
  return {};
}

}  // namespace suzume::analysis::connection_rules
