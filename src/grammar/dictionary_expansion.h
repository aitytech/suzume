#ifndef SUZUME_GRAMMAR_DICTIONARY_EXPANSION_H_
#define SUZUME_GRAMMAR_DICTIONARY_EXPANSION_H_

#include <cstddef>
#include <vector>

#include "dictionary/dictionary.h"
#include "dictionary/source_parser.h"

namespace suzume {
namespace grammar {

struct DictionaryExpansionResult {
  std::vector<dictionary::DictionaryEntry> entries;
  size_t expanded_forms{0};
  size_t duplicates_skipped{0};
};

struct DictionaryExpansionOptions {
  // Runtime source dictionaries can retain grammatical homographs. The
  // current binary dictionary trie stores one record per surface, so its
  // compiler requests deterministic surface collapse and reports every loss.
  bool preserve_surface_homographs{true};
};

/**
 * @brief Expand explicitly typed dictionary source bases into lexical forms.
 *
 * Entries without an explicit verb or i-adjective conjugation marker are kept
 * as one literal surface. This preserves legacy CSV entries whose surface may
 * already be inflected.
 */
std::vector<dictionary::DictionaryEntry> expandDictionarySourceEntry(const dictionary::SourceEntry& source_entry);

/**
 * @brief Expand and exactly deduplicate a dictionary source batch.
 *
 * Non-conjugating entries are processed first to retain stable registration
 * order. Only identical (surface, POS, extended POS, lemma) tuples are removed;
 * grammatical homographs remain available to runtime source dictionaries.
 */
DictionaryExpansionResult expandDictionarySourceEntries(const std::vector<dictionary::SourceEntry>& source_entries,
                                                        DictionaryExpansionOptions options = {});

}  // namespace grammar
}  // namespace suzume

#endif  // SUZUME_GRAMMAR_DICTIONARY_EXPANSION_H_
