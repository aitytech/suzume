#include "dictionary/double_array.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace suzume::dictionary {
namespace {

class DoubleArrayTest : public ::testing::Test {
 protected:
  DoubleArray trie_;
};

TEST_F(DoubleArrayTest, BuildEmpty) {
  std::vector<std::string> keys;
  std::vector<uint32_t> values;

  EXPECT_TRUE(trie_.build(keys, values));
  EXPECT_TRUE(trie_.empty());
}

TEST_F(DoubleArrayTest, BuildSingleKey) {
  std::vector<std::string> keys = {"hello"};
  std::vector<uint32_t> values = {42};

  EXPECT_TRUE(trie_.build(keys, values));
  EXPECT_FALSE(trie_.empty());

  EXPECT_EQ(trie_.exactMatch("hello"), 42);
  EXPECT_EQ(trie_.exactMatch("world"), -1);
  EXPECT_EQ(trie_.exactMatch("hell"), -1);
  EXPECT_EQ(trie_.exactMatch("hello!"), -1);
}

TEST_F(DoubleArrayTest, BuildMultipleKeys) {
  std::vector<std::string> keys = {"a", "ab", "abc", "b", "bc"};
  std::vector<uint32_t> values = {1, 2, 3, 4, 5};

  EXPECT_TRUE(trie_.build(keys, values));

  EXPECT_EQ(trie_.exactMatch("a"), 1);
  EXPECT_EQ(trie_.exactMatch("ab"), 2);
  EXPECT_EQ(trie_.exactMatch("abc"), 3);
  EXPECT_EQ(trie_.exactMatch("b"), 4);
  EXPECT_EQ(trie_.exactMatch("bc"), 5);
  EXPECT_EQ(trie_.exactMatch("c"), -1);
  EXPECT_EQ(trie_.exactMatch("abcd"), -1);
}

TEST_F(DoubleArrayTest, BuildUnsortedFails) {
  std::vector<std::string> keys = {"b", "a"};  // Not sorted
  std::vector<uint32_t> values = {1, 2};

  EXPECT_FALSE(trie_.build(keys, values));
}

TEST_F(DoubleArrayTest, BuildDuplicateFails) {
  std::vector<std::string> keys = {"a", "a"};  // Duplicate
  std::vector<uint32_t> values = {1, 2};

  EXPECT_FALSE(trie_.build(keys, values));
}

TEST_F(DoubleArrayTest, BuildMismatchedSizeFails) {
  std::vector<std::string> keys = {"a", "b"};
  std::vector<uint32_t> values = {1};  // Size mismatch

  EXPECT_FALSE(trie_.build(keys, values));
}

TEST_F(DoubleArrayTest, BuildRejectsNegativeSignedValue) {
  std::vector<std::string> keys = {"a"};
  std::vector<int32_t> values = {-1};

  EXPECT_FALSE(trie_.build(keys, values));
  EXPECT_TRUE(trie_.empty());
}

TEST_F(DoubleArrayTest, BuildRejectsUintValueOutsideSignedResultRange) {
  std::vector<std::string> keys = {"a"};
  std::vector<uint32_t> values = {static_cast<uint32_t>(INT32_MAX) + 1u};

  EXPECT_FALSE(trie_.build(keys, values));
  EXPECT_TRUE(trie_.empty());
}

TEST_F(DoubleArrayTest, CommonPrefixSearchBasic) {
  std::vector<std::string> keys = {"a", "ab", "abc", "abcd"};
  std::vector<uint32_t> values = {1, 2, 3, 4};

  EXPECT_TRUE(trie_.build(keys, values));

  auto results = trie_.commonPrefixSearch("abcde");

  EXPECT_EQ(results.size(), 4u);
  EXPECT_EQ(results[0].value, 1);
  EXPECT_EQ(results[0].length, 1u);
  EXPECT_EQ(results[1].value, 2);
  EXPECT_EQ(results[1].length, 2u);
  EXPECT_EQ(results[2].value, 3);
  EXPECT_EQ(results[2].length, 3u);
  EXPECT_EQ(results[3].value, 4);
  EXPECT_EQ(results[3].length, 4u);
}

TEST_F(DoubleArrayTest, CommonPrefixSearchWithStart) {
  std::vector<std::string> keys = {"a", "ab", "b", "bc"};
  std::vector<uint32_t> values = {1, 2, 3, 4};

  EXPECT_TRUE(trie_.build(keys, values));

  auto results = trie_.commonPrefixSearch("xbc", 1);

  EXPECT_EQ(results.size(), 2u);
  EXPECT_EQ(results[0].value, 3);  // "b"
  EXPECT_EQ(results[0].length, 1u);
  EXPECT_EQ(results[1].value, 4);  // "bc"
  EXPECT_EQ(results[1].length, 2u);
}

TEST_F(DoubleArrayTest, CommonPrefixSearchMaxResults) {
  std::vector<std::string> keys = {"a", "ab", "abc", "abcd"};
  std::vector<uint32_t> values = {1, 2, 3, 4};

  EXPECT_TRUE(trie_.build(keys, values));

  auto results = trie_.commonPrefixSearch("abcde", 0, 2);

  EXPECT_EQ(results.size(), 2u);
  EXPECT_EQ(results[0].value, 1);
  EXPECT_EQ(results[1].value, 2);
}

TEST_F(DoubleArrayTest, CommonPrefixSearchNoMatch) {
  std::vector<std::string> keys = {"a", "ab"};
  std::vector<uint32_t> values = {1, 2};

  EXPECT_TRUE(trie_.build(keys, values));

  auto results = trie_.commonPrefixSearch("xyz");
  EXPECT_TRUE(results.empty());
}

TEST_F(DoubleArrayTest, JapaneseText) {
  std::vector<std::string> keys = {
      "あ",      // Hiragana A
      "あい",    // Hiragana AI
      "東",      // Kanji East
      "東京",    // Tokyo
      "東京都",  // Tokyo Metropolis
  };
  std::vector<uint32_t> values = {1, 2, 3, 4, 5};

  // Sort keys (UTF-8 byte order)
  std::vector<std::pair<std::string, uint32_t>> pairs;
  for (size_t idx = 0; idx < keys.size(); ++idx) {
    pairs.emplace_back(keys[idx], values[idx]);
  }
  std::sort(pairs.begin(), pairs.end());

  std::vector<std::string> sorted_keys;
  std::vector<uint32_t> sorted_values;
  for (const auto& kv_pair : pairs) {
    sorted_keys.push_back(kv_pair.first);
    sorted_values.push_back(kv_pair.second);
  }

  EXPECT_TRUE(trie_.build(sorted_keys, sorted_values));

  // Test exact match
  for (size_t idx = 0; idx < sorted_keys.size(); ++idx) {
    EXPECT_EQ(trie_.exactMatch(sorted_keys[idx]), static_cast<int32_t>(sorted_values[idx]));
  }

  // Test common prefix search
  auto results = trie_.commonPrefixSearch("東京都庁");
  EXPECT_GE(results.size(), 3u);  // Should match "東", "東京", "東京都"
}

TEST_F(DoubleArrayTest, EnumerateReconstructsByteKeysAndValues) {
  std::vector<std::string> keys = {"a", "ab", "東京", "東京都"};
  std::sort(keys.begin(), keys.end());
  std::vector<uint32_t> values = {30, 10, 40, 20};
  ASSERT_TRUE(trie_.build(keys, values));

  std::vector<DoubleArray::KeyValue> key_values;
  ASSERT_TRUE(trie_.enumerate(key_values));
  ASSERT_EQ(key_values.size(), keys.size());

  std::sort(key_values.begin(), key_values.end(),
            [](const DoubleArray::KeyValue& lhs, const DoubleArray::KeyValue& rhs) { return lhs.key < rhs.key; });
  for (size_t idx = 0; idx < keys.size(); ++idx) {
    EXPECT_EQ(key_values[idx].key, keys[idx]);
    EXPECT_EQ(key_values[idx].value, static_cast<int32_t>(values[idx]));
  }
}

TEST_F(DoubleArrayTest, EnumerateEmptyTrie) {
  std::vector<DoubleArray::KeyValue> key_values = {{"old", 1}};
  EXPECT_TRUE(trie_.enumerate(key_values));
  EXPECT_TRUE(key_values.empty());
}

TEST_F(DoubleArrayTest, SerializeDeserialize) {
  std::vector<std::string> keys = {"a", "ab", "abc", "b", "bc"};
  std::vector<uint32_t> values = {10, 20, 30, 40, 50};

  EXPECT_TRUE(trie_.build(keys, values));

  // Serialize
  auto data = trie_.serialize();
  EXPECT_GT(data.size(), 8u);
  EXPECT_EQ(std::string(data.begin(), data.begin() + 4), "DA04");
  EXPECT_LT(data.size(), 8u + trie_.size() * 8u);

  // Create new trie and deserialize
  DoubleArray trie2;
  EXPECT_TRUE(trie2.deserialize(data.data(), data.size()));

  // Verify same behavior
  EXPECT_EQ(trie2.exactMatch("a"), 10);
  EXPECT_EQ(trie2.exactMatch("ab"), 20);
  EXPECT_EQ(trie2.exactMatch("abc"), 30);
  EXPECT_EQ(trie2.exactMatch("b"), 40);
  EXPECT_EQ(trie2.exactMatch("bc"), 50);
  EXPECT_EQ(trie2.exactMatch("c"), -1);
}

TEST_F(DoubleArrayTest, SerializeFallsBackToDa03ForWideValue) {
  std::vector<std::string> keys = {"a"};
  std::vector<uint32_t> values = {70000};
  ASSERT_TRUE(trie_.build(keys, values));

  const std::vector<uint8_t> data = trie_.serialize();
  ASSERT_GE(data.size(), 8u);
  EXPECT_EQ(std::string(data.begin(), data.begin() + 4), "DA03");

  DoubleArray loaded;
  ASSERT_TRUE(loaded.deserialize(data.data(), data.size()));
  EXPECT_EQ(loaded.exactMatch("a"), 70000);
}

TEST_F(DoubleArrayTest, DeserializeLegacyDa03) {
  // Hand-built DA03 for key "a" -> 7. The four units are the root, leaf,
  // unused slot, and byte-transition node respectively.
  std::vector<uint8_t> legacy_data(8 + 4 * 8, 0);
  legacy_data[0] = 'D';
  legacy_data[1] = 'A';
  legacy_data[2] = '0';
  legacy_data[3] = '3';
  const uint32_t unit_count = 4;
  const uint32_t units[] = {98, 0, 0x80000007U, 4, 0, 0, 1, 1};
  std::memcpy(legacy_data.data() + 4, &unit_count, sizeof(unit_count));
  std::memcpy(legacy_data.data() + 8, units, sizeof(units));

  DoubleArray loaded;
  ASSERT_TRUE(loaded.deserialize(legacy_data.data(), legacy_data.size()));
  EXPECT_EQ(loaded.exactMatch("a"), 7);
  EXPECT_EQ(loaded.exactMatch("b"), -1);
}

TEST_F(DoubleArrayTest, DeserializeInvalidData) {
  DoubleArray trie2;

  // Too short
  std::vector<uint8_t> short_data = {'D', 'A', '0', '1'};
  EXPECT_FALSE(trie2.deserialize(short_data.data(), short_data.size()));
  EXPECT_FALSE(trie2.deserialize(nullptr, 8));

  // Wrong magic number
  std::vector<uint8_t> bad_magic = {'X', 'X', 'X', 'X', 0, 0, 0, 0};
  EXPECT_FALSE(trie2.deserialize(bad_magic.data(), bad_magic.size()));

  // A DA02 blob (previous format) must be rejected: its check == parent encoding
  // would be misread as parent + 1 by the current lookup.
  std::vector<uint8_t> old_format = {'D', 'A', '0', '2', 0, 0, 0, 0};
  EXPECT_FALSE(trie2.deserialize(old_format.data(), old_format.size()));

  // Unit count claims far more data than the buffer contains. This must be
  // rejected without overflowing the expected-size calculation on 32-bit builds.
  std::vector<uint8_t> huge_count = {'D', 'A', '0', '3', 0, 0, 0, 0};
  uint32_t num_units = UINT32_MAX;
  std::memcpy(huge_count.data() + 4, &num_units, sizeof(num_units));
  EXPECT_FALSE(trie2.deserialize(huge_count.data(), huge_count.size()));

  // DA04 needs at least one occupancy byte for a non-empty trie.
  std::vector<uint8_t> missing_bitmap = {'D', 'A', '0', '4', 1, 0, 0, 0};
  EXPECT_FALSE(trie2.deserialize(missing_bitmap.data(), missing_bitmap.size()));

  // The compact format is intentionally limited to 16-bit unit positions.
  std::vector<uint8_t> wide_count = {'D', 'A', '0', '4', 0, 0, 1, 0};
  EXPECT_FALSE(trie2.deserialize(wide_count.data(), wide_count.size()));

  std::vector<std::string> keys = {"a"};
  std::vector<uint32_t> values = {7};
  ASSERT_TRUE(trie_.build(keys, values));
  std::vector<uint8_t> truncated = trie_.serialize();
  ASSERT_EQ(std::string(truncated.begin(), truncated.begin() + 4), "DA04");
  truncated.pop_back();
  EXPECT_FALSE(trie2.deserialize(truncated.data(), truncated.size()));

  // For the single-key trie, the second internal record is terminal. Pointing
  // its base at the occupied root would make the reconstructed leaf overlap it.
  std::vector<uint8_t> overlapping_leaf = trie_.serialize();
  ASSERT_GE(overlapping_leaf.size(), 16u);
  overlapping_leaf[14] = 0;
  overlapping_leaf[15] = 0;
  EXPECT_FALSE(trie2.deserialize(overlapping_leaf.data(), overlapping_leaf.size()));
}

TEST_F(DoubleArrayTest, DeserializeFailurePreservesExistingTrie) {
  std::vector<std::string> keys = {"a"};
  std::vector<uint32_t> values = {7};
  EXPECT_TRUE(trie_.build(keys, values));

  std::vector<uint8_t> huge_count = {'D', 'A', '0', '3', 0, 0, 0, 0};
  uint32_t num_units = UINT32_MAX;
  std::memcpy(huge_count.data() + 4, &num_units, sizeof(num_units));
  EXPECT_FALSE(trie_.deserialize(huge_count.data(), huge_count.size()));
  EXPECT_EQ(trie_.exactMatch("a"), 7);

  std::vector<uint8_t> truncated = trie_.serialize();
  ASSERT_EQ(std::string(truncated.begin(), truncated.begin() + 4), "DA04");
  truncated.pop_back();
  EXPECT_FALSE(trie_.deserialize(truncated.data(), truncated.size()));
  EXPECT_EQ(trie_.exactMatch("a"), 7);
}

// Regression: a byte that is not a child of the root must never be accepted as a
// transition. Before the parent+1 check sentinel, an empty cell (check == 0) was
// indistinguishable from a genuine child of the root (parent_pos == 0), so a
// lookup whose first byte's XOR slot landed on an empty cell false-matched.
TEST_F(DoubleArrayTest, RootTransitionRejectsEmptyCells) {
  // Sparse first bytes force wide bases and interior empty cells.
  std::vector<std::string> keys = {std::string("\x01z", 2),
                                   std::string("\x7f"
                                               "z",
                                               2),
                                   std::string("\xf0"
                                               "z",
                                               2)};
  std::vector<uint32_t> values = {1, 2, 3};
  ASSERT_TRUE(trie_.build(keys, values));

  const auto is_root_child = [](int byte) { return byte == 0x01 || byte == 0x7f || byte == 0xf0; };
  for (int byte = 0; byte < 256; ++byte) {
    std::string one(1, static_cast<char>(byte));
    EXPECT_EQ(trie_.exactMatch(one), -1) << "single byte=" << byte;
    if (!is_root_child(byte)) {
      // A two-byte key whose first byte is not a root child must miss outright.
      EXPECT_TRUE(trie_.commonPrefixSearch(one + "z").empty()) << "prefix byte=" << byte;
    }
  }

  // Genuine members still resolve.
  EXPECT_EQ(trie_.exactMatch(std::string("\x01z", 2)), 1);
  EXPECT_EQ(trie_.exactMatch(std::string("\x7f"
                                         "z",
                                         2)),
            2);
  EXPECT_EQ(trie_.exactMatch(std::string("\xf0"
                                         "z",
                                         2)),
            3);
}

TEST_F(DoubleArrayTest, Clear) {
  std::vector<std::string> keys = {"a", "b"};
  std::vector<uint32_t> values = {1, 2};

  EXPECT_TRUE(trie_.build(keys, values));
  EXPECT_FALSE(trie_.empty());

  trie_.clear();
  EXPECT_TRUE(trie_.empty());
  EXPECT_EQ(trie_.exactMatch("a"), -1);
}

TEST_F(DoubleArrayTest, MemoryUsage) {
  std::vector<std::string> keys = {"a", "b", "c"};
  std::vector<uint32_t> values = {1, 2, 3};

  EXPECT_TRUE(trie_.build(keys, values));

  size_t usage = trie_.memoryUsage();
  EXPECT_GT(usage, 0u);
  // Memory usage should be related to the number of nodes
  // Each node uses 2 * sizeof(int32_t) = 8 bytes
  EXPECT_EQ(usage % 8, 0u);
}

}  // namespace
}  // namespace suzume::dictionary
