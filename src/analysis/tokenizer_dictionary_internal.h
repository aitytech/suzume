#ifndef SUZUME_ANALYSIS_TOKENIZER_DICTIONARY_INTERNAL_H_
#define SUZUME_ANALYSIS_TOKENIZER_DICTIONARY_INTERNAL_H_

#include <cstddef>
#include <string_view>
#include <vector>

#include "core/lattice.h"

namespace suzume::analysis::tokenizer_dictionary_detail {

void appendSpecialGrammarCandidates(core::Lattice& lattice, std::string_view text,
                                    const std::vector<char32_t>& codepoints, size_t start_pos, size_t byte_pos);

}  // namespace suzume::analysis::tokenizer_dictionary_detail

#endif  // SUZUME_ANALYSIS_TOKENIZER_DICTIONARY_INTERNAL_H_
