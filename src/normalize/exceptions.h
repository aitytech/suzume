#ifndef SUZUME_NORMALIZE_EXCEPTIONS_H_
#define SUZUME_NORMALIZE_EXCEPTIONS_H_

// =============================================================================
// Exception Sets
// =============================================================================
// Centralized exception sets for tokenization.
// These sets contain words that should not receive normal scoring penalties.
//
// Note: This module is for closed-class exceptions only.
// Open-class vocabulary belongs in dictionaries (L2/L3).
// =============================================================================

#include <string_view>
#include <unordered_set>

namespace suzume::normalize {

// =============================================================================
// Particle/Copula Sets (for verb candidate filtering)
// =============================================================================

// Particle strings that should not be treated as verb endings
// Includes case particles (格助詞), binding particles (係助詞), and compound particles
extern const std::unordered_set<std::string_view> kParticleStrings;

// Copula/auxiliary verb patterns that should not be treated as verb endings
extern const std::unordered_set<std::string_view> kCopulaStrings;

// =============================================================================
// Particle/Copula Lookup Functions
// =============================================================================

// Check if surface is a particle (should not be verb ending)
inline bool isParticle(std::string_view surface) {
  return kParticleStrings.find(surface) != kParticleStrings.end();
}

// Check if surface is a copula pattern (should not be verb ending)
inline bool isCopula(std::string_view surface) {
  return kCopulaStrings.find(surface) != kCopulaStrings.end();
}

// Check if surface is either particle or copula
inline bool isParticleOrCopula(std::string_view surface) {
  return isParticle(surface) || isCopula(surface);
}

// =============================================================================
// Formal Noun Strings (形式名詞)
// =============================================================================

// Formal nouns (形式名詞) - single kanji nouns with abstract grammatical functions
// These should be recognized even when not flagged from dictionary lookup
extern const std::unordered_set<std::string_view> kFormalNounStrings;

// Check if surface is a formal noun
inline bool isFormalNounSurface(std::string_view surface) {
  return kFormalNounStrings.find(surface) != kFormalNounStrings.end();
}

// =============================================================================
// Particle Codepoints (for character-level filtering)
// =============================================================================

// Case particles (格助詞) and binding particles (係助詞) as codepoints
// Used to filter out strings that start with particles from verb/adjective analysis
// を, が, は, も, へ, の, に, で, と, や, か
extern const std::unordered_set<char32_t> kParticleCodepoints;

// Check if a codepoint is a case/binding particle
inline bool isParticleCodepoint(char32_t ch) {
  return kParticleCodepoints.find(ch) != kParticleCodepoints.end();
}

}  // namespace suzume::normalize

#endif  // SUZUME_NORMALIZE_EXCEPTIONS_H_
