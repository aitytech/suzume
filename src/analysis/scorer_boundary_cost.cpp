#include "analysis/scorer.h"
#include "analysis/scorer_constants.h"
#include "core/types.h"
#include "grammar/char_patterns.h"

namespace sc = suzume::analysis::scorer;

namespace suzume::analysis {

float Scorer::bosCost(const core::LatticeEdge& edge) const {
  float cost = 0.0F;

  if (edge.pos == core::PartOfSpeech::Suffix) {
    cost = sc::kBosSuffixPenalty;
  }
  if (edge.pos == core::PartOfSpeech::Conjunction) {
    cost = sc::kBosConjunctionBonus;
  }
  // At BOS, そう should be a demonstrative na-adjective, not appearance aux
  // (e.g. "そうかもしれません").
  if (edge.extended_pos == core::ExtendedPOS::AuxAppearanceSou) {
    cost += sc::kBosAppearanceSouPenalty;
  }
  // いく aspect is only valid after a て-form (食べていく); at BOS it is the
  // verb 行く or part of a pronoun (いくつ).
  if (edge.extended_pos == core::ExtendedPOS::AuxAspectIku) {
    cost += sc::kBosAspectIkuPenalty;
  }
  // くる aspect (き) is only valid after a て-form; at BOS it is 来る
  // or part of a noun (きもの).
  if (edge.extended_pos == core::ExtendedPOS::AuxAspectKuru) {
    cost += sc::kBosAspectKuruPenalty;
  }
  if (edge.extended_pos == core::ExtendedPOS::AuxTenseTa) {
    cost += sc::kBosTensePenalty;
  }
  if (edge.extended_pos == core::ExtendedPOS::AuxHonorific) {
    cost += sc::kBosHonorificAuxPenalty;
  }
  if (edge.extended_pos == core::ExtendedPOS::ParticleFinal) {
    cost += sc::kBosFinalParticlePenalty;
  }
  // A 係助詞 (は/も) marks a topic against a preceding phrase; it cannot
  // open a sentence. Keeps はいった → はいっ (入る) from splitting into
  // は + いっ (言う) at BOS.
  if (edge.extended_pos == core::ExtendedPOS::ParticleTopic) {
    cost += sc::kBosTopicParticlePenalty;
  }

  return cost;
}

float Scorer::eosCost(const core::LatticeEdge& edge) const {
  float cost = 0.0F;

  // Restricted to the bare renyokei き (a single codepoint): it needs a
  // following た/て/ます, whereas the 終止形 くる/くれる legitimately ends a
  // sentence (勉強してくる) and must not be penalized.
  if (edge.extended_pos == core::ExtendedPOS::AuxAspectKuru && edge.end - edge.start == 1) {
    cost += sc::kEosAspectKuruPenalty;
  }
  if (edge.extended_pos == core::ExtendedPOS::ParticleConj && grammar::isListingParticleTariSurface(edge.surface)) {
    cost += sc::kEosListingParticlePenalty;
  }

  return cost;
}

}  // namespace suzume::analysis
