#include <cassert>

#include "bigram_table_internal.h"

namespace suzume::analysis {

namespace bigram_rules {

void applyRules(BigramMatrix& table, const BigramRule* rules, size_t rule_count) {
  for (size_t rule_index = 0; rule_index < rule_count; ++rule_index) {
    const BigramRule& rule = rules[rule_index];
#ifndef NDEBUG
    assert(table[rule.prev][rule.next] == kUnsetCost && "duplicate ExtendedPOS bigram rule");
#endif
    table[rule.prev][rule.next] = rule.cost;
  }
}

}  // namespace bigram_rules

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
#ifndef NDEBUG
    row.fill(bigram_rules::kUnsetCost);
#else
    row.fill(bigram_rules::encodeCost(bigram_cost::kNeutral));
#endif
  }

  bigram_rules::setVerbAndAdjectiveCosts(table);
  bigram_rules::setAuxiliaryAndNounCosts(table);
  bigram_rules::setParticleAndLexicalCosts(table);
#ifndef NDEBUG
  for (auto& row : table) {
    for (uint8_t& encoded_cost : row) {
      if (encoded_cost == bigram_rules::kUnsetCost) {
        encoded_cost = bigram_rules::encodeCost(bigram_cost::kNeutral);
      }
    }
  }
#endif
  return table;
}

const BigramTable::EncodedTable BigramTable::table_ = BigramTable::initTable();

}  // namespace suzume::analysis
