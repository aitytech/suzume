#include <gtest/gtest.h>

#include "suzume.h"

namespace suzume::analysis {
namespace {

Suzume makeDeverbalNounAnalyzer() {
  SuzumeOptions options;
  options.skip_user_dictionary = true;
  return Suzume(options);
}

TEST(DeverbalNounNaku, TreatsVerbRenyokeiAsNounBeforeIndependentNaku) {
  auto analyzer = makeDeverbalNounAnalyzer();
  const auto result = analyzer.analyze("休みなく働く");

  ASSERT_EQ(result.size(), 3U);
  EXPECT_EQ(result[0].surface, "休み");
  EXPECT_EQ(result[0].pos, core::PartOfSpeech::Noun);
  EXPECT_EQ(result[0].lemma, "休み");
  EXPECT_EQ(result[1].surface, "なく");
  EXPECT_EQ(result[1].pos, core::PartOfSpeech::Adjective);
  EXPECT_EQ(result[2].surface, "働く");
  EXPECT_EQ(result[2].pos, core::PartOfSpeech::Verb);
}

TEST(DeverbalNounNaku, PreservesExistingHiraganaNominalAnalysis) {
  auto analyzer = makeDeverbalNounAnalyzer();
  const auto result = analyzer.analyze("たゆみなく進む");

  ASSERT_EQ(result.size(), 3U);
  EXPECT_EQ(result[0].surface, "たゆみ");
  EXPECT_EQ(result[0].pos, core::PartOfSpeech::Noun);
  EXPECT_EQ(result[1].surface, "なく");
  EXPECT_EQ(result[2].surface, "進む");
}

TEST(DeverbalNounNaku, KeepsNegativeAuxiliaryChainVerbal) {
  auto analyzer = makeDeverbalNounAnalyzer();
  const auto result = analyzer.analyze("食べなくて困る");

  ASSERT_EQ(result.size(), 4U);
  EXPECT_EQ(result[0].surface, "食べ");
  EXPECT_EQ(result[0].pos, core::PartOfSpeech::Verb);
  EXPECT_EQ(result[0].lemma, "食べる");
  EXPECT_EQ(result[1].surface, "なく");
  EXPECT_EQ(result[2].surface, "て");
  EXPECT_EQ(result[2].pos, core::PartOfSpeech::Particle);
  EXPECT_EQ(result[3].surface, "困る");
}

}  // namespace
}  // namespace suzume::analysis
