#include "json_loader.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

namespace suzume::test {
namespace {

TEST(JsonLoaderTest, CombinesSurrogatePairIntoFourByteUtf8) {
  const auto suite = JsonLoader::loadFromString(
      R"({"version":"1","cases":[{"id":"emoji","input":"\uD83D\uDE00","expected":[{"surface":"\uD83D\uDE00","pos":"SYMBOL"}]}]})");

  ASSERT_EQ(suite.cases.size(), 1U);
  EXPECT_EQ(suite.cases[0].input, "\xF0\x9F\x98\x80");
  ASSERT_EQ(suite.cases[0].expected.size(), 1U);
  EXPECT_EQ(suite.cases[0].expected[0].surface, "\xF0\x9F\x98\x80");
}

TEST(JsonLoaderTest, RejectsIsolatedSurrogates) {
  const std::string prefix = R"({"version":"1","cases":[{"id":"bad","input":")";
  const std::string suffix = R"(","expected":[{"surface":"x","pos":"SYMBOL"}]}]})";

  EXPECT_THROW(JsonLoader::loadFromString(prefix + R"(\uD83D)" + suffix), std::runtime_error);
  EXPECT_THROW(JsonLoader::loadFromString(prefix + R"(\uDE00)" + suffix), std::runtime_error);
  EXPECT_THROW(JsonLoader::loadFromString(prefix + R"(\uD83D\u0041)" + suffix), std::runtime_error);
}

TEST(JsonLoaderTest, RejectsInvalidUnicodeEscape) {
  EXPECT_THROW(
      JsonLoader::loadFromString(
          R"({"version":"1","cases":[{"id":"bad","input":"\u12xz","expected":[{"surface":"x","pos":"SYMBOL"}]}]})"),
      std::runtime_error);
}

TEST(JsonLoaderTest, RejectsEmptyRequiredCaseFields) {
  EXPECT_THROW(JsonLoader::loadFromString(
                   R"({"version":"1","cases":[{"id":"","input":"x","expected":[{"surface":"x","pos":"NOUN"}]}]})"),
               std::runtime_error);
  EXPECT_THROW(
      JsonLoader::loadFromString(
          R"({"version":"1","cases":[{"id":"empty-input","input":"","expected":[{"surface":"x","pos":"NOUN"}]}]})"),
      std::runtime_error);
  EXPECT_THROW(
      JsonLoader::loadFromString(R"({"version":"1","cases":[{"id":"empty-expected","input":"x","expected":[]}]})"),
      std::runtime_error);
  EXPECT_THROW(
      JsonLoader::loadFromString(
          R"({"version":"1","cases":[{"id":"empty-surface","input":"x","expected":[{"surface":"","pos":"NOUN"}]}]})"),
      std::runtime_error);
}

TEST(JsonLoaderTest, RejectsUnknownExpectedPos) {
  EXPECT_THROW(
      JsonLoader::loadFromString(
          R"({"version":"1","cases":[{"id":"bad-pos","input":"x","expected":[{"surface":"x","pos":"TYPO"}]}]})"),
      std::runtime_error);
}

}  // namespace
}  // namespace suzume::test
