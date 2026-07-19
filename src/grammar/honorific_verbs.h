/**
 * @file honorific_verbs.h
 * @brief Closed-class honorific, benefactive, and modal subsidiary verb data
 *
 * Predicates for closed-class honorific (敬語), benefactive (授受), and modal
 * (可能否定 かねる) subsidiary verbs. Their static tables have one owner in the
 * grammar implementation; callers use these predicates instead of scattered
 * surface literals.
 */

#ifndef SUZUME_GRAMMAR_HONORIFIC_VERBS_H_
#define SUZUME_GRAMMAR_HONORIFIC_VERBS_H_

#include <string_view>

namespace suzume::grammar {

/**
 * @brief Check whether a surface is the renyokei of a humble/honorific verb
 * @param surface Candidate token surface (UTF-8)
 * @return true if the surface is a renyokei of いたす, くださる, or いただく
 */
bool isHumbleHonorificRenyokei(std::string_view surface);

/** Return true for a humble/honorific subsidiary verb lemma. */
bool isHumbleHonorificLemma(std::string_view lemma);

/** Return true for a potential benefactive subsidiary verb lemma. */
bool isPotentialBenefactiveLemma(std::string_view lemma);

/**
 * @brief Check whether a surface is the renyokei of any honorific or
 *        benefactive subsidiary verb (いたす・くださる・いただく・もらう・あげる)
 * @param surface Candidate token surface (UTF-8)
 * @return true if the surface belongs to the closed subsidiary verb set
 */
bool isSubsidiaryHonorificRenyokei(std::string_view surface);

/**
 * @brief Check whether a surface is the renyokei/mizenkei of the modal
 *        subsidiary verb かねる
 * @param surface Candidate token surface (UTF-8)
 * @return true if the surface is a conjugated stem of かねる
 */
bool isModalSubsidiaryRenyokei(std::string_view surface);

}  // namespace suzume::grammar

#endif  // SUZUME_GRAMMAR_HONORIFIC_VERBS_H_
