#include "cli_common.h"

#include <gtest/gtest.h>

namespace suzume::cli {
namespace {

TEST(CliCommonTest, JsonEscapeEscapesSpecialCharacters) {
  EXPECT_EQ(jsonEscape("a\"b\\c"), "a\\\"b\\\\c");
  EXPECT_EQ(jsonEscape("line\nnext\tend"), "line\\nnext\\tend");
}

TEST(CliCommonTest, WildcardMatchesLiteralAndWildcardCharacters) {
  EXPECT_TRUE(wildcardMatches("記号*終", "記号.^$+()[]{}|\\終"));
  EXPECT_TRUE(wildcardMatches("??", "ab"));
  EXPECT_FALSE(wildcardMatches("??", "a"));
  EXPECT_TRUE(wildcardMatches("a**b", "axxxb"));
  EXPECT_FALSE(wildcardMatches("a*b", "axxxc"));
}

TEST(CliCommonTest, WildcardValidationRejectsExcessiveStars) {
  const std::string pattern(65, '*');
  auto result = validateWildcardPattern(pattern);

  EXPECT_FALSE(result.hasValue());
  EXPECT_EQ(result.error().code, core::ErrorCode::InvalidInput);
  EXPECT_NE(result.error().message.find("too many '*'"), std::string::npos);
}

TEST(CliCommonTest, JsonEscapeEscapesControlCharacters) {
  std::string input;
  input.push_back('\x01');
  input += "ok";

  EXPECT_EQ(jsonEscape(input), "\\u0001ok");
}

TEST(CliCommonTest, JsonEscapeRejectsInvalidUnicodeScalars) {
  EXPECT_EQ(jsonEscape(std::string("\xFF", 1)), "\\ufffd");
  EXPECT_EQ(jsonEscape(std::string("\xC0\xAF", 2)), "\\ufffd\\ufffd");
  EXPECT_EQ(jsonEscape(std::string("\xED\xA0\x80", 3)), "\\ufffd\\ufffd\\ufffd");
  EXPECT_EQ(jsonEscape(std::string("\xF4\x90\x80\x80", 4)), "\\ufffd\\ufffd\\ufffd\\ufffd");
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
                        "--tag-pos",
                        "noun",
                        "--tag-pos=verb",
                        "--tag-exclude-basic",
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
  EXPECT_EQ(args.tag_pos_filter, 3u);
  EXPECT_TRUE(args.tag_exclude_basic);
  EXPECT_EQ(args.tag_min_length, 1u);
  EXPECT_EQ(args.tag_max_tags, 3u);
  EXPECT_TRUE(args.parse_error.empty());
}

TEST(CliCommonTest, ParseArgsRejectsUnknownAnalysisOption) {
  const char* argv[] = {"suzume-cli", "-テスト"};
  auto args = parseArgs(static_cast<int>(sizeof(argv) / sizeof(argv[0])), const_cast<char**>(argv));

  EXPECT_EQ(args.command, "analyze");
  EXPECT_TRUE(args.args.empty());
  EXPECT_EQ(args.parse_error, "Unknown analysis option: -テスト");
}

TEST(CliCommonTest, ParseArgsAcceptsLeadingDashTextAfterOptionTerminator) {
  const char* argv[] = {"suzume-cli", "--", "-テスト"};
  auto args = parseArgs(static_cast<int>(sizeof(argv) / sizeof(argv[0])), const_cast<char**>(argv));

  EXPECT_EQ(args.command, "analyze");
  ASSERT_EQ(args.args.size(), 1u);
  EXPECT_EQ(args.args[0], "-テスト");
  EXPECT_TRUE(args.parse_error.empty());
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

TEST(CliCommonTest, ParseArgsRejectsInvalidModeAndFormat) {
  const char* invalid_mode[] = {"suzume-cli", "analyze", "--mode", "wide", "テスト"};
  auto mode_args =
      parseArgs(static_cast<int>(sizeof(invalid_mode) / sizeof(invalid_mode[0])), const_cast<char**>(invalid_mode));
  EXPECT_NE(mode_args.parse_error.find("Invalid mode"), std::string::npos);

  const char* invalid_format[] = {"suzume-cli", "--format=yaml", "テスト"};
  auto format_args = parseArgs(static_cast<int>(sizeof(invalid_format) / sizeof(invalid_format[0])),
                               const_cast<char**>(invalid_format));
  EXPECT_NE(format_args.parse_error.find("Invalid format"), std::string::npos);
}

TEST(CliCommonTest, ParseArgsRejectsMissingOptionValues) {
  const char* missing_dict[] = {"suzume-cli", "analyze", "--dict"};
  auto dict_args =
      parseArgs(static_cast<int>(sizeof(missing_dict) / sizeof(missing_dict[0])), const_cast<char**>(missing_dict));
  EXPECT_EQ(dict_args.parse_error, "Missing value for --dict");

  const char* missing_mode[] = {"suzume-cli", "--mode", "--format", "json", "テスト"};
  auto mode_args =
      parseArgs(static_cast<int>(sizeof(missing_mode) / sizeof(missing_mode[0])), const_cast<char**>(missing_mode));
  EXPECT_EQ(mode_args.parse_error, "Missing value for --mode");

  const char* missing_tag_pos[] = {"suzume-cli", "--tag-pos"};
  auto tag_args = parseArgs(static_cast<int>(sizeof(missing_tag_pos) / sizeof(missing_tag_pos[0])),
                            const_cast<char**>(missing_tag_pos));
  EXPECT_EQ(tag_args.parse_error, "Missing value for --tag-pos");
}

TEST(CliCommonTest, ParseArgsPreservesDictAndTestSubcommandOptions) {
  const char* dict_argv[] = {"suzume-cli", "dict", "list", "test.tsv", "--pos=NOUN", "--limit=3"};
  auto dict_args = parseArgs(static_cast<int>(sizeof(dict_argv) / sizeof(dict_argv[0])), const_cast<char**>(dict_argv));
  EXPECT_EQ(dict_args.command, "dict");
  EXPECT_EQ(dict_args.args, (std::vector<std::string>{"list", "test.tsv", "--pos=NOUN", "--limit=3"}));
  EXPECT_TRUE(dict_args.parse_error.empty());

  const char* test_argv[] = {"suzume-cli", "test", "-f", "cases.tsv", "-d", "user.tsv"};
  auto test_args = parseArgs(static_cast<int>(sizeof(test_argv) / sizeof(test_argv[0])), const_cast<char**>(test_argv));
  EXPECT_EQ(test_args.command, "test");
  EXPECT_EQ(test_args.args, (std::vector<std::string>{"-f", "cases.tsv"}));
  EXPECT_EQ(test_args.dict_paths, (std::vector<std::string>{"user.tsv"}));
  EXPECT_TRUE(test_args.parse_error.empty());
}

TEST(CliCommonTest, ParseArgsRecordsVersionWithoutExiting) {
  const char* argv[] = {"suzume-cli", "-v"};
  auto args = parseArgs(static_cast<int>(sizeof(argv) / sizeof(argv[0])), const_cast<char**>(argv));

  EXPECT_TRUE(args.version);
  EXPECT_TRUE(args.parse_error.empty());
}

TEST(CliCommonTest, ParseArgsKeepsHelpOnlyAtTopLevel) {
  const char* argv[] = {"suzume-cli", "--help"};
  auto args = parseArgs(static_cast<int>(sizeof(argv) / sizeof(argv[0])), const_cast<char**>(argv));

  EXPECT_TRUE(args.help);
  EXPECT_TRUE(args.command.empty());
}

TEST(CliCommonTest, ParseArgsKeepsExplicitAnalyzeHelpCommand) {
  const char* argv[] = {"suzume-cli", "analyze", "--help"};
  auto args = parseArgs(static_cast<int>(sizeof(argv) / sizeof(argv[0])), const_cast<char**>(argv));

  EXPECT_TRUE(args.help);
  EXPECT_EQ(args.command, "analyze");
}

TEST(CliCommonTest, ParseArgsDefaultsNoArgumentsToAnalyze) {
  const char* argv[] = {"suzume-cli"};
  auto args = parseArgs(static_cast<int>(sizeof(argv) / sizeof(argv[0])), const_cast<char**>(argv));

  EXPECT_FALSE(args.help);
  EXPECT_EQ(args.command, "analyze");
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
