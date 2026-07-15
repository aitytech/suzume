#ifndef SUZUME_GRAMMAR_INFLECTION_SCORER_INTERNAL_H_
#define SUZUME_GRAMMAR_INFLECTION_SCORER_INTERNAL_H_

#include <string_view>

#include "core/debug.h"
#include "inflection_scorer.h"

namespace suzume::grammar::inflection_score_detail {

struct InflectionScoreContext {
  VerbType type;
  std::string_view stem;
  size_t aux_total_len;
  size_t aux_count;
  uint16_t required_conn;
  size_t suffix_len;
  const InflectionScorerOptions* opts;
};

inline void logConfidenceAdjustment(float amount, [[maybe_unused]] const char* reason) {
  if (amount != 0.0F) {
    SUZUME_DEBUG_LOG_TRACE("  " << reason << ": " << (amount > 0 ? "+" : "") << amount << "\n");
  }
}

template <size_t N>
bool isInArray(std::string_view value, const char* const (&values)[N]) {
  for (size_t idx = 0; idx < N; ++idx) {
    if (value == values[idx]) {
      return true;
    }
  }
  return false;
}

float scoreStemAndIchidan(float base, const InflectionScoreContext& context);
float scoreGodan(float base, const InflectionScoreContext& context);
float scoreAdjectiveAndForm(float base, const InflectionScoreContext& context);
float scorePotentialAndSuru(float base, const InflectionScoreContext& context);

}  // namespace suzume::grammar::inflection_score_detail

#endif  // SUZUME_GRAMMAR_INFLECTION_SCORER_INTERNAL_H_
