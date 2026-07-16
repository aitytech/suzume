#include "dictionary/core_dict.h"

#include <gtest/gtest.h>

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

}  // namespace
}  // namespace suzume::dictionary
