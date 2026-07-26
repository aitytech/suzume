#include "analysis/scorer.h"
#include "analysis/scorer_constants.h"
#include "core/types.h"
#include "grammar/char_patterns.h"

namespace sc = suzume::analysis::scorer;

namespace suzume::analysis {

float Scorer::bosCost(const core::LatticeEdge& edge) const {
  return sc::getBoundaryCost(edge.extended_pos).bos;
}

float Scorer::eosCost(const core::LatticeEdge& edge) const {
  const sc::BoundaryCost boundary_cost = sc::getBoundaryCost(edge.extended_pos);

  switch (boundary_cost.eos_gate) {
    case sc::EosBoundaryGate::Always:
      return boundary_cost.eos;
    case sc::EosBoundaryGate::SingleCodepoint:
      // The bare renyokei き needs a following た/て/ます, while the
      // 終止形 くる/くれる legitimately ends a sentence.
      return edge.end - edge.start == 1 ? boundary_cost.eos : sc::scale::kNeutral;
    case sc::EosBoundaryGate::ListingParticle:
      return grammar::isListingParticleTariSurface(edge.surface) ? boundary_cost.eos : sc::scale::kNeutral;
  }

  return sc::scale::kNeutral;
}

}  // namespace suzume::analysis
