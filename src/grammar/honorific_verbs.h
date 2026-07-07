/**
 * @file honorific_verbs.h
 * @brief Closed-class honorific and benefactive subsidiary verb data
 *
 * Canonical source for the renyokei surfaces of honorific (敬語) and
 * benefactive (授受) subsidiary verbs. These live in the L2 dictionary as
 * ordinary VERB entries, so conjugated forms receive the generic
 * ExtendedPOS::VerbRenyokei; connection rules that need to single them out
 * use the predicates below instead of scattered surface literals.
 */

#ifndef SUZUME_GRAMMAR_HONORIFIC_VERBS_H_
#define SUZUME_GRAMMAR_HONORIFIC_VERBS_H_

#include <string_view>

namespace suzume::grammar {

/**
 * @brief Renyokei surfaces of humble/honorific verbs (いたす・くださる・いただく)
 *
 * Closed class. These verbs follow another verb's renyokei as honorific
 * subsidiary verbs (お願い+いたし+ます) and directly precede polite ます,
 * where they are prone to over-splitting (いただき → い+た+だき).
 */
inline constexpr std::string_view kHumbleHonorificRenyokei[] = {"いたし", "くださ", "いただき"};

/**
 * @brief Renyokei surfaces of benefactive subsidiary verbs (もらう・あげる)
 *
 * Closed class. These extend the humble/honorific set when attached after
 * another verb's renyokei (見て+もらい+ます, 食べて+あげ+ます).
 */
inline constexpr std::string_view kBenefactiveRenyokei[] = {"もらい", "あげ"};

/**
 * @brief Check whether a surface is the renyokei of a humble/honorific verb
 * @param surface Candidate token surface (UTF-8)
 * @return true if the surface is a renyokei of いたす, くださる, or いただく
 */
inline bool isHumbleHonorificRenyokei(std::string_view surface) {
  for (const std::string_view entry : kHumbleHonorificRenyokei) {
    if (entry == surface) {
      return true;
    }
  }
  return false;
}

/**
 * @brief Check whether a surface is the renyokei of any honorific or
 *        benefactive subsidiary verb (いたす・くださる・いただく・もらう・あげる)
 * @param surface Candidate token surface (UTF-8)
 * @return true if the surface belongs to the closed subsidiary verb set
 */
inline bool isSubsidiaryHonorificRenyokei(std::string_view surface) {
  if (isHumbleHonorificRenyokei(surface)) {
    return true;
  }
  for (const std::string_view entry : kBenefactiveRenyokei) {
    if (entry == surface) {
      return true;
    }
  }
  return false;
}

}  // namespace suzume::grammar

#endif  // SUZUME_GRAMMAR_HONORIFIC_VERBS_H_
