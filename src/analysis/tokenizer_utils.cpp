/**
 * @file tokenizer_utils.cpp
 * @brief Utility functions for tokenizer
 */

#include "tokenizer_utils.h"

#include "core/utf8_constants.h"
#include "normalize/utf8.h"

namespace suzume::analysis {

size_t charPosToBytePos(const std::vector<char32_t>& codepoints, size_t char_pos) {
  size_t byte_pos = 0;
  for (size_t idx = 0; idx < char_pos && idx < codepoints.size(); ++idx) {
    byte_pos += core::utf8ByteLength(codepoints[idx]);
  }
  return byte_pos;
}

size_t advanceCharsToBytePos(const std::vector<char32_t>& codepoints, size_t start_char, size_t start_byte,
                             size_t target_byte) {
  size_t char_pos = start_char;
  size_t byte_count = start_byte;
  while (char_pos < codepoints.size() && byte_count < target_byte) {
    byte_count += core::utf8ByteLength(codepoints[char_pos]);
    ++char_pos;
  }
  return char_pos;
}

std::string extractSubstring(const std::vector<char32_t>& codepoints, size_t start, size_t end) {
  return normalize::encodeRange(codepoints, start, end);
}

}  // namespace suzume::analysis
