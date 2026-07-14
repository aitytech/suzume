/**
 * @file suffix_candidates_counter.cpp
 * @brief Suffix-based unknown word candidate generation
 */

#include "candidate_constants.h"
#include "core/debug.h"
#include "core/utf8_constants.h"
#include "dictionary/dictionary.h"
#include "grammar/char_patterns.h"
#include "grammar/conjugation.h"
#include "grammar/inflection.h"
#include "normalize/char_type.h"
#include "normalize/exceptions.h"
#include "normalize/utf8.h"
#include "suffix_candidates.h"
#include "tokenizer_utils.h"
#include "unknown.h"
#include "verb_candidates_helpers.h"

namespace suzume::analysis {

std::vector<UnknownCandidate> generateCounterCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                                        const std::vector<normalize::CharType>& char_types) {
  std::vector<UnknownCandidate> candidates;

  // Need at least 2 characters (numeral + counter suffix)
  if (start_pos + 1 >= codepoints.size()) {
    return candidates;
  }

  // Quantified time + relational suffix: split 後/前 off a numeral/quantity run that
  // ends in a temporal counter (三日|後, 十年|前, 数日|後, 半年|前). The whole run is
  // otherwise emitted as one kanji_seq token; the left counter token already exists
  // as a kanji_seq candidate, so a discounted duplicate lets the split path win. The
  // counter must be temporal, keeping lexical wholes on non-temporal counters intact
  // (一人前, not 一人|前).
  {
    size_t scan = start_pos;
    bool has_quantity = false;
    if (normalize::isQuantityPrefixKanji(codepoints[scan])) {
      ++scan;
      has_quantity = true;
    }
    while (scan < codepoints.size() && normalize::isNumeralCodepoint(codepoints[scan])) {
      ++scan;
      has_quantity = true;
    }
    size_t counter_start = scan;
    while (scan < codepoints.size()) {
      if (normalize::isTemporalCounterKanji(codepoints[scan])) {
        ++scan;
        continue;
      }
      // ヶ/ケ heads a counter only with a following kanji (ヶ月); take it as part of
      // the temporal run when that kanji is itself a temporal counter (三ヶ月|後).
      if ((codepoints[scan] == U'ヶ' || codepoints[scan] == U'ケ') && scan + 1 < codepoints.size() &&
          normalize::isTemporalCounterKanji(codepoints[scan + 1])) {
        scan += 2;
        continue;
      }
      break;
    }
    if (has_quantity && scan > counter_start && scan < codepoints.size() &&
        normalize::isTemporalRelationSuffixKanji(codepoints[scan])) {
      std::string surface = extractSubstring(codepoints, start_pos, scan);
      if (!surface.empty()) {
        auto cand = makeCandidate(surface, start_pos, scan, core::PartOfSpeech::Noun,
                                  candidate::kCounterRelationSplitBonus, false, CandidateOrigin::Counter);
        cand.lemma = surface;
#ifdef SUZUME_DEBUG_INFO
        cand.pattern = "counter_relation_split";
#endif
        candidates.push_back(cand);
      }
    }
  }

  // First character(s) must be numeral(s)
  if (!normalize::isNumeralCodepoint(codepoints[start_pos])) {
    return candidates;
  }

  // Find the end of the numeral sequence
  size_t numeral_end = start_pos;
  while (numeral_end < codepoints.size() && normalize::isNumeralCodepoint(codepoints[numeral_end])) {
    ++numeral_end;
  }

  // Must have at least one character after numerals
  if (numeral_end >= codepoints.size()) {
    return candidates;
  }

  // Check for counter suffix (つ for native counters)
  char32_t next = codepoints[numeral_end];
  if (next == U'つ') {
    // Generate counter candidate: Nつ
    std::string surface = extractSubstring(codepoints, start_pos, numeral_end + 1);
    if (!surface.empty()) {
      auto cand = makeCandidate(surface, start_pos, numeral_end + 1, core::PartOfSpeech::Noun, 0.0F, false,
                                CandidateOrigin::Counter);
#ifdef SUZUME_DEBUG_INFO
      cand.confidence = 0.95F;
      cand.pattern = "counter_tsu";
#endif
      candidates.push_back(cand);
    }
  }

  // Check for katakana unit suffix (e.g., キロ, ドル, メートル, パーセント)
  // Generate digit + katakana unit candidates like 3キロ, 100ドル, 80パーセント
  if (numeral_end < char_types.size() && char_types[numeral_end] == normalize::CharType::Katakana) {
    // Find end of katakana sequence (max 8 chars for reasonable unit length)
    size_t unit_end = findCharRegionEnd(char_types, numeral_end, 8, normalize::CharType::Katakana);

    // Generate candidate for digit + katakana unit
    size_t unit_len = unit_end - numeral_end;
    // ヶ/ケ alone is not a counter — extend to include following kanji
    // (ヶ月, ヶ所, ヶ国, ヶ年 etc.)
    if (unit_len == 1 && (codepoints[numeral_end] == U'ヶ' || codepoints[numeral_end] == U'ケ') &&
        unit_end < codepoints.size() && unit_end < char_types.size() &&
        char_types[unit_end] == normalize::CharType::Kanji) {
      // Extend unit_end to include the kanji after ヶ/ケ
      unit_end += 1;
      unit_len = unit_end - numeral_end;
    }
    if (unit_len >= 1) {  // unit_len <= 8 guaranteed by findCharRegionEnd
      std::string unit_surface = extractSubstring(codepoints, numeral_end, unit_end);
      // Any all-katakana run merges with the preceding numeral (3キロ, 100メダル);
      // MeCab treats number + katakana as one quantity token, so there is no
      // curated unit list. (ヶ/ケ + kanji surfaces are mixed-script and fall through.)
      if (!normalize::isAllKatakana(unit_surface)) {
        return candidates;
      }
      std::string surface = extractSubstring(codepoints, start_pos, unit_end);
      if (!surface.empty()) {
        // Penalize numbers starting with 0 (e.g., "00ポイント" is unnatural)
        // "0ドル" is fine, but "00ドル", "000キロ" are not typical Japanese patterns
        bool starts_with_zero_prefix = false;
        if (numeral_end - start_pos >= 2 && codepoints[start_pos] == U'0') {
          starts_with_zero_prefix = true;
        }
        // Give bonus to prefer combined token over split
        // Longer units get slightly more bonus (キロ, ドル vs キログラム, パーセント)
        // Strong bonus (-0.5) to beat optimal_length bonuses on split candidates
        float cost = starts_with_zero_prefix ? 2.0F  // Penalize unnatural zero-prefix numbers
                                             : -0.5F - (static_cast<float>(unit_len) * 0.05F);
        auto cand = makeCandidate(surface, start_pos, unit_end, core::PartOfSpeech::Noun, cost, false,
                                  CandidateOrigin::Counter);
#ifdef SUZUME_DEBUG_INFO
        cand.confidence = starts_with_zero_prefix ? 0.3F : 0.9F;
        cand.pattern = "numeric_unit_katakana";
#endif
        candidates.push_back(cand);
      }
    }
  }

  return candidates;
}

}  // namespace suzume::analysis
