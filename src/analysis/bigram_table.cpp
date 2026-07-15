#include "bigram_table_internal.h"

namespace suzume::analysis {

float BigramTable::getCost(core::ExtendedPOS prev, core::ExtendedPOS next) {
  size_t prev_idx = static_cast<size_t>(prev);
  size_t next_idx = static_cast<size_t>(next);
  if (prev_idx >= kSize || next_idx >= kSize) {
    return 0.0F;
  }
  return table_[prev_idx][next_idx];
}

std::array<std::array<float, BigramTable::kSize>, BigramTable::kSize> BigramTable::initTable() {
  bigram_rules::BigramMatrix table{};
  for (auto& row : table) {
    row.fill(bigram_cost::kNeutral);
  }

  bigram_rules::setVerbAndAdjectiveCosts(table);
  bigram_rules::setAuxiliaryAndNounCosts(table);
  bigram_rules::setParticleAndLexicalCosts(table);
  return table;
}

const std::array<std::array<float, BigramTable::kSize>, BigramTable::kSize> BigramTable::table_ =
    BigramTable::initTable();

}  // namespace suzume::analysis
