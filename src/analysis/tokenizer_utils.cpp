/**
 * @file tokenizer_utils.cpp
 * @brief Utility functions for tokenizer
 */

#include "tokenizer_utils.h"

#include "core/utf8_constants.h"
#include "normalize/utf8.h"

namespace suzume::analysis {

size_t findCharRegionEnd(const std::vector<normalize::CharType>& char_types, size_t start_pos, size_t max_len,
                         normalize::CharType target_type) {
  size_t end = start_pos;
  while (end < char_types.size() && end - start_pos < max_len && char_types[end] == target_type) {
    ++end;
  }
  return end;
}

ByteOffsets buildByteOffsets(const std::vector<char32_t>& codepoints) {
  ByteOffsets byte_offsets;
  byte_offsets.reserve(codepoints.size() + 1);
  byte_offsets.push_back(0);
  for (char32_t codepoint : codepoints) {
    byte_offsets.push_back(byte_offsets.back() + core::utf8ByteLength(codepoint));
  }
  return byte_offsets;
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
