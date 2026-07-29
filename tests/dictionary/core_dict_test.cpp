#include "dictionary/core_dict.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "dictionary/dictionary.h"
#include "dictionary/entries/auxiliaries.h"
#include "dictionary/entries/compound_particles.h"
#include "dictionary/entries/conjunctions.h"
#include "dictionary/entries/determiners.h"
#include "dictionary/entries/formal_nouns.h"
#include "dictionary/entries/interjections.h"
#include "dictionary/entries/particles.h"
#include "dictionary/entries/pronouns.h"

namespace suzume::dictionary {
namespace {

TEST(CoreDictionaryTest, ExactLookupPreservesDuplicateSurfaceOrderAndFiltersPos) {
  CoreDictionary dict;

  const auto* first = dict.lookupExact("あれ");
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first->pos, core::PartOfSpeech::Verb);

  const auto* interjection = dict.lookupExact("あれ", core::PartOfSpeech::Interjection);
  ASSERT_NE(interjection, nullptr);
  EXPECT_EQ(interjection->extended_pos, core::ExtendedPOS::Interjection);

  EXPECT_EQ(dict.lookupExact("あれ", core::PartOfSpeech::Symbol), nullptr);
  EXPECT_EQ(dict.lookupExact("あ"), nullptr);
  EXPECT_EQ(dict.lookupExact(""), nullptr);
}

TEST(CoreDictionaryTest, LookupIncludesEveryHomographAtTheMatchedSurface) {
  CoreDictionary dict;

  const auto all_matches = dict.lookup("ない", 0);
  std::vector<LookupResult> matches;
  for (const auto& match : all_matches) {
    if (match.length == 2) {
      matches.push_back(match);
    }
  }
  ASSERT_EQ(matches.size(), 2U);
  EXPECT_EQ(matches[0].entry->pos, core::PartOfSpeech::Auxiliary);
  EXPECT_EQ(matches[0].entry->extended_pos, core::ExtendedPOS::AuxNegativeNai);
  EXPECT_EQ(matches[1].entry->pos, core::PartOfSpeech::Adjective);
  EXPECT_EQ(matches[1].entry->extended_pos, core::ExtendedPOS::AdjBasic);
}

TEST(CoreDictionaryTest, LookupIntoReusesCallerStorageAndMatchesValueLookup) {
  DictionaryManager dict;
  std::vector<LookupResult> reusable;
  reusable.reserve(16);
  const size_t initial_capacity = reusable.capacity();

  dict.lookupInto("ない", 0, reusable);
  const auto expected = dict.lookup("ない", 0);

  ASSERT_EQ(reusable.size(), expected.size());
  EXPECT_EQ(reusable.capacity(), initial_capacity);
  for (size_t idx = 0; idx < expected.size(); ++idx) {
    EXPECT_EQ(reusable[idx].entry, expected[idx].entry);
    EXPECT_EQ(reusable[idx].length, expected[idx].length);
    EXPECT_EQ(reusable[idx].from_user_dict, expected[idx].from_user_dict);
  }
}

TEST(CoreDictionaryTest, MaterializesEveryStaticEntryWithStableMetadata) {
  const entries::EntrySpecRange sources[] = {
      entries::getParticleEntries(),    entries::getCompoundParticleEntries(), entries::getAuxiliaryEntries(),
      entries::getConjunctionEntries(), entries::getDeterminerEntries(),       entries::getPronounEntries(),
      entries::getFormalNounEntries(),  entries::getInterjectionEntries(),
  };

  std::vector<entries::EntrySpec> expected;
  for (const auto source : sources) {
    expected.insert(expected.end(), source.begin(), source.end());
  }
  std::stable_sort(expected.begin(), expected.end(), [](const auto& lhs, const auto& rhs) {
    return std::string_view(lhs.surface) < std::string_view(rhs.surface);
  });

  CoreDictionary dict;
  for (size_t idx = 0; idx < expected.size(); ++idx) {
    const auto* actual = dict.getEntry(static_cast<uint32_t>(idx));
    ASSERT_NE(actual, nullptr) << "entry index=" << idx;
    EXPECT_EQ(actual->surface, expected[idx].surface);
    EXPECT_EQ(actual->pos, expected[idx].pos);
    EXPECT_EQ(actual->extended_pos, expected[idx].extended_pos);
    EXPECT_EQ(actual->lemma, expected[idx].lemma);
  }
  EXPECT_EQ(dict.getEntry(static_cast<uint32_t>(expected.size())), nullptr);
}

TEST(CoreDictionaryTest, StaticEntrySetsContainNoExactDuplicates) {
  const entries::EntrySpecRange sources[] = {
      entries::getParticleEntries(),    entries::getCompoundParticleEntries(), entries::getAuxiliaryEntries(),
      entries::getConjunctionEntries(), entries::getDeterminerEntries(),       entries::getPronounEntries(),
      entries::getFormalNounEntries(),  entries::getInterjectionEntries(),
  };
  std::set<std::tuple<std::string, core::PartOfSpeech, core::ExtendedPOS, std::string>> unique_entries;
  for (const auto source : sources) {
    for (const auto& entry : source) {
      const auto key =
          std::make_tuple(std::string(entry.surface), entry.pos, entry.extended_pos, std::string(entry.lemma));
      EXPECT_TRUE(unique_entries.insert(key).second) << "duplicate L1 entry: " << entry.surface;
    }
  }
}

TEST(CoreDictionaryTest, ContractedCompletiveParadigmsRemainAuxiliaries) {
  constexpr std::string_view kChauForms[] = {"ちゃう", "ちゃわ", "ちゃい", "ちゃっ", "ちゃえ", "ちゃお"};
  constexpr std::string_view kJauForms[] = {"じゃう", "じゃわ", "じゃい", "じゃっ", "じゃえ", "じゃお"};
  const auto entries = entries::getAuxiliaryEntries();
  CoreDictionary dict;

  const auto expect_auxiliary_paradigm = [&entries, &dict](const auto& forms, std::string_view lemma) {
    for (const auto form : forms) {
      const auto entry = std::find_if(entries.begin(), entries.end(), [form, lemma](const auto& candidate) {
        return std::string_view(candidate.surface) == form && std::string_view(candidate.lemma) == lemma &&
               candidate.extended_pos == core::ExtendedPOS::AuxAspectShimau;
      });
      ASSERT_NE(entry, entries.end()) << "missing contracted form: " << form;
      EXPECT_EQ(entry->pos, core::PartOfSpeech::Auxiliary) << "contracted form: " << form;

      const auto* materialized = dict.lookupExact(form, core::PartOfSpeech::Auxiliary);
      ASSERT_NE(materialized, nullptr) << "missing materialized contracted form: " << form;
      EXPECT_EQ(materialized->extended_pos, core::ExtendedPOS::AuxAspectShimau) << "contracted form: " << form;
      EXPECT_EQ(materialized->lemma, lemma) << "contracted form: " << form;
    }
  };

  expect_auxiliary_paradigm(kChauForms, "ちゃう");
  expect_auxiliary_paradigm(kJauForms, "じゃう");
}

TEST(CoreDictionaryTest, QuotativeAdverbsUseTheirDedicatedExtendedPos) {
  CoreDictionary dict;
  for (const std::string_view surface : {"そう", "どう"}) {
    const auto* entry = dict.lookupExact(surface, core::PartOfSpeech::Adverb);
    ASSERT_NE(entry, nullptr) << surface;
    EXPECT_EQ(entry->extended_pos, core::ExtendedPOS::AdverbQuotative) << surface;
  }
}

TEST(CoreDictionaryTest, ClosedAuxiliaryAndBoundAdjectiveParadigmsAreComplete) {
  CoreDictionary dict;

  const auto expect_auxiliary_forms = [&dict](std::initializer_list<std::string_view> forms, std::string_view lemma,
                                              core::ExtendedPOS extended_pos) {
    for (const auto form : forms) {
      const auto* entry = dict.lookupExact(form, core::PartOfSpeech::Auxiliary);
      ASSERT_NE(entry, nullptr) << form;
      EXPECT_EQ(entry->extended_pos, extended_pos) << form;
      EXPECT_EQ(entry->lemma, lemma) << form;
      EXPECT_EQ(core::extendedPosToPos(entry->extended_pos), core::PartOfSpeech::Auxiliary) << form;
    }
  };

  expect_auxiliary_forms({"たい", "たく", "たかっ", "たけれ"}, "たい", core::ExtendedPOS::AuxDesireTai);
  expect_auxiliary_forms({"ない", "なく", "なかっ", "なけれ", "なかろ"}, "ない", core::ExtendedPOS::AuxNegativeNai);
  expect_auxiliary_forms({"ます", "まし", "ませ", "ましょ", "ますれ"}, "ます", core::ExtendedPOS::AuxTenseMasu);
  expect_auxiliary_forms({"れ", "れる", "れれ", "れよ"}, "れる", core::ExtendedPOS::AuxPassive);
  expect_auxiliary_forms({"られ", "られる", "られれ", "られよ"}, "られる", core::ExtendedPOS::AuxPassive);
  expect_auxiliary_forms({"せ", "せる", "せれ", "せろ", "せよ"}, "せる", core::ExtendedPOS::AuxCausative);
  expect_auxiliary_forms({"させ", "させる", "させれ", "させろ", "させよ"}, "させる", core::ExtendedPOS::AuxCausative);

  struct AdjectiveFamily {
    std::string_view stem;
    std::string_view lemma;
  };
  constexpr AdjectiveFamily kFamilies[] = {
      {"にく", "にくい"}, {"やす", "やすい"}, {"がた", "がたい"},
      {"づら", "づらい"}, {"っぽ", "っぽい"}, {"ほし", "ほしい"},
  };
  struct AdjectiveCell {
    std::string_view suffix;
    core::ExtendedPOS extended_pos;
  };
  constexpr AdjectiveCell kCells[] = {
      {"い", core::ExtendedPOS::AdjBasic},      {"く", core::ExtendedPOS::AdjRenyokei},
      {"かっ", core::ExtendedPOS::AdjKatt},     {"けれ", core::ExtendedPOS::AdjKeForm},
      {"かろ", core::ExtendedPOS::AdjMizenkei}, {"", core::ExtendedPOS::AdjStem},
  };

  for (const auto& family : kFamilies) {
    for (const auto& cell : kCells) {
      const std::string surface = std::string(family.stem) + std::string(cell.suffix);
      const auto* entry = dict.lookupExact(surface, core::PartOfSpeech::Adjective);
      ASSERT_NE(entry, nullptr) << surface;
      EXPECT_EQ(entry->extended_pos, cell.extended_pos) << surface;
      EXPECT_EQ(entry->lemma, family.lemma) << surface;
      EXPECT_EQ(core::extendedPosToPos(entry->extended_pos), core::PartOfSpeech::Adjective) << surface;
    }
  }
}

TEST(CoreDictionaryTest, YasuiStemIsNotAnUnconditionalHonorificAuxiliary) {
  CoreDictionary dict;

  EXPECT_EQ(dict.lookupExact("やす", core::PartOfSpeech::Auxiliary), nullptr);
  const auto* adjective_stem = dict.lookupExact("やす", core::PartOfSpeech::Adjective);
  ASSERT_NE(adjective_stem, nullptr);
  EXPECT_EQ(adjective_stem->extended_pos, core::ExtendedPOS::AdjStem);
  EXPECT_EQ(adjective_stem->lemma, "やすい");
}

}  // namespace
}  // namespace suzume::dictionary
