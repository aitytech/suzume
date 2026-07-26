#include "grammar/dictionary_expansion.h"

#include <gtest/gtest.h>

#include <algorithm>

#include "grammar/conjugation.h"

namespace suzume {
namespace grammar {
namespace {

const dictionary::DictionaryEntry* findSurface(const std::vector<dictionary::DictionaryEntry>& entries,
                                               std::string_view surface) {
  auto found =
      std::find_if(entries.begin(), entries.end(), [surface](const auto& entry) { return entry.surface == surface; });
  return found == entries.end() ? nullptr : &*found;
}

TEST(DictionaryExpansionTest, ExpandsExplicitVerbWithLemmaAndExtendedPos) {
  const dictionary::SourceEntry source{"テストする", core::PartOfSpeech::Verb, dictionary::ConjugationType::Suru, "",
                                       1};

  auto expanded = expandDictionarySourceEntry(source);

  const auto* conditional = findSurface(expanded, "テストすれば");
  ASSERT_NE(conditional, nullptr);
  EXPECT_EQ(conditional->lemma, "テストする");
  EXPECT_EQ(conditional->pos, core::PartOfSpeech::Verb);
  EXPECT_EQ(conditional->extended_pos, core::ExtendedPOS::VerbKateikei);
}

TEST(DictionaryExpansionTest, KeepsLegacyInflectedCsvSurfaceLiteral) {
  const dictionary::SourceEntry source{"テストした", core::PartOfSpeech::Verb, dictionary::ConjugationType::None,
                                       "テストする", 1};

  auto expanded = expandDictionarySourceEntry(source);

  ASSERT_EQ(expanded.size(), 1);
  EXPECT_EQ(expanded[0].surface, "テストした");
  EXPECT_EQ(expanded[0].lemma, "テストする");
}

TEST(DictionaryExpansionTest, ReusesKuruKanjiAndKanaForms) {
  const dictionary::SourceEntry source{"来る", core::PartOfSpeech::Verb, dictionary::ConjugationType::Kuru, "", 1};

  auto expanded = expandDictionarySourceEntries({source});

  const auto* kanji = findSurface(expanded.entries, "来れ");
  const auto* kana = findSurface(expanded.entries, "くれ");
  ASSERT_NE(kanji, nullptr);
  ASSERT_NE(kana, nullptr);
  EXPECT_EQ(kanji->lemma, "来る");
  EXPECT_EQ(kana->lemma, "くる");
  EXPECT_EQ(kanji->extended_pos, core::ExtendedPOS::VerbKateikei);
  EXPECT_EQ(kana->extended_pos, core::ExtendedPOS::VerbKateikei);
  EXPECT_EQ(findSurface(expanded.entries, "き"), nullptr);
}

TEST(DictionaryExpansionTest, ConsumesCanonicalKuruStemForms) {
  const dictionary::SourceEntry source{"来る", core::PartOfSpeech::Verb, dictionary::ConjugationType::Kuru, "", 1};
  const KuruStemForms kanji = getKuruStemForms("来る");
  const KuruStemForms kana = getKuruStemForms("くる");

  const auto expanded = expandDictionarySourceEntry(source);

  for (const auto* surface : {&kanji.base, &kanji.mizenkei, &kanji.renyokei, &kanji.kateikei, &kanji.ishikei,
                              &kanji.meireikei, &kana.base, &kana.kateikei, &kana.ishikei, &kana.meireikei}) {
    EXPECT_NE(findSurface(expanded, *surface), nullptr) << *surface;
  }
  // Ambiguous one-mora kana stems remain reverse-inflection candidates instead
  // of receiving the dictionary short-verb bonus.
  EXPECT_EQ(findSurface(expanded, kana.mizenkei), nullptr);
  EXPECT_EQ(findSurface(expanded, kana.renyokei), nullptr);
}

TEST(DictionaryExpansionTest, KeepsNominalSurfaceAheadOfConjugationCollision) {
  const dictionary::SourceEntry noun{"テストすれば", core::PartOfSpeech::Noun, dictionary::ConjugationType::None, "",
                                     1};
  const dictionary::SourceEntry verb{"テストする", core::PartOfSpeech::Verb, dictionary::ConjugationType::Suru, "", 2};

  auto expanded = expandDictionarySourceEntries({verb, noun});

  const auto* collision = findSurface(expanded.entries, "テストすれば");
  ASSERT_NE(collision, nullptr);
  EXPECT_EQ(collision->pos, core::PartOfSpeech::Noun);
  EXPECT_GT(expanded.duplicates_skipped, 0);
}

}  // namespace
}  // namespace grammar
}  // namespace suzume
