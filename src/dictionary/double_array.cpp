#include "dictionary/double_array.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace suzume::dictionary {

namespace {

constexpr size_t kInitialSize = 8192;
constexpr size_t kBlockSize = 256;

inline uint8_t toByte(char chr) {
  return static_cast<uint8_t>(chr);
}

}  // namespace

// BuildState implementation
void DoubleArray::BuildState::resize(size_t new_size) {
  size_t old_size = units.size();
  units.resize(new_size);
  used.resize(new_size, false);

  for (size_t idx = old_size; idx < new_size; ++idx) {
    units[idx].base_or_value = 0;
    units[idx].check = 0;
  }
}

size_t DoubleArray::BuildState::findBase(const std::vector<uint8_t>& children) {
  if (children.empty()) {
    return 0;
  }

  size_t first_child = children[0];

  // Start searching from next_check_pos
  for (size_t base_cand = std::max(next_check_pos, first_child); base_cand < units.size() + kBlockSize; ++base_cand) {
    // Check if all children positions are available
    bool all_empty = true;
    for (uint8_t child : children) {
      size_t pos = base_cand ^ child;
      if (pos < units.size() && used[pos]) {
        all_empty = false;
        break;
      }
    }

    if (all_empty) {
      return base_cand;
    }
  }

  return units.size();
}

// DoubleArray implementation
DoubleArray::DoubleArray() = default;

bool DoubleArray::build(const std::vector<std::string>& keys, const std::vector<int32_t>& values) {
  if (keys.size() != values.size()) {
    return false;
  }

  for (int32_t value : values) {
    if (value < 0) {
      return false;
    }
  }
  return buildInternal(keys, &values);
}

bool DoubleArray::build(const std::vector<std::string>& keys) {
  if (keys.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
    return false;
  }
  return buildInternal(keys, nullptr);
}

bool DoubleArray::buildInternal(const std::vector<std::string>& keys, const std::vector<int32_t>* values) {
  if (keys.empty()) {
    clear();
    return true;
  }

  // Verify keys are sorted and unique
  for (size_t idx = 1; idx < keys.size(); ++idx) {
    if (keys[idx] <= keys[idx - 1]) {
      return false;
    }
  }
  // Initialize build state
  BuildState state;
  state.resize(kInitialSize);
  state.used[0] = true;

  // Build recursively starting from root
  try {
    buildRecursive(state, keys, values, 0, keys.size(), 0, 0);
  } catch (const std::exception&) {
    clear();
    return false;
  }

  // Transfer result
  units_ = std::move(state.units);

  // Shrink to fit
  size_t last_used = 0;
  for (size_t idx = units_.size(); idx > 0; --idx) {
    if (units_[idx - 1].check != 0 || units_[idx - 1].base_or_value != 0) {
      last_used = idx;
      break;
    }
  }
  if (last_used > 0) {
    units_.resize(last_used);
  }

  return true;
}

bool DoubleArray::build(const std::vector<std::string>& keys, const std::vector<uint32_t>& values) {
  std::vector<int32_t> signed_values(values.size());
  for (size_t idx = 0; idx < values.size(); ++idx) {
    if (values[idx] > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
      return false;
    }
    signed_values[idx] = static_cast<int32_t>(values[idx]);
  }
  return build(keys, signed_values);
}

void DoubleArray::buildRecursive(BuildState& state, const std::vector<std::string>& keys,
                                 const std::vector<int32_t>* values, size_t begin, size_t end, size_t depth,
                                 size_t parent_pos) {
  if (begin >= end) {
    return;
  }

  // Collect unique children at current depth
  std::vector<uint8_t> children;
  size_t leaf_begin = begin;
  size_t leaf_end = begin;

  // Find keys that terminate at this depth
  while (leaf_end < end && keys[leaf_end].size() == depth) {
    ++leaf_end;
  }

  // Collect children (including null terminator for leaves)
  if (leaf_end > leaf_begin) {
    children.push_back(0);  // Null terminator for leaf
  }

  // Collect other children
  uint8_t prev_char = 0;
  bool first = true;
  for (size_t idx = leaf_end; idx < end; ++idx) {
    uint8_t chr = toByte(keys[idx][depth]);
    if (first || chr != prev_char) {
      children.push_back(chr);
      prev_char = chr;
      first = false;
    }
  }

  if (children.empty()) {
    return;
  }

  // Find base value for all children
  size_t base_val = state.findBase(children);

  // Ensure array is large enough
  size_t max_pos = base_val;
  for (uint8_t child : children) {
    max_pos = std::max(max_pos, base_val ^ child);
  }
  if (max_pos >= state.units.size()) {
    state.resize(std::max(max_pos + kBlockSize, state.units.size() * 2));
  }

  // Set base value for parent node
  state.units[parent_pos].setBase(static_cast<uint32_t>(base_val));

  // First pass: mark all children as used (before recursion!)
  for (uint8_t chr : children) {
    size_t child_pos = base_val ^ chr;
    if (child_pos >= state.units.size()) {
      state.resize(child_pos + kBlockSize);
    }
    // Store parent_pos + 1 so that an unused cell (check == 0) is never mistaken
    // for a child of the root (parent_pos == 0). Lookups compare against
    // node_pos + 1 to match. Reserving 0 as the "no parent" sentinel is the only
    // way to disambiguate empty cells from root children in this XOR layout.
    state.units[child_pos].check = static_cast<uint32_t>(parent_pos + 1);
    state.used[child_pos] = true;
  }

  // Second pass: set values and recurse
  size_t child_idx = 0;

  // Handle leaf children (null terminator)
  if (leaf_end > leaf_begin) {
    size_t leaf_pos = base_val ^ 0;  // XOR with null
    const int32_t value = values == nullptr ? static_cast<int32_t>(leaf_begin) : (*values)[leaf_begin];
    state.units[leaf_pos].setLeaf(value);
    ++child_idx;
  }

  // Handle other children
  size_t range_begin = leaf_end;
  for (size_t cidx = child_idx; cidx < children.size(); ++cidx) {
    uint8_t chr = children[cidx];
    size_t child_pos = base_val ^ chr;

    // Find range for this child
    size_t range_end = range_begin;
    while (range_end < end && toByte(keys[range_end][depth]) == chr) {
      ++range_end;
    }

    // Recurse
    buildRecursive(state, keys, values, range_begin, range_end, depth + 1, child_pos);

    range_begin = range_end;
  }

  // Update next_check_pos for efficiency
  if (base_val >= state.next_check_pos) {
    state.next_check_pos = base_val + 1;
  }
}

bool DoubleArray::tryLeaf(size_t node_pos, int32_t& out_value) const {
  size_t base_val = units_[node_pos].base();
  size_t leaf_pos = base_val ^ 0;  // XOR with null terminator

  if (leaf_pos >= units_.size()) {
    return false;
  }

  if (units_[leaf_pos].check != node_pos + 1) {  // +1: 0 is the "no parent" sentinel
    return false;
  }

  if (!units_[leaf_pos].hasLeaf()) {
    return false;
  }

  out_value = units_[leaf_pos].value();
  return true;
}

bool DoubleArray::transition(size_t node_pos, uint8_t chr, size_t& next_pos) const {
  size_t base_val = units_[node_pos].base();
  size_t child_pos = base_val ^ chr;

  if (child_pos >= units_.size()) {
    return false;
  }

  if (units_[child_pos].check != node_pos + 1) {  // +1: 0 is the "no parent" sentinel
    return false;
  }

  next_pos = child_pos;
  return true;
}

int32_t DoubleArray::exactMatch(std::string_view key) const {
  if (units_.empty()) {
    return -1;
  }

  size_t node_pos = 0;

  for (char idx : key) {
    size_t next_pos = 0;
    if (!transition(node_pos, toByte(idx), next_pos)) {
      return -1;
    }
    node_pos = next_pos;
  }

  // Check for null terminator (leaf)
  int32_t value = 0;
  if (!tryLeaf(node_pos, value)) {
    return -1;
  }

  return value;
}

std::vector<DoubleArray::Result> DoubleArray::commonPrefixSearch(std::string_view text, size_t start,
                                                                 size_t max_results) const {
  std::vector<Result> results;

  if (units_.empty() || start >= text.size()) {
    return results;
  }

  size_t node_pos = 0;

  for (size_t idx = start; idx <= text.size(); ++idx) {
    // Check for null terminator (leaf) at current position
    int32_t value = 0;
    if (tryLeaf(node_pos, value)) {
      Result res{};
      res.value = value;
      res.length = idx - start;
      results.push_back(res);

      if (max_results > 0 && results.size() >= max_results) {
        return results;
      }
    }

    // End of text
    if (idx >= text.size()) {
      break;
    }

    // Transition to next node
    size_t next_pos = 0;
    if (!transition(node_pos, toByte(text[idx]), next_pos)) {
      break;
    }

    node_pos = next_pos;
  }

  return results;
}

void DoubleArray::clear() {
  units_.clear();
}

size_t DoubleArray::memoryUsage() const {
  return units_.size() * sizeof(Unit);
}

}  // namespace suzume::dictionary
