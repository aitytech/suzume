/**
 * @file tokenizer_utils.cpp
 * @brief Utility functions for tokenizer
 */

#include "tokenizer_utils.h"

#include "core/utf8_constants.h"
#include "dictionary/dictionary.h"
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

bool hasExactPartOfSpeech(const dictionary::DictionaryManager& dict_manager, std::string_view surface,
                          PartOfSpeechMask pos_mask) {
  for (uint8_t pos_value = 0; pos_mask != 0; ++pos_value, pos_mask >>= 1) {
    if ((pos_mask & 1U) != 0 &&
        dict_manager.lookupExact(surface, static_cast<core::PartOfSpeech>(pos_value)) != nullptr) {
      return true;
    }
  }
  return false;
}

bool hasCompleteVerbLemma(const dictionary::DictionaryManager& dict_manager, std::string_view surface,
                          size_t char_length, std::string_view lemma) {
  for (const auto& match : dict_manager.lookup(surface, 0)) {
    if (match.entry != nullptr && match.length == char_length && match.entry->pos == core::PartOfSpeech::Verb &&
        match.entry->lemma == lemma) {
      return true;
    }
  }
  return false;
}

bool startsInsideRegisteredNoun(const dictionary::DictionaryManager& dict_manager, std::string_view text,
                                const ByteOffsets& byte_offsets, size_t start_pos) {
  if (start_pos == 0) {
    return false;
  }
  const size_t scan_start = start_pos > 8 ? start_pos - 8 : 0;
  for (size_t noun_start = scan_start; noun_start < start_pos; ++noun_start) {
    for (const auto& result : dict_manager.lookup(text, byteOffsetAt(byte_offsets, noun_start))) {
      if (result.entry != nullptr && result.entry->pos == core::PartOfSpeech::Noun &&
          noun_start + result.length > start_pos) {
        return true;
      }
    }
  }
  return false;
}

bool hasPrecedingPartOfSpeech(const core::Lattice& lattice, size_t end_pos, PartOfSpeechMask pos_mask) {
  for (size_t edge_start = 0; edge_start < end_pos; ++edge_start) {
    for (const uint32_t edge_id : lattice.edgeIdsAt(edge_start)) {
      const auto& edge = lattice.getEdge(edge_id);
      if (edge.end == end_pos && (pos_mask & partOfSpeechMask(edge.pos)) != 0) {
        return true;
      }
    }
  }
  return false;
}

bool hasPrecedingExtendedPOS(const core::Lattice& lattice, size_t end_pos, core::ExtendedPOS extended_pos) {
  for (size_t edge_start = 0; edge_start < end_pos; ++edge_start) {
    for (const uint32_t edge_id : lattice.edgeIdsAt(edge_start)) {
      const auto& edge = lattice.getEdge(edge_id);
      if (edge.end == end_pos && edge.extended_pos == extended_pos) {
        return true;
      }
    }
  }
  return false;
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

bool joinsParticleToDictionaryAdverb(const core::Lattice& lattice, const dictionary::DictionaryManager& dict_manager,
                                     std::string_view text, const ByteOffsets& byte_offsets, size_t candidate_start,
                                     size_t candidate_end) {
  if (candidate_start == 0 || candidate_start + 1 >= candidate_end || byte_offsets.empty()) {
    return false;
  }

  constexpr PartOfSpeechMask kLeftContextMask =
      partOfSpeechMask(core::PartOfSpeech::Noun) | partOfSpeechMask(core::PartOfSpeech::Pronoun) |
      partOfSpeechMask(core::PartOfSpeech::Verb) | partOfSpeechMask(core::PartOfSpeech::Adjective) |
      partOfSpeechMask(core::PartOfSpeech::Auxiliary);
  if (!hasPrecedingPartOfSpeech(lattice, candidate_start, kLeftContextMask)) {
    return false;
  }

  for (size_t split_pos = candidate_start + 1; split_pos < candidate_end; ++split_pos) {
    const size_t prefix_byte_start = byteOffsetAt(byte_offsets, candidate_start);
    const size_t prefix_byte_end = byteOffsetAt(byte_offsets, split_pos);
    const std::string_view particle = text.substr(prefix_byte_start, prefix_byte_end - prefix_byte_start);
    const auto* particle_entry = dict_manager.lookupExact(particle, core::PartOfSpeech::Particle);
    if (particle_entry == nullptr || (particle_entry->extended_pos != core::ExtendedPOS::ParticleCase &&
                                      particle_entry->extended_pos != core::ExtendedPOS::ParticleTopic &&
                                      particle_entry->extended_pos != core::ExtendedPOS::ParticleConj)) {
      continue;
    }
    for (const auto& result : dict_manager.lookup(text, prefix_byte_end)) {
      if (result.entry == nullptr || result.entry->pos != core::PartOfSpeech::Adverb || result.length < 2) {
        continue;
      }
      if (candidate_end < split_pos + result.length) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace suzume::analysis
