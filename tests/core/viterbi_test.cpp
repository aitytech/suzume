#include "core/viterbi.h"

#include <gtest/gtest.h>

namespace suzume::core {
namespace {

struct ExtendedPosScorer {
  float wordCost(const LatticeEdge& edge) const { return edge.cost; }

  float connectionCost(const LatticeEdge& prev, const LatticeEdge& next) const {
    if (prev.extended_pos == ExtendedPOS::VerbRenyokei && next.extended_pos == ExtendedPOS::AuxTenseMasu) {
      return -10.0F;
    }
    return 0.0F;
  }
};

TEST(ViterbiTest, KeepsDistinctExtendedPosStatesForSamePosAndEnd) {
  Lattice lattice(2);
  const auto onbin_id = lattice.addEdge("a", 0, 1, PartOfSpeech::Verb, 0.0F, 0, {}, dictionary::ConjugationType::None,
                                        CandidateOrigin::Unknown, 0.0F, {}, ExtendedPOS::VerbOnbinkei);
  const auto renyokei_id =
      lattice.addEdge("b", 0, 1, PartOfSpeech::Verb, 1.0F, 0, {}, dictionary::ConjugationType::None,
                      CandidateOrigin::Unknown, 0.0F, {}, ExtendedPOS::VerbRenyokei);
  const auto aux_id =
      lattice.addEdge("c", 1, 2, PartOfSpeech::Auxiliary, 0.0F, 0, {}, dictionary::ConjugationType::None,
                      CandidateOrigin::Unknown, 0.0F, {}, ExtendedPOS::AuxTenseMasu);

  Viterbi viterbi;
  auto result = viterbi.solve(lattice, ExtendedPosScorer{});

  ASSERT_EQ(result.path.size(), 2u);
  EXPECT_NE(result.path[0], onbin_id);
  EXPECT_EQ(result.path[0], renyokei_id);
  EXPECT_EQ(result.path[1], aux_id);
}

}  // namespace
}  // namespace suzume::core
