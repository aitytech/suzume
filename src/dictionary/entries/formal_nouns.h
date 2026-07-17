#ifndef SUZUME_DICTIONARY_ENTRIES_FORMAL_NOUNS_H_
#define SUZUME_DICTIONARY_ENTRIES_FORMAL_NOUNS_H_

// =============================================================================
// Layer 1: Hardcoded Dictionary Entry (entries/*.h)
// =============================================================================
// Classification Criteria:
//   - CLOSED CLASS: Grammatically fixed set with known upper bound
//   - Rarely changes (tied to language structure, not vocabulary)
//   - Required for WASM minimal builds
//
// This file: Formal Nouns (形式名詞) - ~15 entries
//   - Grammaticalized nouns with function word behavior
//   - Examples: こと, もの, ため, ところ, よう, ほう, わけ, はず, etc.
//   - Typically excluded from tag generation (low semantic content)
//
// Registration Rules:
//   - Register kanji form with reading; hiragana is auto-generated
//   - For hiragana-only entries, leave reading empty
//   - Format: {surface, POS, cost, lemma, prefix, formal, low_info, conj, reading}
//
// DO NOT add regular nouns (山, 川, 車, etc.) here.
// For vocabulary, use Layer 2 (core.dic) or Layer 3 (user.dic).
// =============================================================================

#include "dictionary/entries/entry_spec.h"

namespace suzume::dictionary::entries {

/**
 * @brief Get formal noun entries for core dictionary
 *
 * Formal nouns are function words that look like nouns but have
 * grammatical functions. They are typically excluded from tags.
 *
 * @return Non-owning range of static formal noun entry specifications
 */
EntrySpecRange getFormalNounEntries();

}  // namespace suzume::dictionary::entries

#endif  // SUZUME_DICTIONARY_ENTRIES_FORMAL_NOUNS_H_
