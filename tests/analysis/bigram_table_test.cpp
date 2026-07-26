/**
 * @file bigram_table_test.cpp
 * @brief Regression coverage for ExtendedPOS connection rules.
 */

#include "analysis/bigram_table.h"

#include <gtest/gtest.h>

#include <array>

#include "analysis/bigram_table_internal.h"
#include "analysis/scorer.h"

namespace suzume::analysis {
namespace {

struct BigramOverrideCase {
  core::PartOfSpeech prev;
  core::PartOfSpeech next;
  float ScorerOptions::BigramOverrides::*option;
};

struct StaticBigramRuleCase {
  core::PartOfSpeech prev_pos;
  core::ExtendedPOS prev_extended_pos;
  core::PartOfSpeech next_pos;
  core::ExtendedPOS next_extended_pos;
  float expected_cost;
};

core::LatticeEdge makeEdge(core::PartOfSpeech pos) {
  core::LatticeEdge edge;
  edge.surface = "x";
  edge.pos = pos;
  return edge;
}

TEST(BigramTableTest, EveryExposedPosOverrideChangesOnlyItsPairCost) {
  using POS = core::PartOfSpeech;
  using Overrides = ScorerOptions::BigramOverrides;
  constexpr std::array<BigramOverrideCase, 14> kCases = {{
      {POS::Noun, POS::Suffix, &Overrides::noun_to_suffix},
      {POS::Prefix, POS::Noun, &Overrides::prefix_to_noun},
      {POS::Prefix, POS::Verb, &Overrides::prefix_to_verb},
      {POS::Pronoun, POS::Auxiliary, &Overrides::pron_to_aux},
      {POS::Verb, POS::Verb, &Overrides::verb_to_verb},
      {POS::Verb, POS::Noun, &Overrides::verb_to_noun},
      {POS::Verb, POS::Auxiliary, &Overrides::verb_to_aux},
      {POS::Adjective, POS::Auxiliary, &Overrides::adj_to_aux},
      {POS::Adjective, POS::Verb, &Overrides::adj_to_verb},
      {POS::Adjective, POS::Adjective, &Overrides::adj_to_adj},
      {POS::Particle, POS::Verb, &Overrides::part_to_verb},
      {POS::Particle, POS::Noun, &Overrides::part_to_noun},
      {POS::Auxiliary, POS::Particle, &Overrides::aux_to_part},
      {POS::Auxiliary, POS::Auxiliary, &Overrides::aux_to_aux},
  }};

  for (const BigramOverrideCase& test_case : kCases) {
    ScorerOptions lower_options;
    lower_options.bigram.*(test_case.option) = -1.25F;
    ScorerOptions higher_options;
    higher_options.bigram.*(test_case.option) = 2.5F;

    const Scorer lower_scorer(lower_options);
    const Scorer higher_scorer(higher_options);
    const core::LatticeEdge prev = makeEdge(test_case.prev);
    const core::LatticeEdge next = makeEdge(test_case.next);

    EXPECT_FLOAT_EQ(higher_scorer.connectionCost(prev, next) - lower_scorer.connectionCost(prev, next), 3.75F);
  }
}

TEST(BigramTableTest, PureExtendedPosRulesDoNotNeedSurfaceAdjustments) {
  const Scorer scorer;
  core::LatticeEdge prev = makeEdge(core::PartOfSpeech::Particle);
  prev.extended_pos = core::ExtendedPOS::ParticleFinal;
  core::LatticeEdge next = makeEdge(core::PartOfSpeech::Adverb);
  next.extended_pos = core::ExtendedPOS::Adverb;

  EXPECT_FLOAT_EQ(scorer.connectionCost(prev, next), 3.3F);
}

TEST(BigramTableTest, ProperNameSequenceIsTableBacked) {
  const Scorer scorer;
  core::LatticeEdge prev = makeEdge(core::PartOfSpeech::Noun);
  prev.extended_pos = core::ExtendedPOS::NounProperFamily;
  core::LatticeEdge next = makeEdge(core::PartOfSpeech::Noun);
  next.extended_pos = core::ExtendedPOS::NounProperGiven;

  EXPECT_FLOAT_EQ(scorer.connectionCost(prev, next), -0.8F);
}

TEST(BigramTableTest, PureExtendedPosRulesKeepTheirConnectionCosts) {
  using EPOS = core::ExtendedPOS;
  using POS = core::PartOfSpeech;
  constexpr std::array<StaticBigramRuleCase, 10> kCases = {{
      {POS::Adjective, EPOS::AdjNaAdj, POS::Noun, EPOS::Noun, 1.7F},
      {POS::Conjunction, EPOS::Conjunction, POS::Particle, EPOS::ParticleBinding, -2.9F},
      {POS::Pronoun, EPOS::Pronoun, POS::Auxiliary, EPOS::AuxConjectureRashii, -1.8F},
      {POS::Verb, EPOS::VerbShuushikei, POS::Particle, EPOS::ParticleBinding, -1.6F},
      {POS::Verb, EPOS::VerbMizenkei, POS::Particle, EPOS::ParticleCase, 3.0F},
      {POS::Verb, EPOS::VerbRenyokei, POS::Auxiliary, EPOS::AuxAspectIku, 3.4F},
      {POS::Particle, EPOS::ParticleCase, POS::Particle, EPOS::ParticleFinal, 3.5F},
      {POS::Determiner, EPOS::Determiner, POS::Adjective, EPOS::AdjStem, -0.3F},
      {POS::Auxiliary, EPOS::AuxVolitional, POS::Auxiliary, EPOS::AuxTenseTa, 3.3F},
      {POS::Auxiliary, EPOS::AuxNegativeNu, POS::Auxiliary, EPOS::AuxCopulaDa, -2.9F},
  }};

  const Scorer scorer;
  for (size_t case_index = 0; case_index < kCases.size(); ++case_index) {
    SCOPED_TRACE(case_index);
    const StaticBigramRuleCase& test_case = kCases[case_index];
    core::LatticeEdge prev = makeEdge(test_case.prev_pos);
    prev.extended_pos = test_case.prev_extended_pos;
    core::LatticeEdge next = makeEdge(test_case.next_pos);
    next.extended_pos = test_case.next_extended_pos;

    EXPECT_FLOAT_EQ(scorer.connectionCost(prev, next), test_case.expected_cost);
  }
}

TEST(BigramTableTest, EveryNominalHeadHasTheCommonContinuationRules) {
  using EPOS = core::ExtendedPOS;
  struct NominalHeadCase {
    EPOS head;
    float case_cost;
    float adverbial_cost;
    float final_cost;
  };
  constexpr std::array<NominalHeadCase, 5> kNominalHeads = {{
      {EPOS::Noun, bigram_cost::kNeutral, bigram_cost::kStrongBonus, bigram_cost::kModerateBonus},
      {EPOS::NounVerbal, bigram_cost::kNeutral, bigram_cost::kStrongBonus, bigram_cost::kModerateBonus},
      {EPOS::NounNumber, bigram_cost::kNeutral, bigram_cost::kStrongBonus, bigram_cost::kNeutral},
      {EPOS::Pronoun, bigram_cost::kModerateBonus, bigram_cost::kExtraStrongBonus, bigram_cost::kModerateBonus},
      {EPOS::PronounInterrogative, bigram_cost::kNeutral, bigram_cost::kExtraStrongBonus, bigram_cost::kNeutral},
  }};
  struct ContinuationCost {
    EPOS next;
    float expected_cost;
  };
  constexpr std::array<ContinuationCost, 3> kCommonContinuations = {{
      {EPOS::ParticleTopic, bigram_cost::kStrongBonus},
      {EPOS::ParticleBinding, bigram_cost::kVeryStrongBonus},
      {EPOS::AuxCopulaDa, bigram_cost::kExtraStrongBonus},
  }};

  for (const NominalHeadCase& head : kNominalHeads) {
    EXPECT_FLOAT_EQ(BigramTable::getCost(head.head, EPOS::ParticleCase), head.case_cost);
    EXPECT_FLOAT_EQ(BigramTable::getCost(head.head, EPOS::ParticleAdverbial), head.adverbial_cost);
    EXPECT_FLOAT_EQ(BigramTable::getCost(head.head, EPOS::ParticleFinal), head.final_cost);
    for (const ContinuationCost& continuation : kCommonContinuations) {
      EXPECT_FLOAT_EQ(BigramTable::getCost(head.head, continuation.next), continuation.expected_cost);
    }
  }
}

#ifndef NDEBUG
TEST(BigramTableTest, DuplicateRuleAssignmentIsRejectedInDebugBuilds) {
  using EPOS = core::ExtendedPOS;
  bigram_rules::BigramMatrix table{};
  for (auto& row : table) {
    row.fill(bigram_rules::kUnsetCost);
  }
  const bigram_rules::BigramRule rule{EPOS::ParticleFinal, EPOS::Noun, bigram_cost::kProhibitive};
  bigram_rules::applyRules(table, &rule, 1);

  EXPECT_DEATH(bigram_rules::applyRules(table, &rule, 1), "duplicate ExtendedPOS bigram rule");
}
#endif

}  // namespace
}  // namespace suzume::analysis
