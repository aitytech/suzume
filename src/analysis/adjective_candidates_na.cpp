/**
 * @file adjective_candidates_na.cpp
 * @brief Kanji na-adjective candidate generation
 */

#include <array>

#include "adjective_candidates.h"
#include "adjective_candidates_internal.h"
#include "analysis/candidate_constants.h"
#include "core/utf8_constants.h"
#include "normalize/char_type.h"
#include "normalize/exceptions.h"
#include "normalize/utf8.h"
#include "unknown.h"
#include "verb_candidates_helpers.h"

namespace suzume::analysis {

using adj_detail::makeNaAdjCandidate;
using verb_helpers::findCharRegionEnd;

namespace {

constexpr std::array<std::string_view, 1> kNaAdjSuffixes = {
    "的",
};

std::vector<UnknownCandidate> generateHiraganaNariNaAdjectiveCandidates(
    const std::vector<char32_t>& codepoints, size_t start_pos, const std::vector<normalize::CharType>& char_types) {
  std::vector<UnknownCandidate> candidates;
  if (start_pos >= char_types.size() || char_types[start_pos] != normalize::CharType::Hiragana) {
    return candidates;
  }

  // -やか/-らか are productive na-adjective endings.  Before a na-adjective
  // continuation, the whole hiragana stem is an adjective (すこやかなる,
  // あきらかに), not a sequence of short verb candidates.
  // Keep the bounded scan local to one adjective-sized word so an earlier
  // hiragana adverb cannot be absorbed into the stem.
  constexpr size_t kMaxHiraganaNaAdjectiveLength = 6;
  for (size_t stem_end = start_pos + 3;
       stem_end <= codepoints.size() && stem_end - start_pos <= kMaxHiraganaNaAdjectiveLength; ++stem_end) {
    if (char_types[stem_end - 1] != normalize::CharType::Hiragana || stem_end >= codepoints.size()) {
      continue;
    }
    const bool has_classical_attributive =
        stem_end + 2 <= codepoints.size() && extractSubstring(codepoints, stem_end, stem_end + 2) == "なる";
    const bool has_na_adjective_continuation = codepoints[stem_end] == U'に' || codepoints[stem_end] == U'な' ||
                                               codepoints[stem_end] == U'だ' || codepoints[stem_end] == U'さ';
    if (!has_classical_attributive && !has_na_adjective_continuation) {
      continue;
    }
    const std::string stem = extractSubstring(codepoints, start_pos, stem_end);
    if (!utf8::endsWithAny(stem, {"やか", "らか"})) {
      continue;
    }
    candidates.push_back(makeNaAdjCandidate(stem, start_pos, stem_end, candidate::kNaAdjYakaCost, true,
                                            CandidateOrigin::AdjectiveNa, candidate::kHiraganaNaAdjNariConfidence,
                                            "hira_na_adj_yaka_raka_nari"));
    return candidates;
  }
  return candidates;
}

}  // namespace

std::vector<UnknownCandidate> generateNaAdjectiveCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                                            const std::vector<normalize::CharType>& char_types,
                                                            const UnknownOptions& /*options*/) {
  std::vector<UnknownCandidate> candidates;

  if (start_pos >= char_types.size()) {
    return candidates;
  }
  if (char_types[start_pos] == normalize::CharType::Hiragana) {
    return generateHiraganaNariNaAdjectiveCandidates(codepoints, start_pos, char_types);
  }
  if (char_types[start_pos] != normalize::CharType::Kanji) {
    return candidates;
  }

  // Find kanji sequence (max 3 chars for na-adjectives: 獰猛, 不器用)
  constexpr size_t kMaxNaAdjKanjiLength = 3;
  size_t kanji_end = findCharRegionEnd(char_types, start_pos, kMaxNaAdjKanjiLength, normalize::CharType::Kanji);

  size_t kanji_len = kanji_end - start_pos;

  // Pattern 0: Kanji(1) + やか/らか + na-adjective inflection. These productive
  // derivatives can be followed by attributive な, adverbial に, or a copula
  // form (e.g., 華やかな, 明らかになる, 安らかだった).
  if (kanji_len == 1 && kanji_end < char_types.size() && char_types[kanji_end] == normalize::CharType::Hiragana) {
    size_t stem_end = kanji_end + 2;
    if (stem_end < codepoints.size()) {
      std::string stem_suffix = extractSubstring(codepoints, kanji_end, stem_end);
      bool is_yaka_pattern = utf8::equalsAny(stem_suffix, {"やか", "らか"});
      bool has_na_adj_continuation =
          codepoints[stem_end] == U'な' || codepoints[stem_end] == U'に' || codepoints[stem_end] == U'だ';
      if (is_yaka_pattern && has_na_adj_continuation) {
        std::string stem = extractSubstring(codepoints, start_pos, stem_end);
        candidates.push_back(makeNaAdjCandidate(stem, start_pos, stem_end, candidate::kNaAdjYakaCost, true,
                                                CandidateOrigin::AdjectiveNa, candidate::kHiraganaNaAdjNariConfidence,
                                                "na_adj_yaka_raka"));
        return candidates;
      }
    }
  }

  // Most productive patterns need at least two kanji.  A single-kanji stem
  // is also useful immediately before だ: the competing noun analysis stays
  // available, and the surrounding connection rules resolve the ambiguity.
  // A copular だ cannot be followed directly by る. That shape belongs to a
  // kanji-hiragana lexical noun such as 火+だるま, not an open-class
  // single-kanji na-adjective predicate.
  const bool single_kanji_before_copula = kanji_len == 1 && kanji_end < codepoints.size() &&
                                          codepoints[kanji_end] == U'だ' &&
                                          (kanji_end + 1 >= codepoints.size() || codepoints[kanji_end + 1] != U'る');
  if (kanji_len < 2 && !single_kanji_before_copula) {
    return candidates;
  }

  std::string kanji_seq = extractSubstring(codepoints, start_pos, kanji_end);

  // Pattern 1: Check for na-adjective suffixes (的)
  // Keep X+的 as one tokenizer search unit while preserving its na-adjective
  // class.  A bare noun path remains available for contexts that do not
  // license the derived adjective.
  for (const auto& suffix : kNaAdjSuffixes) {
    if (kanji_seq.size() >= suffix.size()) {
      std::string_view kanji_suffix(kanji_seq.data() + kanji_seq.size() - suffix.size(), suffix.size());
      if (kanji_suffix == suffix) {
        candidates.push_back(makeNaAdjCandidate(kanji_seq, start_pos, kanji_end, candidate::kNaAdjTekiCost, true,
                                                CandidateOrigin::AdjectiveNa, 1.0F, "na_adjective_teki"));
        break;
      }
    }
  }

  // Pattern 2: Check for kanji compound + na-adjective continuation (e.g., 獰猛な, 変だ).
  // A bare copula cannot license an arbitrary multi-kanji unknown: nominal
  // predicates such as 学生だ are much more common, and the noun candidate is
  // the grammatically neutral analysis. The one-kanji ambiguity remains
  // useful for open-class predicates such as 変だ.
  // A bare な licenses an attributive na-adjective stem, but なら does not:
  // nouns and na-adjectives both take conditional なら, so generating an
  // adjective for every unknown kanji compound would destroy that ambiguity.
  const bool followed_by_na = kanji_end < codepoints.size() && codepoints[kanji_end] == U'な' &&
                              (kanji_end + 1 >= codepoints.size() ||
                               (codepoints[kanji_end + 1] != U'ら' && codepoints[kanji_end + 1] != U'の'));
  const bool followed_by_sou =
      kanji_end + 1 < codepoints.size() && codepoints[kanji_end] == U'そ' && codepoints[kanji_end + 1] == U'う';
  if (followed_by_na || followed_by_sou || single_kanji_before_copula) {
    // Skip if first character is a formal noun (形式名詞)
    // e.g., 時妙な should be 時+妙な, not 時妙(ADJ)+な
    // Formal nouns (時, 事, 所, etc.) are standalone grammatical words
    std::string first_char_str;
    normalize::encodeUtf8(codepoints[start_pos], first_char_str);
    if (normalize::isFormalNounSurface(first_char_str)) {
      return candidates;
    }

    // Skip if kanji ends with 的 - MeCab splits as NOUN + 的(SUFFIX) + な
    // e.g., 論理的な should be 論理+的+な, not 論理的+な
    if (codepoints[kanji_end - 1] == U'的') {
      return candidates;
    }

    // Skip if な is followed by く/い/か — these indicate ない (auxiliary/adjective)
    // attached to the preceding noun, not a な-adjective stem.
    // Examples:
    //   私心なく → 私心 + ない連用 (not 私心(ADJ_NA) + く)
    //   仕方ない → 仕方 + ない (not 仕方(ADJ_NA) + い)
    //   関係なかった → 関係 + なかっ (か triggers naかった past form)
    // Real な-adjectives followed by these forms (静かなく) are not standard Japanese.
    if (followed_by_na && kanji_end + 1 < codepoints.size()) {
      char32_t after_na = codepoints[kanji_end + 1];
      if (after_na == U'く' || after_na == U'い' || after_na == U'か') {
        return candidates;
      }
    }

    // Found kanji compound + な - potential na-adjective stem
    // Cost similar to dictionary na-adjectives but with small penalty for unknown
    const float stem_cost =
        single_kanji_before_copula ? candidate::kNaAdjSingleKanjiCopulaCost : candidate::kNaAdjStemCost;
    candidates.push_back(makeNaAdjCandidate(kanji_seq, start_pos, kanji_end, stem_cost, true,
                                            CandidateOrigin::AdjectiveNa, 0.8F, "na_adjective_stem"));
  }

  return candidates;
}

}  // namespace suzume::analysis
