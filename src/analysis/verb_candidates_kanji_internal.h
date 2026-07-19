#ifndef SUZUME_ANALYSIS_VERB_CANDIDATES_KANJI_INTERNAL_H_
#define SUZUME_ANALYSIS_VERB_CANDIDATES_KANJI_INTERNAL_H_

#include "analysis/verb_candidates.h"

namespace suzume::analysis::kanji_verb_detail {

void appendGodanMizenkeiPassiveCausativeCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                                   size_t kanji_end, size_t hiragana_end,
                                                   const grammar::Inflection& inflection,
                                                   const dictionary::DictionaryManager* dict_manager,
                                                   std::vector<UnknownCandidate>& candidates);
void appendSaRowContractedMizenkeiCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                             size_t kanji_end, size_t hiragana_end,
                                             const grammar::Inflection& inflection,
                                             std::vector<UnknownCandidate>& candidates);
void appendGodanMizenkeiZuCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t kanji_end,
                                     size_t hiragana_end, const grammar::Inflection& inflection,
                                     const dictionary::DictionaryManager* dict_manager,
                                     std::vector<UnknownCandidate>& candidates);
void appendIchidanRenyokeiCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t kanji_end,
                                     size_t hiragana_end, const grammar::Inflection& inflection,
                                     const dictionary::DictionaryManager* dict_manager,
                                     const VerbCandidateOptions& verb_opts, std::vector<UnknownCandidate>& candidates);
void appendGodanSaRenyokeiCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t kanji_end,
                                     size_t hiragana_end, const grammar::Inflection& inflection,
                                     const dictionary::DictionaryManager* dict_manager,
                                     const VerbCandidateOptions& verb_opts, std::vector<UnknownCandidate>& candidates);
void appendIchidanKateikeiVolitionalCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                               size_t kanji_end, size_t hiragana_end,
                                               const grammar::Inflection& inflection,
                                               const dictionary::DictionaryManager* dict_manager,
                                               std::vector<UnknownCandidate>& candidates);
void appendCausativeRenyokeiCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t kanji_end,
                                       size_t hiragana_end, const grammar::Inflection& inflection,
                                       const dictionary::DictionaryManager* dict_manager,
                                       const VerbCandidateOptions& verb_opts,
                                       std::vector<UnknownCandidate>& candidates);
void appendGodanPassiveRenyokeiCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t kanji_end,
                                          size_t hiragana_end, const grammar::Inflection& inflection,
                                          const dictionary::DictionaryManager* dict_manager,
                                          const VerbCandidateOptions& verb_opts,
                                          std::vector<UnknownCandidate>& candidates);
void appendIchidanStemRareCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t kanji_end,
                                     size_t hiragana_end, const grammar::Inflection& inflection,
                                     const dictionary::DictionaryManager* dict_manager,
                                     std::vector<UnknownCandidate>& candidates);
void appendSingleKanjiIchidanCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t kanji_end,
                                        size_t hiragana_end, const dictionary::DictionaryManager* dict_manager,
                                        std::vector<UnknownCandidate>& candidates);
void appendAnalyzedKanjiVerbCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t kanji_end,
                                       size_t hiragana_end, const grammar::Inflection& inflection,
                                       const dictionary::DictionaryManager* dict_manager,
                                       const VerbCandidateOptions& verb_opts, bool sokuonbin_stem_verified,
                                       const std::string& sokuonbin_lemma, std::vector<UnknownCandidate>& candidates);
void appendKanjiMizenkeiStemCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t kanji_end,
                                       size_t hiragana_end, const grammar::Inflection& inflection,
                                       const dictionary::DictionaryManager* dict_manager,
                                       std::vector<UnknownCandidate>& candidates);
void appendKanjiOnbinCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t kanji_end,
                                size_t hiragana_end, const grammar::Inflection& inflection,
                                const dictionary::DictionaryManager* dict_manager, bool sokuonbin_stem_verified,
                                const std::string& sokuonbin_lemma, grammar::VerbType sokuonbin_verb_type,
                                std::vector<UnknownCandidate>& candidates);
void appendVerifiedTailGodanTaCompoundCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                                 size_t kanji_end, const dictionary::DictionaryManager* dict_manager,
                                                 std::vector<UnknownCandidate>& candidates);

}  // namespace suzume::analysis::kanji_verb_detail

#endif  // SUZUME_ANALYSIS_VERB_CANDIDATES_KANJI_INTERNAL_H_
