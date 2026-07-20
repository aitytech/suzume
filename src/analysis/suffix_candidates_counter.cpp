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
#include "suffix_candidates_counter_internal.h"
#include "tokenizer_utils.h"
#include "unknown.h"
#include "verb_candidates_helpers.h"

namespace suzume::analysis {

std::vector<UnknownCandidate> generateCounterCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                                        const std::vector<normalize::CharType>& char_types,
                                                        const dictionary::DictionaryManager* dict_manager) {
  std::vector<UnknownCandidate> candidates;

  // Need at least 2 characters (numeral + counter suffix)
  if (start_pos + 1 >= codepoints.size()) {
    return candidates;
  }

  counter_detail::appendStructuralCounterCandidates(codepoints, start_pos, char_types, dict_manager, candidates);

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

  counter_detail::appendBasicNumeralCounterCandidates(codepoints, start_pos, numeral_end, char_types, dict_manager,
                                                      candidates);

  // A number + katakana unit merges only when the numeral is written in (half- or
  // full-width) digits: 3キロ, 100ドル, ５センチ are one quantity token. A kanji
  // numeral before katakana (五センチ, 十キロメートル) is split at the natural
  // kanji→katakana boundary (五|センチ), matching MeCab, so it must not merge here.
  bool numeral_is_digits = true;
  for (size_t idx = start_pos; idx < numeral_end; ++idx) {
    if (char_types[idx] != normalize::CharType::Digit) {
      numeral_is_digits = false;
      break;
    }
  }

  // Check for katakana unit suffix (e.g., キロ, ドル, メートル, パーセント)
  // Generate digit + katakana unit candidates like 3キロ, 100ドル, 80パーセント
  if (numeral_is_digits && numeral_end < char_types.size() &&
      char_types[numeral_end] == normalize::CharType::Katakana) {
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
