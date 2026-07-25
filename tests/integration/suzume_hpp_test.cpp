#include <gtest/gtest.h>

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

}  // namespace
