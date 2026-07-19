/**
 * @file adjective_candidates_internal.cpp
 * @brief Shared adjective candidate transformation helpers
 */

#include "adjective_candidates_internal.h"

#include <algorithm>
#include <utility>

#include "analysis/candidate_constants.h"
#include "core/utf8_constants.h"
#include "normalize/utf8.h"
#include "verb_candidates_helpers.h"

namespace suzume::analysis::adj_detail {

namespace {

core::ExtendedPOS detectIAdjEpos(const std::string& surface) {
  if (utf8::endsWith(surface, "かっ")) {
    return core::ExtendedPOS::AdjKatt;
  }
  if (utf8::endsWith(surface, "けれ")) {
    return core::ExtendedPOS::AdjKeForm;
  }
  if (utf8::endsWith(surface, "かろ")) {
    return core::ExtendedPOS::AdjMizenkei;
  }
  if (utf8::endsWith(surface, "く")) {
    return core::ExtendedPOS::AdjRenyokei;
  }
  return core::ExtendedPOS::AdjBasic;
}

}  // namespace

const std::array<std::string_view, 14> kIAdjStemAuxPatterns = {
    "しそう", "しそうだ", "しそうな", "しそうに", "しすぎ", "しすぎる", "しすぎた",
    "きそう", "きそうだ", "きそうな", "きそうに", "きすぎ", "きすぎる", "きすぎた",
};

float firstConfidenceAtLeast(const std::vector<grammar::InflectionCandidate>& candidates, grammar::VerbType type,
                             float minimum) {
  for (const auto& candidate : candidates) {
    if (candidate.verb_type == type && candidate.confidence >= minimum) {
      return candidate.confidence;
    }
  }
  return float{};
}

float maxConfidenceFor(const std::vector<grammar::InflectionCandidate>& candidates,
                       std::initializer_list<grammar::VerbType> types) {
  float confidence{};
  for (const auto& candidate : candidates) {
    if (std::find(types.begin(), types.end(), candidate.verb_type) != types.end()) {
      confidence = std::max(confidence, candidate.confidence);
    }
  }
  return confidence;
}

UnknownCandidate makeIAdjCandidate(const std::string& surface, size_t start, size_t end, const std::string& lemma,
                                   float cost, [[maybe_unused]] CandidateOrigin origin,
                                   [[maybe_unused]] float confidence, [[maybe_unused]] const char* pattern) {
  auto candidate =
      makeCandidate(surface, start, end, core::PartOfSpeech::Adjective, cost, false, origin, detectIAdjEpos(surface));
  candidate.lemma = lemma;
#ifdef SUZUME_DEBUG_INFO
  candidate.confidence = confidence;
  candidate.pattern = pattern;
#endif
  return candidate;
}

UnknownCandidate makeIAdjStemCandidate(const std::string& surface, size_t start, size_t end, const std::string& lemma,
                                       float cost, [[maybe_unused]] CandidateOrigin origin,
                                       [[maybe_unused]] float confidence, [[maybe_unused]] const char* pattern) {
  auto candidate =
      makeCandidate(surface, start, end, core::PartOfSpeech::Adjective, cost, true, origin, core::ExtendedPOS::AdjStem);
  candidate.lemma = lemma;
#ifdef SUZUME_DEBUG_INFO
  candidate.confidence = confidence;
  candidate.pattern = pattern;
#endif
  return candidate;
}

UnknownCandidate makeTrimmedAdjVariant(const UnknownCandidate& candidate, size_t char_trim, float cost_bonus,
                                       core::ExtendedPOS epos, [[maybe_unused]] const char* pattern) {
  UnknownCandidate variant;
  variant.surface = candidate.surface.substr(0, candidate.surface.size() - char_trim * core::kJapaneseCharBytes);
  variant.start = candidate.start;
  variant.end = candidate.end - char_trim;
  variant.pos = core::PartOfSpeech::Adjective;
  variant.lemma = candidate.lemma;
  variant.cost = candidate.cost + cost_bonus;
  variant.has_suffix = true;
  variant.extended_pos = epos;
#ifdef SUZUME_DEBUG_INFO
  variant.origin = candidate.origin;
  variant.confidence = candidate.confidence;
  variant.pattern = pattern;
#endif
  return variant;
}

void appendTrimmedAdjVariants(std::vector<UnknownCandidate>& candidates, const TrimmedAdjVariantRule* rules,
                              size_t rule_count, const dictionary::DictionaryManager* dict_manager) {
  const size_t source_count = candidates.size();
  size_t group_begin = 0;
  while (group_begin < rule_count) {
    size_t group_end = group_begin + 1;
    while (group_end < rule_count && rules[group_end].group == rules[group_begin].group) {
      ++group_end;
    }
    for (size_t candidate_idx = 0; candidate_idx < source_count; ++candidate_idx) {
      for (size_t rule_idx = group_begin; rule_idx < group_end; ++rule_idx) {
        const TrimmedAdjVariantRule& rule = rules[rule_idx];
        const std::string& surface = candidates[candidate_idx].surface;
        if (!utf8::endsWith(surface, rule.suffix)) {
          continue;
        }
        if (rule.reject_contracted_n_past && utf8::endsWith(surface, "んかった")) {
          continue;
        }
        if (rule.require_nonempty_stem && surface.size() <= rule.char_trim * core::kJapaneseCharBytes) {
          continue;
        }

        UnknownCandidate variant =
            makeTrimmedAdjVariant(candidates[candidate_idx], rule.char_trim, rule.cost_bonus, rule.epos,
#ifdef SUZUME_DEBUG_INFO
                                  rule.pattern
#else
                                  nullptr
#endif
            );
        if (rule.prefer_dictionary_lemma && dict_manager != nullptr &&
            verb_helpers::isAdjectiveInDictionary(dict_manager, candidates[candidate_idx].lemma)) {
          variant.cost = candidate::verb_cost::kStrongBonus;
        }
        candidates.push_back(std::move(variant));
      }
    }
    group_begin = group_end;
  }
}

}  // namespace suzume::analysis::adj_detail
