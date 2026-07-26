#include "lattice.h"

#include <queue>

namespace suzume::core {

Lattice::Lattice(size_t text_length)
    : text_length_(text_length), edge_indices_by_start_(text_length + 1), edge_indices_by_end_(text_length + 1) {}

size_t Lattice::addEdge(std::string_view surface, uint32_t start, uint32_t end, PartOfSpeech pos, float cost,
                        uint8_t flags, std::string_view lemma, dictionary::ConjugationType conj_type,
                        CandidateOrigin origin, [[maybe_unused]] float origin_confidence,
                        [[maybe_unused]] std::string_view origin_detail, ExtendedPOS extended_pos,
                        [[maybe_unused]] std::string_view epos_source) {
  if (start >= end || end > text_length_ || !isValidPartOfSpeech(pos) || !isValidExtendedPos(extended_pos) ||
      all_edges_.size() >= kMaxEdges) {
    return static_cast<size_t>(-1);
  }

  // Store surface string
  surface_storage_.emplace_back(surface);
  std::string_view stored_surface = surface_storage_.back();

  // Store lemma if provided
  std::string_view stored_lemma;
  if (!lemma.empty()) {
    lemma_storage_.emplace_back(lemma);
    stored_lemma = lemma_storage_.back();
  }

#ifdef SUZUME_DEBUG_INFO
  // Store origin_detail if provided (debug only)
  std::string_view stored_origin_detail;
  if (!origin_detail.empty()) {
    origin_detail_storage_.emplace_back(origin_detail);
    stored_origin_detail = origin_detail_storage_.back();
  }
  // Store epos_source if provided (debug only)
  std::string_view stored_epos_source;
  if (!epos_source.empty()) {
    epos_source_storage_.emplace_back(epos_source);
    stored_epos_source = epos_source_storage_.back();
  }
#endif

  LatticeEdge edge;
  edge.id = static_cast<uint32_t>(all_edges_.size());
  edge.start = start;
  edge.end = end;
  edge.surface = stored_surface;
  edge.pos = pos;
  // Set extended_pos: use provided value if not Unknown, otherwise auto-detect
  // Track source for debug builds
#ifdef SUZUME_DEBUG_INFO
  const char* auto_epos_source = nullptr;
#endif
  if (extended_pos != ExtendedPOS::Unknown) {
    edge.extended_pos = extended_pos;
  } else if (pos == PartOfSpeech::Verb) {
    // Auto-detect verb form from surface
    edge.extended_pos = detectVerbForm(stored_surface, {});
#ifdef SUZUME_DEBUG_INFO
    auto_epos_source = "lattice_auto_verb";
#endif
  } else if (pos == PartOfSpeech::Adjective) {
    // Auto-detect adjective form from surface
    edge.extended_pos = detectAdjForm(stored_surface, conj_type == dictionary::ConjugationType::NaAdjective);
#ifdef SUZUME_DEBUG_INFO
    auto_epos_source = "lattice_auto_adj";
#endif
  } else {
    edge.extended_pos = posToExtendedPos(pos);
#ifdef SUZUME_DEBUG_INFO
    auto_epos_source = "lattice_default";
#endif
  }
  edge.cost = cost;
  edge.flags = static_cast<EdgeFlags>(flags);
  edge.lemma = stored_lemma;
  edge.conj_type = conj_type;
  edge.origin = origin;
#ifdef SUZUME_DEBUG_INFO
  edge.origin_confidence = origin_confidence;
  edge.origin_detail = stored_origin_detail;
  // Use provided epos_source if available, otherwise use auto-detected source
  edge.epos_source = !stored_epos_source.empty()
                         ? stored_epos_source
                         : (auto_epos_source ? std::string_view(auto_epos_source) : std::string_view{});
#endif

  all_edges_.push_back(edge);
  edge_indices_by_start_[start].push_back(edge.id);
  edge_indices_by_end_[end].push_back(edge.id);

  return edge.id;
}

std::vector<LatticeEdge> Lattice::edgesAt(size_t pos) const {
  if (pos >= edge_indices_by_start_.size()) {
    return {};
  }
  const auto& indices = edge_indices_by_start_[pos];
  std::vector<LatticeEdge> result;
  result.reserve(indices.size());
  for (uint32_t idx : indices) {
    result.push_back(all_edges_[idx]);
  }
  return result;
}

const std::vector<uint32_t>& Lattice::edgeIdsAt(size_t pos) const {
  static const std::vector<uint32_t> empty_ids;
  if (pos >= edge_indices_by_start_.size()) {
    return empty_ids;
  }
  return edge_indices_by_start_[pos];
}

const std::vector<uint32_t>& Lattice::edgeIdsEndingAt(size_t pos) const {
  static const std::vector<uint32_t> empty_ids;
  if (pos >= edge_indices_by_end_.size()) {
    return empty_ids;
  }
  return edge_indices_by_end_[pos];
}

const LatticeEdge& Lattice::getEdge(size_t edge_id) const {
  static const LatticeEdge empty_edge{};
  if (edge_id < all_edges_.size()) {
    return all_edges_[edge_id];
  }
  return empty_edge;
}

void Lattice::setEdgeCost(size_t edge_id, float cost) {
  if (edge_id >= all_edges_.size()) {
    return;
  }
  all_edges_[edge_id].cost = cost;
  all_edges_[edge_id].flags = all_edges_[edge_id].flags | EdgeFlags::HasCustomCost;
}

bool Lattice::isValid() const {
  if (text_length_ == 0) {
    return true;
  }

  // BFS to check if we can reach end from start
  std::vector<bool> reachable(text_length_ + 1, false);
  std::queue<size_t> que;
  que.push(0);
  reachable[0] = true;

  while (!que.empty()) {
    size_t pos = que.front();
    que.pop();

    for (uint32_t edge_id : edgeIdsAt(pos)) {
      const auto& edge = getEdge(edge_id);
      if (edge.end <= text_length_ && !reachable[edge.end]) {
        reachable[edge.end] = true;
        que.push(edge.end);
      }
    }
  }

  return reachable[text_length_];
}

void Lattice::clear() {
  for (auto& indices : edge_indices_by_start_) {
    indices.clear();
  }
  for (auto& indices : edge_indices_by_end_) {
    indices.clear();
  }
  all_edges_.clear();
  surface_storage_.clear();
  lemma_storage_.clear();
#ifdef SUZUME_DEBUG_INFO
  origin_detail_storage_.clear();
  epos_source_storage_.clear();
#endif
}

}  // namespace suzume::core
