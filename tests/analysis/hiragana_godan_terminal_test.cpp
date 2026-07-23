#include <gtest/gtest.h>

#include "suzume.h"

namespace suzume::analysis {
namespace {

Suzume makeHiraganaGodanAnalyzer() {
  SuzumeOptions options;
  options.skip_user_dictionary = true;
  return Suzume(options);
}

TEST(HiraganaGodanTerminal, KeepsCompleteWaRowPredicateBeforeNoun) {
  auto analyzer = makeHiraganaGodanAnalyzer();
  const auto result = analyzer.analyze("そこなう可能性がある");

  ASSERT_EQ(result.size(), 4U);
  EXPECT_EQ(result[0].surface, "そこなう");
  EXPECT_EQ(result[0].pos, core::PartOfSpeech::Verb);
  EXPECT_EQ(result[0].lemma, "そこなう");
  EXPECT_EQ(result[1].surface, "可能性");
}

TEST(HiraganaGodanTerminal, GeneralizesAcrossUnknownWaRowStems) {
  auto analyzer = makeHiraganaGodanAnalyzer();

  const auto first = analyzer.analyze("たたかう可能性がある");
  ASSERT_FALSE(first.empty());
  EXPECT_EQ(first[0].surface, "たたかう");
  EXPECT_EQ(first[0].pos, core::PartOfSpeech::Verb);
  EXPECT_EQ(first[0].lemma, "たたかう");

  const auto second = analyzer.analyze("計画をまかなう方法");
  ASSERT_EQ(second.size(), 4U);
  EXPECT_EQ(second[2].surface, "まかなう");
  EXPECT_EQ(second[2].pos, core::PartOfSpeech::Verb);
  EXPECT_EQ(second[2].lemma, "まかなう");
}

TEST(HiraganaGodanTerminal, KeepsDependentSameSurfaceAsAuxiliary) {
  auto analyzer = makeHiraganaGodanAnalyzer();
  const auto result = analyzer.analyze("読みそこなう");

  ASSERT_EQ(result.size(), 2U);
  EXPECT_EQ(result[0].surface, "読み");
  EXPECT_EQ(result[0].pos, core::PartOfSpeech::Verb);
  EXPECT_EQ(result[0].lemma, "読む");
  EXPECT_EQ(result[1].surface, "そこなう");
  EXPECT_EQ(result[1].pos, core::PartOfSpeech::Auxiliary);
  EXPECT_EQ(result[1].lemma, "そこなう");
}

TEST(HiraganaGodanTerminal, PreservesDemonstrativePronounBoundaries) {
  auto analyzer = makeHiraganaGodanAnalyzer();
  const auto result = analyzer.analyze("そこにある");

  ASSERT_EQ(result.size(), 3U);
  EXPECT_EQ(result[0].surface, "そこ");
  EXPECT_EQ(result[0].pos, core::PartOfSpeech::Pronoun);
  EXPECT_EQ(result[1].surface, "に");
  EXPECT_EQ(result[1].pos, core::PartOfSpeech::Particle);
  EXPECT_EQ(result[2].surface, "ある");
  EXPECT_EQ(result[2].pos, core::PartOfSpeech::Verb);
}

}  // namespace
}  // namespace suzume::analysis
