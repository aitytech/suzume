/**
 * @file verb_endings.h
 * @brief Verb ending patterns for reverse inflection analysis
 */

#ifndef SUZUME_GRAMMAR_VERB_ENDINGS_H_
#define SUZUME_GRAMMAR_VERB_ENDINGS_H_

#include <cstddef>
#include <string>

#include "conjugation.h"
#include "connection.h"

namespace suzume::grammar {

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

}  // namespace suzume::grammar

#endif  // SUZUME_GRAMMAR_VERB_ENDINGS_H_
