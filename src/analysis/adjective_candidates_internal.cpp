/**
 * @file adjective_candidates_internal.cpp
 * @brief Shared adjective candidate transformation helpers
 */

#include "adjective_candidates_internal.h"

#include <utility>

#include "analysis/candidate_constants.h"
#include "normalize/utf8.h"
#include "verb_candidates_helpers.h"

namespace suzume::analysis::adj_detail {

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
