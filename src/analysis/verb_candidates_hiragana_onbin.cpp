/**
 * @file verb_candidates_hiragana_onbin.cpp
 * @brief Internal pure-hiragana verb candidate patterns
 */

#include <algorithm>
#include <cmath>

#include "analysis/bigram_table.h"
#include "analysis/candidate_constants.h"
#include "analysis/scorer_constants.h"
#include "analysis/verb_candidates_helpers.h"
#include "analysis/verb_candidates_hiragana_internal.h"
#include "core/debug.h"
#include "core/kana_constants.h"
#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "grammar/conjugation.h"
#include "normalize/char_type.h"
#include "normalize/exceptions.h"
#include "normalize/utf8.h"
#include "suffix_candidates.h"
#include "unknown.h"
#include "verb_candidates.h"

namespace suzume::analysis::hiragana_verb_detail {
namespace vh = verb_helpers;

// The mora every conjugating class ends in, and the one no verb root starts with.
constexpr char32_t kVerbEndingMora = U'る';

void appendOnbinContractionCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t hiragana_end,
                                      const grammar::Inflection& inflection,
                                      const dictionary::DictionaryManager* dict_manager,
                                      std::vector<UnknownCandidate>& candidates) {
  for (size_t onbin_pos = start_pos + 1; onbin_pos < hiragana_end; ++onbin_pos) {
    char32_t onbin_char = codepoints[onbin_pos];

    // Check for sokuonbin (っ) or hatsuonbin (ん)
    bool is_sokuonbin = (onbin_char == U'っ');
    bool is_hatsuonbin = (onbin_char == U'ん');
    if (!is_sokuonbin && !is_hatsuonbin) {
      continue;
    }

    // Check if followed by contraction auxiliary starter
    if (onbin_pos + 1 >= hiragana_end) {
      continue;
    }
    char32_t next_char = codepoints[onbin_pos + 1];

    bool is_contraction_pattern = false;
    bool is_tense_pattern = false;  // っ+た/て (past/te-form)
    if (is_sokuonbin) {
      // っ + と (とく/といた/といて) or ち (ちゃう/ちゃった/ちゃって)
      is_contraction_pattern = (next_char == U'と' || next_char == U'ち');
      // っ + た/て (past/te-form for GodanWa/Ra/Ta)
      // E.g., かった → かっ + た (from かう), やった → やっ + た (from やる)
      is_tense_pattern = (next_char == U'た' || next_char == U'て');
    } else {
      // ん + ど (どく/どいた/どいて) or じ (じゃう/じゃった/じゃって) or で (でる/でた/でて)
      is_contraction_pattern = (next_char == U'ど' || next_char == U'じ' || next_char == U'で');
      // ん + だ/で (past/te-form for GodanMa/Ba/Na)
      // E.g., 読んだ → 読ん + だ (from 読む), 飛んだ → 飛ん + だ (from 飛ぶ)
      is_tense_pattern = (next_char == U'だ' || next_char == U'で');
    }

    if (!is_contraction_pattern && !is_tense_pattern) {
      continue;
    }

    // Get the stem (part before onbin character)
    std::string stem = extractSubstring(codepoints, start_pos, onbin_pos);
    if (stem.empty()) {
      continue;
    }

    // Check if stem starts with common case particles (と、を、に、で、が、は、へ)
    // Used later to skip short particle+verb patterns unless dictionary-verified
    // E.g., となっ (stem=とな, 2 chars) → skip, particle + なる is more likely
    //       はじまっ (stem=はじま, 3 chars) → allow, longer stems are more likely verbs
    bool starts_with_short_particle_stem = false;
    size_t stem_char_count = suzume::normalize::utf8Length(stem);
    if (stem_char_count == 2) {  // Only skip 2-char stems
      char32_t first_char = codepoints[start_pos];
      starts_with_short_particle_stem =
          (first_char == U'と' || first_char == U'を' || first_char == U'に' || first_char == U'で' ||
           first_char == U'が' || first_char == U'は' || first_char == U'へ');
    }

    // Try different verb types based on onbin type
    const auto& candidates_to_try = vh::getGodanTypesByOnbin(is_sokuonbin ? "っ" : "ん");

    // Try each verb type and check dictionary or inflection analysis
    for (const auto& [verb_type, base_suffix] : candidates_to_try) {
      std::string base_form = stem + std::string(base_suffix);

      // Check if base form exists in dictionary as this verb type
      bool is_valid_verb = !grammar::isSuruBaseForm(base_form) && vh::isVerbInDictionary(dict_manager, base_form) &&
                           hasMatchingGodanInflection(inflection, base_form, verb_type);
      // Capture dictionary attestation before the inflection fallback below may
      // set is_valid_verb on a non-dictionary base.
      const bool lemma_dict_verified = is_valid_verb;

      // For sokuonbin tense and contraction patterns (っ+た/て, っとく), also
      // use inflection analysis. It validates common verbs like かう and やる
      // that may not be in the dictionary.
      // Hatsuonbin (ん) is deliberately EXCLUDED: its godan type is rule-ambiguous
      // (む/ぶ/ぬ are table-identical, all endorsed at equal confidence), so the
      // fallback would validate a table-first NON-WORD base (あそんで→あそむ). The
      // ん-onbin lemma must come from the dictionary path instead (bounded L2 +
      // the dedicated hatsuonbin generator), never from an inflection guess.
      // Exception: skip short particle-starting stems (となっ should be と+なっ);
      // longer stems like はじまっ (3+ chars) are allowed.
      // For an unattested short stem, the final vowel supplies productive
      // phonotactic evidence: an a-row stem before っ naturally reconstructs
      // a wa-row base (あらっ→あらう, かっ→かう). Other vowel rows must retain
      // the ra-row alternative (たどっ→たどる, くぐっ→くぐる).
      const bool prefer_wa_row_fallback = !is_valid_verb && is_sokuonbin && stem_char_count <= 2 &&
                                          grammar::endsWithARow(stem) && verb_type != grammar::VerbType::GodanWa;
      if (prefer_wa_row_fallback) {
        continue;
      }
      if (!is_valid_verb && (is_tense_pattern || is_contraction_pattern) && is_sokuonbin &&
          !starts_with_short_particle_stem) {
        // A contraction is validated through its uncontracted te-form
        // (やっとく -> やって); tense patterns retain their observed suffix.
        std::string_view suffix = is_contraction_pattern                       ? "て"
                                  : (next_char == U'た' || next_char == U'だ') ? "た"
                                                                               : "て";
        std::string full_form = stem + "っ" + std::string(suffix);
        const auto& analysis = inflection.analyze(full_form);
        for (const auto& cand : analysis) {
          // Lower threshold (0.25) for short stems like かっ, やっ
          // since godan_single_hiragana_stem penalty reduces confidence
          if (cand.verb_type == verb_type && cand.base_form == base_form &&
              cand.confidence >= candidate::verb_cost::kShortHiraganaSokuonbinMinConfidence) {
            is_valid_verb = true;
            break;
          }
        }
      }

      // A long pure-hiragana stem followed by the closed ん+だ/で tail is a
      // constructed hatsuonbin predicate even when its open-class lemma is
      // absent from L2. The ma/ba/na rows are surface-identical here; use the
      // productive ma-row fallback only in a predicate slot and retain
      // dictionary evidence whenever any row is attested.
      const bool has_left_predicate_boundary =
          start_pos == 0 || normalize::classifyChar(codepoints[start_pos - 1]) == normalize::CharType::Symbol ||
          normalize::isExtendedParticle(codepoints[start_pos - 1]);
      if (!is_valid_verb && is_hatsuonbin && is_tense_pattern && stem_char_count >= 3 &&
          verb_type == grammar::VerbType::GodanMa && has_left_predicate_boundary) {
        is_valid_verb = true;
      }

      // る is the terminal ending of every conjugating class and the tail of
      // the contracted subsidiaries (てる, でる, とる); it heads no verb root.
      // A single-mora る stem therefore reconstructs a base that is nothing
      // but ending (るる, るつ), and the contracted progressive gets re-cut
      // around it (書いてるって → て + るっ + て). Other u-row morae do head
      // roots (つる, うつ), so the guard is specific to this one.
      if (!lemma_dict_verified && stem_char_count == 1 && codepoints[start_pos] == kVerbEndingMora) {
        continue;
      }

      if (!is_valid_verb) {
        continue;
      }

      // Found a valid verb - generate onbin stem candidate
      std::string onbin_surface = extractSubstring(codepoints, start_pos, onbin_pos + 1);
      // For tense patterns, use higher cost to avoid false positives for short stems
      // Contraction patterns (っとく, っちゃう) are more reliable, use lower cost
      const float cost =
          is_contraction_pattern ? candidate::verb_cost::kContractedOnbinBonus : candidate::verb_cost::kMinorPenalty;
      SUZUME_DEBUG_VERBOSE_BLOCK {
        SUZUME_DEBUG_STREAM << "[VERB_CAND] " << onbin_surface
                            << (is_tense_pattern ? " hiragana_onbin_tense" : " hiragana_onbin_contraction")
                            << " lemma=" << base_form << " cost=" << cost << "\n";
      }
      const char* pattern = is_sokuonbin ? "hiragana_sokuonbin" : "hiragana_hatsuonbin";
      auto onbin_cand = makeVerbCandidate(onbin_surface, start_pos, onbin_pos + 1, cost, base_form,
                                          grammar::verbTypeToConjType(verb_type), true, CandidateOrigin::VerbHiragana,
                                          0.9F, pattern, core::ExtendedPOS::VerbOnbinkei);
      // The reconstructed base is dictionary-attested, irrespective of
      // whether the inflected surface also has a dictionary entry.
      onbin_cand.lemma_verified = lemma_dict_verified;
      candidates.push_back(std::move(onbin_cand));
      break;  // Found valid candidate for this position
    }
  }
}

// Irregular 来る (カ変) mizenkei こ before a ない-family negative (こない,
// こなかった, こなくて, こなければ, こなきゃ), or before the passive/potential
// auxiliary (てこられる, でこられない). Its volitional stem こよ is likewise
// emitted only in the directional auxiliary context (読んでこよう). The short
// surfaces are far too frequent as unconditional dictionary entries (こと,
// これ, きのこ, ...), so each candidate requires its following inflection.
// The reading is chosen from the preceding context:
//   - after a clear て/で form: directional auxiliary てくる → Auxiliary / AuxAspectKuru
//   - otherwise: main verb 来る before negation → Verb / VerbMizenkei
// Emitting a single context-appropriate reading avoids relying on a broad
// AuxAspectKuru connection rule that could mis-flip other subsidiary verbs.
void appendKkoNominalizerCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                    std::vector<UnknownCandidate>& candidates) {
  // The colloquial negative-possibility construction V連用形+っこ+ない
  // (読みっこない, 食べっこなかった) is built on the nominalizing suffix っこ,
  // the same one that names a reciprocal action (かけっこ). Neither mora is a
  // morpheme on its own, so the suffix is emitted as one token — and only when
  // the ない-family ending proves the construction, because っこ is otherwise
  // word-internal material (抱っこ, そこっ子).
  if (start_pos == 0 || start_pos + 2 >= codepoints.size() || codepoints[start_pos] != U'っ' ||
      codepoints[start_pos + 1] != U'こ' || !vh::naiNegativeFollowsAt(codepoints, start_pos + 2)) {
    return;
  }

  const size_t end_pos = start_pos + 2;
  std::string surface = extractSubstring(codepoints, start_pos, end_pos);
  auto candidate =
      makeCandidate(surface, start_pos, end_pos, core::PartOfSpeech::Suffix, candidate::verb_cost::kStrongBonus, true,
                    CandidateOrigin::VerbHiragana, core::ExtendedPOS::Suffix, "hiragana_kko_nominalizer");
  candidate.lemma = surface;
  candidates.push_back(std::move(candidate));
}

void appendSuruInabilityCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                   std::vector<UnknownCandidate>& candidates) {
  // The inability subsidiary かねる productively follows the sahen
  // renyokei し (確認し|かねない, 対応し|かねます). In this exact
  // grammatical environment, make the closed-class する stem competitive
  // with the homographic binding particle しか.
  if (start_pos + 2 >= codepoints.size() || codepoints[start_pos] != U'し' || codepoints[start_pos + 1] != U'か' ||
      codepoints[start_pos + 2] != U'ね') {
    return;
  }

  std::string surface = extractSubstring(codepoints, start_pos, start_pos + 1);
  auto suru_candidate =
      makeCandidate(surface, start_pos, start_pos + 1, core::PartOfSpeech::Verb, candidate::kNounVerbSplitBonus, true,
                    CandidateOrigin::VerbHiragana, core::ExtendedPOS::VerbRenyokei, "hiragana_suru_inability");
  suru_candidate.lemma = "する";
  suru_candidate.conj_type = dictionary::ConjugationType::Suru;
  candidates.push_back(std::move(suru_candidate));
}

void appendEruObligationCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                   std::vector<UnknownCandidate>& candidates) {
  // In the fixed obligation construction 〜ざるをえない, え is the lexical
  // verb 得る in renyokei, not the potential auxiliary. The following
  // negative or polite-negative inflection identifies this reading.
  const bool negative_follows =
      start_pos + 1 < codepoints.size() &&
      (vh::naiNegativeFollowsAt(codepoints, start_pos + 1) || codepoints[start_pos + 1] == U'ず');
  const bool polite_negative_follows =
      start_pos + 2 < codepoints.size() && codepoints[start_pos + 1] == U'ま' && codepoints[start_pos + 2] == U'せ';
  if (start_pos < 3 || codepoints[start_pos] != U'え' || codepoints[start_pos - 1] != U'を' ||
      codepoints[start_pos - 2] != U'る' || codepoints[start_pos - 3] != U'ざ' ||
      (!negative_follows && !polite_negative_follows)) {
    return;
  }

  std::string surface = extractSubstring(codepoints, start_pos, start_pos + 1);
  auto eru_candidate = makeCandidate(
      surface, start_pos, start_pos + 1, core::PartOfSpeech::Verb,
      candidate::kVerifiedTailCompoundVerbBonus + candidate::kVerifiedV1Bonus + candidate::verb_cost::kStrongBonus,
      true, CandidateOrigin::VerbHiragana, core::ExtendedPOS::VerbRenyokei, "hiragana_eru_obligation");
  eru_candidate.lemma = "える";
  eru_candidate.conj_type = dictionary::ConjugationType::Ichidan;
  candidates.push_back(std::move(eru_candidate));
}

void appendKuruMizenkeiNaiCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                     std::vector<UnknownCandidate>& candidates) {
  // The conditional stem くれ is distinct from benefactive くれる when it
  // is followed immediately by ば after a clear te-form (読んでくれ+ば).
  // The benefactive conditional is くれれ+ば, so this context uniquely marks
  // the directional auxiliary 来る.
  if (start_pos + 2 < codepoints.size() && codepoints[start_pos] == U'く' && codepoints[start_pos + 1] == U'れ' &&
      codepoints[start_pos + 2] == U'ば' && isClearTeFormBeforeSubsidiary(codepoints, start_pos, false)) {
    appendContextualSubsidiaryCandidate(codepoints, start_pos, start_pos + 2, "くる", dictionary::ConjugationType::Kuru,
                                        core::ExtendedPOS::AuxAspectKuru, "hiragana_kuru_conditional",
                                        candidate::verb_cost::kStrongBonus, candidates);
    return;
  }
  if (codepoints[start_pos] != U'こ') {
    return;
  }
  const bool negative_follows = vh::naiNegativeFollowsAt(codepoints, start_pos + 1);
  const bool passive_follows = start_pos + 2 < codepoints.size() && codepoints[start_pos + 1] == U'ら' &&
                               codepoints[start_pos + 2] == U'れ' &&
                               isClearTeFormBeforeSubsidiary(codepoints, start_pos, false);
  const bool volitional_follows =
      start_pos + 2 < codepoints.size() && codepoints[start_pos + 1] == U'よ' && codepoints[start_pos + 2] == U'う';
  if (!negative_follows && !passive_follows && !volitional_follows) {
    return;
  }
  constexpr float kCost = candidate::verb_cost::kStandardBonus;
  // A preceding 促音 does NOT license the directional subsidiary: the sokuon
  // there belongs to the nominalizer っこ (できっこない), which is one token,
  // and 来る never contracts a て into it.
  const bool subsidiary = isClearTeFormBeforeSubsidiary(codepoints, start_pos, false);
  if (volitional_follows) {
    if (!subsidiary) {
      return;
    }
    appendContextualSubsidiaryCandidate(codepoints, start_pos, start_pos + 2, "くる", dictionary::ConjugationType::Kuru,
                                        core::ExtendedPOS::AuxAspectKuru, "hiragana_kuru_volitional", kCost,
                                        candidates);
    return;
  }
  const std::string surface = extractSubstring(codepoints, start_pos, start_pos + 1);
  SUZUME_DEBUG_VERBOSE_BLOCK {
    SUZUME_DEBUG_STREAM << "[VERB_CAND] " << surface << " hiragana_kuru_mizenkei lemma=くる cost=" << kCost
                        << " subsidiary=" << subsidiary << "\n";
  }
  if (subsidiary) {
    auto aux_cand =
        makeCandidate(surface, start_pos, start_pos + 1, core::PartOfSpeech::Auxiliary, kCost, true,
                      CandidateOrigin::VerbHiragana, core::ExtendedPOS::AuxAspectKuru, "hiragana_kuru_mizenkei_nai");
    aux_cand.lemma = "くる";
    aux_cand.conj_type = dictionary::ConjugationType::Kuru;
    candidates.push_back(std::move(aux_cand));
    return;
  }
  // A passive/potential continuation is only admitted after a te-form, where
  // it is the directional subsidiary construction; standalone こられる remains
  // available as a lexical verb candidate.
  if (passive_follows) {
    return;
  }
  candidates.push_back(makeVerbCandidate(surface, start_pos, start_pos + 1, kCost, "くる",
                                         dictionary::ConjugationType::Kuru, true, CandidateOrigin::VerbHiragana,
                                         candidate::kHighOriginConfidence, "hiragana_kuru_mizenkei_nai",
                                         core::ExtendedPOS::VerbMizenkei));
}

void appendKuruRenyokeiCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                  std::vector<UnknownCandidate>& candidates) {
  // The one-mora renyokei き is lexical 来る at an ordinary predicate
  // boundary, but becomes the directional/aspectual auxiliary only after a
  // completed て/で-form (歩いて+き+た).  Generate that auxiliary
  // homograph at its licensed left boundary instead of assigning auxiliary
  // POS to sentence-initial and case-marked lexical uses (きた, 朝がきた).
  if (start_pos >= codepoints.size() || codepoints[start_pos] != U'き') {
    return;
  }
  if (isClearTeFormBeforeSubsidiary(codepoints, start_pos, false)) {
    appendContextualSubsidiaryCandidate(codepoints, start_pos, start_pos + 1, "くる", dictionary::ConjugationType::Kuru,
                                        core::ExtendedPOS::AuxAspectKuru, "hiragana_kuru_renyokei_auxiliary",
                                        candidate::verb_cost::kStandardBonus, candidates);
    return;
  }

  // Independent 来る is admitted only when both sides license the one-mora
  // irregular stem.  In particular, do not treat a preceding て/で as an
  // ordinary particle boundary: those codepoints also occur inside でき and
  // the subsidiary case was handled above using a verified te-form.
  const bool right_continuation =
      start_pos + 1 < codepoints.size() && (codepoints[start_pos + 1] == U'た' || codepoints[start_pos + 1] == U'て' ||
                                            vh::finiteMasuFormLengthAt(codepoints, start_pos + 1) > 0);
  if (!right_continuation) {
    return;
  }
  const bool left_boundary = start_pos == 0 ||
                             normalize::classifyChar(codepoints[start_pos - 1]) == normalize::CharType::Symbol ||
                             (normalize::isExtendedParticle(codepoints[start_pos - 1]) &&
                              codepoints[start_pos - 1] != U'て' && codepoints[start_pos - 1] != U'で');
  if (!left_boundary) {
    return;
  }
  candidates.push_back(makeVerbCandidate("き", start_pos, start_pos + 1, candidate::verb_cost::kKuruRenyokeiBonus,
                                         "くる", dictionary::ConjugationType::Kuru, true, CandidateOrigin::VerbHiragana,
                                         candidate::kHighOriginConfidence, "hiragana_kuru_renyokei",
                                         core::ExtendedPOS::VerbRenyokei));
}

// 1-char ichidan renyokei before て/た (ねて → ね + て). Requires the base form
// (stem + る) in the dictionary. Contextual subsidiary verbs are generated
// separately with their auxiliary ExtendedPOS.
void appendIchidanRenyokei1CharCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                          const dictionary::DictionaryManager* dict_manager,
                                          std::vector<UnknownCandidate>& candidates) {
  // Pattern: e-row hiragana followed by て or た
  // IMPORTANT: Only generate if the base form (stem + る) is a known verb in dictionary
  // to avoid false positives like めて → め + て (め is not a verb)
  if (start_pos < codepoints.size()) {
    char32_t first_char = codepoints[start_pos];
    // Check for e-row hiragana (ichidan renyokei ending)
    // e-row: ね(ねる), め(める), け(ける), etc.
    if (grammar::isERowCodepoint(first_char)) {
      // Check if followed by te/ta particle.
      if (start_pos + 1 < codepoints.size()) {
        char32_t next_char = codepoints[start_pos + 1];
        bool is_valid_follow = (next_char == U'て' || next_char == U'た');
        if (is_valid_follow) {
          // Construct base form (stem + る)
          std::string stem_surface = extractSubstring(codepoints, start_pos, start_pos + 1);
          std::string base_form = stem_surface + "る";

          // Require dict check to prevent false positives (め+て, け+て).
          if (vh::isVerbInDictionary(dict_manager, base_form)) {
            // Strong negative cost to beat particle split
            // Particle path can be as low as -0.2, so we need lower
            constexpr float kCost = candidate::verb_cost::kStandardBonus;
            SUZUME_DEBUG_VERBOSE_BLOCK {
              SUZUME_DEBUG_STREAM << "[VERB_CAND] " << stem_surface
                                  << " hiragana_ichidan_renyokei_1char lemma=" << base_form << " cost=" << kCost
                                  << "\n";
            }
            candidates.push_back(makeVerbCandidate(
                stem_surface, start_pos, start_pos + 1, kCost, base_form, dictionary::ConjugationType::Ichidan, true,
                CandidateOrigin::VerbHiragana, 0.8F, "hiragana_ichidan_renyokei_1char",
                core::ExtendedPOS::VerbRenyokei));  // Explicit VerbRenyokei for て/た connection
          }
        }
      }
    }
  }
}

}  // namespace suzume::analysis::hiragana_verb_detail
