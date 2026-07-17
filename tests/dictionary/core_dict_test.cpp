#include "dictionary/core_dict.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string_view>
#include <vector>

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

}  // namespace
}  // namespace suzume::dictionary
