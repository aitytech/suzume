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

namespace {

// Discrete-object counter kanji whose numeral+counter phrase is a pure quantity
// (三名, 二台, 五冊, 三箱) and never heads a lexical compound. Deliberately
// narrower than normalize::isCounterKanji: measure/rank/event counters (段, 本,
// 枚, 件, 頭, 級, …) head four-character lexical nouns (五段活用, 一本調子,
// 一枚看板, 一件落着, 三頭政治) and must keep merging with what follows.
bool isObjectCounterKanji(char32_t code_point) {
  switch (code_point) {
    case U'人':
    case U'名':
    case U'台':
    case U'冊':
    case U'箱':
    case U'袋':
    case U'匹':
    case U'個':
      return true;
    default:
      return false;
  }
}

}  // namespace

std::vector<UnknownCandidate> generateCounterCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                                        const std::vector<normalize::CharType>& char_types,
                                                        const dictionary::DictionaryManager* dict_manager) {
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
        auto cand = makeCandidate(surface, start_pos, scan, core::PartOfSpeech::Noun,
                                  candidate::kCounterRelationSplitBonus, false, CandidateOrigin::Counter);
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

  // Quantity + object counter + independent kanji noun: a numeral+counter phrase
  // followed by exactly two more kanji is compositional (三名|参加, 二台|故障,
  // 五冊|注文) — the counter phrase is a search-unit boundary. The whole run is
  // otherwise emitted as one kanji_seq token that beats the split on total cost,
  // so a discounted duplicate of the counter phrase lets the split path win.
  // Structural gates keep lexical wholes intact:
  //   - discrete-object counters only (isObjectCounterKanji above); measure/rank
  //     counters head lexical compounds and never fire here
  //   - exactly two trailing kanji ending the run: one trailing kanji is a
  //     lexical suffix compound (一人前, 一年生, 二階建て), three or more a
  //     longer lexical term (三人称単数, 二世帯住宅)
  //   - a numeral/quantity kanji heading the trailing pair marks a reduplicated
  //     idiom (十人十色, 一日千秋) and blocks the split
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
    if (has_quantity && scan < codepoints.size() && isObjectCounterKanji(codepoints[scan])) {
      size_t counter_end = scan + 1;
      bool trailing_two_kanji = counter_end + 1 < char_types.size() &&
                                char_types[counter_end] == normalize::CharType::Kanji &&
                                char_types[counter_end + 1] == normalize::CharType::Kanji;
      bool run_ends_after_pair =
          counter_end + 2 >= char_types.size() || char_types[counter_end + 2] != normalize::CharType::Kanji;
      bool trailing_is_reduplication =
          trailing_two_kanji && (normalize::isNumeralCodepoint(codepoints[counter_end]) ||
                                 normalize::isQuantityPrefixKanji(codepoints[counter_end]));
      if (trailing_two_kanji && run_ends_after_pair && !trailing_is_reduplication) {
        std::string surface = extractSubstring(codepoints, start_pos, counter_end);
        if (!surface.empty()) {
          auto cand = makeCandidate(surface, start_pos, counter_end, core::PartOfSpeech::Noun,
                                    candidate::kCounterNounSplitBonus, false, CandidateOrigin::Counter);
          cand.lemma = surface;
#ifdef SUZUME_DEBUG_INFO
          cand.pattern = "counter_object_split";
#endif
          candidates.push_back(cand);
        }
      }
    }
  }

  // Leading kanji noun/prefix + numeral(s) + counter: split before the numeral so the
  // numeral+counter search unit stays intact (徒歩|五分, 約|二時間, 気温|三十度,
  // 定員|五名). The whole run is otherwise one kanji_seq NOUN that beats the split on
  // total cost, so a discounted duplicate of the leading token lets the split win.
  // Structural gates keep lexical wholes intact: the leading run is either a numeric-
  // aggregation prefix (約/計/総) or 2+ kanji — a single non-prefix kanji heads a
  // lexical compound (中二階, 高三) — and the numeral must be immediately followed by a
  // counter kanji or a katakana unit (五メートル, 五キロ), so a bare numeral compound
  // (十字路, 百貨店, 世界一) or a kanji-run non-counter (東京五輪, 富士五湖) never fires.
  {
    size_t lead = start_pos;
    while (lead < codepoints.size() && lead < char_types.size() && char_types[lead] == normalize::CharType::Kanji &&
           !normalize::isNumeralCodepoint(codepoints[lead])) {
      ++lead;
    }
    size_t lead_len = lead - start_pos;
    bool lead_is_prefix = lead_len == 1 && normalize::isNumericApproxPrefixKanji(codepoints[start_pos]);
    if ((lead_len >= 2 || lead_is_prefix) && lead < codepoints.size() &&
        normalize::isNumeralCodepoint(codepoints[lead])) {
      size_t num_end = lead;
      while (num_end < codepoints.size() && normalize::isNumeralCodepoint(codepoints[num_end])) {
        ++num_end;
      }
      bool counter_follows = num_end < codepoints.size() &&
                             (normalize::isCounterKanji(codepoints[num_end]) ||
                              (num_end < char_types.size() && char_types[num_end] == normalize::CharType::Katakana));
      if (counter_follows) {
        std::string surface = extractSubstring(codepoints, start_pos, lead);
        if (!surface.empty()) {
          // An approximation prefix (約/計/総) is a Prefix modifying the quantity; a
          // multi-kanji leading run is an ordinary Noun.
          core::PartOfSpeech lead_pos = lead_is_prefix ? core::PartOfSpeech::Prefix : core::PartOfSpeech::Noun;
          core::ExtendedPOS lead_epos = lead_is_prefix ? core::ExtendedPOS::Prefix : core::ExtendedPOS::Unknown;
          auto cand = makeCandidate(surface, start_pos, lead, lead_pos, candidate::kLeadingNounCounterSplitBonus, false,
                                    CandidateOrigin::Counter, lead_epos);
          cand.lemma = surface;
#ifdef SUZUME_DEBUG_INFO
          cand.pattern = "leading_noun_counter_split";
#endif
          candidates.push_back(cand);
        }
      }
    }
  }

  // Approximate-quantity prefix + numeral run + counter: 数/何 modify the numeral
  // run they head (数十件, 何十回, 数百万円) and belong inside the quantity token.
  // The merged numeral+counter candidate below (十件) otherwise undercuts the whole
  // kanji_seq token (数十件) and strands the prefix (数|十件), so the same discounted
  // merge is emitted extended over the prefix. Gates mirror the plain merge: a lone
  // counter kanji at a kanji→non-kanji boundary. The prefix must be directly
  // followed by a numeral, so a prefix bound straight to a counter (数日, 半年,
  // 何回), a prefix heading an ordinary noun (数値, 数学), and the reverse pattern
  // (十数年: 数 binds the following 年, not the preceding numeral) never fire.
  if (normalize::isQuantityPrefixKanji(codepoints[start_pos]) &&
      normalize::isNumeralCodepoint(codepoints[start_pos + 1])) {
    size_t num_end = start_pos + 1;
    while (num_end < codepoints.size() && normalize::isNumeralCodepoint(codepoints[num_end])) {
      ++num_end;
    }
    bool lone_counter_at_boundary =
        num_end < char_types.size() && normalize::isCounterKanji(codepoints[num_end]) &&
        (num_end + 1 >= char_types.size() || char_types[num_end + 1] != normalize::CharType::Kanji);
    if (lone_counter_at_boundary) {
      std::string surface = extractSubstring(codepoints, start_pos, num_end + 1);
      if (!surface.empty()) {
        auto cand = makeCandidate(surface, start_pos, num_end + 1, core::PartOfSpeech::Noun,
                                  candidate::kNumeralCounterMergeBonus, false, CandidateOrigin::Counter);
        cand.lemma = surface;
#ifdef SUZUME_DEBUG_INFO
        cand.pattern = "quantity_prefix_counter_merge";
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
      auto cand = makeCandidate(surface, start_pos, numeral_end + 1, core::PartOfSpeech::Noun, 0.0F, false,
                                CandidateOrigin::Counter);
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
