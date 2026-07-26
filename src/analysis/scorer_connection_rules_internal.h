#ifndef SUZUME_ANALYSIS_SCORER_CONNECTION_RULES_INTERNAL_H_
#define SUZUME_ANALYSIS_SCORER_CONNECTION_RULES_INTERNAL_H_

#include <string_view>

#include "core/lattice.h"

namespace suzume::analysis::connection_rules {

// Returns true when surface is the Godan renyokei of the same lemma. Shared by
// the suffix and progressive/honorific connection-rule families.
bool isGodanRenyokeiOfLemma(std::string_view surface, std::string_view lemma);

// Returns true for a one-character hiragana continuative verb edge. Shared by
// the adverb and final-particle boundary rules.
bool isSingleHiraganaVerbRenyokei(const core::LatticeEdge& edge);

}  // namespace suzume::analysis::connection_rules

#endif  // SUZUME_ANALYSIS_SCORER_CONNECTION_RULES_INTERNAL_H_
