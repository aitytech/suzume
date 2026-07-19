/**
 * @file scorer_word_cost_test.cpp
 * @brief Regression coverage for data-driven word-cost rules.
 */

#include <gtest/gtest.h>

#include <array>

#include "analysis/bigram_table.h"
#include "analysis/scorer.h"

namespace suzume::analysis {
namespace {

struct VerbEndingPenaltyCase {
  const char* surface;
  float expected_penalty;
};

core::LatticeEdge makeUnknownHiraganaVerb(std::string_view surface) {
  core::LatticeEdge edge;
  edge.surface = surface;
  edge.pos = core::PartOfSpeech::Verb;
  edge.extended_pos = core::ExtendedPOS::VerbShuushikei;
  return edge;
}

TEST(ScorerWordCostTest, HiraganaVerbEndingPenaltiesAreTableDriven) {
  constexpr std::array<VerbEndingPenaltyCase, 4> kCases = {{
      {"あそう", bigram_cost::kRare},
      {"あてき", bigram_cost::kVeryRare},
      {"あまし", bigram_cost::kVeryRare},
      {"あてい", bigram_cost::kVeryRare},
  }};

  const Scorer scorer;
  const float baseline_cost = scorer.wordCost(makeUnknownHiraganaVerb("あいう"));
  for (const VerbEndingPenaltyCase& test_case : kCases) {
    EXPECT_FLOAT_EQ(scorer.wordCost(makeUnknownHiraganaVerb(test_case.surface)) - baseline_cost,
                    test_case.expected_penalty);
  }
}

}  // namespace
}  // namespace suzume::analysis
