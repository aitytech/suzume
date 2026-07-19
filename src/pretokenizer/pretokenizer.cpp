/**
 * @file pretokenizer.cpp
 * @brief Pre-tokenizer matcher orchestration
 */

#include "normalize/utf8.h"
#include "pretokenizer/pretokenizer_internal.h"

namespace suzume::pretokenizer {

using namespace pretokenizer_detail;

bool PreTokenizer::isSentenceBoundary(char32_t codepoint) const {
  return codepoint == U'。' || codepoint == U'！' || codepoint == U'？' || codepoint == U'!' || codepoint == U'?' ||
         codepoint == U'\n' || codepoint == U'・';  // Nakaguro: token boundary (splits カタカナ・カタカナ)
}

PreTokenResult PreTokenizer::process(std::string_view text) const {
  PreTokenResult result;

  if (text.empty()) {
    return result;
  }

  size_t pos = 0;
  size_t span_start = 0;

  while (pos < text.size()) {
    PreToken token;

    // Try to match patterns in priority order
    // Note: URL must come before Email (URLs contain @ in some cases)
    // Note: Email must come before Mention (emails have @ followed by domain)
    // Note: Percentage must come before Version to avoid "3.14%" being parsed as version
    // Note: Date must come before Time (日付 includes 日 which looks like time suffix)
    if (tryMatchUrl(text, pos, token) || tryMatchEmail(text, pos, token) || tryMatchHashtag(text, pos, token) ||
        tryMatchMention(text, pos, token) || tryMatchDate(text, pos, token) || tryMatchCounter(text, pos, token) ||
        tryMatchTime(text, pos, token) || tryMatchCurrency(text, pos, token) || tryMatchStorage(text, pos, token) ||
        tryMatchPercentage(text, pos, token) || tryMatchAddressNumber(text, pos, token) ||
        tryMatchVersion(text, pos, token) || tryMatchAsciiWithDots(text, pos, token)) {
      // Add span before this token if any
      if (pos > span_start) {
        result.spans.push_back({span_start, pos});
      }

      result.tokens.push_back(token);
      pos = token.end;
      span_start = pos;
      continue;
    }

    // Check for sentence boundary
    size_t byte_pos = pos;
    char32_t codepoint = normalize::decodeUtf8(text, byte_pos);

    if (isSentenceBoundary(codepoint)) {
      // Add span before boundary if any
      if (pos > span_start) {
        result.spans.push_back({span_start, pos});
      }

      // Add boundary token
      setTokenFromRange(token, text, pos, byte_pos, PreTokenType::Boundary, core::PartOfSpeech::Symbol);
      result.tokens.push_back(token);

      pos = byte_pos;
      span_start = pos;
      continue;
    }

    // Move to next character
    pos = byte_pos;
  }

  // Add final span if any
  if (pos > span_start) {
    result.spans.push_back({span_start, pos});
  }

  return result;
}
}  // namespace suzume::pretokenizer
