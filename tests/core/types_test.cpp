#include "core/types.h"

#include <gtest/gtest.h>

namespace suzume {
namespace core {
namespace {

TEST(TypesTest, PosToStringConvertsAllTypes) {
  EXPECT_EQ(posToString(PartOfSpeech::Noun), "NOUN");
  EXPECT_EQ(posToString(PartOfSpeech::Verb), "VERB");
  EXPECT_EQ(posToString(PartOfSpeech::Adjective), "ADJ");
  EXPECT_EQ(posToString(PartOfSpeech::Particle), "PARTICLE");
  EXPECT_EQ(posToString(PartOfSpeech::Auxiliary), "AUX");
  EXPECT_EQ(posToString(PartOfSpeech::Conjunction), "CONJ");
  EXPECT_EQ(posToString(PartOfSpeech::Adverb), "ADV");
  EXPECT_EQ(posToString(PartOfSpeech::Symbol), "SYMBOL");
  EXPECT_EQ(posToString(PartOfSpeech::Other), "OTHER");
}

TEST(TypesTest, StringToPosConvertsAllTypes) {
  EXPECT_EQ(stringToPos("NOUN"), PartOfSpeech::Noun);
  EXPECT_EQ(stringToPos("名詞"), PartOfSpeech::Noun);
  EXPECT_EQ(stringToPos("VERB"), PartOfSpeech::Verb);
  EXPECT_EQ(stringToPos("動詞"), PartOfSpeech::Verb);
  EXPECT_EQ(stringToPos("ADJ"), PartOfSpeech::Adjective);
  EXPECT_EQ(stringToPos("形容詞"), PartOfSpeech::Adjective);
  EXPECT_EQ(stringToPos("PARTICLE"), PartOfSpeech::Particle);
  EXPECT_EQ(stringToPos("助詞"), PartOfSpeech::Particle);
  EXPECT_EQ(stringToPos("AUX"), PartOfSpeech::Auxiliary);
  EXPECT_EQ(stringToPos("助動詞"), PartOfSpeech::Auxiliary);
  EXPECT_EQ(stringToPos("CONJ"), PartOfSpeech::Conjunction);
  EXPECT_EQ(stringToPos("接続詞"), PartOfSpeech::Conjunction);
  EXPECT_EQ(stringToPos("UNKNOWN"), PartOfSpeech::Other);
}

TEST(TypesTest, StringToPosAcceptsLongAliases) {
  // Long-form English aliases must resolve, not fall through to Other.
  EXPECT_EQ(stringToPos("ADJECTIVE"), PartOfSpeech::Adjective);
  EXPECT_EQ(stringToPos("ADVERB"), PartOfSpeech::Adverb);
  EXPECT_EQ(stringToPos("AUXILIARY"), PartOfSpeech::Auxiliary);
  EXPECT_EQ(stringToPos("CONJUNCTION"), PartOfSpeech::Conjunction);
  EXPECT_EQ(stringToPos("PRONOUN"), PartOfSpeech::Pronoun);
  EXPECT_EQ(stringToPos("DETERMINER"), PartOfSpeech::Determiner);
  EXPECT_EQ(stringToPos("INTERJECTION"), PartOfSpeech::Interjection);
  EXPECT_EQ(stringToPos("PROPN"), PartOfSpeech::Noun);
}

TEST(TypesTest, StringToPosStrictRejectsUnknown) {
  // Strict variant distinguishes the OTHER aliases from genuinely invalid input.
  EXPECT_EQ(stringToPosStrict("OTHER"), PartOfSpeech::Other);
  EXPECT_EQ(stringToPosStrict("PHRASE"), PartOfSpeech::Other);
  EXPECT_EQ(stringToPosStrict("ADJECTIVE"), PartOfSpeech::Adjective);
  EXPECT_FALSE(stringToPosStrict("NOPE").has_value());
  EXPECT_FALSE(stringToPosStrict("").has_value());
}

TEST(TypesTest, AnalysisModeHasCorrectValues) {
  EXPECT_NE(AnalysisMode::Normal, AnalysisMode::Search);
  EXPECT_NE(AnalysisMode::Search, AnalysisMode::Split);
}

TEST(TypesTest, PosCountMatchesEnumRange) {
  EXPECT_EQ(static_cast<size_t>(PartOfSpeech::Count_), 15u);
  EXPECT_EQ(posToString(PartOfSpeech::Count_), "OTHER");
  EXPECT_EQ(posToExtendedPos(PartOfSpeech::Count_), ExtendedPOS::Other);
}

}  // namespace
}  // namespace core
}  // namespace suzume
