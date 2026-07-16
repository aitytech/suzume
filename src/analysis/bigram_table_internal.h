#ifndef SUZUME_ANALYSIS_BIGRAM_TABLE_INTERNAL_H_
#define SUZUME_ANALYSIS_BIGRAM_TABLE_INTERNAL_H_

#include "bigram_table.h"

namespace suzume::analysis::bigram_rules {

using BigramMatrix = std::array<std::array<uint8_t, BigramTable::kSize>, BigramTable::kSize>;

// Every value used by the grammatical rule tables. Keep this list compact:
// the dense matrix stores only an index into it.
inline constexpr std::array<float, 18> kCostPalette = {
    -2.5F,
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

inline void setCell(BigramMatrix& table, core::ExtendedPOS prev, core::ExtendedPOS next, float value) {
  table[static_cast<size_t>(prev)][static_cast<size_t>(next)] = encodeCost(value);
}

void setVerbAndAdjectiveCosts(BigramMatrix& table);
void setAuxiliaryAndNounCosts(BigramMatrix& table);
void setParticleAndLexicalCosts(BigramMatrix& table);

}  // namespace suzume::analysis::bigram_rules

#endif  // SUZUME_ANALYSIS_BIGRAM_TABLE_INTERNAL_H_
