#ifndef SUZUME_ANALYSIS_BIGRAM_TABLE_INTERNAL_H_
#define SUZUME_ANALYSIS_BIGRAM_TABLE_INTERNAL_H_

#include "bigram_table.h"

namespace suzume::analysis::bigram_rules {

using BigramMatrix = std::array<std::array<uint8_t, BigramTable::kSize>, BigramTable::kSize>;

// Every value used by the grammatical rule tables. Keep this list compact:
// the dense matrix stores only an index into it.
inline constexpr std::array<float, 20> kCostPalette = {
    -2.5F,
    bigram_cost::kDoubleVeryStrongBonus,
    bigram_cost::kTripleVeryStrongBonus,
    bigram_cost::kExtremeBonus,
    bigram_cost::kVeryStrongBonus,
    bigram_cost::kExtraStrongBonus,
    bigram_cost::kStrongBonus,
    bigram_cost::kModerateBonus,
    bigram_cost::kMinorBonus,
    bigram_cost::kNeutral,
    bigram_cost::kNegligible,
    bigram_cost::kUncommon,
    bigram_cost::kMinor,
    bigram_cost::kRare,
    bigram_cost::kStrong,
    bigram_cost::kVeryRare,
    bigram_cost::kSevere,
    bigram_cost::kAlmostNever,
    bigram_cost::kNever,
    bigram_cost::kProhibitive,
};

constexpr uint8_t encodeCost(float value) {
  for (size_t index = 0; index < kCostPalette.size(); ++index) {
    if (kCostPalette[index] == value) {
      return static_cast<uint8_t>(index);
    }
  }
  return 7;  // kNeutral
}

inline float decodeCost(uint8_t index) {
  return index < kCostPalette.size() ? kCostPalette[index] : bigram_cost::kNeutral;
}

// Three encoded bytes replace hundreds of individually emitted table stores.
// Rules are expanded into the same dense matrix once at startup, preserving
// the hot getCost() lookup path while saving ~1.7 KB raw / ~0.3 KB gzip.
struct BigramRule {
  constexpr BigramRule(core::ExtendedPOS prev_value, core::ExtendedPOS next_value, float cost_value)
      : prev(static_cast<uint8_t>(prev_value)), next(static_cast<uint8_t>(next_value)), cost(encodeCost(cost_value)) {}

  uint8_t prev;
  uint8_t next;
  uint8_t cost;
};
static_assert(sizeof(BigramRule) == 3);

template <size_t RuleCount>
inline void applyRules(BigramMatrix& table, const BigramRule (&rules)[RuleCount]) {
  for (const BigramRule& rule : rules) {
    table[rule.prev][rule.next] = rule.cost;
  }
}

void setVerbAndAdjectiveCosts(BigramMatrix& table);
void setAuxiliaryAndNounCosts(BigramMatrix& table);
void setParticleAndLexicalCosts(BigramMatrix& table);

}  // namespace suzume::analysis::bigram_rules

#endif  // SUZUME_ANALYSIS_BIGRAM_TABLE_INTERNAL_H_
