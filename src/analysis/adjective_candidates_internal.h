/**
 * @file adjective_candidates_internal.h
 * @brief Shared UnknownCandidate factory helpers for the adjective candidate generators
 */

#ifndef SUZUME_ANALYSIS_ADJECTIVE_CANDIDATES_INTERNAL_H_
#define SUZUME_ANALYSIS_ADJECTIVE_CANDIDATES_INTERNAL_H_

#include <string>

#include "core/utf8_constants.h"
#include "unknown.h"

namespace suzume::analysis::adj_detail {

// =============================================================================
// Cost Helpers
// =============================================================================

/**
 * @brief Confidence-scaled candidate cost: base + (1 - confidence) * scale
 *
 * The shared shape for adjective candidate costs — a base cost that a higher
 * inflection confidence discounts toward (lower cost wins in the lattice).
 */
inline float confidenceScaledCost(float base, float confidence, float scale) {
  return base + (1.0F - confidence) * scale;
}

// =============================================================================
// UnknownCandidate Factory Helpers
// =============================================================================

/**
 * @brief Detect i-adjective EPOS based on surface ending
 */
inline core::ExtendedPOS detectIAdjEpos(const std::string& surface) {
  // Check surface ending to determine conjugation form
  // Note: Longer patterns must be checked first
  if (utf8::endsWith(surface, "かっ")) {
    return core::ExtendedPOS::AdjKatt;  // 美しかっ（た）
  }
  if (utf8::endsWith(surface, "けれ")) {
    return core::ExtendedPOS::AdjKeForm;  // 美しけれ（ば）
  }
  if (utf8::endsWith(surface, "かろ")) {
    return core::ExtendedPOS::AdjMizenkei;  // 美しかろ（う）
  }
  if (utf8::endsWith(surface, "く")) {
    return core::ExtendedPOS::AdjRenyokei;  // 美しく
  }
  if (utf8::endsWith(surface, "い")) {
    return core::ExtendedPOS::AdjBasic;  // 美しい
  }
  // Default to basic form
  return core::ExtendedPOS::AdjBasic;
}

/**
 * @brief Create an i-adjective candidate with common settings
 */
inline UnknownCandidate makeIAdjCandidate(const std::string& surface, size_t start, size_t end,
                                          const std::string& lemma, float cost, [[maybe_unused]] CandidateOrigin origin,
                                          [[maybe_unused]] float confidence, [[maybe_unused]] const char* pattern) {
  // Detect correct EPOS based on surface ending
  core::ExtendedPOS epos = detectIAdjEpos(surface);
  auto cand = makeCandidate(surface, start, end, core::PartOfSpeech::Adjective, cost, false, origin, epos);
  cand.lemma = lemma;
#ifdef SUZUME_DEBUG_INFO
  cand.confidence = confidence;
  cand.pattern = pattern;
#endif
  return cand;
}

/**
 * @brief Create an i-adjective stem candidate (expects suffix)
 */
inline UnknownCandidate makeIAdjStemCandidate(const std::string& surface, size_t start, size_t end,
                                              const std::string& lemma, float cost,
                                              [[maybe_unused]] CandidateOrigin origin,
                                              [[maybe_unused]] float confidence, [[maybe_unused]] const char* pattern) {
  auto cand = makeCandidate(surface, start, end, core::PartOfSpeech::Adjective, cost, true, origin,
                            core::ExtendedPOS::AdjStem);  // For bigram: AdjStem→AuxAppearanceSou
  cand.lemma = lemma;
#ifdef SUZUME_DEBUG_INFO
  cand.confidence = confidence;
  cand.pattern = pattern;
#endif
  return cand;
}

/**
 * @brief Derive a conjugated i-adjective variant by trimming trailing kana off
 *        an existing candidate.
 *
 * Spins the renyokei/katt/ke conjugation forms out of a full adjective surface
 * for MeCab-compatible splits (良くない → 良く + ない, 美しかった → 美しかっ + た).
 * The variant keeps the source lemma/origin/confidence, drops @p char_trim
 * trailing characters (all such tails are 3-byte kana), applies @p cost_bonus on
 * top of the source cost, and carries the connection-form @p epos. Callers keep
 * their own endsWith() guard and may override the cost afterward (e.g. the
 * ke-form dictionary-adjective disambiguation).
 */
inline UnknownCandidate makeTrimmedAdjVariant(const UnknownCandidate& cand, size_t char_trim, float cost_bonus,
                                              core::ExtendedPOS epos, [[maybe_unused]] const char* pattern) {
  UnknownCandidate var;
  var.surface = cand.surface.substr(0, cand.surface.size() - char_trim * core::kJapaneseCharBytes);
  var.start = cand.start;
  var.end = cand.end - char_trim;
  var.pos = core::PartOfSpeech::Adjective;
  var.lemma = cand.lemma;
  var.cost = cand.cost + cost_bonus;
  var.has_suffix = true;
  var.extended_pos = epos;
#ifdef SUZUME_DEBUG_INFO
  var.origin = cand.origin;
  var.confidence = cand.confidence;
  var.pattern = pattern;
#endif
  return var;
}

}  // namespace suzume::analysis::adj_detail

#endif  // SUZUME_ANALYSIS_ADJECTIVE_CANDIDATES_INTERNAL_H_
