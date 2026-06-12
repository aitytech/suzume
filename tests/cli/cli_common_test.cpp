#include "cli_common.h"

#include <gtest/gtest.h>

namespace suzume::cli {
namespace {

TEST(CliCommonTest, JsonEscapeEscapesSpecialCharacters) {
  EXPECT_EQ(jsonEscape("a\"b\\c"), "a\\\"b\\\\c");
  EXPECT_EQ(jsonEscape("line\nnext\tend"), "line\\nnext\\tend");
}

TEST(CliCommonTest, JsonEscapeEscapesControlCharacters) {
  std::string input;
  input.push_back('\x01');
  input += "ok";

  EXPECT_EQ(jsonEscape(input), "\\u0001ok");
}

TEST(CliCommonTest, ParseSizeOptionRejectsInvalidInput) {
  size_t value = 123;

  EXPECT_FALSE(parseSizeOption("", &value));
  EXPECT_FALSE(parseSizeOption("12x", &value));
  EXPECT_FALSE(parseSizeOption("-1", &value));
  EXPECT_FALSE(parseSizeOption("1.5", &value));
  EXPECT_EQ(value, 123u);
}

TEST(CliCommonTest, ParseSizeOptionAcceptsSize) {
  size_t value = 0;

  EXPECT_TRUE(parseSizeOption("42", &value));
  EXPECT_EQ(value, 42u);
}

TEST(CliCommonTest, ParseArgsAcceptsTagOptions) {
  const char* argv[] = {"suzume-cli",
                        "-f",
                        "tags",
                        "--include-particles",
                        "--include-auxiliaries",
                        "--include-formal-nouns",
                        "--include-low-info",
                        "--tag-keep-duplicates",
                        "--tag-use-surface",
                        "--tag-min-length",
                        "1",
                        "--tag-max-tags",
                        "3",
                        "猫が走る"};
  auto args = parseArgs(static_cast<int>(sizeof(argv) / sizeof(argv[0])), const_cast<char**>(argv));

  EXPECT_EQ(args.format, OutputFormat::Tags);
  EXPECT_TRUE(args.tag_include_particles);
  EXPECT_TRUE(args.tag_include_auxiliaries);
  EXPECT_TRUE(args.tag_include_formal_nouns);
  EXPECT_TRUE(args.tag_include_low_info);
  EXPECT_TRUE(args.tag_keep_duplicates);
  EXPECT_TRUE(args.tag_use_surface);
  EXPECT_EQ(args.tag_min_length, 1u);
  EXPECT_EQ(args.tag_max_tags, 3u);
}

TEST(CliCommonTest, ParseArgsTreatsLeadingDashTextAsImplicitAnalyzeInput) {
  const char* argv[] = {"suzume-cli", "-テスト"};
  auto args = parseArgs(static_cast<int>(sizeof(argv) / sizeof(argv[0])), const_cast<char**>(argv));

  EXPECT_EQ(args.command, "analyze");
  ASSERT_EQ(args.args.size(), 1u);
  EXPECT_EQ(args.args[0], "-テスト");
}

TEST(CliCommonTest, ParseArgsConnectsAdvancedAnalyzeOptions) {
  const char* argv[] = {"suzume-cli", "-VV", "--no-core-dict", "--no-lemmatize", "--merge-compounds", "テスト"};
  auto args = parseArgs(static_cast<int>(sizeof(argv) / sizeof(argv[0])), const_cast<char**>(argv));

  EXPECT_TRUE(args.verbose);
  EXPECT_TRUE(args.very_verbose);
  EXPECT_TRUE(args.debug);
  EXPECT_TRUE(args.no_core_dict);
  EXPECT_TRUE(args.no_lemmatize);
  EXPECT_TRUE(args.merge_compounds);
}

TEST(CliCommonTest, StripUtf8BomRemovesOnlyLeadingBom) {
  std::string value = "\xEF\xBB\xBFtext";
  stripUtf8Bom(&value);
  EXPECT_EQ(value, "text");

  value = "text\xEF\xBB\xBF";
  stripUtf8Bom(&value);
  EXPECT_EQ(value, "text\xEF\xBB\xBF");
}

}  // namespace
}  // namespace suzume::cli
