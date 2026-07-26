#include "tsv_parser.h"

#include <fstream>
#include <set>
#include <sstream>

namespace suzume::cli {

TsvParser::TsvParser() = default;

core::Expected<std::vector<TsvEntry>, core::Error> TsvParser::parseFile(const std::string& path) {
  std::ifstream file(path);
  if (!file) {
    return core::makeUnexpected(core::Error(core::ErrorCode::FileNotFound, "Failed to open TSV file: " + path));
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  return parseString(buffer.str());
}

core::Expected<std::vector<TsvEntry>, core::Error> TsvParser::parseString(std::string_view content) {
  entries_parsed_ = 0;
  comment_lines_ = 0;
  empty_lines_ = 0;
  error_lines_ = 0;

  auto result = dictionary::parseDictionarySource(content);
  if (!result.hasValue()) {
    ++error_lines_;
    return core::makeUnexpected(result.error());
  }

  entries_parsed_ = result.value().stats.entries;
  comment_lines_ = result.value().stats.comment_lines;
  empty_lines_ = result.value().stats.empty_lines;
  return std::move(result.value().entries);
}

core::Expected<TsvEntry, core::Error> TsvParser::parseLine(std::string_view line, size_t line_number) {
  return dictionary::parseDictionarySourceLine(line, line_number);
}

size_t TsvParser::validate(const std::vector<TsvEntry>& entries, std::vector<std::string>* issues) {
  std::set<std::pair<std::string, core::PartOfSpeech>> seen;
  size_t issue_count = 0;

  for (const auto& entry : entries) {
    auto key = std::make_pair(entry.surface, entry.pos);
    if (seen.count(key) > 0) {
      ++issue_count;
      if (issues != nullptr) {
        std::string pos_str(core::posToString(entry.pos));
        issues->push_back("Duplicate entry at line " + std::to_string(entry.line_number) + ": " + entry.surface + " (" +
                          pos_str + ")");
      }
    }
    seen.insert(key);

    // Check conjugation type for verbs/adjectives
    if (entry.pos == core::PartOfSpeech::Verb || entry.pos == core::PartOfSpeech::Adjective) {
      if (entry.conj_type == dictionary::ConjugationType::None) {
        ++issue_count;
        if (issues != nullptr) {
          issues->push_back("Missing conjugation type at line " + std::to_string(entry.line_number) + ": " +
                            entry.surface);
        }
      }
    }
  }

  return issue_count;
}

core::Expected<size_t, core::Error> writeTsvFile(const std::string& path, const std::vector<TsvEntry>& entries) {
  std::ofstream file(path);
  if (!file) {
    return core::makeUnexpected(core::Error(core::ErrorCode::InternalError, "Failed to create file: " + path));
  }

  // Write header comment
  file << "# suzume dictionary source file\n";
  file << "# Format: surface<TAB>pos<TAB>conj_type<TAB>lemma\n";
  file << "\n";

  for (const auto& entry : entries) {
    file << entry.surface << "\t" << core::posToString(entry.pos);

    if (entry.conj_type != dictionary::ConjugationType::None || !entry.lemma.empty()) {
      file << "\t";
      // Interjection is represented canonically by its POS field. Every other
      // marker, including FAMILY/GIVEN, belongs in the conjugation column.
      if (entry.conj_type != dictionary::ConjugationType::Interjection) {
        file << dictionary::conjTypeToCanonicalString(entry.conj_type);
      }
    }
    if (!entry.lemma.empty()) {
      file << "\t" << entry.lemma;
    }

    file << "\n";
  }

  return entries.size();
}

}  // namespace suzume::cli
