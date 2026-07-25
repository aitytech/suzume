// Test case data structures for data-driven testing.

#pragma once

#include <ostream>
#include <string>
#include <vector>

#include "core/types.h"

namespace suzume::test {

// Expected morpheme in a test case
struct ExpectedMorpheme {
  std::string surface;
  std::string pos;    // String representation of POS (e.g., "Noun", "Verb")
  std::string lemma;  // Optional: empty if not checked

  // Convert string POS to PartOfSpeech enum
  core::PartOfSpeech posEnum() const;
};

// Instruction printed by every layer that detects a banned oracle override.
// Mirrored verbatim in scripts/check_oracle_overrides.py and the MCP review
// tools so the remediation reads the same wherever the tripwire is hit.
inline constexpr const char* kOracleOverrideRemediation =
    "A test case must not carry its own oracle. Encode the intentional MeCab difference as a\n"
    "normalization rule under scripts/mcp/src/suzume_mcp/core/ (merge_rules.py, split_rules.py,\n"
    "postprocessors.py, pos_mapping.py), then sync expectations with\n"
    "test_needs_suzume_update(apply=True) and drop the field with test_reset_suzume(apply=True).\n"
    "See AGENTS.md section 7 (Tokenization Design).";

// Oracle-override metadata. Banned; parsed only to be rejected.
struct AcceptedDiff {
  std::string reason;    // Why the difference was claimed to be acceptable
  std::string category;  // Claimed category (e.g., "pos-limitation", "lemma-diff")
};

// A single test case
struct TestCase {
  std::string id;                          // Unique identifier
  std::string input;                       // Input text to analyze
  std::vector<ExpectedMorpheme> expected;  // Expected morphemes (the oracle's output)
  std::vector<std::string> tags;           // Tags for filtering (e.g., "verb", "basic")
  std::string description;                 // Optional description

  // Banned per-case oracle overrides, deliberately still parsed.
  //
  // These once let a single case promote Suzume's current output to the expectation,
  // which silences that one case instead of generalizing the rule. The oracle now
  // lives entirely in the Python normalization pipeline.
  //
  // They are kept in the loader so that writing one is a hard, localized test
  // failure. Deleting the fields would make the JSON loader skip the keys as
  // unknown, and an override would slip in silently — the opposite of the intent.
  std::vector<ExpectedMorpheme> suzume_expected;
  AcceptedDiff accepted_diff;

  // Check if this test case has a specific tag
  bool hasTag(const std::string& tag) const;

  // Check for a banned oracle override, whether the expectation or just the reason is present
  bool hasOracleOverride() const {
    return !suzume_expected.empty() || !accepted_diff.reason.empty() || !accepted_diff.category.empty();
  }
};

// Collection of test cases
struct TestSuite {
  std::string version;
  std::vector<TestCase> cases;

  // Filter cases by tag
  std::vector<TestCase> filterByTag(const std::string& tag) const;
};

// Google Test printing support
inline void PrintTo(const ExpectedMorpheme& m, std::ostream* os) {
  *os << "{surface: \"" << m.surface << "\"";
  if (!m.pos.empty()) {
    *os << ", pos: \"" << m.pos << "\"";
  }
  if (!m.lemma.empty()) {
    *os << ", lemma: \"" << m.lemma << "\"";
  }
  *os << "}";
}

inline void PrintTo(const TestCase& tc, std::ostream* os) {
  *os << tc.id << ": \"" << tc.input << "\"";
}

}  // namespace suzume::test
