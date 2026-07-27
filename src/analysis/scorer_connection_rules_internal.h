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

// Returns true for a conjunctive particle that can only complete the
// hypothetical/已然形 slot of a predicate (読め+ば, 飲め+ど, 読め+ども). Shared by
// the rule that rewards that inflection and by the focus-particle rule that has
// to refuse it, since a particle offers no inflected stem to complete.
bool isHypotheticalSelectingConjunctiveParticle(std::string_view surface);

}  // namespace suzume::analysis::connection_rules

#endif  // SUZUME_ANALYSIS_SCORER_CONNECTION_RULES_INTERNAL_H_
