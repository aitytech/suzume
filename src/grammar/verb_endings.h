/**
 * @file verb_endings.h
 * @brief Verb ending patterns for reverse inflection analysis
 */

#ifndef SUZUME_GRAMMAR_VERB_ENDINGS_H_
#define SUZUME_GRAMMAR_VERB_ENDINGS_H_

#include <array>
#include <cstddef>
#include <string>

#include "conjugation.h"
#include "connection.h"

namespace suzume::grammar {

// This is the canonical closed verb-paradigm order.  Consumers that need the
// complete paradigm iterate this table instead of open-coding connection-ID
// ranges, whose intervening IDs include non-ConjForm categories.
constexpr std::array<ConjForm, static_cast<size_t>(ConjForm::Count_)> kAllVerbConjForms = {
    ConjForm::Base,     ConjForm::Mizenkei,  ConjForm::Renyokei, ConjForm::Onbinkei,
    ConjForm::Kateikei, ConjForm::Meireikei, ConjForm::Ishikei,
};

constexpr bool hasEveryVerbConjFormExactlyOnce() {
  std::array<bool, static_cast<size_t>(ConjForm::Count_)> seen{};
  for (ConjForm form : kAllVerbConjForms) {
    const size_t index = static_cast<size_t>(form);
    if (index >= seen.size() || seen[index]) {
      return false;
    }
    seen[index] = true;
  }
  for (bool is_seen : seen) {
    if (!is_seen) {
      return false;
    }
  }
  return true;
}

static_assert(hasEveryVerbConjFormExactlyOnce(), "Every ConjForm must have exactly one canonical table cell");

constexpr std::array<uint16_t, static_cast<size_t>(ConjForm::Count_)> kVerbConjFormConnections = {
    conn::kVerbBase,  conn::kVerbMizenkei,  conn::kVerbRenyokei,   conn::kVerbOnbinkei,
    conn::kVerbKatei, conn::kVerbMeireikei, conn::kVerbVolitional,
};

/**
 * @brief Verb ending pattern for reverse lookup
 *
 * Used to identify verb stems by matching ending patterns and
 * determining what connection ID the stem provides.
 */
struct VerbEnding {
  std::string suffix;       ///< Ending suffix to match (e.g., "い", "き", "")
  std::string base_suffix;  ///< Base form suffix to restore (e.g., "く", "る")
  VerbType verb_type;       ///< Verb conjugation type
  bool is_onbin;            ///< True if this is euphonic (音便) form
};

class VerbEndingRange {
 public:
  constexpr VerbEndingRange(const VerbEnding* data, size_t size) : data_(data), size_(size) {}

  const VerbEnding* begin() const { return data_; }
  const VerbEnding* end() const { return data_ + size_; }
  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

 private:
  const VerbEnding* data_;
  size_t size_;
};

/**
 * @brief Get verb endings grouped by provides_conn value for efficient lookup
 * @return Non-owning range of matching verb endings
 *
 * Avoids scanning all ~120 endings when only a specific connection type is needed.
 */
VerbEndingRange getVerbEndingsByConn(uint16_t provides_conn);

/** @brief Get all reverse endings for a canonical conjugation-form cell. */
VerbEndingRange getVerbEndingsByForm(ConjForm form);

}  // namespace suzume::grammar

#endif  // SUZUME_GRAMMAR_VERB_ENDINGS_H_
