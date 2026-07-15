/**
 * @file suffix_candidates_prefix.cpp
 * @brief Suffix-based unknown word candidate generation
 */

#include <unordered_set>

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

// =============================================================================
// Prefix + Single Kanji Compound Candidates (接頭的複合語)
// =============================================================================

namespace {

// Prefix-like kanji that can form compounds with single kanji
// These are kanji that commonly appear at the start of temporal compounds
// Note: 本 excluded - too many non-prefix uses (本当, 本人, 本社, etc.)
// Note: 全/各/両/諸 excluded - require more context to determine compound boundary
const std::unordered_set<char32_t>& getPrefixLikeKanji() {
  static const std::unordered_set<char32_t> kPrefixKanji = {
      U'今',  // 今日, 今週, 今月, 今年, 今朝, 今晩, 今夜
      U'来',  // 来日, 来週, 来月, 来年
      U'先',  // 先日, 先週, 先月, 先年
      U'昨',  // 昨日, 昨年
      U'翌',  // 翌日, 翌週, 翌月, 翌年
      U'毎',  // 毎日, 毎週, 毎月, 毎年
  };
  return kPrefixKanji;
}

// Interrogative kanji that should NOT form compounds
// These act as strong anchors in the dictionary
const std::unordered_set<char32_t>& getInterrogativeKanji() {
  static const std::unordered_set<char32_t> kInterrogatives = {
      U'何',  // 何 (なに/なん) - what
      U'誰',  // 誰 (だれ) - who
      U'幾',  // 幾 (いく) - how many (幾つ, 幾日)
  };
  return kInterrogatives;
}

}  // namespace

bool isPrefixLikeKanji(char32_t cp) {
  const auto& prefix_kanji = getPrefixLikeKanji();
  return prefix_kanji.find(cp) != prefix_kanji.end();
}

bool isInterrogativeKanji(char32_t cp) {
  const auto& interrogatives = getInterrogativeKanji();
  return interrogatives.find(cp) != interrogatives.end();
}

std::vector<UnknownCandidate> generatePrefixCompoundCandidates(const std::vector<char32_t>& codepoints,
                                                               size_t start_pos,
                                                               const std::vector<normalize::CharType>& char_types,
                                                               const grammar::Inflection& inflection) {
  std::vector<UnknownCandidate> candidates;

  // Need at least 2 kanji characters
  if (start_pos + 1 >= codepoints.size()) {
    return candidates;
  }

  // First character must be kanji
  if (start_pos >= char_types.size() || char_types[start_pos] != normalize::CharType::Kanji) {
    return candidates;
  }

  // Check if first character is a prefix-like kanji
  char32_t first_char = codepoints[start_pos];
  const auto& prefix_kanji = getPrefixLikeKanji();
  if (prefix_kanji.find(first_char) == prefix_kanji.end()) {
    return candidates;
  }

  // A temporal prefix kanji heads a compound only at the start of a kanji run.
  // Preceded by another kanji these characters are overwhelmingly the TAIL of a
  // kango noun (将来/従来/以来, 優先, 一昨), so emitting the discounted prefix
  // compound mid-run would carve that noun apart (将|来性, 一|昨日).
  if (start_pos > 0 && char_types[start_pos - 1] == normalize::CharType::Kanji) {
    return candidates;
  }

  // Second character must also be kanji
  if (start_pos + 1 >= char_types.size() || char_types[start_pos + 1] != normalize::CharType::Kanji) {
    return candidates;
  }

  // Skip if second character is an interrogative (何, 誰, etc.)
  // These act as anchors and should not form compounds with prefix
  char32_t second_char = codepoints[start_pos + 1];
  const auto& interrogatives = getInterrogativeKanji();
  if (interrogatives.find(second_char) != interrogatives.end()) {
    return candidates;  // Don't generate compound, let dictionary anchor win
  }

  // Generate 2-character compound (prefix + single kanji) ONLY when:
  // - Not followed by more kanji, OR
  // - Followed by a temporal-span suffix kanji 中/末, which binds to the
  //   prefix-formed temporal noun (今月|中, 今月|末) rather than extending the
  //   kanji compound.
  // This prevents invalid splits like 翌営|業日 (should be 翌営業日)
  bool followed_by_kanji =
      (start_pos + 2 < char_types.size() && char_types[start_pos + 2] == normalize::CharType::Kanji);
  bool followed_by_span_suffix = (followed_by_kanji && start_pos + 2 < codepoints.size() &&
                                  normalize::isTemporalSpanSuffixKanji(codepoints[start_pos + 2]));

  // Suppress the compound when the second kanji heads a verb that continues into
  // the following hiragana. Temporal-unit kanji (日/週/月/年/朝/晩/夜) are never
  // verb stems, so this only fires for verb-stem kanji: 今食べてる must split as
  // 今|食べ|てる, not 今食|べてる. Probe the second kanji plus a growing hiragana
  // window (食べ → 食べる ichidan) since the full run (食べてる) may include a
  // colloquial aux the inflection analyzer cannot peel.
  if (!followed_by_kanji && start_pos + 2 < char_types.size() &&
      char_types[start_pos + 2] == normalize::CharType::Hiragana) {
    size_t hira_end = start_pos + 2;
    while (hira_end < char_types.size() && char_types[hira_end] == normalize::CharType::Hiragana) {
      ++hira_end;
    }
    for (size_t probe_end = start_pos + 3; probe_end <= hira_end; ++probe_end) {
      std::string verb_probe = extractSubstring(codepoints, start_pos + 1, probe_end);
      grammar::InflectionCandidate best = inflection.getBest(verb_probe);
      if (best.verb_type != grammar::VerbType::Unknown && best.confidence >= candidate::kPrefixCompoundVerbStemConf) {
        return candidates;
      }
    }
  }

  if (!followed_by_kanji || followed_by_span_suffix) {
    std::string surface = extractSubstring(codepoints, start_pos, start_pos + 2);
    if (!surface.empty()) {
      // Strong bonus to prefer compound over split
      // Must beat: single_kanji(1.4+2) + single_kanji(1.4+2) = 6.8
      // And compete with dictionary entries
      auto cand = makeCandidate(surface, start_pos, start_pos + 2, core::PartOfSpeech::Noun, -1.0F, false,
                                CandidateOrigin::PrefixCompound);
#ifdef SUZUME_DEBUG_INFO
      cand.confidence = 0.9F;
      cand.pattern = "prefix_single_kanji";
#endif
      candidates.push_back(cand);
    }
  }

  // Note: N中 compounds (今日中, 一日中, 世界中) are now split per MeCab:
  // 今日中 → 今日 + 中 (noun + suffix)
  // The 中 suffix is registered in L1 dictionary (entries.cpp)

  return candidates;
}

std::vector<UnknownCandidate> generateTemporalNounBoundaryCandidates(
    const std::vector<char32_t>& codepoints, size_t start_pos, const std::vector<normalize::CharType>& char_types) {
  std::vector<UnknownCandidate> candidates;

  // Need temporal 2-kanji + at least 2 more trailing kanji (gate against lexical
  // 1-kanji suffixes: 現在地/将来性 must stay whole).
  if (start_pos + 3 >= codepoints.size() || start_pos + 3 >= char_types.size()) {
    return candidates;
  }
  for (size_t offset = 0; offset < 4; ++offset) {
    if (char_types[start_pos + offset] != normalize::CharType::Kanji) {
      return candidates;
    }
  }

  if (!normalize::isTemporalAdverbialNounPair(codepoints[start_pos], codepoints[start_pos + 1])) {
    return candidates;
  }

  // The 3rd kanji being a span/relation suffix is handled elsewhere (今月|中/末,
  // …後/前) — don't compete there.
  if (normalize::isTemporalSpanSuffixKanji(codepoints[start_pos + 2]) ||
      normalize::isTemporalRelationSuffixKanji(codepoints[start_pos + 2])) {
    return candidates;
  }

  std::string surface = extractSubstring(codepoints, start_pos, start_pos + 2);
  if (!surface.empty()) {
    auto cand = makeCandidate(surface, start_pos, start_pos + 2, core::PartOfSpeech::Noun,
                              candidate::kTemporalNounBoundarySplitBonus, false, CandidateOrigin::PrefixCompound);
#ifdef SUZUME_DEBUG_INFO
    cand.confidence = candidate::kHighOriginConfidence;
    cand.pattern = "temporal_noun_boundary";
#endif
    candidates.push_back(cand);
  }

  return candidates;
}

}  // namespace suzume::analysis
