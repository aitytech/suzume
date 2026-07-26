#include "dictionary/source_parser.h"

#include <gtest/gtest.h>

namespace suzume {
namespace dictionary {
namespace {

TEST(SourceParserTest, ParsesBomTrimmedTsvAndCanonicalMarkers) {
  const std::string source =
      "\xEF\xBB\xBF  東京  \t PROPER_NOUN \t FAMILY \n"
      "  しほ  \t PROPER_NOUN \t GIVEN \n"
      "  なるほど  \t INTJ \n";

  auto result = parseDictionarySource(source);

  ASSERT_TRUE(result.hasValue()) << result.error().message;
  ASSERT_EQ(result.value().entries.size(), 3);
  EXPECT_EQ(result.value().entries[0].surface, "東京");
  EXPECT_EQ(result.value().entries[0].conj_type, ConjugationType::ProperFamily);
  EXPECT_EQ(result.value().entries[1].conj_type, ConjugationType::ProperGiven);
  EXPECT_EQ(result.value().entries[2].pos, core::PartOfSpeech::Interjection);
  EXPECT_EQ(result.value().entries[2].conj_type, ConjugationType::Interjection);
}

TEST(SourceParserTest, ParsesQuotedCsvEscapesAndEmbeddedNewline) {
  const std::string source =
      "\"東京,大阪\",NOUN,0.5,\"東\"\"京\"\n"
      "\"二行\n"
      "表記\",NOUN,0.5,\"基底\"\r\n";

  auto result = parseDictionarySource(source);

  ASSERT_TRUE(result.hasValue()) << result.error().message;
  ASSERT_EQ(result.value().entries.size(), 2);
  EXPECT_EQ(result.value().entries[0].surface, "東京,大阪");
  EXPECT_EQ(result.value().entries[0].lemma, "東\"京");
  EXPECT_EQ(result.value().entries[1].surface, "二行\n表記");
  EXPECT_EQ(result.value().entries[1].lemma, "基底");
  EXPECT_EQ(result.value().entries[1].line_number, 2);
  EXPECT_EQ(result.value().stats.empty_lines, 0);
}

TEST(SourceParserTest, PreservesLegacyTsvAndCurrentLemmaLayouts) {
  const std::string source =
      "読む\tVERB\tヨム\t0.5\tGODAN_MA\t読む\n"
      "東京\tNOUN\tトウキョウ\t0.5\n"
      "読んだ\tVERB\t読む\t読む\n";

  auto result = parseDictionarySource(source);

  ASSERT_TRUE(result.hasValue()) << result.error().message;
  ASSERT_EQ(result.value().entries.size(), 3);
  EXPECT_EQ(result.value().entries[0].conj_type, ConjugationType::GodanMa);
  EXPECT_EQ(result.value().entries[0].lemma, "読む");
  EXPECT_TRUE(result.value().entries[1].lemma.empty());
  EXPECT_EQ(result.value().entries[2].lemma, "読む");
}

TEST(SourceParserTest, PreservesLiteralQuoteInsideUnquotedTsvField) {
  auto result = parseDictionarySource(
      "引用\"符\tNOUN\n"
      "テスト\tNOUN\n");

  ASSERT_TRUE(result.hasValue()) << result.error().message;
  ASSERT_EQ(result.value().entries.size(), 2);
  EXPECT_EQ(result.value().entries[0].surface, "引用\"符");
  EXPECT_EQ(result.value().entries[1].surface, "テスト");
}

TEST(SourceParserTest, RejectsInvalidQuotedRecordWithoutPartialResult) {
  auto result = parseDictionarySource(
      "東京,NOUN,0.5\n"
      "\"大阪,NOUN,0.5\n");

  EXPECT_FALSE(result.hasValue());
  EXPECT_NE(result.error().message.find("unterminated quoted field"), std::string::npos);
}

TEST(SourceParserTest, ConvertsMarkerToRuntimeExtendedPos) {
  SourceEntry family{"東京", core::PartOfSpeech::Noun, ConjugationType::ProperFamily, "", 1};
  SourceEntry given{"しほ", core::PartOfSpeech::Noun, ConjugationType::ProperGiven, "", 2};
  SourceEntry interjection{"なるほど", core::PartOfSpeech::Interjection, ConjugationType::Interjection, "", 3};

  EXPECT_EQ(sourceToDictionaryEntry(family).extended_pos, core::ExtendedPOS::NounProperFamily);
  EXPECT_EQ(sourceToDictionaryEntry(given).extended_pos, core::ExtendedPOS::NounProperGiven);
  EXPECT_EQ(sourceToDictionaryEntry(interjection).extended_pos, core::ExtendedPOS::Interjection);
}

}  // namespace
}  // namespace dictionary
}  // namespace suzume
