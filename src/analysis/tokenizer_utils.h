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
#include <string_view>
#include <vector>

#include "core/lattice.h"
#include "normalize/char_type.h"

namespace suzume {
namespace dictionary {
class DictionaryManager;
struct LookupResult;
}  // namespace dictionary
}  // namespace suzume

namespace suzume::analysis {

using ByteOffsets = std::vector<size_t>;
using PartOfSpeechMask = uint32_t;

constexpr PartOfSpeechMask partOfSpeechMask(core::PartOfSpeech pos) {
  return 1U << static_cast<uint8_t>(pos);
}

/** Whether an exact dictionary surface has any of the requested parts of speech. */
bool hasExactPartOfSpeech(const dictionary::DictionaryManager& dict_manager, std::string_view surface,
                          PartOfSpeechMask pos_mask);

/** Whether dictionary lookup results contain a requested POS, optionally at an exact character length. */
bool lookupResultsHavePartOfSpeech(const std::vector<dictionary::LookupResult>& results, PartOfSpeechMask pos_mask,
                                   size_t length = 0);

/** Whether dictionary lookup results contain a requested ExtendedPOS, optionally at an exact character length. */
bool lookupResultsHaveExtendedPOS(const std::vector<dictionary::LookupResult>& results, core::ExtendedPOS extended_pos,
                                  size_t length = 0);

/** Whether a complete dictionary match is a verb with the requested lemma. */
bool hasCompleteVerbLemma(const dictionary::DictionaryManager& dict_manager, std::string_view surface,
                          size_t char_length, std::string_view lemma);

/** Whether a character boundary starts inside a registered noun. */
bool startsInsideRegisteredNoun(const dictionary::DictionaryManager& dict_manager, std::string_view text,
                                const ByteOffsets& byte_offsets, size_t start_pos);

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
 * @brief Check whether a kanji run at @p start_pos is followed by する.
 *
 * @param minimum_kanji_count Minimum length required for the kanji run.
 */
bool hasKanjiSuruPredicateAt(const std::vector<char32_t>& codepoints,
                             const std::vector<normalize::CharType>& char_types, size_t start_pos,
                             size_t minimum_kanji_count = 1);

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

/** Whether an edge with one of the requested parts of speech ends at a boundary. */
bool hasPrecedingPartOfSpeech(const core::Lattice& lattice, size_t end_pos, PartOfSpeechMask pos_mask);

/** Whether an edge with the requested extended part of speech ends at a boundary. */
bool hasPrecedingExtendedPOS(const core::Lattice& lattice, size_t end_pos, core::ExtendedPOS extended_pos);

/**
 * @brief Find the end of a dictionary-evidenced compound verb covering pos.
 *
 * Only the explicit LemmaVerified flag counts. Compound candidates also use
 * FromDictionary for their component evidence, which is not sufficient to
 * establish the complete lexical compound.
 */
size_t verifiedCompoundEndCovering(const core::Lattice& lattice, size_t pos);

/** Find a dictionary-derived verb onbin form spanning an interior boundary. */
size_t dictionarySokuonbinEndCovering(const core::Lattice& lattice, size_t pos);

/**
 * @brief Find the end of any structurally generated compound verb covering pos.
 */
size_t compoundVerbEndCovering(const core::Lattice& lattice, size_t pos);

/**
 * @brief Whether a candidate joins a licensed particle to an adverb prefix.
 *
 * A particle is treated as grammatical only when a content/predicate edge
 * ends at the candidate start. This prevents homographic kana inside open
 * adjectives and nouns from claiming a particle boundary.
 */
bool joinsParticleToDictionaryAdverb(const core::Lattice& lattice, const dictionary::DictionaryManager& dict_manager,
                                     std::string_view text, const ByteOffsets& byte_offsets, size_t candidate_start,
                                     size_t candidate_end);

}  // namespace suzume::analysis

#endif  // SUZUME_ANALYSIS_TOKENIZER_UTILS_H_
