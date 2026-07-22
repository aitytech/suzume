#include "bigram_table_internal.h"

namespace suzume::analysis::bigram_rules {

using EPOS = core::ExtendedPOS;
namespace cost = bigram_cost;

void setNominalParticleCosts(BigramMatrix& table) {
  static constexpr BigramRule kRules[] = {
      // Nominal particle attachment and formal-noun continuation.
      {EPOS::Noun, EPOS::ParticleCase, cost::kNeutral},
      {EPOS::Noun, EPOS::ParticleConj, cost::kStrong},
      {EPOS::NounNumber, EPOS::Suffix, cost::kStrongBonus},
      {EPOS::NounNumber, EPOS::ParticleAdverbial, cost::kStrongBonus},
      {EPOS::Noun, EPOS::ParticleTopic, cost::kStrongBonus},
      // A lexicalized/derived continuative noun takes nominal particles just
      // like an ordinary noun (答え+は, 読み+を).  Without this entry the
      // homographic verb continuative inherits the topic bonus while the
      // explicitly generated NounVerbal edge does not.
      {EPOS::NounVerbal, EPOS::ParticleTopic, cost::kStrongBonus},
      {EPOS::NounProperFamily, EPOS::NounProperGiven, cost::kStrongBonus},
      {EPOS::Noun, EPOS::Conjunction, cost::kDoubleVeryStrongBonus},
      {EPOS::Conjunction, EPOS::Noun, cost::kStrongBonus},
      {EPOS::Conjunction, EPOS::Pronoun, cost::kMinorBonus},
      {EPOS::Noun, EPOS::ParticleAdverbial, cost::kStrongBonus},
      {EPOS::Noun, EPOS::ParticleBinding, cost::kVeryStrongBonus},
      {EPOS::ParticleCase, EPOS::ParticleBinding, cost::kStrongBonus},
      {EPOS::ParticleBinding, EPOS::AdjBasic, cost::kStrongBonus},
      {EPOS::ParticleBinding, EPOS::AdjRenyokei, cost::kStrongBonus},
      {EPOS::Noun, EPOS::AdjNaAdj, cost::kStrongBonus},
      {EPOS::NounFormal, EPOS::AdjNaAdj, cost::kModerateBonus},
      {EPOS::Noun, EPOS::NounFormal, cost::kMinorBonus},
      {EPOS::Noun, EPOS::Noun, cost::kMinor},
      {EPOS::NounFormal, EPOS::ParticleCase, cost::kModerateBonus},
      {EPOS::NounFormal, EPOS::ParticleTopic, cost::kModerateBonus},
      {EPOS::NounFormal, EPOS::Adverb, cost::kStrong},
      {EPOS::NounFormal, EPOS::ParticleBinding, cost::kVeryStrongBonus},
      {EPOS::NounFormal, EPOS::AuxCopulaDa, cost::kVeryStrongBonus},
      {EPOS::NounFormal, EPOS::AuxCopulaDesu, cost::kStrongBonus},
      {EPOS::NounFormal, EPOS::AuxNegativeNai, cost::kModerateBonus},
      {EPOS::NounFormal, EPOS::AdjBasic, cost::kDoubleVeryStrongBonus},
      {EPOS::NounFormal, EPOS::ParticleAdverbial, cost::kDoubleVeryStrongBonus},
      {EPOS::ParticleNo, EPOS::NounFormal, cost::kStrongBonus},
      {EPOS::AuxAspectIru, EPOS::NounFormal, cost::kVeryStrongBonus},
      {EPOS::ParticleQuote, EPOS::NounFormal, cost::kVeryStrongBonus},
  };
  applyRules(table, kRules, sizeof(kRules) / sizeof(kRules[0]));
}

}  // namespace suzume::analysis::bigram_rules
