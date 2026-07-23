#include "interactive_utils.h"

#include <cctype>

namespace suzume::cli {

std::string trim(std::string_view str) {
  size_t start = 0;
  while (start < str.size() && (std::isspace(str[start]) != 0)) {
    ++start;
  }
  size_t end = str.size();
  while (end > start && (std::isspace(str[end - 1]) != 0)) {
    --end;
  }
  return std::string(str.substr(start, end - start));
}

std::string toUpper(std::string_view str) {
  std::string result(str);
  for (char& chr : result) {
    chr = static_cast<char>(std::toupper(static_cast<unsigned char>(chr)));
  }
  return result;
}

std::string conjTypeToString(dictionary::ConjugationType conj_type) {
  return std::string(dictionary::conjTypeToCanonicalString(conj_type));
}

std::optional<dictionary::ConjugationType> parseConjType(const std::string& str) {
  auto conj_type = dictionary::conjTypeFromCanonical(str);
  if (!conj_type) {
    return std::nullopt;
  }
  // The interactive editor accepts only verb/adjective conjugation markers, not
  // interjection or proper-name markers.
  if (*conj_type == dictionary::ConjugationType::Interjection ||
      *conj_type == dictionary::ConjugationType::ProperFamily ||
      *conj_type == dictionary::ConjugationType::ProperGiven) {
    return std::nullopt;
  }
  return conj_type;
}

std::optional<core::PartOfSpeech> parsePos(const std::string& str) {
  return core::stringToPosStrict(str);
}

}  // namespace suzume::cli
