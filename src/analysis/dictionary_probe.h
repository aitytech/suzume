/**
 * @file dictionary_probe.h
 * @brief Bounded dictionary lookups over a codepoint window
 */

#ifndef SUZUME_ANALYSIS_DICTIONARY_PROBE_H_
#define SUZUME_ANALYSIS_DICTIONARY_PROBE_H_

#include <algorithm>
#include <cstddef>
#include <vector>

#include "analysis/tokenizer_utils.h"
#include "core/types.h"
#include "dictionary/dictionary.h"

namespace suzume::analysis {

/**
 * @brief First dictionary entry spanning @p start that @p accept takes
 *
 * Scans the spans [start, start + len] for len in [min_len, max_len], shortest
 * first, and returns the first entry the predicate accepts. @p max_len is
 * clamped to the end of @p codepoints, so callers state the closed class's
 * longest member rather than repeating the bounds arithmetic.
 *
 * @param pos Restricts the lookup to one part of speech; Unknown accepts any
 * @param accept Called with each candidate entry; the first it takes wins
 * @return the accepted entry, or nullptr when no span in range has one
 * @note The predicate is a template parameter so it inlines. This runs per
 *       boundary during candidate generation.
 */
template <typename Accept>
const dictionary::DictionaryEntry* firstDictionaryEntryFrom(const dictionary::DictionaryManager* dict_manager,
                                                            const std::vector<char32_t>& codepoints, size_t start,
                                                            size_t min_len, size_t max_len, core::PartOfSpeech pos,
                                                            Accept accept) {
  if (dict_manager == nullptr || start >= codepoints.size() || min_len == 0) {
    return nullptr;
  }
  const size_t last_end = std::min(codepoints.size(), start + max_len);
  for (size_t end = start + min_len; end <= last_end; ++end) {
    const auto* entry = dict_manager->lookupExact(extractSubstring(codepoints, start, end), pos);
    if (entry != nullptr && accept(*entry)) {
      return entry;
    }
  }
  return nullptr;
}

/** @brief Whether any dictionary entry spanning @p start is accepted */
template <typename Accept>
bool hasDictionaryEntryFrom(const dictionary::DictionaryManager* dict_manager, const std::vector<char32_t>& codepoints,
                            size_t start, size_t min_len, size_t max_len, core::PartOfSpeech pos, Accept accept) {
  return firstDictionaryEntryFrom(dict_manager, codepoints, start, min_len, max_len, pos, accept) != nullptr;
}

/**
 * @brief Length of the longest accepted dictionary entry spanning @p start
 *
 * The mirror of firstDictionaryEntryFrom for the callers that need the longest
 * match rather than the shortest, and the span length rather than the entry.
 *
 * @return the accepted length, or 0 when no span in range has one
 */
template <typename Accept>
size_t longestDictionaryEntryLengthFrom(const dictionary::DictionaryManager* dict_manager,
                                        const std::vector<char32_t>& codepoints, size_t start, size_t min_len,
                                        size_t max_len, core::PartOfSpeech pos, Accept accept) {
  if (dict_manager == nullptr || start >= codepoints.size() || min_len == 0) {
    return 0;
  }
  const size_t longest = std::min(max_len, codepoints.size() - start);
  for (size_t len = longest; len >= min_len; --len) {
    const auto* entry = dict_manager->lookupExact(extractSubstring(codepoints, start, start + len), pos);
    if (entry != nullptr && accept(*entry)) {
      return len;
    }
  }
  return 0;
}

}  // namespace suzume::analysis

#endif  // SUZUME_ANALYSIS_DICTIONARY_PROBE_H_
