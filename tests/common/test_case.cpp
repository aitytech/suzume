#include "test_case.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace suzume::test {

core::PartOfSpeech ExpectedMorpheme::posEnum() const {
  static constexpr std::array<std::pair<std::string_view, core::PartOfSpeech>, 14> kCanonicalNames = {{
      {"Noun", core::PartOfSpeech::Noun},
      {"Verb", core::PartOfSpeech::Verb},
      {"Adjective", core::PartOfSpeech::Adjective},
      {"Adverb", core::PartOfSpeech::Adverb},
      {"Particle", core::PartOfSpeech::Particle},
      {"Auxiliary", core::PartOfSpeech::Auxiliary},
      {"Conjunction", core::PartOfSpeech::Conjunction},
      {"Determiner", core::PartOfSpeech::Determiner},
      {"Pronoun", core::PartOfSpeech::Pronoun},
      {"Prefix", core::PartOfSpeech::Prefix},
      {"Suffix", core::PartOfSpeech::Suffix},
      {"Interjection", core::PartOfSpeech::Interjection},
      {"Symbol", core::PartOfSpeech::Symbol},
      {"Other", core::PartOfSpeech::Other},
  }};
  const auto canonical = std::find_if(kCanonicalNames.begin(), kCanonicalNames.end(),
                                      [this](const auto& entry) { return entry.first == pos; });
  if (canonical != kCanonicalNames.end()) {
    return canonical->second;
  }
  const auto parsed = core::stringToPosStrict(pos);
  if (!parsed.has_value() || parsed.value() == core::PartOfSpeech::Unknown) {
    throw std::invalid_argument("Unknown expected POS: " + pos);
  }
  return parsed.value();
}

bool TestCase::hasTag(const std::string& tag) const {
  return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

std::vector<TestCase> TestSuite::filterByTag(const std::string& tag) const {
  std::vector<TestCase> filtered;
  for (const auto& tc : cases) {
    if (tc.hasTag(tag)) {
      filtered.push_back(tc);
    }
  }
  return filtered;
}

}  // namespace suzume::test
