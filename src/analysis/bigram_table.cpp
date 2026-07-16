#include "bigram_table_internal.h"

namespace suzume::analysis {

float BigramTable::getCost(core::ExtendedPOS prev, core::ExtendedPOS next) {
  size_t prev_idx = static_cast<size_t>(prev);
  size_t next_idx = static_cast<size_t>(next);
  if (prev_idx >= kSize || next_idx >= kSize) {
    return 0.0F;
  }
  return bigram_rules::decodeCost(table_[prev_idx][next_idx]);
}

BigramTable::EncodedTable BigramTable::initTable() {
  bigram_rules::BigramMatrix table{};
  for (auto& row : table) {
    row.fill(bigram_rules::encodeCost(bigram_cost::kNeutral));
  }

  bigram_rules::setVerbAndAdjectiveCosts(table);
  bigram_rules::setAuxiliaryAndNounCosts(table);
  bigram_rules::setParticleAndLexicalCosts(table);
  return table;
}

const BigramTable::EncodedTable BigramTable::table_ = BigramTable::initTable();

}  // namespace suzume::analysis
