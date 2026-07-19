#ifndef SUZUME_ANALYSIS_SCORER_CONNECTION_RULES_INTERNAL_H_
#define SUZUME_ANALYSIS_SCORER_CONNECTION_RULES_INTERNAL_H_

#include <string_view>

namespace suzume::analysis::connection_rules {

// Returns true when surface is the Godan renyokei of the same lemma. Shared by
// the suffix and progressive/honorific connection-rule families.
bool isGodanRenyokeiOfLemma(std::string_view surface, std::string_view lemma);

}  // namespace suzume::analysis::connection_rules

#endif  // SUZUME_ANALYSIS_SCORER_CONNECTION_RULES_INTERNAL_H_
