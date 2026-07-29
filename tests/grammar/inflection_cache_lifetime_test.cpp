/**
 * @file inflection_cache_lifetime_test.cpp
 * @brief References from Inflection::analyze() survive later analyze() calls.
 *
 * Candidate generators routinely bind a result and keep reading it while
 * analysing a related surface. Clearing the active cache from inside analyze()
 * when it reaches its bound leaves those callers with a dangling reference,
 * which surfaces as garbled lemmas on long inputs. A full active generation is
 * instead retained as the previous generation before a new one begins.
 *
 * A failure here means a cache generation was cleared instead of being retained
 * across its first rollover; run this file under AddressSanitizer to see the
 * use-after-free directly.
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "grammar/inflection.h"

namespace suzume::grammar {
namespace {

/// Comfortably above the 4,096-entry cache-generation bound, so a rollover
/// during analyze() is guaranteed to fire.
constexpr int kSurfacesForcingRollover = 6000;
constexpr size_t kMaxGenerationEntries = 4096;

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

TEST(InflectionCacheLifetimeTest, CacheGenerationsStayBoundedWithinOneChunk) {
  Inflection inflection;

  // One long analyzer chunk can issue far more distinct inflection probes than
  // the per-generation bound. Repeated rollover must retain at most the active
  // and immediately previous generations, instead of retaining every probe.
  for (int index = 0; index < kSurfacesForcingRollover * 4; ++index) {
    inflection.analyze("食べ" + std::to_string(index) + "ました");
    EXPECT_LE(inflection.activeCacheSize(), kMaxGenerationEntries);
    EXPECT_LE(inflection.previousCacheSize(), kMaxGenerationEntries);
  }

  EXPECT_LE(inflection.activeCacheSize() + inflection.previousCacheSize(), kMaxGenerationEntries * 2);
}

}  // namespace
}  // namespace suzume::grammar
