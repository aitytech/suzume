/**
 * @file scorer_character_count_test.cpp
 * @brief Connection rules that mean "one character" must count codepoints.
 *
 * A byte-length test (`surface.size() == 3`) is wrong in both directions: it
 * misses a supplementary-plane kanji, which is four bytes, and it fires on an
 * unrelated three-byte ASCII run.
 */

#include <gtest/gtest.h>

#include "analysis/scorer.h"

namespace suzume::analysis {
namespace {

// U+20B9F, a CJK Extension B kanji: one character, four bytes in UTF-8.
constexpr const char* kSupplementaryKanji = "𠮟";

core::LatticeEdge makeNoun(std::string_view surface) {
  core::LatticeEdge edge;
  edge.surface = surface;
  edge.pos = core::PartOfSpeech::Noun;
  edge.extended_pos = core::ExtendedPOS::Noun;
  return edge;
}

core::LatticeEdge makePassiveAuxiliary() {
  core::LatticeEdge edge;
  edge.surface = "れる";
  edge.pos = core::PartOfSpeech::Auxiliary;
  edge.extended_pos = core::ExtendedPOS::AuxPassive;
  return edge;
}

TEST(ScorerCharacterCountTest, SingleKanjiNounBeforePassiveIsPenalizedRegardlessOfEncodedLength) {
  const Scorer scorer;
  const core::LatticeEdge passive = makePassiveAuxiliary();

  const float bmp = scorer.connectionCost(makeNoun("揺"), passive);
  const float supplementary = scorer.connectionCost(makeNoun(kSupplementaryKanji), passive);
  const float multi_kanji = scorer.connectionCost(makeNoun("経済"), passive);

  // The supplementary-plane kanji is one character, so it gets the same
  // treatment as any other single-kanji noun.
  EXPECT_FLOAT_EQ(supplementary, bmp);
  EXPECT_GT(bmp, multi_kanji) << "the penalty applies only to a single-character noun";
}

TEST(ScorerCharacterCountTest, ThreeByteAsciiNounIsNotTreatedAsASingleKanji) {
  const Scorer scorer;
  const core::LatticeEdge passive = makePassiveAuxiliary();

  const float ascii_run = scorer.connectionCost(makeNoun("abc"), passive);
  const float single_kanji = scorer.connectionCost(makeNoun("揺"), passive);
  const float multi_kanji = scorer.connectionCost(makeNoun("経済"), passive);

  EXPECT_LT(ascii_run, single_kanji) << "a three-byte ASCII run is three characters";
  EXPECT_FLOAT_EQ(ascii_run, multi_kanji);
}

}  // namespace
}  // namespace suzume::analysis
