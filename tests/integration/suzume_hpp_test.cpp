#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "suzume/suzume.hpp"

namespace {

// The header-only wrapper is what a find_package/pkg-config consumer sees, so
// its surface has to stay aligned with the C ABI and the npm/PyPI bindings.

TEST(SuzumeHppTest, AnalyzeReturnsMorphemesForJapaneseText) {
  suzume::Tokenizer tokenizer;
  ASSERT_TRUE(tokenizer.valid()) << suzume::Tokenizer::lastError();

  const std::vector<suzume::Morpheme> morphemes = tokenizer.analyze("本を読む");
  ASSERT_FALSE(morphemes.empty());
  EXPECT_EQ(morphemes.front().surface, "本");
  EXPECT_FALSE(morphemes.front().pos.empty());
}

TEST(SuzumeHppTest, AnalyzeExposesNormalizedTextAndPreservesEmbeddedNull) {
  suzume::Tokenizer tokenizer;
  ASSERT_TRUE(tokenizer.valid()) << suzume::Tokenizer::lastError();

  const std::string input("東京\0大阪", 13);
  const suzume::AnalysisResult result = tokenizer.analyzeWithNormalizedText(input);
  EXPECT_NE(result.normalized_text.find("大阪"), std::string::npos);
  EXPECT_FALSE(result.morphemes.empty());
}

TEST(SuzumeHppTest, ConjugationLabelsCoverTheSerializedBoundary) {
  EXPECT_TRUE(suzume::detail::conjugationTypeLabel(0).empty());
  EXPECT_EQ(suzume::detail::conjugationTypeLabel(14), "ナ形容詞");
  EXPECT_EQ(suzume::detail::conjugationTypeLabel(15), "感動詞");
  EXPECT_EQ(suzume::detail::conjugationTypeLabel(16), "固有名詞・姓");
  EXPECT_EQ(suzume::detail::conjugationTypeLabel(17), "固有名詞・名");
  EXPECT_TRUE(suzume::detail::conjugationTypeLabel(18).empty());

  EXPECT_EQ(suzume::detail::conjugationFormLabel(6), "意志形");
  EXPECT_TRUE(suzume::detail::conjugationFormLabel(7).empty());
}

TEST(SuzumeHppTest, AnalyzeReportsMalformedInputThroughLastError) {
  suzume::Tokenizer tokenizer;
  ASSERT_TRUE(tokenizer.valid()) << suzume::Tokenizer::lastError();

  const std::vector<suzume::Morpheme> morphemes = tokenizer.analyze(std::string("\xE3\x81", 2));
  EXPECT_TRUE(morphemes.empty());
  EXPECT_NE(suzume::Tokenizer::lastError().find("UTF-8"), std::string::npos);
}

TEST(SuzumeHppTest, GeneratedTagsUseTheSameFieldNameAsTheOtherBindings) {
  suzume::Tokenizer tokenizer;
  ASSERT_TRUE(tokenizer.valid()) << suzume::Tokenizer::lastError();

  const std::vector<suzume::Tag> tags = tokenizer.generateTags("東京の観光地を調べる");
  ASSERT_FALSE(tags.empty());
  // The field is `tag`, matching TagEntry::tag in the core, Tag.tag in the
  // WASM binding and Tag.tag in the Python binding.
  EXPECT_FALSE(tags.front().tag.empty());
  EXPECT_FALSE(tags.front().pos.empty());
}

TEST(SuzumeHppTest, DictionaryWarningsAndClearAreExposed) {
  suzume::Tokenizer tokenizer;
  ASSERT_TRUE(tokenizer.valid()) << suzume::Tokenizer::lastError();
  EXPECT_TRUE(tokenizer.dictionaryWarnings().empty());
  EXPECT_TRUE(tokenizer.hasCoreDictionary());
  const auto bundled_before = tokenizer.analyze("コーヒー豆");
  ASSERT_EQ(bundled_before.size(), 1u);
  EXPECT_TRUE(bundled_before[0].is_user_dict);
  EXPECT_GT(tokenizer.loadUserDictionaryCount("検査する\tVERB\tSURU\n"), 1u);
  EXPECT_TRUE(tokenizer.clearUserDictionaries());
  EXPECT_EQ(suzume::Tokenizer::lastErrorCode(), SUZUME_ERROR_SUCCESS);
  const auto bundled_after = tokenizer.analyze("コーヒー豆");
  ASSERT_EQ(bundled_after.size(), 1u);
  EXPECT_TRUE(bundled_after[0].is_user_dict);
}

TEST(SuzumeHppTest, EnvironmentScorerConfigCanBeDisabled) {
#ifndef __EMSCRIPTEN__
  setenv("SUZUME_SCORER_INFL_confidence_ceiling", "0", 1);
  suzume::Options options;
  options.skip_user_dictionary = true;
  options.skip_core_dictionary = true;
  options.skip_env_config = true;
  suzume::Tokenizer tokenizer(options);
  unsetenv("SUZUME_SCORER_INFL_confidence_ceiling");
  ASSERT_TRUE(tokenizer.valid()) << suzume::Tokenizer::lastError();

  const auto morphemes = tokenizer.analyze("歩いています");
  ASSERT_FALSE(morphemes.empty());
  EXPECT_EQ(morphemes.front().surface, "歩い");
#endif
}

}  // namespace
