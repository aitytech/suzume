#ifndef SUZUME_DICTIONARY_ENTRIES_ENTRY_SPEC_H_
#define SUZUME_DICTIONARY_ENTRIES_ENTRY_SPEC_H_

#include <cstddef>

#include "core/types.h"

namespace suzume::dictionary::entries {

// Trivial source representation for the built-in L1 dictionary. Keeping
// string literals as pointers lets all entry sets share one materialization
// loop instead of instantiating hundreds of std::string constructions.
struct EntrySpec {
  const char* surface;
  core::PartOfSpeech pos;
  core::ExtendedPOS extended_pos;
  const char* lemma;
};

class EntrySpecRange {
 public:
  constexpr EntrySpecRange(const EntrySpec* data, size_t size) : data_(data), size_(size) {}

  constexpr const EntrySpec* begin() const { return data_; }
  constexpr const EntrySpec* end() const { return data_ + size_; }
  constexpr size_t size() const { return size_; }

 private:
  const EntrySpec* data_;
  size_t size_;
};

template <size_t Size>
constexpr EntrySpecRange makeEntrySpecRange(const EntrySpec (&entries)[Size]) {
  return {entries, Size};
}

}  // namespace suzume::dictionary::entries

#endif  // SUZUME_DICTIONARY_ENTRIES_ENTRY_SPEC_H_
