#include <gtest/gtest.h>

#include <string_view>
#include <vector>

#include "analysis/suffix_candidates.h"
#include "analysis/suffix_candidates_counter_internal.h"
#include "dictionary/dictionary.h"
#include "normalize/char_type.h"
#include "normalize/utf8.h"
#include "suzume.h"

namespace suzume::analysis {
namespace {

std::vector<normalize::CharType> classify(const std::vector<char32_t>& codepoints) {
  std::vector<normalize::CharType> char_types;
  char_types.reserve(codepoints.size());
  for (const char32_t codepoint : codepoints) {
    char_types.push_back(normalize::classifyChar(codepoint));
  }
  return char_types;
}

TEST(SuffixCandidatesCounterTest, FindsRepeatedNumeralKanjiUnitsOfVariableLength) {
  struct TestCase {
    std::string_view text;
    size_t repeated_end;
  };
  for (const TestCase& test_case :
       {TestCase{"一件一件点検する", 4}, TestCase{"十一件十一件点検する", 6}, TestCase{"3冊3冊点検する", 4}}) {
    const auto codepoints = normalize::toCodepoints(test_case.text);
    const auto char_types = classify(codepoints);
    EXPECT_EQ(repeatedNumeralNounUnitEndAt(codepoints, char_types, 0), test_case.repeated_end) << test_case.text;
  }
}

TEST(SuffixCandidatesCounterTest, DoesNotTreatPureNumeralRepetitionAsDistributiveUnit) {
  const auto codepoints = normalize::toCodepoints("十一十一点検する");
  const auto char_types = classify(codepoints);

  EXPECT_EQ(repeatedNumeralNounUnitEndAt(codepoints, char_types, 0), 0u);
}

TEST(SuffixCandidatesCounterTest, TemporalCandidatesAcceptNonQuantityAtSentenceStart) {
  const auto codepoints = normalize::toCodepoints("ありがとう");
  const auto char_types = classify(codepoints);
  dictionary::DictionaryManager dictionary_manager;
  std::vector<UnknownCandidate> candidates;

  counter_detail::appendTemporalCounterCandidates(codepoints, 0, char_types, &dictionary_manager, candidates);
}

TEST(SuffixCandidatesCounterTest, KeepsRepeatedQuantityTogetherBeforeSuruPredicate) {
  SuzumeOptions options;
  options.skip_user_dictionary = true;
  Suzume analyzer(options);

  for (const std::string_view text :
       {"一件一件点検する", "一冊一冊点検する", "一日一日確認する", "十一件十一件点検する", "一語一語確認する"}) {
    const auto results = analyzer.analyze(text);
    ASSERT_FALSE(results.empty()) << text;
    const auto codepoints = normalize::toCodepoints(text);
    const auto char_types = classify(codepoints);
    const size_t repeated_end = repeatedNumeralNounUnitEndAt(codepoints, char_types, 0);
    ASSERT_NE(repeated_end, 0u) << text;
    EXPECT_EQ(results.front().surface, normalize::encodeRange(codepoints, 0, repeated_end)) << text;
  }
}

}  // namespace
}  // namespace suzume::analysis
