#include "dictionary/double_array.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace suzume::dictionary {

namespace {

constexpr size_t kInitialSize = 8192;
constexpr size_t kBlockSize = 256;
constexpr size_t kSerializedHeaderSize = 8;
constexpr size_t kCompactRecordSize = 4;

inline uint8_t toByte(char chr) {
  return static_cast<uint8_t>(chr);
}

size_t bitmapByteSize(size_t bit_count) {
  return bit_count / 8 + (bit_count % 8 != 0 ? 1 : 0);
}

bool bitmapContains(const uint8_t* bitmap, size_t bit_idx) {
  return (bitmap[bit_idx / 8] & static_cast<uint8_t>(1U << (bit_idx % 8))) != 0;
}

void bitmapInsert(uint8_t* bitmap, size_t bit_idx) {
  bitmap[bit_idx / 8] |= static_cast<uint8_t>(1U << (bit_idx % 8));
}

uint16_t readUint16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(data[1] << 8U);
}

void writeUint16(uint8_t* data, uint16_t value) {
  data[0] = static_cast<uint8_t>(value & 0xFFU);
  data[1] = static_cast<uint8_t>(value >> 8U);
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
  for (int32_t value : values) {
    if (value < 0) {
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
                                 const std::vector<int32_t>& values, size_t begin, size_t end, size_t depth,
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
    state.units[leaf_pos].setLeaf(values[leaf_begin]);
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

bool DoubleArray::enumerate(std::vector<KeyValue>& key_values) const {
  std::vector<KeyValue> decoded;

  for (size_t leaf_pos = 0; leaf_pos < units_.size(); ++leaf_pos) {
    const Unit& leaf = units_[leaf_pos];
    if (!leaf.hasLeaf()) {
      continue;
    }

    if (leaf.check == 0) {
      return false;
    }
    size_t node_pos = static_cast<size_t>(leaf.check - 1);
    if (node_pos >= units_.size() || units_[node_pos].hasLeaf() || units_[node_pos].base() != leaf_pos) {
      return false;
    }

    std::string reversed_key;
    size_t steps = 0;
    while (node_pos != 0) {
      if (++steps > units_.size() || units_[node_pos].check == 0 || units_[node_pos].hasLeaf()) {
        return false;
      }

      size_t parent_pos = static_cast<size_t>(units_[node_pos].check - 1);
      if (parent_pos >= units_.size() || units_[parent_pos].hasLeaf()) {
        return false;
      }

      uint32_t label = units_[parent_pos].base() ^ static_cast<uint32_t>(node_pos);
      if (label == 0 || label > std::numeric_limits<uint8_t>::max()) {
        return false;
      }
      reversed_key.push_back(static_cast<char>(label));
      node_pos = parent_pos;
    }

    std::reverse(reversed_key.begin(), reversed_key.end());
    decoded.push_back({std::move(reversed_key), leaf.value()});
  }

  key_values = std::move(decoded);
  return true;
}

void DoubleArray::clear() {
  units_.clear();
}

size_t DoubleArray::memoryUsage() const {
  return units_.size() * sizeof(Unit);
}

std::vector<uint8_t> DoubleArray::serialize() const {
  // DA04 uses 16-bit fields and omits empty and leaf units. A terminal bitmap
  // stores leaf values alongside their parents; the leaf position and check are
  // reconstructed from the parent base. Fall back to fixed-width DA03 when a
  // field exceeds 16 bits.
  bool use_compact_format = units_.size() <= std::numeric_limits<uint16_t>::max();
  size_t internal_count = 0;
  size_t leaf_count = 0;
  if (use_compact_format) {
    for (const Unit& unit : units_) {
      if (unit.base_or_value == 0 && unit.check == 0) {
        continue;
      }
      if (unit.hasLeaf()) {
        ++leaf_count;
      } else {
        ++internal_count;
      }
      const uint32_t base_or_value = unit.hasLeaf() ? static_cast<uint32_t>(unit.value()) : unit.base();
      if (base_or_value > std::numeric_limits<uint16_t>::max() || unit.check > std::numeric_limits<uint16_t>::max()) {
        use_compact_format = false;
        break;
      }
    }
  }

  size_t terminal_count = 0;
  if (use_compact_format) {
    for (size_t unit_idx = 0; unit_idx < units_.size(); ++unit_idx) {
      const Unit& unit = units_[unit_idx];
      if ((unit.base_or_value == 0 && unit.check == 0) || unit.hasLeaf()) {
        continue;
      }
      const size_t leaf_pos = unit.base();
      if (leaf_pos < units_.size() && units_[leaf_pos].hasLeaf() && units_[leaf_pos].check == unit_idx + 1) {
        ++terminal_count;
      }
    }
    // Malformed DA03 input may contain an orphan leaf that cannot be represented
    // compactly. Preserve it through the lossless legacy format instead.
    use_compact_format = terminal_count == leaf_count;
  }

  const size_t num_units = units_.size();
  if (!use_compact_format) {
    std::vector<uint8_t> data(kSerializedHeaderSize + num_units * sizeof(Unit));
    data[0] = 'D';
    data[1] = 'A';
    data[2] = '0';
    data[3] = '3';
    const auto count = static_cast<uint32_t>(num_units);
    std::memcpy(data.data() + 4, &count, sizeof(count));
    std::memcpy(data.data() + kSerializedHeaderSize, units_.data(), num_units * sizeof(Unit));
    return data;
  }

  const size_t occupancy_size = bitmapByteSize(num_units);
  const size_t terminal_size = bitmapByteSize(internal_count);
  const size_t records_size = internal_count * kCompactRecordSize + terminal_count * sizeof(uint16_t);
  std::vector<uint8_t> data(kSerializedHeaderSize + occupancy_size + terminal_size + records_size, 0);
  data[0] = 'D';
  data[1] = 'A';
  data[2] = '0';
  data[3] = '4';
  const auto count = static_cast<uint32_t>(num_units);
  std::memcpy(data.data() + 4, &count, sizeof(count));

  uint8_t* occupancy = data.data() + kSerializedHeaderSize;
  uint8_t* terminal = occupancy + occupancy_size;
  uint8_t* records = terminal + terminal_size;
  size_t internal_idx = 0;
  size_t record_offset = 0;
  for (size_t unit_idx = 0; unit_idx < num_units; ++unit_idx) {
    const Unit& unit = units_[unit_idx];
    if ((unit.base_or_value == 0 && unit.check == 0) || unit.hasLeaf()) {
      continue;
    }
    bitmapInsert(occupancy, unit_idx);
    writeUint16(records + record_offset, static_cast<uint16_t>(unit.base()));
    record_offset += sizeof(uint16_t);
    writeUint16(records + record_offset, static_cast<uint16_t>(unit.check));
    record_offset += sizeof(uint16_t);

    const size_t leaf_pos = unit.base();
    if (leaf_pos < num_units && units_[leaf_pos].hasLeaf() && units_[leaf_pos].check == unit_idx + 1) {
      bitmapInsert(terminal, internal_idx);
      writeUint16(records + record_offset, static_cast<uint16_t>(units_[leaf_pos].value()));
      record_offset += sizeof(uint16_t);
    }
    ++internal_idx;
  }
  return data;
}

bool DoubleArray::deserialize(const uint8_t* data, size_t size) {
  if (data == nullptr) {
    return false;
  }

  if (size < kSerializedHeaderSize) {
    return false;
  }

  const bool is_da03 = data[0] == 'D' && data[1] == 'A' && data[2] == '0' && data[3] == '3';
  const bool is_da04 = data[0] == 'D' && data[1] == 'A' && data[2] == '0' && data[3] == '4';
  if (!is_da03 && !is_da04) {
    return false;
  }

  // Read number of units
  uint32_t num_units = 0;
  std::memcpy(&num_units, data + 4, 4);

  if (is_da03) {
    if (static_cast<size_t>(num_units) > (size - kSerializedHeaderSize) / sizeof(Unit)) {
      return false;
    }
    std::vector<Unit> loaded_units(num_units);
    std::memcpy(loaded_units.data(), data + kSerializedHeaderSize, static_cast<size_t>(num_units) * sizeof(Unit));
    units_ = std::move(loaded_units);
    return true;
  }

  if (num_units > std::numeric_limits<uint16_t>::max()) {
    return false;
  }
  const size_t occupancy_size = bitmapByteSize(num_units);
  if (occupancy_size > size - kSerializedHeaderSize) {
    return false;
  }
  const uint8_t* occupancy = data + kSerializedHeaderSize;
  size_t internal_count = 0;
  for (size_t unit_idx = 0; unit_idx < num_units; ++unit_idx) {
    if (bitmapContains(occupancy, unit_idx)) {
      ++internal_count;
    }
  }
  const size_t terminal_size = bitmapByteSize(internal_count);
  const size_t metadata_size = kSerializedHeaderSize + occupancy_size + terminal_size;
  if (metadata_size > size) {
    return false;
  }

  const uint8_t* terminal = occupancy + occupancy_size;
  size_t terminal_count = 0;
  for (size_t internal_idx = 0; internal_idx < internal_count; ++internal_idx) {
    terminal_count += bitmapContains(terminal, internal_idx) ? 1 : 0;
  }
  const size_t records_size = internal_count * kCompactRecordSize + terminal_count * sizeof(uint16_t);
  if (records_size != size - metadata_size) {
    return false;
  }

  const uint8_t* records = terminal + terminal_size;
  std::vector<Unit> loaded_units(num_units);
  size_t internal_idx = 0;
  size_t record_offset = 0;
  for (size_t unit_idx = 0; unit_idx < num_units; ++unit_idx) {
    if (!bitmapContains(occupancy, unit_idx)) {
      continue;
    }
    const uint16_t base = readUint16(records + record_offset);
    record_offset += sizeof(uint16_t);
    const uint16_t check = readUint16(records + record_offset);
    record_offset += sizeof(uint16_t);
    if (check > num_units) {
      return false;
    }
    loaded_units[unit_idx].base_or_value = base;
    loaded_units[unit_idx].check = check;

    if (bitmapContains(terminal, internal_idx)) {
      if (base >= num_units || bitmapContains(occupancy, base) || loaded_units[base].base_or_value != 0 ||
          loaded_units[base].check != 0) {
        return false;
      }
      loaded_units[base].setLeaf(readUint16(records + record_offset));
      loaded_units[base].check = static_cast<uint32_t>(unit_idx + 1);
      record_offset += sizeof(uint16_t);
    }
    ++internal_idx;
  }
  units_ = std::move(loaded_units);
  return true;
}

}  // namespace suzume::dictionary
