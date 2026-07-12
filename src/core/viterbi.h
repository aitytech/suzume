#ifndef SUZUME_CORE_VITERBI_H_
#define SUZUME_CORE_VITERBI_H_

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

// <iostream> is pulled in by debug.h only under SUZUME_DEBUG; the debug streaming
// below goes through SUZUME_DEBUG_STREAM, so no unconditional include here (keeps
// iostream out of the release/WASM build).
#include "debug.h"
#include "lattice.h"
#include "morpheme.h"

// Forward declaration for scorer interface
namespace suzume::analysis {
class IScorer;
}  // namespace suzume::analysis

namespace suzume::core {

inline constexpr size_t kNumExtendedPosTypes = static_cast<size_t>(ExtendedPOS::Count_);

// BOS (beginning-of-sentence) connection-cost adjustments. A morpheme that
// cannot naturally start a sentence is penalized; a conjunction is rewarded.
inline constexpr float kBosSuffixPenalty = 3.0F;         // Suffix cannot lead a sentence
inline constexpr float kBosConjunctionBonus = -0.5F;     // でも / しかし are natural at BOS
inline constexpr float kBosAppearanceSouPenalty = 0.5F;  // 様態そう should be demonstrative at BOS
inline constexpr float kBosAspectIkuPenalty = 1.0F;      // いく aspect needs a preceding て-form
inline constexpr float kBosTensePenalty = 2.0F;          // た/だ needs a preceding verb/adj stem
inline constexpr float kBosFinalParticlePenalty = 2.0F;  // Sentence-final particle cannot lead
// Per-transition tie-break: slightly prefer fewer, longer morphemes.
inline constexpr float kTransitionCost = 0.001F;

// Number of lowest-cost paths retained per (position, ExtendedPOS) state key.
// Connection rules branch on the predecessor's surface, so a single-best key
// would prune a surface that a downstream surface-conditional rule prefers.
inline constexpr size_t kStatesPerKey = 2;

// Sentinel edge ID marking the BOS state (no arriving edge).
inline constexpr uint32_t kBosEdgeId = std::numeric_limits<uint32_t>::max();

/**
 * @brief Viterbi result with path and cost
 */
struct ViterbiResult {
  std::vector<size_t> path;  // Edge IDs in order
  float total_cost{0.0F};    // Total path cost
};

/**
 * @brief Viterbi algorithm for finding optimal path
 */
class Viterbi {
 public:
  Viterbi() = default;
  ~Viterbi() = default;

  // Non-copyable, movable
  Viterbi(const Viterbi&) = delete;
  Viterbi& operator=(const Viterbi&) = delete;
  Viterbi(Viterbi&&) = default;
  Viterbi& operator=(Viterbi&&) = default;

  /**
   * @brief Solve with custom scorer (returns edge IDs)
   * @param lattice Lattice graph
   * @param scorer Custom scorer
   * @return ViterbiResult with path and cost
   */
  template <typename Scorer>
  ViterbiResult solve(const Lattice& lattice, const Scorer& scorer) const {
    ViterbiResult result;
    result.total_cost = 0.0F;

    const size_t text_len = lattice.textLength();
    if (text_len == 0) {
      return result;
    }

    // States are keyed on (position, ExtendedPOS). Each key retains up to
    // kStatesPerKey lowest-cost paths instead of a single one, so predecessors
    // with the same ExtendedPOS but different surfaces survive into the next
    // position's connectionCost (which branches on the predecessor's surface).
    // The cost of extending a path depends only on the edge it arrived with,
    // so per edge we insert only its cheapest predecessor. Entries within a
    // key are kept surface-distinct: among same-surface arrivals only the
    // cheapest path is retained, because the K-best diversity exists for
    // surface-conditional connection rules.
    struct StateEntry {
      float cost{std::numeric_limits<float>::max()};
      uint32_t edge_id{kBosEdgeId};  // Edge the path arrived with (kBosEdgeId = BOS)
      uint16_t prev_epos_idx{0};     // ExtendedPOS key of the predecessor state
      uint8_t prev_slot{0};          // K-best slot within the predecessor key
    };

    struct StateSlots {
      std::array<StateEntry, kStatesPerKey> entries{};
      uint8_t count{0};

      // Insert a candidate, keeping entries sorted by ascending cost and
      // dropping the most expensive one when the key is full. A candidate
      // whose surface matches an existing entry replaces it only if cheaper
      // (surface-distinct invariant). Slot indices of existing entries may
      // shift here; this is safe because every write to a position's keys
      // happens before any backpointer into them is recorded (the forward
      // pass is position-ordered and start < end).
      void insert(const Lattice& lattice, float cost, uint32_t edge_id, uint16_t prev_epos_idx, uint8_t prev_slot) {
        const std::string_view surface = lattice.getEdge(edge_id).surface;
        for (size_t existing = 0; existing < count; ++existing) {
          if (lattice.getEdge(entries[existing].edge_id).surface == surface) {
            if (cost >= entries[existing].cost) {
              return;
            }
            size_t dedup_slot = existing;
            while (dedup_slot > 0 && cost < entries[dedup_slot - 1].cost) {
              entries[dedup_slot] = entries[dedup_slot - 1];
              --dedup_slot;
            }
            entries[dedup_slot] = StateEntry{cost, edge_id, prev_epos_idx, prev_slot};
            return;
          }
        }
        size_t slot = count;
        if (slot == kStatesPerKey) {
          if (cost >= entries[kStatesPerKey - 1].cost) {
            return;
          }
          slot = kStatesPerKey - 1;
        } else {
          ++count;
        }
        while (slot > 0 && cost < entries[slot - 1].cost) {
          entries[slot] = entries[slot - 1];
          --slot;
        }
        entries[slot] = StateEntry{cost, edge_id, prev_epos_idx, prev_slot};
      }
    };

    // Pre-allocate for all positions + 1 (for final position)
    std::vector<std::array<StateSlots, kNumExtendedPosTypes>> states_by_pos(text_len + 1);

    // Initialize BOS state at position 0, ExtendedPOS=Unknown
    states_by_pos[0][static_cast<size_t>(ExtendedPOS::Unknown)].insert(lattice, 0.0F, kBosEdgeId, 0, 0);

    // Forward pass - process positions in order
    for (size_t pos = 0; pos < text_len; ++pos) {
      const auto& slots_at_pos = states_by_pos[pos];

      // Check if any valid state exists at this position
      bool has_valid_state = false;
      for (size_t i = 0; i < kNumExtendedPosTypes; ++i) {
        if (slots_at_pos[i].count > 0) {
          has_valid_state = true;
          break;
        }
      }
      if (!has_valid_state) {
        continue;
      }

      for (uint32_t edge_id : lattice.edgeIdsAt(pos)) {
        const auto& edge = lattice.getEdge(edge_id);
        // Bounds check - skip edges that go beyond text length
        if (edge.end > text_len) {
          continue;
        }
        const float word_cost = scorer.wordCost(edge);

        // Find the cheapest predecessor for this edge across all retained
        // states at this position.
        float best_total = std::numeric_limits<float>::max();
        uint16_t best_prev_epos = 0;
        uint8_t best_prev_slot = 0;

        for (size_t epos_idx = 0; epos_idx < kNumExtendedPosTypes; ++epos_idx) {
          const auto& slots = slots_at_pos[epos_idx];
          for (uint8_t slot = 0; slot < slots.count; ++slot) {
            const auto& entry = slots.entries[slot];

            float conn_cost = 0.0F;
            if (entry.edge_id != kBosEdgeId) {
              conn_cost = scorer.connectionCost(lattice.getEdge(entry.edge_id), edge);
            } else {
              // BOS (beginning of sentence) connection cost
              if (edge.pos == PartOfSpeech::Suffix) {
                conn_cost = kBosSuffixPenalty;
              }
              if (edge.pos == PartOfSpeech::Conjunction) {
                conn_cost = kBosConjunctionBonus;
              }
              // At BOS, そう should be a demonstrative na-adjective, not appearance aux
              // (e.g. "そうかもしれません").
              if (edge.extended_pos == ExtendedPOS::AuxAppearanceSou) {
                conn_cost += kBosAppearanceSouPenalty;
              }
              // いく aspect is only valid after a て-form (食べていく); at BOS it is the
              // verb 行く or part of a pronoun (いくつ).
              if (edge.extended_pos == ExtendedPOS::AuxAspectIku) {
                conn_cost += kBosAspectIkuPenalty;
              }
              if (edge.extended_pos == ExtendedPOS::AuxTenseTa) {
                conn_cost += kBosTensePenalty;
              }
              if (edge.extended_pos == ExtendedPOS::ParticleFinal) {
                conn_cost += kBosFinalParticlePenalty;
              }
            }

            const float total = entry.cost + word_cost + conn_cost + kTransitionCost;

            SUZUME_DEBUG_VERBOSE_BLOCK {
              SUZUME_DEBUG_STREAM << "[VITERBI] pos=" << pos << " \"" << edge.surface << "\" (" << posToString(edge.pos)
                                  << "/" << extendedPosToString(edge.extended_pos) << ")";
#ifdef SUZUME_DEBUG_INFO
              if (edge.origin != CandidateOrigin::Unknown) {
                SUZUME_DEBUG_STREAM << " [src:" << originToString(edge.origin);
                if (!edge.origin_detail.empty()) {
                  SUZUME_DEBUG_STREAM << "/" << edge.origin_detail;
                }
                SUZUME_DEBUG_STREAM << "]";
              }
#endif
              SUZUME_DEBUG_STREAM << " from " << extendedPosToString(static_cast<ExtendedPOS>(epos_idx)) << "#"
                                  << static_cast<int>(slot) << " word=" << word_cost << " conn=" << conn_cost
                                  << " total=" << total << "\n";
            }

            if (total < best_total) {
              best_total = total;
              best_prev_epos = static_cast<uint16_t>(epos_idx);
              best_prev_slot = slot;
            }
          }
        }

        if (best_total == std::numeric_limits<float>::max()) {
          continue;
        }

        size_t next_epos_idx = static_cast<size_t>(edge.extended_pos);
        if (next_epos_idx >= kNumExtendedPosTypes) {
          next_epos_idx = static_cast<size_t>(ExtendedPOS::Unknown);
        }
        states_by_pos[edge.end][next_epos_idx].insert(lattice, best_total, edge.id, best_prev_epos, best_prev_slot);
      }
    }

    // Find best and second-best states at final position
    size_t best_final_epos_idx = 0;
    uint8_t best_final_slot = 0;
    size_t second_final_epos_idx = 0;
    uint8_t second_final_slot = 0;
    float best_cost = std::numeric_limits<float>::max();
    float second_cost = std::numeric_limits<float>::max();

    const auto& final_states = states_by_pos[text_len];
    for (size_t i = 0; i < kNumExtendedPosTypes; ++i) {
      for (uint8_t slot = 0; slot < final_states[i].count; ++slot) {
        const float cost = final_states[i].entries[slot].cost;
        if (cost < best_cost) {
          second_cost = best_cost;
          second_final_epos_idx = best_final_epos_idx;
          second_final_slot = best_final_slot;
          best_cost = cost;
          best_final_epos_idx = i;
          best_final_slot = slot;
        } else if (cost < second_cost) {
          second_cost = cost;
          second_final_epos_idx = i;
          second_final_slot = slot;
        }
      }
    }
    (void)second_final_epos_idx;
    (void)second_final_slot;

    // Backtrack: follow the specific (ExtendedPOS, slot) entry that produced
    // each chosen edge.
    if (best_cost < std::numeric_limits<float>::max()) {
      result.total_cost = best_cost;
      size_t current_pos = text_len;
      size_t current_epos_idx = best_final_epos_idx;
      uint8_t current_slot = best_final_slot;

      while (current_pos > 0) {
        const auto& slots = states_by_pos[current_pos][current_epos_idx];
        if (current_slot >= slots.count) {
          break;
        }
        const auto& entry = slots.entries[current_slot];
        if (entry.edge_id == kBosEdgeId) {
          break;
        }

        result.path.push_back(entry.edge_id);
        current_pos = lattice.getEdge(entry.edge_id).start;
        current_epos_idx = entry.prev_epos_idx;
        current_slot = entry.prev_slot;
      }
      std::reverse(result.path.begin(), result.path.end());
    }

    // Debug: print final path and runner-up comparison
    SUZUME_DEBUG_IF(!result.path.empty()) {
      SUZUME_DEBUG_STREAM << "[VITERBI] Best path (cost=" << result.total_cost << "): ";
      for (size_t i = 0; i < result.path.size(); ++i) {
        const auto& edge = lattice.getEdge(result.path[i]);
        if (i > 0)
          SUZUME_DEBUG_STREAM << " → ";
        SUZUME_DEBUG_STREAM << "\"" << edge.surface << "\"(" << posToString(edge.pos) << "/"
                            << extendedPosToString(edge.extended_pos) << ")";
      }
      // Show margin over second-best if available
      if (second_cost < std::numeric_limits<float>::max() && second_cost != best_cost) {
        SUZUME_DEBUG_STREAM << " [margin=" << (second_cost - best_cost) << "]";
      }
      SUZUME_DEBUG_STREAM << "\n";

      // Show runner-up path at verbose level
      SUZUME_DEBUG_VERBOSE_BLOCK {
        if (second_cost < std::numeric_limits<float>::max() && second_cost != best_cost) {
          // Backtrack runner-up path
          std::vector<size_t> runner_up_path;
          size_t ru_pos = text_len;
          size_t ru_epos_idx = second_final_epos_idx;
          uint8_t ru_slot = second_final_slot;
          while (ru_pos > 0) {
            const auto& slots = states_by_pos[ru_pos][ru_epos_idx];
            if (ru_slot >= slots.count) {
              break;
            }
            const auto& entry = slots.entries[ru_slot];
            if (entry.edge_id == kBosEdgeId) {
              break;
            }
            runner_up_path.push_back(entry.edge_id);
            ru_pos = lattice.getEdge(entry.edge_id).start;
            ru_epos_idx = entry.prev_epos_idx;
            ru_slot = entry.prev_slot;
          }
          std::reverse(runner_up_path.begin(), runner_up_path.end());

          if (!runner_up_path.empty()) {
            SUZUME_DEBUG_STREAM << "[VITERBI] Runner-up (cost=" << second_cost << "): ";
            for (size_t i = 0; i < runner_up_path.size(); ++i) {
              const auto& edge = lattice.getEdge(runner_up_path[i]);
              if (i > 0)
                SUZUME_DEBUG_STREAM << " → ";
              SUZUME_DEBUG_STREAM << "\"" << edge.surface << "\"(" << posToString(edge.pos) << ")";
            }
            SUZUME_DEBUG_STREAM << "\n";
          }
        }
      }
    }

    return result;
  }
};

}  // namespace suzume::core

#endif  // SUZUME_CORE_VITERBI_H_
