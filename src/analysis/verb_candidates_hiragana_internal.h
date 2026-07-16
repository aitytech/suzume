#ifndef SUZUME_ANALYSIS_VERB_CANDIDATES_HIRAGANA_INTERNAL_H_
#define SUZUME_ANALYSIS_VERB_CANDIDATES_HIRAGANA_INTERNAL_H_

#include "analysis/verb_candidates.h"
#include "normalize/char_type.h"

namespace suzume::analysis::hiragana_verb_detail {

// Guard (fabricated closed-class absorption family, tail class): true when the
// hiragana run [start_pos, end_pos) is a verb prefix followed by a 副助詞
// (しか/とか), so the run is verb + particle rather than a single fabricated
// 未然形 (やるしか → や|る|しか, never a form of the non-word 〜しく). Declared
// here so the guard can be exercised directly by characterization tests; see the
// guard-family note in verb_candidates_helpers.h.
bool endsWithParticleAfterVerb(const dictionary::DictionaryManager* dict_manager, const grammar::Inflection& inflection,
                               const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos);

bool pronounEndsAt(const dictionary::DictionaryManager* dict_manager, const std::vector<char32_t>& codepoints,
                   size_t pos);
void appendPassiveMizenkeiCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t hiragana_end,
                                     const grammar::Inflection& inflection,
                                     const dictionary::DictionaryManager* dict_manager,
                                     std::vector<UnknownCandidate>& candidates);
void appendIchidanRareruCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t hiragana_end,
                                   const grammar::Inflection& inflection,
                                   const dictionary::DictionaryManager* dict_manager,
                                   std::vector<UnknownCandidate>& candidates);
void appendMizenkeiNCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t hiragana_end,
                               const dictionary::DictionaryManager* dict_manager,
                               std::vector<UnknownCandidate>& candidates);
void appendMizenkeiNaiCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t hiragana_end,
                                 const grammar::Inflection& inflection,
                                 const dictionary::DictionaryManager* dict_manager,
                                 std::vector<UnknownCandidate>& candidates);
void appendMizenkeiNakyaCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t hiragana_end,
                                   const grammar::Inflection& inflection,
                                   const dictionary::DictionaryManager* dict_manager,
                                   std::vector<UnknownCandidate>& candidates);
void appendNOnbinNaiCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t hiragana_end,
                               const grammar::Inflection& inflection, const dictionary::DictionaryManager* dict_manager,
                               std::vector<UnknownCandidate>& candidates);
void appendOnbinContractionCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t hiragana_end,
                                      const grammar::Inflection& inflection,
                                      const dictionary::DictionaryManager* dict_manager,
                                      std::vector<UnknownCandidate>& candidates);
void appendKuruMizenkeiNaiCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                     std::vector<UnknownCandidate>& candidates);
void appendMiruAuxiliaryCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                   const dictionary::DictionaryManager* dict_manager,
                                   std::vector<UnknownCandidate>& candidates);
void appendOkuAuxiliaryCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                  std::vector<UnknownCandidate>& candidates);
void appendIchidanRenyokei1CharCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                          const dictionary::DictionaryManager* dict_manager,
                                          std::vector<UnknownCandidate>& candidates);
void appendHiraganaDerivedCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t hiragana_end,
                                     const std::vector<normalize::CharType>& char_types,
                                     const grammar::Inflection& inflection,
                                     const dictionary::DictionaryManager* dict_manager,
                                     std::vector<UnknownCandidate>& candidates);

}  // namespace suzume::analysis::hiragana_verb_detail

#endif  // SUZUME_ANALYSIS_VERB_CANDIDATES_HIRAGANA_INTERNAL_H_
