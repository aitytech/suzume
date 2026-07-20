#ifndef SUZUME_ANALYSIS_SUFFIX_CANDIDATES_COUNTER_INTERNAL_H_
#define SUZUME_ANALYSIS_SUFFIX_CANDIDATES_COUNTER_INTERNAL_H_

#include <cstddef>
#include <vector>

#include "dictionary/dictionary.h"
#include "normalize/char_type.h"
#include "unknown.h"

namespace suzume::analysis::counter_detail {

void appendTemporalCounterCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                     const std::vector<normalize::CharType>& char_types,
                                     const dictionary::DictionaryManager* dict_manager,
                                     std::vector<UnknownCandidate>& candidates);
void appendStructuralCounterCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                       const std::vector<normalize::CharType>& char_types,
                                       const dictionary::DictionaryManager* dict_manager,
                                       std::vector<UnknownCandidate>& candidates);

}  // namespace suzume::analysis::counter_detail

#endif  // SUZUME_ANALYSIS_SUFFIX_CANDIDATES_COUNTER_INTERNAL_H_
