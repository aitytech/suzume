#include "dictionary/binary_dict.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <unordered_map>

#include "core/debug.h"
#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "normalize/utf8.h"

namespace suzume::dictionary {

namespace {

// Flag bits
constexpr uint8_t kFlagFormalNoun = 0x01;
constexpr uint8_t kFlagInterjection = 0x08;
constexpr uint8_t kFlagProperFamily = 0x10;
constexpr uint8_t kFlagProperGiven = 0x20;

uint8_t posToUint8(core::PartOfSpeech pos) {
  return static_cast<uint8_t>(pos);
}

core::PartOfSpeech uint8ToPos(uint8_t val) {
  return static_cast<core::PartOfSpeech>(val);
}

bool isValidPos(uint8_t val) {
  return core::isValidPartOfSpeech(static_cast<core::PartOfSpeech>(val));
}

uint8_t extendedPosToUint8(core::ExtendedPOS epos) {
  return static_cast<uint8_t>(epos);
}

core::ExtendedPOS uint8ToExtendedPos(uint8_t val) {
  if (val >= static_cast<uint8_t>(core::ExtendedPOS::Count_)) {
    return core::ExtendedPOS::Unknown;
  }
  return static_cast<core::ExtendedPOS>(val);
}

bool isValidExtendedPos(uint8_t val) {
  return core::isValidExtendedPos(static_cast<core::ExtendedPOS>(val));
}

struct BinaryDictEntryV0 {
  uint32_t surface_offset;
  uint32_t lemma_offset;
  uint8_t surface_length;
  uint8_t lemma_length;
  uint8_t pos;
  uint8_t flags;
};

struct BinaryDictEntryV1 {
  uint32_t surface_offset;
  uint32_t lemma_offset;
  uint8_t surface_length;
  uint8_t lemma_length;
  uint8_t pos;
  uint8_t flags;
  uint8_t extended_pos;
  uint8_t reserved[3];
};

struct GrammarPair {
  uint8_t pos;
  uint8_t extended_pos;
};

struct CompactEntry {
  uint16_t lemma_index;
  uint8_t grammar_index;
};

static_assert(sizeof(BinaryDictEntryV0) == 12, "Legacy v2.0 records must remain readable");
static_assert(sizeof(BinaryDictEntryV1) == 16, "Legacy v2.1 records must remain readable");
static_assert(sizeof(GrammarPair) == 2, "Compact grammar palette entries must remain two bytes");

template <typename T>
T readPod(const uint8_t* data, size_t offset) {
  T value{};
  std::memcpy(&value, data + offset, sizeof(T));
  return value;
}

}  // namespace

// BinaryDictionary implementation

BinaryDictionary::BinaryDictionary() = default;
BinaryDictionary::~BinaryDictionary() = default;

core::Expected<size_t, core::Error> BinaryDictionary::loadFromFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    return core::makeUnexpected(core::Error(core::ErrorCode::FileNotFound, "Failed to open dictionary file: " + path));
  }

  size_t file_size = static_cast<size_t>(file.tellg());
  file.seekg(0);

  std::vector<uint8_t> loaded_data(file_size);
  if (!file.read(reinterpret_cast<char*>(loaded_data.data()), file_size)) {
    return core::makeUnexpected(core::Error(core::ErrorCode::InternalError, "Failed to read dictionary file: " + path));
  }

  DoubleArray loaded_trie;
  std::vector<DictionaryEntry> loaded_entries;
  auto result = parseData(loaded_data.data(), loaded_data.size(), loaded_trie, loaded_entries);
  if (!result.hasValue()) {
    return result;
  }

  // loaded_data is not retained: the trie and entries own independent copies of
  // everything they need (deserialized units and constructed strings), so the
  // raw file bytes can be freed here.
  trie_ = std::move(loaded_trie);
  entries_ = std::move(loaded_entries);
  return result;
}

core::Expected<size_t, core::Error> BinaryDictionary::loadFromMemory(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Empty dictionary data"));
  }

  DoubleArray loaded_trie;
  std::vector<DictionaryEntry> loaded_entries;
  auto result = parseData(data, size, loaded_trie, loaded_entries);
  if (!result.hasValue()) {
    return result;
  }

  // loaded_data is not retained (see loadFromFile): decoded structures own copies.
  trie_ = std::move(loaded_trie);
  entries_ = std::move(loaded_entries);
  return result;
}

core::Expected<size_t, core::Error> BinaryDictionary::parseData(const uint8_t* data, size_t size, DoubleArray& trie,
                                                                std::vector<DictionaryEntry>& entries) {
  if (size < sizeof(BinaryDictHeader)) {
    return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Dictionary file too small"));
  }

  const auto header = readPod<BinaryDictHeader>(data, 0);

  // Validate magic
  if (header.magic != BinaryDictHeader::kMagic) {
    return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Invalid dictionary magic number"));
  }

  // Validate version
  if (header.version_major != BinaryDictHeader::kVersionMajor) {
    return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Unsupported dictionary version"));
  }
  if (header.version_minor > BinaryDictHeader::kVersionMinor) {
    return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Unsupported dictionary minor version"));
  }

  // Validate offsets and section ranges before reading variable-length data.
  if (header.trie_offset > size || header.trie_size > size - header.trie_offset || header.entry_offset > size ||
      header.string_offset > size) {
    return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Invalid dictionary offsets"));
  }
  if (header.trie_offset < sizeof(BinaryDictHeader) || header.entry_offset < header.trie_offset + header.trie_size) {
    return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Invalid dictionary section order"));
  }

  const size_t entry_count = header.entry_count;
  size_t entry_table_offset = header.entry_offset;
  size_t entry_record_size = sizeof(BinaryDictEntryV0);
  std::vector<GrammarPair> grammar_palette;
  if (header.version_minor >= 3) {
    if (entry_table_offset >= header.string_offset) {
      return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Missing dictionary grammar palette"));
    }
    const size_t palette_count = data[entry_table_offset++];
    const size_t palette_bytes = palette_count * sizeof(GrammarPair);
    if (palette_count == 0 || palette_bytes > header.string_offset - entry_table_offset) {
      return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Invalid dictionary grammar palette"));
    }
    grammar_palette.reserve(palette_count);
    for (size_t idx = 0; idx < palette_count; ++idx) {
      const size_t pair_offset = entry_table_offset + idx * sizeof(GrammarPair);
      GrammarPair pair{data[pair_offset], data[pair_offset + 1]};
      if (!isValidPos(pair.pos) || !isValidExtendedPos(pair.extended_pos)) {
        return core::makeUnexpected(
            core::Error(core::ErrorCode::InvalidInput, "Invalid dictionary grammar palette value"));
      }
      grammar_palette.push_back(pair);
    }
    entry_table_offset += palette_bytes;
    switch (header.flags & BinaryDictHeader::kEntryEncodingMask) {
      case BinaryDictHeader::kGrammarOnlyEntries:
        entry_record_size = 1;
        break;
      case BinaryDictHeader::kPackedEntries:
        entry_record_size = 2;
        break;
      case BinaryDictHeader::kWideEntries:
        entry_record_size = kWideCompactEntrySize;
        break;
      default:
        return core::makeUnexpected(
            core::Error(core::ErrorCode::InvalidInput, "Invalid compact dictionary entry encoding"));
    }
  } else if (header.version_minor == 2) {
    entry_record_size = sizeof(BinaryDictEntry);
  } else if (header.version_minor == 1) {
    entry_record_size = sizeof(BinaryDictEntryV1);
  }
  if (entry_count > (std::numeric_limits<size_t>::max() / entry_record_size)) {
    return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Dictionary entry table too large"));
  }

  size_t entry_table_size = entry_count * entry_record_size;
  if (entry_table_offset > size || entry_table_size > size - entry_table_offset ||
      entry_table_offset + entry_table_size > header.string_offset) {
    return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Invalid dictionary entry table"));
  }

  if (header.string_offset < entry_table_offset + entry_table_size) {
    return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Invalid dictionary string pool offset"));
  }

  size_t string_pool_size = size - header.string_offset;

  // Load trie
  if (!trie.deserialize(data + header.trie_offset, header.trie_size)) {
    return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Failed to load dictionary trie"));
  }

  std::vector<std::string> trie_surfaces;
  if (header.version_minor >= 2) {
    std::vector<DoubleArray::KeyValue> key_values;
    if (!trie.enumerate(key_values)) {
      return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Failed to enumerate dictionary trie"));
    }
    if (key_values.size() != entry_count) {
      return core::makeUnexpected(
          core::Error(core::ErrorCode::InvalidInput, "Dictionary trie and entry table counts differ"));
    }

    trie_surfaces.resize(entry_count);
    std::vector<bool> assigned(entry_count, false);
    for (auto& key_value : key_values) {
      if (key_value.key.empty()) {
        return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Dictionary surface must not be empty"));
      }
      if (key_value.value < 0 || static_cast<size_t>(key_value.value) >= entry_count ||
          assigned[static_cast<size_t>(key_value.value)]) {
        return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Invalid dictionary trie entry value"));
      }
      size_t entry_idx = static_cast<size_t>(key_value.value);
      trie_surfaces[entry_idx] = std::move(key_value.key);
      assigned[entry_idx] = true;
    }
  }

  // Load entries
  const char* string_pool = reinterpret_cast<const char*>(data + header.string_offset);
  std::vector<std::string_view> compact_lemmas;
  if (header.version_minor >= 3) {
    size_t lemma_offset = header.string_offset;
    while (lemma_offset < size) {
      const size_t lemma_length = data[lemma_offset++];
      if (lemma_length == 0 || lemma_length > size - lemma_offset) {
        return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Invalid compact lemma table"));
      }
      compact_lemmas.emplace_back(reinterpret_cast<const char*>(data + lemma_offset), lemma_length);
      lemma_offset += lemma_length;
      if (compact_lemmas.size() > std::numeric_limits<uint16_t>::max()) {
        return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Compact lemma table is too large"));
      }
    }
  }

  entries.clear();
  entries.reserve(entry_count);

  for (uint32_t idx = 0; idx < header.entry_count; ++idx) {
    size_t entry_pos = entry_table_offset + idx * entry_record_size;
    uint32_t surface_offset = 0;
    uint32_t lemma_offset = 0;
    uint8_t surface_length = 0;
    uint8_t lemma_length = 0;
    uint8_t pos = 0;
    uint8_t flags = 0;
    uint8_t extended_pos = static_cast<uint8_t>(core::ExtendedPOS::Unknown);
    uint16_t lemma_index = 0;

    if (header.version_minor >= 3) {
      uint8_t grammar_idx = 0;
      switch (header.flags & BinaryDictHeader::kEntryEncodingMask) {
        case BinaryDictHeader::kGrammarOnlyEntries:
          grammar_idx = data[entry_pos];
          break;
        case BinaryDictHeader::kPackedEntries: {
          const uint16_t packed = static_cast<uint16_t>(data[entry_pos]) |
                                  static_cast<uint16_t>(static_cast<uint16_t>(data[entry_pos + 1]) << 8U);
          lemma_index = packed & 0x07FFU;
          grammar_idx = static_cast<uint8_t>(packed >> 11U);
          break;
        }
        case BinaryDictHeader::kWideEntries:
          lemma_index = static_cast<uint16_t>(data[entry_pos]) |
                        static_cast<uint16_t>(static_cast<uint16_t>(data[entry_pos + 1]) << 8U);
          grammar_idx = data[entry_pos + 2];
          break;
        default:
          break;
      }
      if (grammar_idx >= grammar_palette.size()) {
        return core::makeUnexpected(
            core::Error(core::ErrorCode::InvalidInput, "Invalid dictionary grammar palette index"));
      }
      pos = grammar_palette[grammar_idx].pos;
      extended_pos = grammar_palette[grammar_idx].extended_pos;
    } else if (header.version_minor == 2) {
      const auto rec = readPod<BinaryDictEntry>(data, entry_pos);
      if (rec.reserved != 0) {
        return core::makeUnexpected(
            core::Error(core::ErrorCode::InvalidInput, "Dictionary entry reserved byte must be zero"));
      }
      lemma_offset = rec.lemma_offset;
      lemma_length = rec.lemma_length;
      pos = rec.pos;
      extended_pos = rec.extended_pos;
    } else if (header.version_minor == 1) {
      const auto rec = readPod<BinaryDictEntryV1>(data, entry_pos);
      surface_offset = rec.surface_offset;
      lemma_offset = rec.lemma_offset;
      surface_length = rec.surface_length;
      lemma_length = rec.lemma_length;
      pos = rec.pos;
      flags = rec.flags;
      extended_pos = rec.extended_pos;
    } else {
      const auto legacy = readPod<BinaryDictEntryV0>(data, entry_pos);
      surface_offset = legacy.surface_offset;
      lemma_offset = legacy.lemma_offset;
      surface_length = legacy.surface_length;
      lemma_length = legacy.lemma_length;
      pos = legacy.pos;
      flags = legacy.flags;
    }

    if (header.version_minor < 2) {
      if (surface_offset > string_pool_size || surface_length > string_pool_size - surface_offset) {
        return core::makeUnexpected(
            core::Error(core::ErrorCode::InvalidInput, "Invalid dictionary surface string range"));
      }
      if (surface_length == 0) {
        return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Dictionary surface must not be empty"));
      }
    }

    if (!isValidPos(pos)) {
      return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Invalid dictionary POS value"));
    }

    if (header.version_minor >= 1 && !isValidExtendedPos(extended_pos)) {
      return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Invalid dictionary extended POS value"));
    }

    if (header.version_minor < 3 && lemma_length > 0 &&
        (lemma_offset > string_pool_size || lemma_length > string_pool_size - lemma_offset)) {
      return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Invalid dictionary lemma string range"));
    }

    DictionaryEntry entry;
    if (header.version_minor >= 2) {
      entry.surface = std::move(trie_surfaces[idx]);
    } else {
      entry.surface = std::string(string_pool + surface_offset, surface_length);
    }
    entry.pos = uint8ToPos(pos);

    if (header.version_minor >= 3 && lemma_index > 0) {
      if (lemma_index > compact_lemmas.size()) {
        return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Invalid compact lemma index"));
      }
      entry.lemma = compact_lemmas[lemma_index - 1];
    } else if (lemma_length > 0) {
      entry.lemma = std::string(string_pool + lemma_offset, lemma_length);
    } else {
      entry.lemma = entry.surface;
    }

    entry.extended_pos = uint8ToExtendedPos(extended_pos);

    if (entry.extended_pos != core::ExtendedPOS::Unknown) {
      // Use the serialized fine-grained category when present.
    } else if (header.version_minor < 2 && (flags & kFlagFormalNoun) != 0) {
      entry.extended_pos = core::ExtendedPOS::NounFormal;
    } else if (header.version_minor < 2 && (flags & kFlagInterjection) != 0) {
      entry.extended_pos = core::ExtendedPOS::Interjection;
    } else if (header.version_minor < 2 && (flags & kFlagProperFamily) != 0) {
      entry.extended_pos = core::ExtendedPOS::NounProperFamily;
    } else if (header.version_minor < 2 && (flags & kFlagProperGiven) != 0) {
      entry.extended_pos = core::ExtendedPOS::NounProperGiven;
    } else {
      // Derive default extended_pos from POS for proper cost calculation
      switch (entry.pos) {
        case core::PartOfSpeech::Adjective: {
          // Distinguish I-adjective forms from NA-adjective based on ending
          // I-adjective forms: い, く, くて, かった, かっ, ければ, そう, etc.
          // NA-adjectives: don't end in these patterns
          // Exceptions: きれい, きらい are na-adjectives ending in い
          using namespace std::string_view_literals;
          if (utf8::endsWithAny(entry.surface, {"きれい"sv, "きらい"sv, "嫌い"sv, "綺麗"sv})) {
            entry.extended_pos = core::ExtendedPOS::AdjNaAdj;
          } else if (utf8::endsWith(entry.surface, "い")) {
            entry.extended_pos = core::ExtendedPOS::AdjBasic;
          } else if (utf8::endsWithAny(entry.surface, {"く"sv, "くて"sv})) {
            // く-form (adverbial/te-form): 美しく, 美しくて
            entry.extended_pos = core::ExtendedPOS::AdjRenyokei;
          } else if (utf8::endsWithAny(entry.surface, {"かっ"sv})) {
            // かっ-form (past stem): 美しかっ
            entry.extended_pos = core::ExtendedPOS::AdjKatt;
          } else if (utf8::endsWithAny(entry.surface, {"ければ"sv, "かったら"sv})) {
            // Conditional forms: 美しければ, 美しかったら
            entry.extended_pos = core::ExtendedPOS::AdjKeForm;
          } else if (utf8::endsWithAny(entry.surface, {"そう"sv})) {
            // Stem+そう: 美しそう
            entry.extended_pos = core::ExtendedPOS::AdjStem;
          } else {
            // Doesn't match I-adjective patterns → NA-adjective
            entry.extended_pos = core::ExtendedPOS::AdjNaAdj;
          }
          break;
        }
        case core::PartOfSpeech::Verb: {
          // Distinguish verb forms based on ending
          // 音便形: ends with っ/ん (onbin for ta/te form)
          using namespace std::string_view_literals;
          if (utf8::endsWithAny(entry.surface, {"っ"sv, "ん"sv})) {
            // Sokuonbin (っ) or hatsuonbin (ん): あっ, 飲ん, etc.
            entry.extended_pos = core::ExtendedPOS::VerbOnbinkei;
          } else if (utf8::endsWith(entry.surface, "い") && entry.surface.size() > core::kTwoJapaneseCharBytes) {
            // Godan-ka/ga i-onbin (い音便) for 3+ char compound verbs
            // e.g., たどり着い from たどり着く, 引っかい from 引っかく
            // Short forms (1-2 chars) are handled by the short-verb rules below
            entry.extended_pos = core::ExtendedPOS::VerbOnbinkei;
          } else if (utf8::endsWithAny(entry.surface, {"れば"sv, "けば"sv, "せば"sv, "てば"sv, "ねば"sv, "べば"sv,
                                                       "めば"sv, "えば"sv})) {
            // Conditional form
            entry.extended_pos = core::ExtendedPOS::VerbKateikei;
          } else if (entry.surface.size() == core::kJapaneseCharBytes) {
            // Single hiragana character verb forms are renyokei (連用形)
            // e.g., い from いる expansion, not shuushikei
            // This prevents incorrect VERB_終止→AUX_意志 connections like と→い→う
            entry.extended_pos = core::ExtendedPOS::VerbRenyokei;
          } else if (!utf8::endsWith(entry.surface, "る") && entry.surface.size() <= core::kTwoJapaneseCharBytes) {
            // Short verb forms (1-2 chars) not ending in る
            if (grammar::endsWithARow(entry.surface) && grammar::containsKanji(entry.surface)) {
              // Kanji + A-row ending = godan mizenkei (読ま, 書か, 行か)
              entry.extended_pos = core::ExtendedPOS::VerbMizenkei;
            } else {
              // Other short forms likely renyoukei (すぎ from すぎる)
              entry.extended_pos = core::ExtendedPOS::VerbRenyokei;
            }
          } else if (utf8::endsWithAny(entry.surface, {"き"sv, "ぎ"sv, "し"sv, "じ"sv, "ち"sv, "ぢ"sv, "に"sv, "ひ"sv,
                                                       "び"sv, "ぴ"sv, "み"sv, "り"sv})) {
            // Godan verb renyokei endings (I-row hiragana except い)
            // e.g., いただき from いただく → いただき + ます should work
            // Note: い excluded because godan-wa renyokei (思い) would need
            // disambiguation from noun/adj uses. Short forms are handled above.
            entry.extended_pos = core::ExtendedPOS::VerbRenyokei;
          } else if (utf8::endsWithAny(entry.surface,
                                       {"え"sv, "け"sv, "げ"sv, "せ"sv, "ぜ"sv, "ね"sv, "べ"sv, "め"sv, "れ"sv}) &&
                     entry.surface.size() > core::kTwoJapaneseCharBytes) {
            // Ichidan verb renyokei endings (E-row hiragana)
            // e.g., いただけ from いただける, 成し遂げ from 成し遂げる
            // Only for 3+ char forms to avoid te-form fragments (食べ+て, 捨て)
            // Short E-row forms are handled by the 1-2 char rule above
            // Note: て/で excluded — conflicts with te-form (捨て, 出で)
            entry.extended_pos = core::ExtendedPOS::VerbRenyokei;
          } else if (grammar::endsWithARow(entry.surface) && entry.surface.size() > core::kTwoJapaneseCharBytes) {
            // Godan verb mizenkei endings (A-row hiragana)
            // e.g., サボら from サボる → サボら + れる (passive) should work
            // Only for 3+ char forms to avoid conflicts with short words
            entry.extended_pos = core::ExtendedPOS::VerbMizenkei;
          } else {
            // Default: shuushikei (dictionary form or other forms)
            entry.extended_pos = core::ExtendedPOS::VerbShuushikei;
          }
          break;
        }
        case core::PartOfSpeech::Noun:
          entry.extended_pos = core::ExtendedPOS::Noun;
          break;
        case core::PartOfSpeech::Adverb:
          entry.extended_pos = core::ExtendedPOS::Adverb;
          break;
        case core::PartOfSpeech::Particle:
          entry.extended_pos = core::ExtendedPOS::ParticleCase;
          break;
        case core::PartOfSpeech::Auxiliary:
          entry.extended_pos = core::ExtendedPOS::AuxTenseTa;  // Default aux
          break;
        case core::PartOfSpeech::Suffix:
          entry.extended_pos = core::ExtendedPOS::Suffix;
          break;
        case core::PartOfSpeech::Prefix:
          entry.extended_pos = core::ExtendedPOS::Prefix;
          break;
        case core::PartOfSpeech::Conjunction:
          entry.extended_pos = core::ExtendedPOS::Conjunction;
          break;
        case core::PartOfSpeech::Determiner:
          entry.extended_pos = core::ExtendedPOS::Determiner;
          break;
        case core::PartOfSpeech::Pronoun:
          entry.extended_pos = core::ExtendedPOS::Pronoun;
          break;
        case core::PartOfSpeech::Symbol:
          entry.extended_pos = core::ExtendedPOS::Symbol;
          break;
        case core::PartOfSpeech::Other:
          entry.extended_pos = core::ExtendedPOS::Other;
          break;
        default:
          entry.extended_pos = core::ExtendedPOS::Unknown;
          break;
      }
    }
    // is_low_info, is_prefix, conj_type are no longer stored

    // Debug: log entries with Unknown extended_pos (indicates missing category mapping)
    // These entries get high cost (2.0) which may cause unexpected tokenization
    // At trace level (SUZUME_DEBUG=3) to avoid flooding output at lower levels
    if (entry.extended_pos == core::ExtendedPOS::Unknown) {
      SUZUME_DEBUG_LOG_TRACE("[DICT_LOAD] WARNING: \"" << entry.surface << "\" pos=" << core::posToString(entry.pos)
                                                       << " has epos=UNKNOWN (cost=2.0)\n");
    }

    entries.push_back(std::move(entry));
  }

  return entries.size();
}

std::vector<LookupResult> BinaryDictionary::lookup(std::string_view text, size_t start_pos) const {
  std::vector<LookupResult> results;

  if (!isLoaded() || start_pos >= text.size()) {
    return results;
  }

  auto trie_results = trie_.commonPrefixSearch(text, start_pos);

  for (const auto& tres : trie_results) {
    if (tres.value >= 0 && static_cast<size_t>(tres.value) < entries_.size()) {
      LookupResult result{};
      result.entry_id = static_cast<uint32_t>(tres.value);
      // Convert byte length from trie to character count
      result.length = normalize::utf8Length(text.substr(start_pos, tres.length));
      result.entry = &entries_[tres.value];
      results.push_back(result);
    }
  }

  return results;
}

const DictionaryEntry* BinaryDictionary::lookupExact(std::string_view surface, core::PartOfSpeech pos) const {
  const int32_t idx = trie_.exactMatch(surface);
  if (idx < 0 || static_cast<size_t>(idx) >= entries_.size()) {
    return nullptr;
  }
  const auto& entry = entries_[static_cast<size_t>(idx)];
  return pos == core::PartOfSpeech::Unknown || entry.pos == pos ? &entry : nullptr;
}

const DictionaryEntry* BinaryDictionary::getEntry(uint32_t idx) const {
  if (idx < entries_.size()) {
    return &entries_[idx];
  }
  return nullptr;
}

// BinaryDictWriter implementation

BinaryDictWriter::BinaryDictWriter() = default;

void BinaryDictWriter::addEntry(const DictionaryEntry& entry) {
  entries_.push_back(entry);
}

void BinaryDictWriter::replaceEntry(const DictionaryEntry& entry) {
  for (auto& existing : entries_) {
    if (existing.surface == entry.surface) {
      existing = entry;
      return;
    }
  }
}

core::Expected<std::vector<uint8_t>, core::Error> BinaryDictWriter::build() {
  if (entries_.empty()) {
    return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "No entries to write"));
  }

  // Sort entries by surface for trie building
  std::sort(entries_.begin(), entries_.end(),
            [](const DictionaryEntry& lhs, const DictionaryEntry& rhs) { return lhs.surface < rhs.surface; });

  // v2.3 uses a three-byte entry record: a 16-bit index into a deduplicated
  // lemma table plus an 8-bit index into a per-file POS/ExtendedPOS palette.
  // Fall back to v2.2 only for unusually diverse external dictionaries that
  // exceed either compact index, preserving the previous format's limits.
  bool use_compact_format = true;
  std::vector<char> compact_lemma_table;
  std::unordered_map<std::string, uint16_t> lemma_indices;
  std::vector<GrammarPair> grammar_palette;
  std::unordered_map<uint16_t, uint8_t> grammar_indices;
  std::vector<CompactEntry> compact_entries;
  compact_entries.reserve(entries_.size());

  for (const auto& ent : entries_) {
    if (ent.surface.empty()) {
      return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Dictionary surface must not be empty"));
    }

    if (ent.surface.size() > std::numeric_limits<uint8_t>::max()) {
      return core::makeUnexpected(
          core::Error(core::ErrorCode::InvalidInput, "Dictionary surface exceeds 255 bytes: " + ent.surface));
    }

    if (!ent.lemma.empty() && ent.lemma.size() > std::numeric_limits<uint8_t>::max()) {
      return core::makeUnexpected(
          core::Error(core::ErrorCode::InvalidInput, "Dictionary lemma exceeds 255 bytes: " + ent.lemma));
    }

    if (!isValidPos(posToUint8(ent.pos))) {
      return core::makeUnexpected(core::Error(core::ErrorCode::InvalidInput, "Dictionary entry has invalid POS"));
    }

    if (!isValidExtendedPos(extendedPosToUint8(ent.extended_pos))) {
      return core::makeUnexpected(
          core::Error(core::ErrorCode::InvalidInput, "Dictionary entry has invalid extended POS"));
    }

    if (!use_compact_format) {
      continue;
    }

    uint16_t lemma_index = 0;
    if (!ent.lemma.empty() && ent.lemma != ent.surface) {
      auto lemma_iter = lemma_indices.find(ent.lemma);
      if (lemma_iter != lemma_indices.end()) {
        lemma_index = lemma_iter->second;
      } else if (lemma_indices.size() >= std::numeric_limits<uint16_t>::max()) {
        use_compact_format = false;
        continue;
      } else {
        lemma_index = static_cast<uint16_t>(lemma_indices.size() + 1);
        lemma_indices.emplace(ent.lemma, lemma_index);
        compact_lemma_table.push_back(static_cast<char>(ent.lemma.size()));
        compact_lemma_table.insert(compact_lemma_table.end(), ent.lemma.begin(), ent.lemma.end());
      }
    }

    const uint8_t pos = posToUint8(ent.pos);
    const uint8_t extended_pos = extendedPosToUint8(ent.extended_pos);
    const uint16_t grammar_key = static_cast<uint16_t>(pos) << 8U | extended_pos;
    uint8_t grammar_index = 0;
    auto grammar_iter = grammar_indices.find(grammar_key);
    if (grammar_iter != grammar_indices.end()) {
      grammar_index = grammar_iter->second;
    } else if (grammar_palette.size() >= std::numeric_limits<uint8_t>::max()) {
      use_compact_format = false;
      continue;
    } else {
      grammar_index = static_cast<uint8_t>(grammar_palette.size());
      grammar_indices.emplace(grammar_key, grammar_index);
      grammar_palette.push_back({pos, extended_pos});
    }

    compact_entries.push_back({lemma_index, grammar_index});
  }

  uint16_t output_minor_version = BinaryDictHeader::kVersionMinor;
  std::vector<uint8_t> entry_data;
  std::vector<char> string_pool;
  uint32_t output_flags = 0;
  if (use_compact_format) {
    size_t compact_entry_size = kWideCompactEntrySize;
    if (lemma_indices.empty()) {
      output_flags = BinaryDictHeader::kGrammarOnlyEntries;
      compact_entry_size = 1;
    } else if (lemma_indices.size() <= 0x07FFU && grammar_palette.size() <= 32) {
      output_flags = BinaryDictHeader::kPackedEntries;
      compact_entry_size = 2;
    } else {
      output_flags = BinaryDictHeader::kWideEntries;
    }
    entry_data.reserve(1 + grammar_palette.size() * sizeof(GrammarPair) + compact_entries.size() * compact_entry_size);
    entry_data.push_back(static_cast<uint8_t>(grammar_palette.size()));
    for (const auto& pair : grammar_palette) {
      entry_data.push_back(pair.pos);
      entry_data.push_back(pair.extended_pos);
    }
    for (const auto& entry : compact_entries) {
      if (output_flags == BinaryDictHeader::kGrammarOnlyEntries) {
        entry_data.push_back(entry.grammar_index);
      } else if (output_flags == BinaryDictHeader::kPackedEntries) {
        const uint16_t packed = entry.lemma_index | static_cast<uint16_t>(entry.grammar_index << 11U);
        entry_data.push_back(static_cast<uint8_t>(packed & 0xFFU));
        entry_data.push_back(static_cast<uint8_t>(packed >> 8U));
      } else {
        entry_data.push_back(static_cast<uint8_t>(entry.lemma_index & 0xFFU));
        entry_data.push_back(static_cast<uint8_t>(entry.lemma_index >> 8U));
        entry_data.push_back(entry.grammar_index);
      }
    }
    string_pool = std::move(compact_lemma_table);
  } else {
    output_minor_version = 2;
    std::unordered_map<std::string, uint32_t> string_offsets;
    std::vector<BinaryDictEntry> binary_entries;
    binary_entries.reserve(entries_.size());
    for (const auto& ent : entries_) {
      BinaryDictEntry record{};
      if (!ent.lemma.empty() && ent.lemma != ent.surface) {
        auto [iter, inserted] = string_offsets.emplace(ent.lemma, static_cast<uint32_t>(string_pool.size()));
        if (inserted) {
          string_pool.insert(string_pool.end(), ent.lemma.begin(), ent.lemma.end());
        }
        record.lemma_offset = iter->second;
        record.lemma_length = static_cast<uint8_t>(ent.lemma.size());
      }
      record.pos = posToUint8(ent.pos);
      record.extended_pos = extendedPosToUint8(ent.extended_pos);
      binary_entries.push_back(record);
    }
    entry_data.resize(binary_entries.size() * sizeof(BinaryDictEntry));
    std::memcpy(entry_data.data(), binary_entries.data(), entry_data.size());
  }

  // Build trie
  std::vector<std::string> keys;
  std::vector<int32_t> values;
  keys.reserve(entries_.size());
  values.reserve(entries_.size());

  for (size_t idx = 0; idx < entries_.size(); ++idx) {
    keys.push_back(entries_[idx].surface);
    values.push_back(static_cast<int32_t>(idx));
  }

  DoubleArray trie;
  if (!trie.build(keys, values)) {
    // Trie construction most commonly fails on duplicate keys. entries_ is sorted
    // by surface above, so duplicates are adjacent — scan cheaply and name the
    // first offending surface to make dictionary-compilation errors actionable.
    std::string message = "Failed to build dictionary trie (" + std::to_string(entries_.size()) + " entries)";
    for (size_t idx = 1; idx < keys.size(); ++idx) {
      if (keys[idx] == keys[idx - 1]) {
        message += "; duplicate surface: \"" + keys[idx] + "\"";
        break;
      }
    }
    return core::makeUnexpected(core::Error(core::ErrorCode::InternalError, message));
  }

  auto trie_data = trie.serialize();

  // Calculate offsets
  size_t header_size = sizeof(BinaryDictHeader);
  size_t trie_offset = header_size;
  size_t trie_size = trie_data.size();
  size_t entry_offset = trie_offset + trie_size;
  size_t entry_size = entry_data.size();
  size_t string_offset = entry_offset + entry_size;
  size_t total_size = string_offset + string_pool.size();

  // Build output
  std::vector<uint8_t> output(total_size);
  uint8_t* ptr = output.data();

  // Write header
  BinaryDictHeader header{};
  header.magic = BinaryDictHeader::kMagic;
  header.version_major = BinaryDictHeader::kVersionMajor;
  header.version_minor = output_minor_version;
  header.entry_count = static_cast<uint32_t>(entries_.size());
  header.trie_offset = static_cast<uint32_t>(trie_offset);
  header.trie_size = static_cast<uint32_t>(trie_size);
  header.entry_offset = static_cast<uint32_t>(entry_offset);
  header.string_offset = static_cast<uint32_t>(string_offset);
  header.flags = output_flags;
  header.checksum = 0;

  std::memcpy(ptr, &header, sizeof(header));
  ptr += sizeof(header);

  // Write trie
  std::memcpy(ptr, trie_data.data(), trie_data.size());
  ptr += trie_data.size();

  // Write entries
  std::memcpy(ptr, entry_data.data(), entry_size);
  ptr += entry_size;

  // Write string pool
  if (!string_pool.empty()) {
    std::memcpy(ptr, string_pool.data(), string_pool.size());
  }

  return output;
}

core::Expected<size_t, core::Error> BinaryDictWriter::writeToFile(const std::string& path) {
  auto result = build();
  if (!result) {
    return core::makeUnexpected(result.error());
  }

  std::ofstream file(path, std::ios::binary);
  if (!file) {
    return core::makeUnexpected(
        core::Error(core::ErrorCode::InternalError, "Failed to create dictionary file: " + path));
  }

  const auto& data = result.value();
  file.write(reinterpret_cast<const char*>(data.data()), data.size());
  if (!file) {
    return core::makeUnexpected(
        core::Error(core::ErrorCode::InternalError, "Failed to write dictionary file: " + path));
  }

  return data.size();
}

}  // namespace suzume::dictionary
