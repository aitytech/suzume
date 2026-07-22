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

bool hasKanjiSuruPredicateAt(const std::vector<char32_t>& codepoints,
                             const std::vector<normalize::CharType>& char_types, size_t start_pos,
                             size_t minimum_kanji_count) {
  const size_t predicate_end = findCharRegionEnd(
      char_types, start_pos, char_types.size() - std::min(start_pos, char_types.size()), normalize::CharType::Kanji);
  return predicate_end - start_pos >= minimum_kanji_count && predicate_end + 1 < codepoints.size() &&
         codepoints[predicate_end] == U'す' && codepoints[predicate_end + 1] == U'る';
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

namespace {

size_t compoundEndCovering(const core::Lattice& lattice, size_t pos, bool require_lexical_evidence) {
  size_t covering_end = 0;
  for (size_t edge_start = 0; edge_start < pos; ++edge_start) {
    for (const uint32_t edge_id : lattice.edgeIdsAt(edge_start)) {
      const auto& edge = lattice.getEdge(edge_id);
      const bool is_open_compound_stem =
          edge.extended_pos == core::ExtendedPOS::VerbRenyokei || edge.extended_pos == core::ExtendedPOS::VerbMizenkei;
      const bool is_compound_stem =
          edge.origin == core::CandidateOrigin::VerbCompound && is_open_compound_stem &&
          (!require_lexical_evidence || core::hasFlag(edge.flags, core::EdgeFlags::LemmaVerified));
      if (is_compound_stem && edge.start < pos && edge.end > pos) {
        covering_end = std::max(covering_end, static_cast<size_t>(edge.end));
      }
    }
  }
  return covering_end;
}

}  // namespace

size_t verifiedCompoundEndCovering(const core::Lattice& lattice, size_t pos) {
  return compoundEndCovering(lattice, pos, true);
}

size_t dictionarySokuonbinEndCovering(const core::Lattice& lattice, size_t pos) {
  size_t covering_end = 0;
  for (size_t edge_start = 0; edge_start < pos; ++edge_start) {
    for (const uint32_t edge_id : lattice.edgeIdsAt(edge_start)) {
      const auto& edge = lattice.getEdge(edge_id);
      if (edge.origin == core::CandidateOrigin::Dictionary && edge.pos == core::PartOfSpeech::Verb &&
          edge.extended_pos == core::ExtendedPOS::VerbOnbinkei && utf8::endsWith(edge.surface, "っ") &&
          edge.start < pos && edge.end > pos) {
        covering_end = std::max(covering_end, static_cast<size_t>(edge.end));
      }
    }
  }
  return covering_end;
}

size_t compoundVerbEndCovering(const core::Lattice& lattice, size_t pos) {
  return compoundEndCovering(lattice, pos, false);
}

}  // namespace suzume::analysis
