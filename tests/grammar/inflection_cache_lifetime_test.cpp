/**
 * @file inflection_cache_lifetime_test.cpp
 * @brief References from Inflection::analyze() survive later analyze() calls.
 *
 * Candidate generators routinely bind a result and keep reading it while
 * analysing a related surface. Discarding the cache from inside analyze() —
 * as a size-triggered rollover once did — leaves those callers with a dangling
 * reference, which surfaces as garbled lemmas on long inputs. The cache may
 * only be dropped at an explicit rollCache() point.
 *
 * A failure here means the rollover moved back inside analyze(); run this file
 * under AddressSanitizer to see the use-after-free directly.
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "grammar/inflection.h"

namespace suzume::grammar {
namespace {

/// Comfortably above Inflection::kMaxCacheEntries so any size-triggered
/// rollover inside analyze() is guaranteed to fire.
constexpr int kSurfacesForcingRollover = 6000;

std::vector<InflectionCandidate> snapshot(const std::vector<InflectionCandidate>& candidates) {
  return candidates;
}

TEST(InflectionCacheLifetimeTest, ReferenceSurvivesManyLaterAnalyses) {
  Inflection inflection;

  const std::vector<InflectionCandidate>& held = inflection.analyze("住んでいます");
  ASSERT_FALSE(held.empty());
  const std::vector<InflectionCandidate> expected = snapshot(held);

  // Distinct surfaces, so every call misses the cache and grows it.
  for (int index = 0; index < kSurfacesForcingRollover; ++index) {
    inflection.analyze("食べ" + std::to_string(index) + "ました");
  }

  ASSERT_EQ(held.size(), expected.size());
  for (size_t index = 0; index < expected.size(); ++index) {
    EXPECT_EQ(held[index].base_form, expected[index].base_form);
    EXPECT_EQ(held[index].stem, expected[index].stem);
    EXPECT_EQ(held[index].suffix, expected[index].suffix);
  }
}

TEST(InflectionCacheLifetimeTest, RollCacheKeepsResultsCorrect) {
  Inflection inflection;

  const std::vector<InflectionCandidate> before = snapshot(inflection.analyze("走りました"));
  ASSERT_FALSE(before.empty());

  for (int index = 0; index < kSurfacesForcingRollover; ++index) {
    inflection.analyze("読み" + std::to_string(index) + "ました");
  }
  inflection.rollCache();

  const std::vector<InflectionCandidate>& after = inflection.analyze("走りました");
  ASSERT_EQ(after.size(), before.size());
  for (size_t index = 0; index < before.size(); ++index) {
    EXPECT_EQ(after[index].base_form, before[index].base_form);
  }
}

}  // namespace
}  // namespace suzume::grammar
