#include "normalize/exceptions.h"

#include <algorithm>
#include <array>

namespace suzume::normalize {

namespace {

template <typename T, size_t Size>
bool contains(const std::array<T, Size>& values, const T& value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

// Particles that should not be treated as verb endings when generating
// verb candidates from kanji + hiragana patterns.
constexpr std::array<std::string_view, 15> kParticleStrings = {
    // Case particles (格助詞)
    "が",
    "を",
    "に",
    "で",
    "と",
    "へ",
    "の",
    // Binding particles (係助詞)
    "は",
    "も",
    // Other particles (副助詞・接続助詞)
    "や",
    "か",
    // Compound particles (複合助詞)
    "から",
    "まで",
    "より",
    "ほど",
};

// Copula and auxiliary patterns that should not be treated as verb endings.
constexpr std::array<std::string_view, 6> kCopulaStrings = {
    // Basic copula (基本形)
    "だ",
    "です",
    // Past forms (過去形)
    "だった",
    "でした",
    // Partial forms (途中形)
    "でし",
    // Formal copula (文語形)
    "である",
};

// Formal nouns (形式名詞) with abstract grammatical functions. These remain
// recognizable even when a dictionary lookup did not flag the candidate.
constexpr std::array<std::string_view, 6> kFormalNounStrings = {
    "所", "物", "事", "時", "方", "為",
};

}  // namespace

bool isParticle(std::string_view surface) {
  return contains(kParticleStrings, surface);
}

bool isCopula(std::string_view surface) {
  return contains(kCopulaStrings, surface);
}

bool isParticleOrCopula(std::string_view surface) {
  return isParticle(surface) || isCopula(surface);
}

bool isFormalNounSurface(std::string_view surface) {
  return contains(kFormalNounStrings, surface);
}

bool isParticleCodepoint(char32_t ch) {
  switch (ch) {
    case U'が':
    case U'を':
    case U'に':
    case U'で':
    case U'と':
    case U'へ':
    case U'の':
    case U'は':
    case U'も':
    case U'や':
    case U'か':
      return true;
    default:
      return false;
  }
}

}  // namespace suzume::normalize
