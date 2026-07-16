#ifndef SUZUME_DICTIONARY_BINARY_DICT_H_
#define SUZUME_DICTIONARY_BINARY_DICT_H_

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "core/types.h"
#include "dictionary/dictionary.h"
#include "dictionary/double_array.h"

namespace suzume::dictionary {

// ConjugationType is now defined in dictionary.h

/**
 * @brief Binary dictionary header
 */
struct BinaryDictHeader {
  uint32_t magic;          // "SZMD" (0x444D5A53)
  uint16_t version_major;  // Major version (2 = compact format)
  uint16_t version_minor;  // Minor version
  uint32_t entry_count;    // Number of entries
  uint32_t trie_offset;    // Offset to trie data
  uint32_t trie_size;      // Size of trie data
  uint32_t entry_offset;   // Offset to entry array
  uint32_t string_offset;  // Offset to string pool
  uint32_t flags;          // v2.3 entry encoding; zero in older formats
  uint32_t checksum;       // CRC32 checksum (reserved)

  static constexpr uint32_t kMagic = 0x444D5A53;  // "SZMD"
  static constexpr uint16_t kVersionMajor = 2;
  static constexpr uint16_t kVersionMinor = 3;
  static constexpr uint32_t kEntryEncodingMask = 0x03;
  static constexpr uint32_t kGrammarOnlyEntries = 0x01;  // 1 byte/entry, no differing lemmas
  static constexpr uint32_t kPackedEntries = 0x02;       // 2 bytes/entry, 11-bit lemma + 5-bit grammar
  static constexpr uint32_t kWideEntries = 0x03;         // 3 bytes/entry, 16-bit lemma + 8-bit grammar
};

/**
 * @brief Legacy binary dictionary entry record v2.2 (8 bytes)
 *
 * Surface strings are encoded by the trie and reconstructed when loading, so
 * only a differing lemma needs to be retained in the string pool.
 */
struct BinaryDictEntry {
  uint32_t lemma_offset;  // Lemma offset (0 = same as surface)
  uint8_t lemma_length;   // Lemma byte length (0 = same as surface, max 255)
  uint8_t pos;            // Part of speech
  uint8_t extended_pos;   // Extended POS for fine-grained connection scoring
  uint8_t reserved;       // Reserved, must be zero
};

static_assert(sizeof(BinaryDictEntry) == 8, "BinaryDictEntry must remain an 8-byte v2.2 on-disk record");

/**
 * v2.3 stores a POS/ExtendedPOS palette followed by an adaptive one-, two-, or
 * three-byte entry array. The lemma table contains length-prefixed,
 * deduplicated strings. The header flags select the entry encoding.
 */
inline constexpr size_t kWideCompactEntrySize = 3;

/**
 * @brief Binary dictionary (read-only, memory-mapped friendly)
 *
 * File format:
 *   [Header]
 *   [Double-Array Trie]
 *   [Entry Array]
 *   [String Pool]
 */
class BinaryDictionary {
 public:
  BinaryDictionary();
  ~BinaryDictionary();

  /**
   * @brief Load dictionary from file
   * @param path File path
   * @return Number of entries on success, error on failure
   */
  core::Expected<size_t, core::Error> loadFromFile(const std::string& path);

  /**
   * @brief Load dictionary from memory (WASM compatible)
   * @param data Pointer to binary data
   * @param size Data size
   * @return Number of entries on success, error on failure
   */
  core::Expected<size_t, core::Error> loadFromMemory(const uint8_t* data, size_t size);

  /**
   * @brief Lookup entries at position
   */
  std::vector<LookupResult> lookup(std::string_view text, size_t start_pos) const;

  /**
   * @brief Look up an exact-surface entry, optionally constrained by POS
   */
  const DictionaryEntry* lookupExact(std::string_view surface,
                                     core::PartOfSpeech pos = core::PartOfSpeech::Unknown) const;

  /**
   * @brief Get entry by ID
   */
  const DictionaryEntry* getEntry(uint32_t idx) const;

  /**
   * @brief Get number of entries
   */
  size_t size() const { return entries_.size(); }

  /**
   * @brief Check if dictionary is loaded
   */
  bool isLoaded() const { return !entries_.empty(); }

 private:
  DoubleArray trie_;
  std::vector<DictionaryEntry> entries_;

  core::Expected<size_t, core::Error> parseData(const uint8_t* data, size_t size, DoubleArray& trie,
                                                std::vector<DictionaryEntry>& entries);
};

/**
 * @brief Binary dictionary writer (for compilation)
 */
class BinaryDictWriter {
 public:
  BinaryDictWriter();

  /**
   * @brief Add an entry
   * v0.8: conj_type parameter removed
   */
  void addEntry(const DictionaryEntry& entry);

  /**
   * @brief Replace an existing entry with the same surface
   */
  void replaceEntry(const DictionaryEntry& entry);

  /**
   * @brief Build and write to file
   * @param path Output file path
   * @return Number of bytes written on success, error on failure
   */
  core::Expected<size_t, core::Error> writeToFile(const std::string& path);

  /**
   * @brief Build and get binary data
   * @return Binary data
   */
  core::Expected<std::vector<uint8_t>, core::Error> build();

  /**
   * @brief Get number of entries
   */
  size_t size() const { return entries_.size(); }

 private:
  std::vector<DictionaryEntry> entries_;
};

}  // namespace suzume::dictionary

#endif  // SUZUME_DICTIONARY_BINARY_DICT_H_
