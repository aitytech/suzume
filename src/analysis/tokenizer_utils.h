/**
 * @file tokenizer_utils.h
 * @brief Utility functions for tokenizer
 */

#ifndef SUZUME_ANALYSIS_TOKENIZER_UTILS_H_
#define SUZUME_ANALYSIS_TOKENIZER_UTILS_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "normalize/char_type.h"

namespace suzume::analysis {

using ByteOffsets = std::vector<size_t>;

/**
 * @brief Find end position of consecutive characters of a given type
 *
 * Scans from start_pos until one of: bounds exceeded, max_len reached,
 * or character type changes.
 *
 * @param char_types Character type array
 * @param start_pos Starting position
 * @param max_len Maximum characters to scan
 * @param target_type Character type to match
 * @return End position (exclusive)
 *
 * Example:
 *   // Find up to 3 kanji starting at start_pos
 *   size_t kanji_end = findCharRegionEnd(char_types, start_pos, 3, CharType::Kanji);
 */
size_t findCharRegionEnd(const std::vector<normalize::CharType>& char_types, size_t start_pos, size_t max_len,
                         normalize::CharType target_type);

/**
 * @brief Build UTF-8 byte offsets for every character boundary
 *
 * @param codepoints Vector of Unicode codepoints
 * @return Prefix offsets with codepoints.size() + 1 entries
 */
ByteOffsets buildByteOffsets(const std::vector<char32_t>& codepoints);

/**
 * @brief Look up a character boundary's UTF-8 byte offset
 *
 * Positions beyond the available boundaries safely resolve to the final byte
 * offset, matching the former scanning helper's clamping behavior.
 */
inline size_t byteOffsetAt(const ByteOffsets& byte_offsets, size_t char_pos) {
  return byte_offsets.empty() ? 0 : byte_offsets[std::min(char_pos, byte_offsets.size() - 1)];
}

/**
 * @brief Advance a character position until its byte offset reaches a target
 *
 * Starting from @p start_char (whose UTF-8 byte offset is @p start_byte), walk
 * forward one codepoint at a time, accumulating each codepoint's UTF-8 byte
 * length, and stop as soon as the accumulated byte offset is no longer below
 * @p target_byte or the codepoints are exhausted.
 *
 * @param codepoints Vector of Unicode codepoints
 * @param start_char Character position to start from (0-indexed)
 * @param start_byte Byte offset corresponding to @p start_char
 * @param target_byte Byte offset to advance up to
 * @return Character position whose byte offset reaches @p target_byte
 */
size_t advanceCharsToBytePos(const std::vector<char32_t>& codepoints, size_t start_char, size_t start_byte,
                             size_t target_byte);

/**
 * @brief Encode a codepoint range as UTF-8.
 */
std::string extractSubstring(const std::vector<char32_t>& codepoints, size_t start, size_t end);

}  // namespace suzume::analysis

#endif  // SUZUME_ANALYSIS_TOKENIZER_UTILS_H_
