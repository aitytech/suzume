#ifndef SUZUME_DICTIONARY_SOURCE_PARSER_H_
#define SUZUME_DICTIONARY_SOURCE_PARSER_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "core/types.h"
#include "dictionary/dictionary.h"

namespace suzume {
namespace dictionary {

/**
 * @brief One entry parsed from a dictionary source file.
 *
 * The source parser accepts the current TSV format and the legacy runtime CSV
 * formats. Per-entry cost and reading fields are accepted for compatibility but
 * intentionally discarded.
 */
struct SourceEntry {
  std::string surface;
  core::PartOfSpeech pos{core::PartOfSpeech::Noun};
  ConjugationType conj_type{ConjugationType::None};
  std::string lemma;
  size_t line_number{0};
};

struct SourceParseStats {
  size_t entries{0};
  size_t comment_lines{0};
  size_t empty_lines{0};
};

struct SourceParseResult {
  std::vector<SourceEntry> entries;
  SourceParseStats stats;
};

struct SourceParseOptions {
  // Legacy runtime CSV loading skipped records without a POS column. The CLI
  // compiler remains strict by using the default.
  bool skip_single_field_records{false};
};

/**
 * @brief Parse current TSV or legacy CSV dictionary source text.
 *
 * The delimiter is selected from the first data record. UTF-8 BOM, CRLF,
 * comments, surrounding ASCII whitespace, and RFC 4180 quoted fields
 * (including escaped quotes and embedded newlines) are supported.
 */
core::Expected<SourceParseResult, core::Error> parseDictionarySource(std::string_view content,
                                                                     SourceParseOptions options = {});

/**
 * @brief Parse one dictionary source record using an explicit line number.
 */
core::Expected<SourceEntry, core::Error> parseDictionarySourceLine(std::string_view line, size_t line_number);

/**
 * @brief Convert a parsed source entry to the runtime dictionary shape.
 */
DictionaryEntry sourceToDictionaryEntry(const SourceEntry& source_entry);

}  // namespace dictionary
}  // namespace suzume

#endif  // SUZUME_DICTIONARY_SOURCE_PARSER_H_
