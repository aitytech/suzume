#ifndef SUZUME_GRAMMAR_INFLECTION_SCORER_INTERNAL_H_
#define SUZUME_GRAMMAR_INFLECTION_SCORER_INTERNAL_H_

#include <string_view>

#include "core/debug.h"
#include "core/utf8_constants.h"
#include "inflection_scorer.h"

namespace suzume::grammar::inflection_score_detail {

struct InflectionScoreContext {
  VerbType type;
  std::string_view stem;
  size_t aux_total_len;
  size_t aux_count;
  uint16_t required_conn;
  size_t suffix_len;
  bool first_aux_starts_with_te_de;
  const InflectionScorerOptions* opts;
};

inline void logConfidenceAdjustment(float amount, [[maybe_unused]] const char* reason) {
  if (amount != 0.0F) {
    SUZUME_DEBUG_LOG_TRACE("  " << reason << ": " << (amount > 0 ? "+" : "") << amount << "\n");
  }
}

using ::utf8::equalsAny;

float scoreStemAndIchidan(float base, const InflectionScoreContext& context);
float scoreGodan(float base, const InflectionScoreContext& context);
float scoreAdjectiveAndForm(float base, const InflectionScoreContext& context);
float scorePotentialAndSuru(float base, const InflectionScoreContext& context);

}  // namespace suzume::grammar::inflection_score_detail

#endif  // SUZUME_GRAMMAR_INFLECTION_SCORER_INTERNAL_H_
