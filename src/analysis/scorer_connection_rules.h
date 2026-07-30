#ifndef SUZUME_ANALYSIS_SCORER_CONNECTION_RULES_H_
#define SUZUME_ANALYSIS_SCORER_CONNECTION_RULES_H_

#include "core/lattice.h"

namespace suzume::analysis::connection_rules {

float computeVerbRenyokeiEarlyBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next);
float computePassiveCausativeBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next);
float computeTaFormVolitionalBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next);
float computeNegativeAndNounVerbBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next);
float computeParticleDeterminerBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next);
float computePrefixSymbolBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next);

/** @brief Waives the particle→polite-auxiliary bar for a continuative-based compound particle. */
float computeCompoundParticlePoliteBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next);

/** @brief Bars a multi-mora conjunctive particle ending in で from governing the copular ある. */
float computeConjunctiveParticleCopulaPenalty(const core::LatticeEdge& prev, const core::LatticeEdge& next);

/** @brief Bars a finished predicate from introducing an adverb ending in the adverbializing に. */
float computeAdverbialNiAfterPredicatePenalty(const core::LatticeEdge& prev, const core::LatticeEdge& next);
float computeSuffixShortVerbBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next);
float computeParticleQuoteBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next);
float computeCompoundNominalizationBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next);
float computeProgressiveHonorificBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next);
float computeSugiFinalParticleBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next);
float computeCopulaConditionalBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next);
float computePastConditionalVerbBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next);
float computeExistentialAruNominalPredicateBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next);
float computeCompletionAuxiliaryBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next);
float computeBarePotentialRenyokeiPenalty(const core::LatticeEdge& prev, const core::LatticeEdge& next);
float computeAdjectiveTePredicatePenalty(const core::LatticeEdge& prev, const core::LatticeEdge& next);
float computeClassicalNegativeBoundaryPenalty(const core::LatticeEdge& prev, const core::LatticeEdge& next);
float computeAdjectiveDerivationHostPenalty(const core::LatticeEdge& prev, const core::LatticeEdge& next);

}  // namespace suzume::analysis::connection_rules

#endif  // SUZUME_ANALYSIS_SCORER_CONNECTION_RULES_H_
