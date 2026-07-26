#include "bigram_table_internal.h"

namespace suzume::analysis::bigram_rules {

using EPOS = core::ExtendedPOS;
namespace cost = bigram_cost;

namespace {

struct NominalContinuation {
  EPOS next;
  float cost;
};

struct NominalHeadProfile {
  EPOS head;
  float case_cost;
  float adverbial_cost;
  bool accepts_final_particle;
};

constexpr std::array<NominalHeadProfile, 5> kNominalHeadProfiles = {{
    {EPOS::Noun, cost::kNeutral, cost::kStrongBonus, true},
    {EPOS::NounVerbal, cost::kNeutral, cost::kStrongBonus, true},
    {EPOS::NounNumber, cost::kNeutral, cost::kStrongBonus, false},
    {EPOS::Pronoun, cost::kModerateBonus, cost::kExtraStrongBonus, true},
    {EPOS::PronounInterrogative, cost::kNeutral, cost::kExtraStrongBonus, false},
}};

constexpr std::array<NominalContinuation, 4> kCommonNominalContinuations = {{
    {EPOS::ParticleTopic, cost::kStrongBonus},
    {EPOS::ParticleBinding, cost::kVeryStrongBonus},
    {EPOS::AuxCopulaDa, cost::kExtraStrongBonus},
    {EPOS::ParticleCase, cost::kNeutral},
}};

void applyRule(BigramMatrix& table, EPOS head, EPOS next, float rule_cost) {
  const BigramRule rule{head, next, rule_cost};
  applyRules(table, &rule, 1);
}

void applyNominalHeadRules(BigramMatrix& table) {
  for (const NominalHeadProfile& profile : kNominalHeadProfiles) {
    for (const NominalContinuation& continuation : kCommonNominalContinuations) {
      const float rule_cost = continuation.next == EPOS::ParticleCase ? profile.case_cost : continuation.cost;
      applyRule(table, profile.head, continuation.next, rule_cost);
    }
    applyRule(table, profile.head, EPOS::ParticleAdverbial, profile.adverbial_cost);
    if (profile.accepts_final_particle) {
      // Ordinary/deverbal nouns and personal pronouns can form a
      // sentence-final nominal predicate. Number phrases and interrogative
      // pronouns need a following predicate;
      // rewarding their homographic final-particle path breaks 一昼夜+かけて
      // and the fixed indefinite pronoun 何かしら.
      applyRule(table, profile.head, EPOS::ParticleFinal, cost::kModerateBonus);
    }
  }
}

}  // namespace

void setNominalParticleCosts(BigramMatrix& table) {
  applyNominalHeadRules(table);

  static constexpr BigramRule kRules[] = {
      // Nominal particle attachment and formal-noun continuation.
      {EPOS::Noun, EPOS::ParticleConj, cost::kStrong},
      {EPOS::NounNumber, EPOS::Suffix, cost::kStrongBonus},
      {EPOS::NounProperFamily, EPOS::NounProperGiven, cost::kStrongBonus},
      {EPOS::Noun, EPOS::Conjunction, cost::kDoubleVeryStrongBonus},
      {EPOS::Conjunction, EPOS::Noun, cost::kStrongBonus},
      {EPOS::Conjunction, EPOS::Pronoun, cost::kMinorBonus},
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
