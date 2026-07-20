/**
 * @file compound_verb_verify.cpp
 * @brief V1 reconstruction and verification for compound verbs
 */

#include "join_compound_verb_internal.h"

namespace suzume::analysis::compound_verb_detail {

namespace {

// A continuative can be ambiguous across conjugation classes (降り → 降りる /
// 降る). Compound-verb generation has already reconstructed the V1 base from
// the continuative ending, so validate that base against every inflection
// candidate instead of discarding it merely because another analysis scores
// higher in isolation.
bool hasInflectionCandidateForBase(const grammar::Inflection& inflection, std::string_view surface,
                                   std::string_view base_form, float min_confidence) {
  for (const auto& candidate : inflection.analyze(surface)) {
    if (candidate.confidence >= min_confidence && candidate.base_form == base_form) {
      return true;
    }
  }
  return false;
}

}  // namespace

CompoundV1Verification verifyCompoundVerbV1(const CompoundV1VerificationRequest& request) {
  const std::string_view text = request.text;
  const auto& codepoints = request.codepoints;
  const auto& byte_offsets = request.byte_offsets;
  const size_t start_pos = request.start_pos;
  const size_t kanji_end = request.kanji_end;
  const size_t v2_start = request.v2_start;
  const size_t start_byte = request.start_byte;
  const size_t v2_start_byte = request.v2_start_byte;
  char32_t base_ending = request.base_ending;
  const bool is_sokuonbin = request.is_sokuonbin;
  const bool is_ichidan = request.is_ichidan;
  const bool has_kanji_v2_after_bare_ichidan = request.has_kanji_v2_after_bare_ichidan;
  const bool dict_compound_v1 = request.dict_compound_v1;
  const std::string_view dict_compound_v1_lemma = request.dict_compound_v1_lemma;
  const auto& dict_manager = request.dict_manager;
  const auto& inflection = request.inflection;

  CompoundV1Verification result;
  std::string& v1_base = result.base_form;
  bool& v1_verified = result.verified;
  bool& v1_dict_verified = result.dict_verified;
  bool& v1_embedded_verified = result.embedded_verified;
  bool& v1_ichidan_inflection = result.ichidan_inflection;
  bool& v1_godan_inflection = result.godan_inflection;

  // Build the V1 base form for verification.
  const size_t v1_end_byte = is_ichidan ? v2_start_byte : byteOffsetAt(byte_offsets, kanji_end);
  v1_verified = dict_compound_v1;
  v1_dict_verified = dict_compound_v1;
  if (dict_compound_v1) {
    // Already resolved: V1 is the dict-verified compound verb (引きずる).
    v1_base = dict_compound_v1_lemma;
  } else {
    v1_base = std::string(text.substr(start_byte, v1_end_byte - start_byte));

    if (!is_sokuonbin && !is_ichidan) {
      v1_base += normalize::encodeUtf8(base_ending);
    } else if (!is_sokuonbin) {
      v1_base += "る";
    }

    if (is_sokuonbin) {
      // Try all sokuonbin-compatible godan endings.
      for (char32_t ending : kSokuonbinEndings) {
        std::string candidate = v1_base + normalize::encodeUtf8(ending);
        if (dict_manager.lookupExact(candidate, core::PartOfSpeech::Verb) != nullptr) {
          v1_verified = true;
          v1_dict_verified = true;
          v1_base = candidate;
          base_ending = ending;
          break;
        }
      }
    } else if (dict_manager.lookupExact(v1_base, core::PartOfSpeech::Verb) != nullptr) {
      v1_verified = true;
      v1_dict_verified = true;
    }
  }

  // A kanji-led V1 can have more than one kana before its continuative
  // ending (混じり+合う). The first kana may look like an Ichidan stem, but
  // the complete span can instead prove a Godan continuative. Preserve the
  // Ichidan reading unless inflection recognizes the whole span as Godan
  // and its final kana is that row's continuative form.
  if (!v1_verified && !dict_compound_v1 && is_ichidan && v2_start > kanji_end + 1) {
    const std::string v1_renyokei(text.substr(start_byte, v2_start_byte - start_byte));
    const auto inflection_candidate = inflection.getBest(v1_renyokei);
    const auto* godan_row = grammar::Conjugation::getGodanRow(inflection_candidate.verb_type);
    if (godan_row != nullptr &&
        inflection_candidate.confidence >= candidate::verb_cost::kConstructedVerbMinConfidence &&
        codepoints[v2_start - 1] == godan_row->i_row) {
      v1_base = inflection_candidate.base_form;
      v1_verified = true;
      v1_godan_inflection = true;
    }
  }

  SUZUME_DEBUG_LOG_VERBOSE("[COMPOUND] V1 base=" << v1_base << " verified=" << v1_verified
                                                 << " sokuonbin=" << is_sokuonbin << "\n");

  // Fallback: use inflection analysis for unknown V1 verbs.
  if (!v1_verified) {
    const size_t kanji_count = has_kanji_v2_after_bare_ichidan ? 1 : kanji_end - start_pos;

    // A single-kanji sokuonbin V1 plus a verified V2 is sufficient evidence.
    if (is_sokuonbin && kanji_count == 1) {
      v1_verified = true;
      for (char32_t ending : kSokuonbinEndings) {
        std::string candidate = v1_base + normalize::encodeUtf8(ending);
        if (dict_manager.lookupExact(candidate) != nullptr) {
          v1_base = candidate;
          base_ending = ending;
          break;
        }
      }
    }

    // For multi-kanji ichidan V1 stems, accept when stripping the leading
    // kanji yields a dictionary verb (e.g., 仕立てる = 仕 + 立てる).
    if (!v1_verified && is_ichidan && kanji_count >= 2) {
      const size_t v1_second_char_byte = byteOffsetAt(byte_offsets, start_pos + 1);
      std::string embedded_base(text.substr(v1_second_char_byte, v1_end_byte - v1_second_char_byte));
      embedded_base += "る";
      if (dict_manager.lookupExact(embedded_base, core::PartOfSpeech::Verb) != nullptr) {
        v1_verified = true;
        v1_embedded_verified = true;
        SUZUME_DEBUG_LOG_VERBOSE("[COMPOUND] V1 verified via embedded dict verb \"" << embedded_base << "\"\n");
      }
    }

    bool use_inflection_fallback = !v1_verified;

    // Multi-kanji stems require direct dictionary evidence. The inflection
    // analyzer accepts long kanji sequences too freely for this boundary.
    if (use_inflection_fallback && kanji_count >= 2) {
      use_inflection_fallback = false;
    }

    // Single-kanji + に is normally a noun-plus-particle boundary, not a
    // Godan-Na continuative.
    const char32_t renyokei_char = codepoints[kanji_end];
    if (!is_ichidan && kanji_count == 1 && renyokei_char == U'に') {
      use_inflection_fallback = false;
    }

    // A known non-verb continuative blocks fallback unless an exact
    // single-kanji Godan analysis proves the productive V1.
    if (use_inflection_fallback) {
      const size_t v1_renyokei_end = is_ichidan ? v2_start_byte : byteOffsetAt(byte_offsets, kanji_end + 1);
      const std::string v1_renyokei(text.substr(start_byte, v1_renyokei_end - start_byte));
      if (verb_helpers::hasNonVerbDictionaryEntry(&dict_manager, v1_renyokei)) {
        const auto inflection_candidate = inflection.getBest(v1_renyokei);
        const bool productive_godan_compound =
            !is_ichidan && kanji_count == 1 &&
            inflection_candidate.confidence >= candidate::verb_cost::kConstructedVerbMinConfidence &&
            inflection_candidate.base_form == v1_base;
        if (!productive_godan_compound) {
          use_inflection_fallback = false;
        }
      }
    }

    // A single-kanji Ichidan stem followed by a verified V2 is productive,
    // except at known copular, hatsuonbin, and formal-noun boundaries.
    bool starts_inside_formal_noun = false;
    if (use_inflection_fallback && is_ichidan && kanji_count == 1 && start_pos > 0) {
      const std::string enclosing_surface = extractSubstring(codepoints, start_pos - 1, start_pos + 1);
      const auto* enclosing_entry = dict_manager.lookupExact(enclosing_surface, core::PartOfSpeech::Noun);
      starts_inside_formal_noun =
          enclosing_entry != nullptr && enclosing_entry->extended_pos == core::ExtendedPOS::NounFormal;
    }
    if (use_inflection_fallback && is_ichidan && kanji_count == 1) {
      const bool bare_ichidan_stem = v2_start == kanji_end;
      if (bare_ichidan_stem && !verb_helpers::isSingleKanjiIchidan(codepoints[start_pos])) {
        use_inflection_fallback = false;
      } else if (renyokei_char == U'で' || renyokei_char == U'ん' || starts_inside_formal_noun) {
        use_inflection_fallback = false;
      } else {
        v1_verified = true;
        v1_ichidan_inflection = true;
        use_inflection_fallback = false;
      }
    }

    // A single-kanji Godan renyokei must reconstruct exactly to its V1 base.
    if (use_inflection_fallback && !is_ichidan && kanji_count == 1) {
      const size_t v1_renyokei_end = byteOffsetAt(byte_offsets, kanji_end + 1);
      const std::string v1_renyokei(text.substr(start_byte, v1_renyokei_end - start_byte));
      if (hasInflectionCandidateForBase(inflection, v1_renyokei, v1_base,
                                        candidate::verb_cost::kConstructedVerbMinConfidence)) {
        v1_verified = true;
        v1_godan_inflection = true;
        use_inflection_fallback = false;
      }
    }

    if (use_inflection_fallback) {
      const size_t v1_renyokei_end = is_ichidan ? v2_start_byte : byteOffsetAt(byte_offsets, kanji_end + 1);
      const std::string v1_renyokei(text.substr(start_byte, v1_renyokei_end - start_byte));
      // A particle at the end of the proposed V1 marks a compositional boundary.
      for (size_t split = core::kJapaneseCharBytes; split < v1_renyokei.size(); split += core::kJapaneseCharBytes) {
        const std::string_view suffix(v1_renyokei.data() + split, v1_renyokei.size() - split);
        if (dict_manager.lookupExact(suffix, core::PartOfSpeech::Particle) != nullptr) {
          use_inflection_fallback = false;
          break;
        }
      }
    }

    if (use_inflection_fallback) {
      const size_t v1_renyokei_end = is_ichidan ? v2_start_byte : byteOffsetAt(byte_offsets, kanji_end + 1);
      const std::string v1_renyokei(text.substr(start_byte, v1_renyokei_end - start_byte));
      const auto infl_result = inflection.getBest(v1_renyokei);
      const float min_confidence = is_ichidan ? candidate::verb_cost::kCompoundVerbIchidanMinConfidence
                                              : candidate::verb_cost::kConstructedVerbMinConfidence;
      if (infl_result.confidence >= min_confidence) {
        if (infl_result.base_form == v1_base) {
          v1_verified = true;
          if (is_ichidan && kanji_count == 1) {
            v1_ichidan_inflection = true;
          }
        } else if (is_sokuonbin) {
          for (char32_t ending : kSokuonbinEndings) {
            std::string candidate = v1_base + normalize::encodeUtf8(ending);
            if (infl_result.base_form == candidate) {
              v1_verified = true;
              v1_base = candidate;
              base_ending = ending;
              break;
            }
          }
        }
      }
    }
  }

  return result;
}

}  // namespace suzume::analysis::compound_verb_detail
