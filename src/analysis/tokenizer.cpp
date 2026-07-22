/**
 * @file tokenizer.cpp
 * @brief Tokenizer that builds lattice from text
 *
 * This file orchestrates candidate generation for tokenization:
 * - Dictionary candidates (direct lookup)
 * - Unknown word candidates (delegated to UnknownWordGenerator)
 * - Split candidates (delegated to split_candidates.h)
 * - Join candidates (delegated to join_candidates.h)
 */

#include "analysis/tokenizer.h"

#include "analysis/category_cost.h"
#include "candidate_constants.h"
#include "core/debug.h"
#include "core/kana_constants.h"
#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "join_candidates.h"
#include "normalize/utf8.h"
#include "split_candidates.h"
#include "suffix_candidates.h"
#include "tokenizer_utils.h"
#include "verb_candidates_helpers.h"

namespace suzume::analysis {

Tokenizer::Tokenizer(const dictionary::DictionaryManager& dict_manager, const Scorer& scorer,
                     const UnknownWordGenerator& unknown_gen, core::AnalysisMode mode)
    : dict_manager_(dict_manager),
      scorer_(scorer),
      unknown_gen_(unknown_gen),
      inflection_(unknown_gen.inflection()),
      mode_(mode) {}

core::Lattice Tokenizer::buildLattice(std::string_view text, const std::vector<char32_t>& codepoints,
                                      const std::vector<normalize::CharType>& char_types) const {
  core::Lattice lattice(codepoints.size());
  const ByteOffsets byte_offsets = buildByteOffsets(codepoints);

  // Process each position
  for (size_t pos = 0; pos < codepoints.size(); ++pos) {
    // These run at every position
    addDictionaryCandidates(lattice, text, codepoints, byte_offsets, pos);
    addUnknownCandidates(lattice, text, codepoints, byte_offsets, pos, char_types);
    if (mode_ != core::AnalysisMode::Split) {
      addPronounPluralJoinCandidates(lattice, text, codepoints, byte_offsets, pos);
    }
    if (mode_ != core::AnalysisMode::Split) {
      addMixedScriptCandidates(lattice, text, codepoints, byte_offsets, pos, char_types);
    }

    // CharType-based dispatch: skip generators that can't match at this position
    auto ct = char_types[pos];
    if (ct == normalize::CharType::Kanji) {
      addCompoundSplitCandidates(lattice, text, byte_offsets, pos, char_types);
      addNounVerbSplitCandidates(lattice, text, codepoints, byte_offsets, pos, char_types);
      if (mode_ != core::AnalysisMode::Split) {
        addCompoundVerbJoinCandidates(lattice, text, codepoints, byte_offsets, pos, char_types);
        addPrefixNounJoinCandidates(lattice, text, codepoints, byte_offsets, pos, char_types);
        addTaruAdjectiveJoinCandidates(lattice, text, codepoints, byte_offsets, pos, char_types);
        addVerbSuffixNounJoinCandidates(lattice, text, codepoints, byte_offsets, pos, char_types);
      }
    } else if (ct == normalize::CharType::Hiragana) {
      if (mode_ != core::AnalysisMode::Split) {
        addPrefixNounJoinCandidates(lattice, text, codepoints, byte_offsets, pos, char_types);
        addHiraganaCompoundVerbJoinCandidates(lattice, text, codepoints, byte_offsets, pos, char_types);
        addVerbSuffixNounJoinCandidates(lattice, text, codepoints, byte_offsets, pos, char_types);
      }
      addTeFormAuxiliaryCandidates(lattice, text, codepoints, byte_offsets, pos, char_types);
    }

    SUZUME_DEBUG_LOG("[LATTICE] pos=" << pos << " candidates=" << lattice.edgeIdsAt(pos).size() << "\n");
  }

  // Fallback: ensure every position has at least one edge
  // This prevents the lattice from becoming invalid when no candidates are generated
  // (e.g., positions starting with small kana like っ, ゃ, ゅ, ょ)
  for (size_t pos = 0; pos < codepoints.size(); ++pos) {
    if (lattice.edgeIdsAt(pos).empty()) {
      // Generate a single-character fallback candidate with high penalty
      size_t byte_start = byteOffsetAt(byte_offsets, pos);
      size_t byte_end = byteOffsetAt(byte_offsets, pos + 1);
      std::string surface(text.substr(byte_start, byte_end - byte_start));

      lattice.addEdge(surface, static_cast<uint32_t>(pos), static_cast<uint32_t>(pos + 1), core::PartOfSpeech::Other,
                      candidate::kFallbackCandidateCost, core::LatticeEdge::kIsUnknown);
    }
  }

  return lattice;
}

void Tokenizer::addMixedScriptCandidates(core::Lattice& lattice, std::string_view text,
                                         const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                         size_t start_pos, const std::vector<normalize::CharType>& char_types) const {
  analysis::addMixedScriptCandidates(lattice, text, codepoints, byte_offsets, start_pos, char_types, scorer_,
                                     dict_manager_);
}

void Tokenizer::addCompoundSplitCandidates(core::Lattice& lattice, std::string_view text,
                                           const ByteOffsets& byte_offsets, size_t start_pos,
                                           const std::vector<normalize::CharType>& char_types) const {
  analysis::addCompoundSplitCandidates(lattice, text, byte_offsets, start_pos, char_types, dict_manager_, scorer_);
}

void Tokenizer::addNounVerbSplitCandidates(core::Lattice& lattice, std::string_view text,
                                           const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                           size_t start_pos, const std::vector<normalize::CharType>& char_types) const {
  analysis::addNounVerbSplitCandidates(lattice, text, codepoints, byte_offsets, start_pos, char_types, dict_manager_,
                                       scorer_, inflection_);
}

void Tokenizer::addCompoundVerbJoinCandidates(core::Lattice& lattice, std::string_view text,
                                              const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                              size_t start_pos,
                                              const std::vector<normalize::CharType>& char_types) const {
  analysis::addCompoundVerbJoinCandidates(lattice, text, codepoints, byte_offsets, start_pos, char_types, dict_manager_,
                                          scorer_, inflection_);
}

void Tokenizer::addHiraganaCompoundVerbJoinCandidates(core::Lattice& lattice, std::string_view text,
                                                      const std::vector<char32_t>& codepoints,
                                                      const ByteOffsets& byte_offsets, size_t start_pos,
                                                      const std::vector<normalize::CharType>& char_types) const {
  analysis::addHiraganaCompoundVerbJoinCandidates(lattice, text, codepoints, byte_offsets, start_pos, char_types,
                                                  dict_manager_, scorer_, inflection_);
}

void Tokenizer::addPrefixNounJoinCandidates(core::Lattice& lattice, std::string_view text,
                                            const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                            size_t start_pos,
                                            const std::vector<normalize::CharType>& char_types) const {
  analysis::addPrefixNounJoinCandidates(lattice, text, codepoints, byte_offsets, start_pos, char_types, dict_manager_,
                                        scorer_);
}

void Tokenizer::addPronounPluralJoinCandidates(core::Lattice& lattice, std::string_view text,
                                               const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                               size_t start_pos) const {
  analysis::addPronounPluralJoinCandidates(lattice, text, codepoints, byte_offsets, start_pos, dict_manager_, scorer_);
}

void Tokenizer::addTeFormAuxiliaryCandidates(core::Lattice& lattice, std::string_view text,
                                             const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                             size_t start_pos,
                                             const std::vector<normalize::CharType>& char_types) const {
  analysis::addTeFormAuxiliaryCandidates(lattice, text, codepoints, byte_offsets, start_pos, char_types, scorer_,
                                         inflection_);
}

void Tokenizer::addTaruAdjectiveJoinCandidates(core::Lattice& lattice, std::string_view text,
                                               const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                               size_t start_pos,
                                               const std::vector<normalize::CharType>& char_types) const {
  analysis::addTaruAdjectiveJoinCandidates(lattice, text, codepoints, byte_offsets, start_pos, char_types,
                                           dict_manager_, scorer_);
}

void Tokenizer::addVerbSuffixNounJoinCandidates(core::Lattice& lattice, std::string_view text,
                                                const std::vector<char32_t>& codepoints,
                                                const ByteOffsets& byte_offsets, size_t start_pos,
                                                const std::vector<normalize::CharType>& char_types) const {
  analysis::addVerbSuffixNounJoinCandidates(lattice, text, codepoints, byte_offsets, start_pos, char_types,
                                            dict_manager_, scorer_, inflection_);
}

}  // namespace suzume::analysis
