#include "tsv_parser.h"

#include <algorithm>
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
  std::vector<TsvEntry> entries;
  entries_parsed_ = 0;
  comment_lines_ = 0;
  empty_lines_ = 0;
  error_lines_ = 0;

  size_t line_number = 0;
  size_t pos = 0;

  while (pos < content.size()) {
    ++line_number;

    // Find end of line
    size_t eol = content.find('\n', pos);
    if (eol == std::string_view::npos) {
      eol = content.size();
    }

    std::string_view line = content.substr(pos, eol - pos);
    if (line_number == 1 && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
        static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) {
      line.remove_prefix(3);
    }

    // Remove carriage return if present
    if (!line.empty() && line.back() == '\r') {
      line = line.substr(0, line.size() - 1);
    }

    // Skip empty lines
    if (line.empty() || line.find_first_not_of(" \t") == std::string_view::npos) {
      ++empty_lines_;
      pos = eol + 1;
      continue;
    }

    // Skip comments
    size_t first_char = line.find_first_not_of(" \t");
    if (first_char != std::string_view::npos && line[first_char] == '#') {
      ++comment_lines_;
      pos = eol + 1;
      continue;
    }

    // Parse line
    auto result = parseLine(line, line_number);
    if (result.hasValue()) {
      entries.push_back(std::move(result.value()));
      ++entries_parsed_;
    } else {
      ++error_lines_;
      // Return first error
      return core::makeUnexpected(result.error());
    }

    pos = eol + 1;
  }

  return entries;
}

core::Expected<TsvEntry, core::Error> TsvParser::parseLine(std::string_view line, size_t line_number) {
  TsvEntry entry;
  entry.line_number = line_number;

  // Split by tab
  std::vector<std::string_view> fields;
  size_t start = 0;
  while (start < line.size()) {
    size_t tab = line.find('\t', start);
    if (tab == std::string_view::npos) {
      fields.push_back(line.substr(start));
      break;
    }
    fields.push_back(line.substr(start, tab - start));
    start = tab + 1;
  }

  if (fields.empty()) {
    return core::makeUnexpected(
        core::Error(core::ErrorCode::ParseError, "Line " + std::to_string(line_number) + ": Empty line"));
  }

  // Field 0: surface (required)
  entry.surface = std::string(fields[0]);
  if (entry.surface.empty()) {
    return core::makeUnexpected(
        core::Error(core::ErrorCode::ParseError, "Line " + std::to_string(line_number) + ": Empty surface"));
  }

  // Field 1: POS (required)
  if (fields.size() < 2) {
    return core::makeUnexpected(
        core::Error(core::ErrorCode::ParseError, "Line " + std::to_string(line_number) + ": Missing POS field"));
  }

  // Check for INTERJECTION before parsing POS (to set conj_type marker)
  std::string_view pos_str = fields[1];
  size_t pos_start = pos_str.find_first_not_of(" \t");
  size_t pos_end = pos_str.find_last_not_of(" \t");
  if (pos_start != std::string_view::npos) {
    pos_str = pos_str.substr(pos_start, pos_end - pos_start + 1);
  }
  bool is_interjection = (pos_str == "INTJ" || pos_str == "INTERJECTION");

  auto pos_result = parsePos(fields[1], line_number);
  if (!pos_result.hasValue()) {
    return core::makeUnexpected(pos_result.error());
  }
  entry.pos = pos_result.value();

  // Set conj_type for interjections (used to assign ExtendedPOS::Interjection)
  if (is_interjection) {
    entry.conj_type = dictionary::ConjugationType::Interjection;
  }

  // Field 2: conj_type (optional, default None)
  // v0.8: simplified format - reading and cost removed
  if (fields.size() > 2 && !fields[2].empty()) {
    auto conj_result = parseConjType(fields[2], line_number);
    if (!conj_result.hasValue()) {
      return core::makeUnexpected(conj_result.error());
    }
    entry.conj_type = conj_result.value();
  }

  return entry;
}

core::Expected<core::PartOfSpeech, core::Error> TsvParser::parsePos(std::string_view str, size_t line) {
  // Trim whitespace
  size_t start = str.find_first_not_of(" \t");
  size_t end = str.find_last_not_of(" \t");
  if (start == std::string_view::npos) {
    return core::makeUnexpected(
        core::Error(core::ErrorCode::ParseError, "Line " + std::to_string(line) + ": Empty POS"));
  }
  str = str.substr(start, end - start + 1);

  // Map string to POS via the shared canonical parser (proper nouns use Noun
  // POS; FAMILY/GIVEN is carried in conj_type).
  auto pos = core::stringToPosStrict(str);
  if (!pos) {
    return core::makeUnexpected(
        core::Error(core::ErrorCode::ParseError, "Line " + std::to_string(line) + ": Invalid POS: " + std::string(str)));
  }
  return *pos;
}

core::Expected<dictionary::ConjugationType, core::Error> TsvParser::parseConjType(std::string_view str, size_t line) {
  // Trim whitespace
  size_t start = str.find_first_not_of(" \t");
  size_t end = str.find_last_not_of(" \t");
  if (start == std::string_view::npos) {
    return dictionary::ConjugationType::None;
  }
  str = str.substr(start, end - start + 1);

  // Interjection is carried via the POS field, not conj_type, so "INTJ" is not a
  // valid conjugation column here even though it is a canonical spelling.
  auto conj_type = dictionary::conjTypeFromCanonical(str);
  if (conj_type && *conj_type != dictionary::ConjugationType::Interjection) {
    return *conj_type;
  }

  return core::makeUnexpected(core::Error(
      core::ErrorCode::ParseError, "Line " + std::to_string(line) + ": Invalid conjugation type: " + std::string(str)));
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
  file << "# suzume dictionary source file (v0.8 format)\n";
  file << "# Format: surface<TAB>pos<TAB>conj_type\n";
  file << "\n";

  for (const auto& entry : entries) {
    file << entry.surface << "\t" << core::posToString(entry.pos);

    if (entry.conj_type != dictionary::ConjugationType::None) {
      file << "\t";
      // Only verb/adjective conjugation is serialized; interjection and
      // proper-name markers leave the conjugation column empty (v0.8 format).
      if (entry.conj_type != dictionary::ConjugationType::Interjection &&
          entry.conj_type != dictionary::ConjugationType::ProperFamily &&
          entry.conj_type != dictionary::ConjugationType::ProperGiven) {
        file << dictionary::conjTypeToCanonicalString(entry.conj_type);
      }
    }

    file << "\n";
  }

  return entries.size();
}

dictionary::DictionaryEntry tsvToDictEntry(const TsvEntry& tsv_entry) {
  dictionary::DictionaryEntry entry;
  entry.surface = tsv_entry.surface;
  entry.pos = tsv_entry.pos;
  // v0.8: cost removed
  entry.lemma = tsv_entry.surface;  // Default lemma to surface
  // v0.8: conj_type removed
  return entry;
}

}  // namespace suzume::cli
