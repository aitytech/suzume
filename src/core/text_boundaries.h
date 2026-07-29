#ifndef SUZUME_CORE_TEXT_BOUNDARIES_H_
#define SUZUME_CORE_TEXT_BOUNDARIES_H_

namespace suzume {
namespace core {

// Boundaries shared by pre-tokenization and long-input chunking. Nakaguro is
// included because it separates Japanese word units such as katakana compounds.
constexpr bool isSentenceBoundaryCodepoint(char32_t codepoint) {
  return codepoint == U'。' || codepoint == U'！' || codepoint == U'？' || codepoint == U'!' || codepoint == U'?' ||
         codepoint == U'\n' || codepoint == U'・';
}

}  // namespace core
}  // namespace suzume

#endif  // SUZUME_CORE_TEXT_BOUNDARIES_H_
