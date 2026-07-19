#ifndef SUZUME_DICTIONARY_ENTRIES_PACKED_CORE_ENTRIES_H_
#define SUZUME_DICTIONARY_ENTRIES_PACKED_CORE_ENTRIES_H_

#include <cstddef>
#include <cstdint>

namespace suzume::dictionary::entries {

// The generated WASM-only L1 representation keeps string data in one byte pool
// and replaces two relocated pointers per source record with 16-bit offsets.
struct PackedCoreEntry {
  uint16_t surface_offset;
  uint16_t lemma_offset;
  uint8_t pos;
  uint8_t extended_pos;
};
static_assert(sizeof(PackedCoreEntry) == 6);

class PackedCoreEntryRange {
 public:
  constexpr PackedCoreEntryRange(const PackedCoreEntry* data, size_t size, const uint8_t* string_data)
      : data_(data), size_(size), string_data_(string_data) {}

  constexpr const PackedCoreEntry* begin() const { return data_; }
  constexpr const PackedCoreEntry* end() const { return data_ + size_; }
  constexpr size_t size() const { return size_; }
  constexpr const uint8_t* stringData() const { return string_data_; }

 private:
  const PackedCoreEntry* data_;
  size_t size_;
  const uint8_t* string_data_;
};

PackedCoreEntryRange getPackedCoreEntries();

}  // namespace suzume::dictionary::entries

#endif  // SUZUME_DICTIONARY_ENTRIES_PACKED_CORE_ENTRIES_H_
