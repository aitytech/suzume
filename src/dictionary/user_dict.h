#ifndef SUZUME_DICTIONARY_USER_DICT_H_
#define SUZUME_DICTIONARY_USER_DICT_H_

#include <memory>
#include <string>
#include <vector>

#include "core/error.h"
#include "dictionary/dictionary.h"
#include "dictionary/trie.h"

namespace suzume::dictionary {

/**
 * @brief User dictionary loaded at runtime
 *
 * Supports loading from file (native) or memory (WASM).
 * Accepts current source TSV and legacy surface,pos,cost,lemma CSV.
 */
class UserDictionary {
 public:
  UserDictionary();
  ~UserDictionary() = default;

  // Non-copyable, non-movable
  UserDictionary(const UserDictionary&) = delete;
  UserDictionary& operator=(const UserDictionary&) = delete;
  UserDictionary(UserDictionary&&) = delete;
  UserDictionary& operator=(UserDictionary&&) = delete;

  /**
   * @brief Load dictionary from file (native)
   * @param path File path
   * @return Number of entries on success, error on failure
   */
  core::Expected<size_t, core::Error> loadFromFile(const std::string& path);

  /**
   * @brief Load dictionary from memory (WASM)
   * @param data Pointer to dictionary source data
   * @param size Data size
   * @return Number of entries on success, error on failure
   */
  core::Expected<size_t, core::Error> loadFromMemory(const char* data, size_t size);

  /**
   * @brief Add a single entry
   * @param entry Entry to add
   * @note Not thread-safe. Do not call during concurrent reads.
   */
  void addEntry(const DictionaryEntry& entry);

  /**
   * @brief Lookup entries at position
   * @param text Text to search
   * @param start_pos Start position in bytes
   * @return Vector of lookup results
   */
  std::vector<LookupResult> lookup(std::string_view text, size_t start_pos) const;

  /**
   * @brief Look up the first exact-surface entry, optionally constrained by POS
   */
  const DictionaryEntry* lookupExact(std::string_view surface,
                                     core::PartOfSpeech pos = core::PartOfSpeech::Unknown) const;

  /**
   * @brief Get entry by ID
   * @param idx Entry ID
   * @return Entry pointer, or nullptr if not found
   */
  const DictionaryEntry* getEntry(uint32_t idx) const;

  /**
   * @brief Get number of entries
   */
  size_t size() const { return entries_.size(); }

  /**
   * @brief Clear all entries
   */
  void clear();

 private:
  std::vector<DictionaryEntry> entries_;
  Trie trie_;

  core::Expected<size_t, core::Error> parseSource(std::string_view source_data);
};

}  // namespace suzume::dictionary

#endif  // SUZUME_DICTIONARY_USER_DICT_H_
