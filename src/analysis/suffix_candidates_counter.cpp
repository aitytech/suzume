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

  // A numeral+counter phrase can modify an i-adjective in adverbial form
  // (百件|近く確認する, 三日|早く終える). Preserve the quantity boundary so
  // the generic kanji sequence cannot absorb the adjective's stem. The same
  // structural boundary is also valid when the following ～く is a verb
  // (十人|歩く), so no lexical adjective list is needed here.
  if (normalize::isCounterKanji(codepoints[numeral_end]) && numeral_end + 2 < char_types.size() &&
      char_types[numeral_end + 1] == normalize::CharType::Kanji && codepoints[numeral_end + 2] == U'く') {
    std::string surface = extractSubstring(codepoints, start_pos, numeral_end + 1);
    if (!surface.empty()) {
      auto cand = makeCandidate(surface, start_pos, numeral_end + 1, core::PartOfSpeech::Noun,
                                candidate::kCounterNounSplitBonus, false, CandidateOrigin::Counter,
                                core::ExtendedPOS::NounNumber);
      cand.lemma = surface;
#ifdef SUZUME_DEBUG_INFO
      cand.pattern = "counter_before_kanji_ku_split";
#endif
      candidates.push_back(cand);
    }
  }

  // A numeral+counter phrase before the independent comparison expression
  // 以上 is a compositional boundary (百倍|以上, 三名|以上).  The counter
  // candidate exists already, but discount this instance so a long unknown
  // kanji run cannot absorb the comparison term and a following predicate.
  if (numeral_end + 2 < codepoints.size() && normalize::isCounterKanji(codepoints[numeral_end]) &&
      codepoints[numeral_end + 1] == U'以' && codepoints[numeral_end + 2] == U'上') {
    std::string surface = extractSubstring(codepoints, start_pos, numeral_end + 1);
    if (!surface.empty()) {
      auto cand = makeCandidate(surface, start_pos, numeral_end + 1, core::PartOfSpeech::Noun,
                                candidate::kCounterComparisonSplitBonus, false, CandidateOrigin::Counter,
                                core::ExtendedPOS::NounNumber);
      cand.lemma = surface;
#ifdef SUZUME_DEBUG_INFO
      cand.pattern = "counter_comparison_split";
#endif
      candidates.push_back(cand);
    }
  }

  // Approximate count: numeral + 数 + counter (十数件, 百数名).  数 binds
  // directly to the following counter, while the leading cardinal remains a
  // separate search unit.  Requiring a counter after 数 excludes ordinary
  // lexical compounds beginning with 数.
  if (numeral_end + 1 < codepoints.size() && codepoints[numeral_end] == U'数' &&
      normalize::isCounterKanji(codepoints[numeral_end + 1])) {
    std::string surface = extractSubstring(codepoints, start_pos, numeral_end);
    if (!surface.empty()) {
      auto cand = makeCandidate(surface, start_pos, numeral_end, core::PartOfSpeech::Noun,
                                candidate::kApproximateNumeralSplitBonus, false, CandidateOrigin::Counter,
                                core::ExtendedPOS::NounNumber);
      cand.lemma = surface;
#ifdef SUZUME_DEBUG_INFO
      cand.pattern = "approximate_numeral_before_su_counter";
#endif
      candidates.push_back(cand);
    }
  }

  // Fraction: numeral + 分 + の + numeral (三分の一, 十分の三).  The
  // denominator marker requires both numeric sides, so duration phrases such
  // as 一分の休憩 never enter this branch.  Keep the complete fraction as one
  // quantity search unit, including when a following counter is present
  // (三分の一秒).
  if (numeral_end + 2 < codepoints.size() && codepoints[numeral_end] == U'分' && codepoints[numeral_end + 1] == U'の' &&
      normalize::isNumeralCodepoint(codepoints[numeral_end + 2])) {
    size_t denominator_end = numeral_end + 2;
    while (denominator_end < codepoints.size() && normalize::isNumeralCodepoint(codepoints[denominator_end])) {
      ++denominator_end;
    }
    std::string surface = extractSubstring(codepoints, start_pos, denominator_end);
    if (!surface.empty()) {
      auto cand =
          makeCandidate(surface, start_pos, denominator_end, core::PartOfSpeech::Noun, candidate::kFractionMergeCost,
                        false, CandidateOrigin::Counter, core::ExtendedPOS::NounNumber);
      cand.lemma = surface;
#ifdef SUZUME_DEBUG_INFO
      cand.pattern = "fraction_numerator_bun_no_denominator";
#endif
      candidates.push_back(cand);
    }
  }

  // A numeral+counter preceding a registered suffix is compositional even when
  // the suffix starts with kanji (二階|建て, 二本|立て).  Consult the suffix
  // lexicon rather than enumerating suffix spellings here, so every closed-class
  // suffix can share the same quantity boundary rule.
  if (dict_manager != nullptr && normalize::isCounterKanji(codepoints[numeral_end])) {
    size_t counter_end = numeral_end + 1;
    std::string suffix_text = extractSubstring(codepoints, counter_end, codepoints.size());
    bool suffix_follows = false;
    for (const auto& result : dict_manager->lookup(suffix_text, 0)) {
      if (result.entry != nullptr && result.entry->pos == core::PartOfSpeech::Suffix) {
        suffix_follows = true;
        break;
      }
    }
    if (suffix_follows) {
      std::string surface = extractSubstring(codepoints, start_pos, counter_end);
      if (!surface.empty()) {
        auto cand = makeCandidate(surface, start_pos, counter_end, core::PartOfSpeech::Noun,
                                  candidate::kCounterNounSplitBonus, false, CandidateOrigin::Counter);
        cand.lemma = surface;
#ifdef SUZUME_DEBUG_INFO
        cand.pattern = "counter_registered_suffix_split";
#endif
        candidates.push_back(cand);
      }
    }
  }

  // Check for counter suffix (つ for native counters)
  char32_t next = codepoints[numeral_end];
  if (next == U'つ') {
    // Generate counter candidate: Nつ
    std::string surface = extractSubstring(codepoints, start_pos, numeral_end + 1);
    if (!surface.empty()) {
      auto cand = makeCandidate(surface, start_pos, numeral_end + 1, core::PartOfSpeech::Noun,
                                candidate::kNativeTsuCounterBonus, false, CandidateOrigin::Counter);
#ifdef SUZUME_DEBUG_INFO
      cand.confidence = 0.95F;
      cand.pattern = "counter_tsu";
#endif
      candidates.push_back(cand);
    }
  }

  // Numeral(s) + a single kanji counter at a kanji→non-kanji boundary (三十度, 九十度,
  // 三十分, 十本) is a number+counter search unit. 度 also reads as a generic nominal
  // suffix (態度, 難易度), so its dictionary Suffix reading plus the suffix-stem split
  // pull a multi-digit numeral apart (三十|度); a discounted merged candidate keeps the
  // unit whole. Gated to a lone counter kanji followed by a non-kanji so a following
  // kanji noun/suffix (五度目, 五度見た, 三年間) keeps its own boundary.
  if (numeral_end < char_types.size() && normalize::isCounterKanji(codepoints[numeral_end]) &&
      (numeral_end + 1 >= char_types.size() || char_types[numeral_end + 1] != normalize::CharType::Kanji)) {
    std::string surface = extractSubstring(codepoints, start_pos, numeral_end + 1);
    if (!surface.empty()) {
      auto cand = makeCandidate(surface, start_pos, numeral_end + 1, core::PartOfSpeech::Noun,
                                candidate::kNumeralCounterMergeBonus, false, CandidateOrigin::Counter);
      cand.lemma = surface;
#ifdef SUZUME_DEBUG_INFO
      cand.pattern = "numeral_kanji_counter";
#endif
      candidates.push_back(cand);
    }
  }

  // A quantity followed by a kanji サ変名詞 keeps its counter boundary
  // (一回|実施する, 三名|確認する).  The ordinary lone-counter branch above
  // deliberately avoids a following kanji because lexical compounds such as
  // 一回戦 must remain available; requiring the complete nominal+する
  // predicate distinguishes the productive quantity construction.
  if (numeral_end < char_types.size() && normalize::isCounterKanji(codepoints[numeral_end])) {
    if (hasKanjiSuruPredicateAt(codepoints, char_types, numeral_end + 1)) {
      std::string surface = extractSubstring(codepoints, start_pos, numeral_end + 1);
      if (!surface.empty()) {
        auto cand = makeCandidate(surface, start_pos, numeral_end + 1, core::PartOfSpeech::Noun,
                                  candidate::kCounterNounSplitBonus, false, CandidateOrigin::Counter,
                                  core::ExtendedPOS::NounNumber);
        cand.lemma = surface;
#ifdef SUZUME_DEBUG_INFO
        cand.pattern = "counter_suru_predicate_split";
#endif
        candidates.push_back(cand);
      }
    }
  }

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
