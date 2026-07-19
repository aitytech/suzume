#include "dictionary/entries/packed_core_entries.h"

#include <gtest/gtest.h>

#include "dictionary/core_dict.h"

namespace suzume::dictionary {
namespace {

TEST(PackedCoreEntriesTest, MatchesTheNativeCoreDictionaryTupleByTuple) {
  const entries::PackedCoreEntryRange packed_entries = entries::getPackedCoreEntries();
  const char* string_data = reinterpret_cast<const char*>(packed_entries.stringData());
  CoreDictionary dictionary;

  ASSERT_EQ(dictionary.size(), packed_entries.size());
  for (size_t index = 0; index < packed_entries.size(); ++index) {
    const auto* actual = dictionary.getEntry(static_cast<uint32_t>(index));
    ASSERT_NE(actual, nullptr) << "entry index=" << index;

    const entries::PackedCoreEntry& packed = packed_entries.begin()[index];
    EXPECT_EQ(actual->surface, string_data + packed.surface_offset) << "entry index=" << index;
    EXPECT_EQ(actual->pos, static_cast<core::PartOfSpeech>(packed.pos)) << "entry index=" << index;
    EXPECT_EQ(actual->extended_pos, static_cast<core::ExtendedPOS>(packed.extended_pos)) << "entry index=" << index;
    EXPECT_EQ(actual->lemma, string_data + packed.lemma_offset) << "entry index=" << index;
  }
}

}  // namespace
}  // namespace suzume::dictionary
