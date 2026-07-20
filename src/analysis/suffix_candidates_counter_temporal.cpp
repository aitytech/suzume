/**
 * @file suffix_candidates_counter_temporal.cpp
 * @brief Temporal quantity candidates for numeral-counter expressions
 */

#include <algorithm>

#include "candidate_constants.h"
#include "dictionary/dictionary.h"
#include "normalize/char_type.h"
#include "normalize/exceptions.h"
#include "suffix_candidates_counter_internal.h"
#include "tokenizer_utils.h"
#include "unknown.h"

namespace suzume::analysis::counter_detail {

void appendTemporalCounterCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                     const std::vector<normalize::CharType>& char_types,
                                     const dictionary::DictionaryManager* dict_manager,
                                     std::vector<UnknownCandidate>& candidates) {
  // Month counters admit all three common kana spellings between a numeral and
  // 月 (一か月, 一ヶ月, 一ケ月). Keep the complete duration together before
  // any following comparison expression.
  if (normalize::isNumeralCodepoint(codepoints[start_pos])) {
    size_t numeral_end = start_pos;
    while (numeral_end < codepoints.size() && normalize::isNumeralCodepoint(codepoints[numeral_end])) {
      ++numeral_end;
    }
    // 時間 is a lexicalized duration unit. The regular counter scan recognizes
    // 時 first, so retain a competing numeral+時+間 candidate for kanji
    // numerals as well as digit-based pretokenized durations.
    if (numeral_end + 1 < codepoints.size() && codepoints[numeral_end] == U'時' &&
        codepoints[numeral_end + 1] == U'間') {
      std::string surface = extractSubstring(codepoints, start_pos, numeral_end + 2);
      if (!surface.empty()) {
        auto cand = makeCandidate(surface, start_pos, numeral_end + 2, core::PartOfSpeech::Noun,
                                  candidate::kNumeralKanaMonthMergeBonus, false, CandidateOrigin::Counter,
                                  core::ExtendedPOS::NounNumber);
        cand.lemma = surface;
#ifdef SUZUME_DEBUG_INFO
        cand.pattern = "numeral_jikan_duration_merge";
#endif
        candidates.push_back(cand);
        if (numeral_end + 2 < codepoints.size() && codepoints[numeral_end + 2] == U'目') {
          std::string completed_surface = extractSubstring(codepoints, start_pos, numeral_end + 3);
          auto completed = makeCandidate(completed_surface, start_pos, numeral_end + 3, core::PartOfSpeech::Noun,
                                         candidate::kClosedTemporalCounterMergeBonus, false, CandidateOrigin::Counter,
                                         core::ExtendedPOS::NounNumber);
          completed.lemma = completed_surface;
#ifdef SUZUME_DEBUG_INFO
          completed.pattern = "temporal_counter_ordinal_merge";
#endif
          candidates.push_back(completed);
        }
      }
    }
    if (numeral_end + 1 < codepoints.size() &&
        (codepoints[numeral_end] == U'か' || codepoints[numeral_end] == U'ヶ' || codepoints[numeral_end] == U'ケ') &&
        codepoints[numeral_end + 1] == U'月') {
      std::string surface = extractSubstring(codepoints, start_pos, numeral_end + 2);
      if (!surface.empty()) {
        auto cand = makeCandidate(surface, start_pos, numeral_end + 2, core::PartOfSpeech::Noun,
                                  candidate::kNumeralKanaMonthMergeBonus, false, CandidateOrigin::Counter,
                                  core::ExtendedPOS::NounNumber);
        cand.lemma = surface;
#ifdef SUZUME_DEBUG_INFO
        cand.pattern = "numeral_kana_month_merge";
#endif
        candidates.push_back(cand);
        if (numeral_end + 2 < codepoints.size() && codepoints[numeral_end + 2] == U'間') {
          std::string completed_surface = extractSubstring(codepoints, start_pos, numeral_end + 3);
          auto completed = makeCandidate(completed_surface, start_pos, numeral_end + 3, core::PartOfSpeech::Noun,
                                         candidate::kClosedTemporalCounterMergeBonus, false, CandidateOrigin::Counter,
                                         core::ExtendedPOS::NounNumber);
          completed.lemma = completed_surface;
#ifdef SUZUME_DEBUG_INFO
          completed.pattern = "temporal_counter_span_merge";
#endif
          candidates.push_back(completed);
        }
      }
    }
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
    // A temporal counter run followed by a suffix that is always compositional:
    //   - 後/前 relation suffix (三日|後, 十年|前)
    //   - 半 "and a half" (三時間|半, 二年|半, 五分|半, 六ヶ月|半)
    // The 半 case excludes a run ending in bare 時, which keeps the clock reading
    // (三時半 = half past three), not a duration-plus-half.
    bool suffix_is_compositional = false;
    bool suffix_is_half = false;
    if (scan < codepoints.size()) {
      if (normalize::isTemporalRelationSuffixKanji(codepoints[scan])) {
        suffix_is_compositional = true;
      } else if (scan > 0 && codepoints[scan] == U'半' && codepoints[scan - 1] != U'時') {
        suffix_is_compositional = true;
        suffix_is_half = true;
      }
    }
    if (has_quantity && scan > counter_start && suffix_is_compositional) {
      std::string surface = extractSubstring(codepoints, start_pos, scan);
      if (!surface.empty()) {
        auto cand =
            makeCandidate(surface, start_pos, scan, core::PartOfSpeech::Noun, candidate::kCounterRelationSplitBonus,
                          false, CandidateOrigin::Counter, core::ExtendedPOS::NounNumber);
        cand.lemma = surface;
#ifdef SUZUME_DEBUG_INFO
        cand.pattern = "counter_relation_split";
#endif
        candidates.push_back(cand);
      }
      // Unlike 後/前 (single-kanji dict relation nouns), the split-off 半 only
      // exists as a generic kanji_seq NOUN, which the single-kanji-noun →
      // hiragana-verb compound protection penalizes before かかっ/すぎ etc.
      // Emit it as a NounNumber quantity token so that connection scoring can
      // recognize it as a legitimate pre-verb quantity (三時間|半|かかった).
      if (suffix_is_half) {
        std::string half_surface = extractSubstring(codepoints, scan, scan + 1);
        if (!half_surface.empty()) {
          auto half_cand =
              makeCandidate(half_surface, scan, scan + 1, core::PartOfSpeech::Noun, candidate::kCounterHalfSuffixCost,
                            false, CandidateOrigin::Counter, core::ExtendedPOS::NounNumber);
          half_cand.lemma = half_surface;
#ifdef SUZUME_DEBUG_INFO
          half_cand.pattern = "counter_half_suffix";
#endif
          candidates.push_back(half_cand);
        }
      }
    }
  }

  // A numeral followed by one or more temporal-unit kanji is a complete
  // quantity before a hiragana word or degree particle (一昼夜+かけて,
  // 二時間+待つ, 三時間+ほど). Emit the quantity boundary so an unknown
  // word candidate cannot absorb the final temporal kanji as its stem.
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
    size_t unit_start = scan;
    while (scan < codepoints.size()) {
      if (normalize::isTemporalCounterKanji(codepoints[scan])) {
        ++scan;
      } else if (codepoints[scan] == U'昼' && scan + 1 < codepoints.size() && codepoints[scan + 1] == U'夜') {
        // 昼夜 is one cyclic temporal unit only as a pair (一昼夜).
        scan += 2;
      } else {
        break;
      }
    }
    bool followed_by_hiragana = scan < char_types.size() && char_types[scan] == normalize::CharType::Hiragana;
    bool followed_by_quantity_particle = false;
    constexpr size_t kMaxQuantityParticleLength = 4;
    const size_t max_particle_end = std::min(codepoints.size(), scan + kMaxQuantityParticleLength);
    for (size_t particle_end = scan + 1; particle_end <= max_particle_end; ++particle_end) {
      const std::string particle_surface = extractSubstring(codepoints, scan, particle_end);
      const auto* particle = dict_manager->lookupExact(particle_surface, core::PartOfSpeech::Particle);
      if (particle != nullptr && particle->extended_pos == core::ExtendedPOS::ParticleAdverbial) {
        followed_by_quantity_particle = true;
        break;
      }
    }
    if (has_quantity && scan > unit_start && (followed_by_hiragana || followed_by_quantity_particle)) {
      std::string surface = extractSubstring(codepoints, start_pos, scan);
      if (!surface.empty()) {
        auto cand = makeCandidate(surface, start_pos, scan, core::PartOfSpeech::Noun, candidate::kCounterNounSplitBonus,
                                  false, CandidateOrigin::Counter, core::ExtendedPOS::NounNumber);
        cand.lemma = surface;
#ifdef SUZUME_DEBUG_INFO
        cand.pattern = "temporal_quantity_hiragana_split";
#endif
        candidates.push_back(cand);
      }
    }
  }

  // Duration span + independent kanji noun: a numeral-led temporal-counter run closed
  // by the span marker 間 (三年間, 三ヶ月間, 二時間) is a complete duration, and a kanji
  // noun immediately after 間 is a separate word (三年間|勉強, 三ヶ月間|入院, 二時間|睡眠).
  // The whole run is otherwise one kanji_seq token that beats the split on total cost, so
  // a discounted duplicate of the duration phrase lets the split path win. The trailing
  // kanji must be an ordinary noun char: a temporal counter (三日月 = one word), a
  // relation/span suffix (後/前/中/末, handled elsewhere), or a lone ordinal 目 (二時間目)
  // keeps its own reading. The interval member 隔 heads 間隔 (三年間隔 = 三年|間隔), so the
  // split falls BEFORE 間 instead. The run heads with a numeral or a quantity prefix
  // (数, 半, 何): a 間-closed duration does not merge with a following independent kanji
  // noun (数年間|海外) regardless of how its interior tokenizes — the split-after-間 here
  // only carves the following noun off; the 半年 vs 半|年 interior is decided elsewhere.
  {
    size_t scan = start_pos;
    bool has_quantity = false;
    if (scan < codepoints.size() && normalize::isQuantityPrefixKanji(codepoints[scan])) {
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
      if ((codepoints[scan] == U'ヶ' || codepoints[scan] == U'ケ') && scan + 1 < codepoints.size() &&
          normalize::isTemporalCounterKanji(codepoints[scan + 1])) {
        scan += 2;
        continue;
      }
      break;
    }
    // The run must end in 間, and that 間 must be preceded by another counter char in the
    // run (a bare numeral+間 is not a duration).
    bool run_ends_in_span =
        has_quantity && scan > counter_start && scan - 1 > counter_start && codepoints[scan - 1] == U'間';
    if (run_ends_in_span && scan < char_types.size() && char_types[scan] == normalize::CharType::Kanji) {
      // 間 heading the interval word 間隔 splits the numeral+counter off before 間
      // (三年|間隔); otherwise an ordinary kanji noun after 間 splits after it (三年間|勉強).
      // A lone ordinal 目 binds to the duration (二時間目 = one word); 目 heading a noun
      // still splits (五年間|目標, gate: 目 followed by a non-kanji).
      bool trailing_ordinal_me = codepoints[scan] == U'目' &&
                                 (scan + 1 >= char_types.size() || char_types[scan + 1] != normalize::CharType::Kanji);
      if (normalize::isIntervalCompoundSecondKanji(codepoints[scan])) {
        size_t split_end = scan - 1;  // before 間
        std::string surface = extractSubstring(codepoints, start_pos, split_end);
        if (!surface.empty()) {
          auto cand = makeCandidate(surface, start_pos, split_end, core::PartOfSpeech::Noun,
                                    candidate::kDurationSpanSplitBonus, false, CandidateOrigin::Counter);
          cand.lemma = surface;
#ifdef SUZUME_DEBUG_INFO
          cand.pattern = "duration_interval_split";
#endif
          candidates.push_back(cand);
        }
      } else if (!trailing_ordinal_me && !normalize::isTemporalCounterKanji(codepoints[scan]) &&
                 !normalize::isTemporalRelationSuffixKanji(codepoints[scan]) &&
                 !normalize::isTemporalSpanSuffixKanji(codepoints[scan])) {
        std::string surface = extractSubstring(codepoints, start_pos, scan);
        if (!surface.empty()) {
          auto cand = makeCandidate(surface, start_pos, scan, core::PartOfSpeech::Noun,
                                    candidate::kDurationSpanSplitBonus, false, CandidateOrigin::Counter);
          cand.lemma = surface;
#ifdef SUZUME_DEBUG_INFO
          cand.pattern = "duration_span_split";
#endif
          candidates.push_back(cand);
        }
      }
    }
  }
}

}  // namespace suzume::analysis::counter_detail
