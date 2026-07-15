#ifndef SUZUME_ANALYSIS_BIGRAM_TABLE_INTERNAL_H_
#define SUZUME_ANALYSIS_BIGRAM_TABLE_INTERNAL_H_

#include "bigram_table.h"

namespace suzume::analysis::bigram_rules {

using BigramMatrix = std::array<std::array<float, BigramTable::kSize>, BigramTable::kSize>;

inline void setCell(BigramMatrix& table, core::ExtendedPOS prev, core::ExtendedPOS next, float value) {
  table[static_cast<size_t>(prev)][static_cast<size_t>(next)] = value;
}

void setVerbAndAdjectiveCosts(BigramMatrix& table);
void setAuxiliaryAndNounCosts(BigramMatrix& table);
void setParticleAndLexicalCosts(BigramMatrix& table);

}  // namespace suzume::analysis::bigram_rules

#endif  // SUZUME_ANALYSIS_BIGRAM_TABLE_INTERNAL_H_
