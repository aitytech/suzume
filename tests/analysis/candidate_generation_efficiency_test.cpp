#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "analysis/scorer.h"
#include "analysis/split_candidates.h"
#include "analysis/tokenizer_utils.h"
#include "analysis/unknown.h"
#include "core/lattice.h"
#include "dictionary/dictionary.h"
#include "dictionary/user_dict.h"
#include "normalize/char_type.h"
#include "normalize/utf8.h"

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

TEST(CandidateGenerationEfficiencyTest, BoundsClosedClassProbeToFiveCodepoints) {
  const auto codepoints = normalize::toCodepoints("あいうえおかき");

  EXPECT_EQ(extractClosedClassProbe(codepoints, 0), "あいうえお");
  EXPECT_EQ(extractClosedClassProbe(codepoints, 4), "おかき");
}

TEST(CandidateGenerationEfficiencyTest, EmitsOneAdjectiveStemEdgeBeforeSaNominalizer) {
  dictionary::DictionaryManager dictionary_manager;
  ASSERT_TRUE(dictionary_manager.loadCoreDictionary("data/core.dic"));
  const std::string text = "美しさ";
  const auto codepoints = normalize::toCodepoints(text);
  const auto char_types = classify(codepoints);
  const UnknownWordGenerator generator({}, &dictionary_manager);

  const auto candidates = generator.generate(text, codepoints, 0, char_types);
  const size_t matching_edges =
      static_cast<size_t>(std::count_if(candidates.begin(), candidates.end(), [](const UnknownCandidate& candidate) {
        return candidate.surface == "美し" && candidate.start == 0 && candidate.end == 2 &&
               candidate.pos == core::PartOfSpeech::Adjective && candidate.extended_pos == core::ExtendedPOS::AdjStem;
      }));

  EXPECT_EQ(matching_edges, 1u);
}

TEST(CandidateGenerationEfficiencyTest, EmitsOneDictionaryBackedCompoundSplitEdge) {
  auto user_dictionary = std::make_shared<dictionary::UserDictionary>();
  ASSERT_TRUE(user_dictionary->addEntry(
      dictionary::DictionaryEntry{"人工", core::PartOfSpeech::Noun, core::ExtendedPOS::Noun, "人工"}));
  ASSERT_TRUE(user_dictionary->addEntry(
      dictionary::DictionaryEntry{"知能", core::PartOfSpeech::Noun, core::ExtendedPOS::Noun, "知能"}));
  dictionary::DictionaryManager dictionary_manager;
  dictionary_manager.addUserDictionary(user_dictionary);

  const std::string text = "人工知能";
  const auto codepoints = normalize::toCodepoints(text);
  const auto char_types = classify(codepoints);
  const auto byte_offsets = buildByteOffsets(codepoints);
  const Scorer scorer;
  core::Lattice lattice(codepoints.size());

  addCompoundSplitCandidates(lattice, text, byte_offsets, 0, char_types, dictionary_manager, scorer);

  const size_t matching_edges = static_cast<size_t>(
      std::count_if(lattice.edgeIdsAt(0).begin(), lattice.edgeIdsAt(0).end(), [&lattice](const uint32_t edge_id) {
        const auto& edge = lattice.getEdge(edge_id);
        return edge.surface == "人工" && edge.start == 0 && edge.end == 2 && edge.pos == core::PartOfSpeech::Noun;
      }));
  EXPECT_EQ(matching_edges, 1u);
}

TEST(CandidateGenerationEfficiencyTest, KeepsBoundedAndFullAlternativesForLongSameTypeRun) {
  UnknownOptions options;
  options.max_kanji_length = 4;
  dictionary::DictionaryManager dictionary_manager;
  const UnknownWordGenerator generator(options, &dictionary_manager);
  const std::string text = "研究研究研究研究";
  const auto codepoints = normalize::toCodepoints(text);
  const auto char_types = classify(codepoints);

  const auto candidates = generator.generate(text, codepoints, 0, char_types);
  auto has_same_type_end = [&candidates](size_t end) {
    return std::any_of(candidates.begin(), candidates.end(), [end](const UnknownCandidate& candidate) {
      return candidate.origin == core::CandidateOrigin::SameType && candidate.start == 0 && candidate.end == end &&
             candidate.pos == core::PartOfSpeech::Noun;
    });
  };

  EXPECT_TRUE(has_same_type_end(options.max_kanji_length));
  EXPECT_TRUE(has_same_type_end(codepoints.size()));
  for (size_t end = options.max_kanji_length + 1; end < codepoints.size(); ++end) {
    EXPECT_FALSE(has_same_type_end(end));
  }
}

}  // namespace
}  // namespace suzume::analysis
