#include "grammar/dictionary_expansion.h"

#include <string>
#include <unordered_map>
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
    {"い", core::ExtendedPOS::AdjBasic},        {"かっ", core::ExtendedPOS::AdjKatt},
    {"ければ", core::ExtendedPOS::AdjKeForm},   {"く", core::ExtendedPOS::AdjRenyokei},
    {"かったら", core::ExtendedPOS::AdjKeForm}, {"そう", core::ExtendedPOS::AdjStem},
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
      entry.extended_pos = core::posToExtendedPos(entry.pos);
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
      result.push_back({form.kanji_surface, core::PartOfSpeech::Verb, form.extended_pos, kanji_lemma});
      // A one-mora kana dictionary edge (き/こ) is indistinguishable from
      // ordinary word-internal kana and receives the short-dictionary-verb
      // bonus. Reverse inflection already generates these two forms with
      // grammatical context, so only materialize the unambiguous 2+ mora
      // kana spellings here.
      const bool crosses_auxiliary_boundary = utf8::endsWithAny(form.kana_surface, {"られる", "れる"});
      if (form.kana_surface.size() > core::kJapaneseCharBytes && !crosses_auxiliary_boundary) {
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
    if (bound_suffix_verb && surface != base_entry.surface && surface.size() <= 2 * core::kJapaneseCharBytes) {
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

DictionaryExpansionResult expandDictionarySourceEntries(const std::vector<dictionary::SourceEntry>& source_entries) {
  DictionaryExpansionResult result;
  struct SeenEntry {
    size_t index;
    size_t lemma_length;
    core::PartOfSpeech pos;
  };
  std::unordered_map<std::string, SeenEntry> seen_surfaces;

  auto append_source = [&](const dictionary::SourceEntry& source_entry) {
    auto expanded_entries = expandDictionarySourceEntry(source_entry);
    const bool is_expanded = expanded_entries.size() > 1;
    for (auto& entry : expanded_entries) {
      auto found = seen_surfaces.find(entry.surface);
      if (found != seen_surfaces.end()) {
        if (entry.pos == found->second.pos && entry.lemma.size() > found->second.lemma_length) {
          found->second.lemma_length = entry.lemma.size();
          result.entries[found->second.index] = std::move(entry);
        }
        ++result.duplicates_skipped;
        continue;
      }
      const size_t index = result.entries.size();
      seen_surfaces.emplace(entry.surface, SeenEntry{index, entry.lemma.size(), entry.pos});
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
