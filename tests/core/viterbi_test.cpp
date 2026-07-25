#include "core/viterbi.h"

#include <gtest/gtest.h>

namespace suzume::core {
namespace {

// Minimal model of the Scorer concept Viterbi::solve is templated on: word,
// connection, and the two sentence-boundary costs.
struct ExtendedPosScorer {
  float wordCost(const LatticeEdge& edge) const { return edge.cost; }

  float bosCost(const LatticeEdge& /*edge*/) const { return 0.0F; }

  float eosCost(const LatticeEdge& /*edge*/) const { return 0.0F; }

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

// Boundary costs are supplied by the scorer, so Viterbi must apply bosCost only
// to an edge leaving the BOS state and eosCost only to one ending the sentence.
struct BoundaryScorer {
  float wordCost(const LatticeEdge& edge) const { return edge.cost; }

  float bosCost(const LatticeEdge& edge) const {
    return edge.extended_pos == ExtendedPOS::ParticleTopic ? kBoundaryPenalty : 0.0F;
  }

  float eosCost(const LatticeEdge& edge) const {
    return edge.extended_pos == ExtendedPOS::AuxAspectKuru ? kBoundaryPenalty : 0.0F;
  }

  float connectionCost(const LatticeEdge& /*prev*/, const LatticeEdge& /*next*/) const { return 0.0F; }

  static constexpr float kBoundaryPenalty = 10.0F;
};

TEST(ViterbiTest, AppliesScorerBosCostToTheSentenceOpeningEdge) {
  Lattice lattice(2);
  const auto topic_id =
      lattice.addEdge("a", 0, 1, PartOfSpeech::Particle, 0.0F, 0, {}, dictionary::ConjugationType::None,
                      CandidateOrigin::Unknown, 0.0F, {}, ExtendedPOS::ParticleTopic);
  const auto verb_id = lattice.addEdge("b", 0, 1, PartOfSpeech::Verb, 1.0F, 0, {}, dictionary::ConjugationType::None,
                                       CandidateOrigin::Unknown, 0.0F, {}, ExtendedPOS::VerbRenyokei);
  lattice.addEdge("c", 1, 2, PartOfSpeech::Noun, 0.0F, 0, {}, dictionary::ConjugationType::None,
                  CandidateOrigin::Unknown, 0.0F, {}, ExtendedPOS::Noun);

  Viterbi viterbi;
  auto result = viterbi.solve(lattice, BoundaryScorer{});

  // The topic particle is cheaper by word cost but cannot open a sentence.
  ASSERT_EQ(result.path.size(), 2U);
  EXPECT_EQ(result.path[0], verb_id);
  EXPECT_NE(result.path[0], topic_id);
}

TEST(ViterbiTest, AppliesScorerEosCostOnlyToTheSentenceClosingEdge) {
  Lattice lattice(2);
  lattice.addEdge("a", 0, 1, PartOfSpeech::Noun, 0.0F, 0, {}, dictionary::ConjugationType::None,
                  CandidateOrigin::Unknown, 0.0F, {}, ExtendedPOS::Noun);
  const auto aspect_id =
      lattice.addEdge("b", 1, 2, PartOfSpeech::Auxiliary, 0.0F, 0, {}, dictionary::ConjugationType::None,
                      CandidateOrigin::Unknown, 0.0F, {}, ExtendedPOS::AuxAspectKuru);
  const auto noun_id = lattice.addEdge("c", 1, 2, PartOfSpeech::Noun, 1.0F, 0, {}, dictionary::ConjugationType::None,
                                       CandidateOrigin::Unknown, 0.0F, {}, ExtendedPOS::Noun);

  Viterbi viterbi;
  auto result = viterbi.solve(lattice, BoundaryScorer{});

  ASSERT_EQ(result.path.size(), 2U);
  EXPECT_EQ(result.path[1], noun_id);
  EXPECT_NE(result.path[1], aspect_id);
}

TEST(ViterbiTest, LeavesTheSameAspectEdgeAloneWhenItDoesNotEndTheSentence) {
  Lattice lattice(2);
  const auto aspect_id =
      lattice.addEdge("a", 0, 1, PartOfSpeech::Auxiliary, 0.0F, 0, {}, dictionary::ConjugationType::None,
                      CandidateOrigin::Unknown, 0.0F, {}, ExtendedPOS::AuxAspectKuru);
  const auto noun_id = lattice.addEdge("b", 0, 1, PartOfSpeech::Noun, 1.0F, 0, {}, dictionary::ConjugationType::None,
                                       CandidateOrigin::Unknown, 0.0F, {}, ExtendedPOS::Noun);
  lattice.addEdge("c", 1, 2, PartOfSpeech::Noun, 0.0F, 0, {}, dictionary::ConjugationType::None,
                  CandidateOrigin::Unknown, 0.0F, {}, ExtendedPOS::Noun);

  Viterbi viterbi;
  auto result = viterbi.solve(lattice, BoundaryScorer{});

  // Same ExtendedPOS as the penalized edge above, but it is followed by a noun,
  // so no EOS cost applies and its lower word cost wins.
  ASSERT_EQ(result.path.size(), 2U);
  EXPECT_EQ(result.path[0], aspect_id);
  EXPECT_NE(result.path[0], noun_id);
}

}  // namespace
}  // namespace suzume::core
