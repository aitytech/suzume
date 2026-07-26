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

/**
 * @brief Expand explicitly typed dictionary source bases into lexical forms.
 *
 * Entries without an explicit verb or i-adjective conjugation marker are kept
 * as one literal surface. This preserves legacy CSV entries whose surface may
 * already be inflected.
 */
std::vector<dictionary::DictionaryEntry> expandDictionarySourceEntry(const dictionary::SourceEntry& source_entry);

/**
 * @brief Expand and surface-deduplicate a dictionary source batch.
 *
 * Non-conjugating entries are processed first so a nominal entry keeps
 * precedence over a colliding conjugated surface. For same-POS collisions the
 * entry with the longer lemma wins. The compiled dictionary format requires
 * surfaces to be unique.
 */
DictionaryExpansionResult expandDictionarySourceEntries(const std::vector<dictionary::SourceEntry>& source_entries);

}  // namespace grammar
}  // namespace suzume

#endif  // SUZUME_GRAMMAR_DICTIONARY_EXPANSION_H_
