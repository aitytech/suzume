#include "dictionary/dictionary.h"

#include <array>
#include <cstdlib>
#include <iterator>
#ifndef __EMSCRIPTEN__
#include <filesystem>
#endif

#include "dictionary/binary_dict.h"
#include "dictionary/core_dict.h"
#include "dictionary/user_dict.h"

namespace suzume::dictionary {

namespace {

// Single source of truth for conjugation-type spellings.
//   canonical  - short SCREAMING_SNAKE used in TSV/CLI output ("" for None)
//   pascal     - PascalCase enum name
//   screaming  - long SCREAMING_SNAKE alias (== canonical except None/INTJ/
//                FAMILY/GIVEN, which spell out NONE/INTERJECTION/PROPER_*)
struct ConjTypeAlias {
  ConjugationType type;
  std::string_view canonical;
  std::string_view pascal;
  std::string_view screaming;
};

constexpr std::array<ConjTypeAlias, 18> kConjTypeAliases = {{
    {ConjugationType::None, "", "None", "NONE"},
    {ConjugationType::Ichidan, "ICHIDAN", "Ichidan", "ICHIDAN"},
    {ConjugationType::GodanKa, "GODAN_KA", "GodanKa", "GODAN_KA"},
    {ConjugationType::GodanGa, "GODAN_GA", "GodanGa", "GODAN_GA"},
    {ConjugationType::GodanSa, "GODAN_SA", "GodanSa", "GODAN_SA"},
    {ConjugationType::GodanTa, "GODAN_TA", "GodanTa", "GODAN_TA"},
    {ConjugationType::GodanNa, "GODAN_NA", "GodanNa", "GODAN_NA"},
    {ConjugationType::GodanBa, "GODAN_BA", "GodanBa", "GODAN_BA"},
    {ConjugationType::GodanMa, "GODAN_MA", "GodanMa", "GODAN_MA"},
    {ConjugationType::GodanRa, "GODAN_RA", "GodanRa", "GODAN_RA"},
    {ConjugationType::GodanWa, "GODAN_WA", "GodanWa", "GODAN_WA"},
    {ConjugationType::Suru, "SURU", "Suru", "SURU"},
    {ConjugationType::Kuru, "KURU", "Kuru", "KURU"},
    {ConjugationType::IAdjective, "I_ADJ", "IAdjective", "I_ADJ"},
    {ConjugationType::NaAdjective, "NA_ADJ", "NaAdjective", "NA_ADJ"},
    {ConjugationType::Interjection, "INTJ", "Interjection", "INTERJECTION"},
    {ConjugationType::ProperFamily, "FAMILY", "ProperFamily", "PROPER_FAMILY"},
    {ConjugationType::ProperGiven, "GIVEN", "ProperGiven", "PROPER_GIVEN"},
}};

void appendLookupResults(std::vector<LookupResult>& destination, std::vector<LookupResult>&& source,
                         bool from_user_dict = false) {
  if (from_user_dict) {
    for (auto& result : source) {
      result.from_user_dict = true;
    }
  }
  destination.insert(destination.end(), std::make_move_iterator(source.begin()), std::make_move_iterator(source.end()));
}

}  // namespace

std::string_view conjTypeToCanonicalString(ConjugationType type) {
  for (const auto& alias : kConjTypeAliases) {
    if (alias.type == type) {
      return alias.canonical;
    }
  }
  return "";
}

std::optional<ConjugationType> conjTypeFromCanonical(std::string_view str) {
  if (str.empty() || str == "NONE") {
    return ConjugationType::None;
  }
  for (const auto& alias : kConjTypeAliases) {
    if (!alias.canonical.empty() && alias.canonical == str) {
      return alias.type;
    }
  }
  return std::nullopt;
}

std::optional<ConjugationType> conjTypeFromAnyAlias(std::string_view str) {
  for (const auto& alias : kConjTypeAliases) {
    if (alias.pascal == str || alias.screaming == str) {
      return alias.type;
    }
  }
  return std::nullopt;
}

#ifndef __EMSCRIPTEN__
namespace {

/**
 * @brief Get home directory path
 */
std::string getHomeDir() {
  if (const char* home = std::getenv("HOME")) {
    return home;
  }
  return "";
}

}  // namespace
#endif  // __EMSCRIPTEN__

DictionaryManager::DictionaryManager() : core_dict_(std::make_unique<CoreDictionary>()) {}

DictionaryManager::~DictionaryManager() = default;

DictionaryManager::DictionaryManager(DictionaryManager&&) noexcept = default;
DictionaryManager& DictionaryManager::operator=(DictionaryManager&&) noexcept = default;

void DictionaryManager::addUserDictionary(std::shared_ptr<UserDictionary> dict) {
  if (dict) {
    user_dicts_.push_back(std::move(dict));
  }
}

void DictionaryManager::clearUserDictionaries() {
  user_binary_dicts_.clear();
  user_dicts_.clear();
}

std::vector<LookupResult> DictionaryManager::lookup(std::string_view text, size_t start_pos) const {
  std::vector<LookupResult> results;

  // Lookup in core dictionary (Layer 1: hardcoded)
  appendLookupResults(results, core_dict_->lookup(text, start_pos));

  // Lookup in core binary dictionary (Layer 2: core.dic)
  if (core_binary_dict_ && core_binary_dict_->isLoaded()) {
    appendLookupResults(results, core_binary_dict_->lookup(text, start_pos));
  }

  // Lookup in binary user dictionaries (Layer 3)
  for (const auto& user_binary_dict : user_binary_dicts_) {
    appendLookupResults(results, user_binary_dict->lookup(text, start_pos), true);
  }

  // Lookup in source user dictionaries (Layer 4: CSV/TSV files)
  for (const auto& user_dict : user_dicts_) {
    appendLookupResults(results, user_dict->lookup(text, start_pos), true);
  }

  return results;
}

const DictionaryEntry* DictionaryManager::lookupExact(std::string_view surface, core::PartOfSpeech pos) const {
  if (surface.empty()) {
    return nullptr;
  }

  if (const auto* entry = core_dict_->lookupExact(surface, pos)) {
    return entry;
  }
  if (core_binary_dict_ && core_binary_dict_->isLoaded()) {
    if (const auto* entry = core_binary_dict_->lookupExact(surface, pos)) {
      return entry;
    }
  }
  for (const auto& user_binary_dict : user_binary_dicts_) {
    if (const auto* entry = user_binary_dict->lookupExact(surface, pos)) {
      return entry;
    }
  }
  for (const auto& user_dict : user_dicts_) {
    if (const auto* entry = user_dict->lookupExact(surface, pos)) {
      return entry;
    }
  }
  return nullptr;
}

const CoreDictionary& DictionaryManager::coreDictionary() const {
  return *core_dict_;
}

bool DictionaryManager::loadCoreDictionary(const std::string& path) {
  return loadCoreDictionaryResult(path).hasValue();
}

core::Expected<size_t, core::Error> DictionaryManager::loadCoreDictionaryResult(const std::string& path) {
  if (!core_binary_dict_) {
    core_binary_dict_ = std::make_unique<BinaryDictionary>();
  }

  return core_binary_dict_->loadFromFile(path);
}

core::Expected<size_t, core::Error> DictionaryManager::loadCoreDictionaryFromMemoryResult(const uint8_t* data,
                                                                                          size_t size) {
  if (!core_binary_dict_) {
    core_binary_dict_ = std::make_unique<BinaryDictionary>();
  }

  return core_binary_dict_->loadFromMemory(data, size);
}

bool DictionaryManager::hasCoreBinaryDictionary() const {
  return core_binary_dict_ && core_binary_dict_->isLoaded();
}

core::Expected<size_t, core::Error> DictionaryManager::loadUserBinaryDictionaryResult(const std::string& path) {
  auto dictionary = std::make_unique<BinaryDictionary>();
  auto result = dictionary->loadFromFile(path);
  if (!result.hasValue()) {
    return core::makeUnexpected(result.error());
  }
  user_binary_dicts_.push_back(std::move(dictionary));
  return result.value();
}

bool DictionaryManager::loadUserBinaryDictionaryFromMemory(const uint8_t* data, size_t size) {
  return loadUserBinaryDictionaryFromMemoryResult(data, size).hasValue();
}

core::Expected<size_t, core::Error> DictionaryManager::loadUserBinaryDictionaryFromMemoryResult(const uint8_t* data,
                                                                                                size_t size) {
  auto dictionary = std::make_unique<BinaryDictionary>();
  auto result = dictionary->loadFromMemory(data, size);
  if (!result.hasValue()) {
    return core::makeUnexpected(result.error());
  }
  user_binary_dicts_.push_back(std::move(dictionary));
  return result.value();
}

bool DictionaryManager::hasUserBinaryDictionary() const {
  return !user_binary_dicts_.empty();
}

bool DictionaryManager::tryAutoLoadCoreDictionary() {
  // Already loaded
  if (hasCoreBinaryDictionary()) {
    return true;
  }

#ifdef __EMSCRIPTEN__
  // WASM: Dictionaries are embedded and loaded via Suzume::Impl constructor
  // This function is not used in WASM builds
  return false;
#else
  namespace fs = std::filesystem;

  std::vector<std::string> search_paths;

  // 1. $SUZUME_DATA_DIR/core.dic
  if (const char* data_dir = std::getenv("SUZUME_DATA_DIR")) {
    search_paths.push_back(std::string(data_dir) + "/core.dic");
  }

  // 2. ./data/core.dic
  search_paths.push_back("./data/core.dic");

  // 3. ~/.suzume/core.dic
  std::string home = getHomeDir();
  if (!home.empty()) {
    search_paths.push_back(home + "/.suzume/core.dic");
  }

  // 4. /usr/local/share/suzume/core.dic
  search_paths.push_back("/usr/local/share/suzume/core.dic");

  // 5. /usr/share/suzume/core.dic
  search_paths.push_back("/usr/share/suzume/core.dic");

  // Try each path
  for (const auto& path : search_paths) {
    if (fs::exists(path) && loadCoreDictionary(path)) {
      return true;
    }
  }

  return false;
#endif  // __EMSCRIPTEN__
}

}  // namespace suzume::dictionary
