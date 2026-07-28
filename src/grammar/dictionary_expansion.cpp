#include "grammar/dictionary_expansion.h"

#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "grammar/conjugation.h"
#include "grammar/honorific_verbs.h"

namespace suzume {
namespace grammar {

namespace {

struct IAdjectiveSuffix {
  const char* suffix;
  core::ExtendedPOS extended_pos;
};

constexpr IAdjectiveSuffix kIAdjectiveSuffixes[] = {
    {"い", core::ExtendedPOS::AdjBasic},      {"く", core::ExtendedPOS::AdjRenyokei},
    {"かっ", core::ExtendedPOS::AdjKatt},     {"けれ", core::ExtendedPOS::AdjKeForm},
    {"かろ", core::ExtendedPOS::AdjMizenkei},
};

dictionary::DictionaryEntry makeBaseEntry(const dictionary::SourceEntry& source_entry) {
  dictionary::DictionaryEntry entry;
  entry.surface = source_entry.surface;
  entry.pos = source_entry.pos;
  entry.lemma = source_entry.lemma.empty() ? source_entry.surface : source_entry.lemma;

  switch (source_entry.conj_type) {
    case dictionary::ConjugationType::Interjection:
      entry.extended_pos = core::ExtendedPOS::Interjection;
      break;
    case dictionary::ConjugationType::NaAdjective:
      entry.extended_pos = core::ExtendedPOS::AdjNaAdj;
      break;
    case dictionary::ConjugationType::ProperFamily:
      entry.extended_pos = core::ExtendedPOS::NounProperFamily;
      break;
    case dictionary::ConjugationType::ProperGiven:
      entry.extended_pos = core::ExtendedPOS::NounProperGiven;
      break;
    default:
      entry.extended_pos =
          source_entry.is_proper_noun ? core::ExtendedPOS::NounProper : core::posToExtendedPos(entry.pos);
      break;
  }
  return entry;
}

bool needsExpansion(const dictionary::SourceEntry& source_entry) {
  const VerbType verb_type = conjTypeToVerbType(source_entry.conj_type);
  return (source_entry.pos == core::PartOfSpeech::Verb && verb_type != VerbType::Unknown &&
          verb_type != VerbType::IAdjective) ||
         (source_entry.pos == core::PartOfSpeech::Adjective &&
          source_entry.conj_type == dictionary::ConjugationType::IAdjective);
}

std::vector<dictionary::DictionaryEntry> expandIAdjective(const dictionary::DictionaryEntry& base_entry) {
  // The いい-ending adjective family uses a suppletive よ stem. Do not emit
  // mechanically invalid い-stem forms; its valid forms come from L1 entries.
  if (!utf8::endsWith(base_entry.surface, "い") || utf8::endsWith(base_entry.surface, "いい")) {
    return {base_entry};
  }

  const std::string stem(utf8::dropLastChar(base_entry.surface));
  std::vector<dictionary::DictionaryEntry> result;
  result.reserve(sizeof(kIAdjectiveSuffixes) / sizeof(kIAdjectiveSuffixes[0]));
  for (const auto& suffix : kIAdjectiveSuffixes) {
    result.push_back({stem + suffix.suffix, core::PartOfSpeech::Adjective, suffix.extended_pos, base_entry.lemma});
  }
  return result;
}

std::vector<dictionary::DictionaryEntry> expandVerb(const dictionary::DictionaryEntry& base_entry, VerbType verb_type) {
  std::vector<dictionary::DictionaryEntry> result;
  if (verb_type == VerbType::Kuru) {
    const bool source_uses_kanji = containsKanji(base_entry.surface);
    const std::string kanji_lemma = source_uses_kanji ? base_entry.lemma : "来る";
    const std::string kana_lemma = source_uses_kanji ? "くる" : base_entry.lemma;
    for (const auto& form : getKuruDictionaryForms()) {
      if (form.emit_kanji) {
        result.push_back({form.kanji_surface, core::PartOfSpeech::Verb, form.extended_pos, kanji_lemma});
      }
      // A one-mora kana dictionary edge (き/こ) is indistinguishable from
      // ordinary word-internal kana and receives the short-dictionary-verb
      // bonus. Contextual candidate generation supplies the mizenkei before
      // a selecting auxiliary, so only materialize the safe 2+ mora forms.
      if (form.emit_kana && form.kana_surface.size() > core::kJapaneseCharBytes) {
        result.push_back({form.kana_surface, core::PartOfSpeech::Verb, form.extended_pos, kana_lemma});
      }
    }
    return result;
  }

  const Conjugation conjugation;
  const auto suffixes = conjugation.getDictionarySuffixes(verb_type, base_entry.surface);
  const std::string stem = Conjugation::getStem(base_entry.surface, verb_type);
  // A bound derivational suffix verb is written only onto a nominal host, so a
  // two-mora inflected spelling of one is indistinguishable from ordinary
  // word-internal kana — the same objection the one-mora くる forms above
  // answer (ばっ inside ばったり). Reverse inflection still reaches those cells
  // from the base entry, which is kept, and it sees the host. Longer stems are
  // unambiguous enough to materialize (がかっ, がかり).
  const bool bound_suffix_verb = isBoundDerivationalSuffixVerbLemma(base_entry.lemma);
  result.reserve(suffixes.size());
  for (const auto& suffix : suffixes) {
    std::string surface = stem + suffix.suffix;
    if (bound_suffix_verb && suffix.extended_pos != core::ExtendedPOS::VerbShuushikei &&
        surface.size() <= 2 * core::kJapaneseCharBytes) {
      continue;
    }
    result.push_back({std::move(surface), core::PartOfSpeech::Verb, suffix.extended_pos, base_entry.lemma});
  }
  return result.empty() ? std::vector<dictionary::DictionaryEntry>{base_entry} : result;
}

}  // namespace

std::vector<dictionary::DictionaryEntry> expandDictionarySourceEntry(const dictionary::SourceEntry& source_entry) {
  auto base_entry = makeBaseEntry(source_entry);
  if (!needsExpansion(source_entry)) {
    return {std::move(base_entry)};
  }

  if (source_entry.pos == core::PartOfSpeech::Adjective) {
    base_entry.extended_pos = core::ExtendedPOS::AdjBasic;
    return expandIAdjective(base_entry);
  }

  base_entry.extended_pos = core::ExtendedPOS::VerbShuushikei;
  return expandVerb(base_entry, conjTypeToVerbType(source_entry.conj_type));
}

DictionaryExpansionResult expandDictionarySourceEntries(const std::vector<dictionary::SourceEntry>& source_entries,
                                                        DictionaryExpansionOptions options) {
  DictionaryExpansionResult result;
  struct EntryIdentity {
    std::string surface;
    core::PartOfSpeech pos;
    core::ExtendedPOS extended_pos;
    std::string lemma;

    bool operator==(const EntryIdentity& other) const {
      return std::tie(surface, pos, extended_pos, lemma) ==
             std::tie(other.surface, other.pos, other.extended_pos, other.lemma);
    }
  };
  struct EntryIdentityHash {
    size_t operator()(const EntryIdentity& identity) const {
      size_t hash = std::hash<std::string>{}(identity.surface);
      hash ^= static_cast<size_t>(identity.pos) + 0x9E3779B9U + (hash << 6U) + (hash >> 2U);
      hash ^= static_cast<size_t>(identity.extended_pos) + 0x9E3779B9U + (hash << 6U) + (hash >> 2U);
      hash ^= std::hash<std::string>{}(identity.lemma) + 0x9E3779B9U + (hash << 6U) + (hash >> 2U);
      return hash;
    }
  };
  std::unordered_set<EntryIdentity, EntryIdentityHash> seen_entries;
  struct SeenSurface {
    size_t index;
    size_t lemma_length;
    core::PartOfSpeech pos;
  };
  std::unordered_map<std::string, SeenSurface> seen_surfaces;

  auto append_source = [&](const dictionary::SourceEntry& source_entry) {
    auto expanded_entries = expandDictionarySourceEntry(source_entry);
    const bool is_expanded = expanded_entries.size() > 1;
    for (auto& entry : expanded_entries) {
      if (options.preserve_surface_homographs) {
        EntryIdentity identity{entry.surface, entry.pos, entry.extended_pos, entry.lemma};
        if (!seen_entries.insert(std::move(identity)).second) {
          ++result.duplicates_skipped;
          continue;
        }
      } else {
        auto found = seen_surfaces.find(entry.surface);
        if (found != seen_surfaces.end()) {
          if (entry.pos == found->second.pos && entry.lemma.size() > found->second.lemma_length) {
            found->second.lemma_length = entry.lemma.size();
            result.entries[found->second.index] = std::move(entry);
          }
          ++result.duplicates_skipped;
          continue;
        }
        seen_surfaces.emplace(entry.surface, SeenSurface{result.entries.size(), entry.lemma.size(), entry.pos});
      }
      result.entries.push_back(std::move(entry));
      if (is_expanded) {
        ++result.expanded_forms;
      }
    }
  };

  for (const auto& source_entry : source_entries) {
    if (!needsExpansion(source_entry)) {
      append_source(source_entry);
    }
  }
  for (const auto& source_entry : source_entries) {
    if (needsExpansion(source_entry)) {
      append_source(source_entry);
    }
  }
  return result;
}

}  // namespace grammar
}  // namespace suzume
