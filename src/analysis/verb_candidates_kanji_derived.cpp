/**
 * @file verb_candidates_kanji_derived.cpp
 * @brief Derived kanji verb inflection candidate patterns
 */

#include <algorithm>
#include <cmath>

#include "analysis/bigram_table.h"
#include "analysis/candidate_constants.h"
#include "analysis/scorer_constants.h"
#include "analysis/verb_candidates_helpers.h"
#include "analysis/verb_candidates_kanji_internal.h"
#include "core/debug.h"
#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "grammar/conjugation.h"
#include "grammar/inflection_scorer_constants.h"
#include "normalize/char_type.h"
#include "normalize/exceptions.h"
#include "normalize/utf8.h"
#include "suffix_candidates.h"
#include "unknown.h"
#include "verb_candidates.h"

namespace suzume::analysis::kanji_verb_detail {
namespace vh = verb_helpers;

// Try Ichidan verb kateikei (conditional) + volitional stem patterns.
// Kateikei: renyokei + れ + ば (食べれば → 食べれ + ば).
// Volitional: renyokei + よ + う (食べよう → 食べよ + う).
// MeCab splits these; generate the stem candidate for the split.
void appendIchidanKateikeiVolitionalCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                               size_t kanji_end, size_t hiragana_end,
                                               const grammar::Inflection& inflection,
                                               const dictionary::DictionaryManager* dict_manager,
                                               std::vector<UnknownCandidate>& candidates) {
  // Dictionary-backed single-kanji する verbs form 仮定形 with すれ+ば
  // (反する→反すれ+ば).  The generic analyzer can otherwise detach the
  // lexical kanji and select the standalone する paradigm.
  if (kanji_end == start_pos + 1 && kanji_end + 2 < codepoints.size() && codepoints[kanji_end] == U'す' &&
      codepoints[kanji_end + 1] == U'れ' && codepoints[kanji_end + 2] == U'ば') {
    const std::string suru_base = extractSubstring(codepoints, start_pos, kanji_end) + "する";
    if (vh::isVerbInDictionary(dict_manager, suru_base)) {
      const size_t kateikei_end = kanji_end + 2;
      auto suru_candidate =
          makeVerbCandidate(extractSubstring(codepoints, start_pos, kateikei_end), start_pos, kateikei_end,
                            candidate::verb_cost::kStrongBonus, suru_base, dictionary::ConjugationType::Suru, true,
                            CandidateOrigin::VerbKanji, candidate::kVerifiedConfidence,
                            "verified_single_kanji_suru_kateikei", core::ExtendedPOS::VerbKateikei);
      suru_candidate.lemma_verified = true;
      candidates.push_back(std::move(suru_candidate));
    }
  }

  // A Godan causative is itself an Ichidan-form predicate. Its conditional
  // surface is stem + a-row + せれ + ば (遊ばせれば), so preserve the full
  // conditional stem instead of splitting the causative auxiliary midway.
  if (kanji_end + 3 < codepoints.size() && grammar::isARowCodepoint(codepoints[kanji_end]) &&
      codepoints[kanji_end + 1] == U'せ' && codepoints[kanji_end + 2] == U'れ' && codepoints[kanji_end + 3] == U'ば') {
    size_t kateikei_end = kanji_end + 3;
    std::string surface = extractSubstring(codepoints, start_pos, kateikei_end);
    std::string causative_stem = extractSubstring(codepoints, start_pos, kanji_end + 2);
    std::string base_form = causative_stem + "る";
    float confidence =
        getIchidanConfidence(inflection.analyze(surface), candidate::verb_cost::kIchidanKateikeiMinConfidence);
    if (confidence >= candidate::verb_cost::kIchidanKateikeiMinConfidence) {
      candidates.push_back(makeVerbCandidate(surface, start_pos, kateikei_end, candidate::verb_cost::kStrongBonus,
                                             base_form, dictionary::ConjugationType::Ichidan, true,
                                             CandidateOrigin::VerbKanji, confidence, "causative_kateikei",
                                             core::ExtendedPOS::VerbKateikei));
    }
  }

  // A single-kanji ichidan stem can attach directly to よう (見よう, 着よう).
  // Unlike 食べよう, there is no e-row renyokei kana before よ, so emit the
  // mizenkei stem separately after inflection confirms the full form.
  if (kanji_end == start_pos + 1 && kanji_end + 1 < codepoints.size() && codepoints[kanji_end] == U'よ' &&
      codepoints[kanji_end + 1] == U'う') {
    std::string full_surface = extractSubstring(codepoints, start_pos, kanji_end + 2);
    float confidence =
        getIchidanConfidence(inflection.analyze(full_surface), candidate::verb_cost::kIchidanKateikeiMinConfidence);
    if (confidence >= candidate::verb_cost::kIchidanKateikeiMinConfidence) {
      std::string stem = extractSubstring(codepoints, start_pos, kanji_end);
      candidates.push_back(makeVerbCandidate(stem, start_pos, kanji_end, candidate::verb_cost::kStrongBonus,
                                             stem + "る", dictionary::ConjugationType::Ichidan, true,
                                             CandidateOrigin::VerbKanji, confidence, "single_kanji_volitional",
                                             core::ExtendedPOS::VerbMizenkei));
    }
  }

  if (kanji_end < hiragana_end) {
    char32_t first_hira = codepoints[kanji_end];
    // Check if first hiragana is e-row or i-row (ichidan renyokei ending)
    if (grammar::isERowCodepoint(first_hira) || grammar::isIRowCodepoint(first_hira)) {
      size_t renyokei_end = kanji_end + 1;  // kanji + e/i-row
      // Check for れ + ば pattern after renyokei
      if (renyokei_end + 1 < codepoints.size() && codepoints[renyokei_end] == U'れ' &&
          codepoints[renyokei_end + 1] == U'ば') {
        // E.g., 食べ + れ + ば → 食べれ is kateikei
        size_t kateikei_end = renyokei_end + 1;  // renyokei + れ
        std::string surface = extractSubstring(codepoints, start_pos, kateikei_end);
        std::string renyokei_surface = extractSubstring(codepoints, start_pos, renyokei_end);
        std::string base_form = renyokei_surface + "る";  // 食べ + る = 食べる

        // Disambiguate i-adjective 仮定形 from ichidan verb 仮定形 for the ければ case.
        // "高ければ"(高い) and "受ければ"(受ける) are grammatically indistinguishable by
        // inflection rules alone (both yield a plausible ichidan base 高ける/受ける).
        // The distinguishing signal is lexical: when kanji-stem + い is a known
        // i-adjective, this is the adjective 仮定形 (高い→高けれ+ば), not a verb.
        // Suppress the fake ichidan verb candidate so the i-adjective ke-form wins.
        bool is_iadj_kateikei = false;
        if (renyokei_end > start_pos && codepoints[renyokei_end - 1] == U'け' && dict_manager != nullptr) {
          std::string adj_base = extractSubstring(codepoints, start_pos, renyokei_end - 1) + "い";
          if (vh::isAdjectiveInDictionary(dict_manager, adj_base)) {
            SUZUME_DEBUG_LOG_VERBOSE("[VERB_SKIP] \"" << surface << "\" ichidan_kateikei: " << adj_base
                                                      << " is i-adjective (prefer ADJ 仮定形)\n");
            is_iadj_kateikei = true;
          }
        }

        // Verify using inflection analysis on the kateikei form
        const auto& all_candidates = inflection.analyze(surface);
        float ichidan_confidence = getIchidanConfidence(all_candidates, 0.3F);

        if (!is_iadj_kateikei && ichidan_confidence >= 0.3F) {
          // Negative cost to beat the split path 語幹+れ(受身)+ば
          constexpr float kKateikeiCost = candidate::verb_cost::kStrongBonus;
          SUZUME_DEBUG_VERBOSE_BLOCK {
            SUZUME_DEBUG_STREAM << "[VERB_CAND] " << surface << " ichidan_kateikei lemma=" << base_form
                                << " conf=" << ichidan_confidence << " cost=" << kKateikeiCost << "\n";
          }
          candidates.push_back(makeVerbCandidate(
              surface, start_pos, kateikei_end, kKateikeiCost, base_form, dictionary::ConjugationType::Ichidan, true,
              CandidateOrigin::VerbKanji, ichidan_confidence, "ichidan_kateikei", core::ExtendedPOS::VerbKateikei));
        }
      }

      // Ichidan verbs form both the volitional stem (食べよ+う) and
      // the literary imperative (食べよ) from renyokei + よ.
      if (renyokei_end < codepoints.size() && codepoints[renyokei_end] == U'よ') {
        const bool is_volitional = renyokei_end + 1 < codepoints.size() && codepoints[renyokei_end + 1] == U'う';
        // Skip suru-verb pattern: 漢字 + し + よう
        // Suru-verbs (勉強しよう, 説明しよう) should be split as: 漢字|しよ|う
        // Check if renyokei ends with し preceded by kanji
        bool is_suru_pattern = false;
        if (renyokei_end > start_pos && codepoints[renyokei_end - 1] == U'し' && renyokei_end - 1 > start_pos) {
          // Check if there's at least one kanji before し
          bool has_kanji_before = false;
          for (size_t i = start_pos; i < renyokei_end - 1; ++i) {
            if (normalize::isKanjiCodepoint(codepoints[i])) {
              has_kanji_before = true;
              break;
            }
          }
          is_suru_pattern = has_kanji_before;
        }

        if (!is_suru_pattern) {
          // E.g., 食べ + よ + う → 食べよ is volitional stem;
          //       食べ + よ → 食べよ is the literary imperative.
          size_t volitional_end = renyokei_end + 1;  // renyokei + よ
          std::string surface = extractSubstring(codepoints, start_pos, volitional_end);
          std::string renyokei_surface = extractSubstring(codepoints, start_pos, renyokei_end);
          std::string base_form = renyokei_surface + "る";  // 食べ + る = 食べる

          // Check if renyokei looks like an adjective (kanji+い pattern)
          // E.g., 良い, 高い, 赤い - these are adjectives, not ichidan verb stems
          // Require higher confidence to avoid false volitional candidates
          // like 良いよ(う) being parsed as volitional of non-existent 良いる
          bool could_be_adjective = false;
          if (renyokei_end > start_pos + 1 && codepoints[renyokei_end - 1] == U'い') {
            // Check if chars before い are all kanji
            bool all_kanji_before_i = true;
            for (size_t k = start_pos; k < renyokei_end - 1; ++k) {
              if (!normalize::isKanjiCodepoint(codepoints[k])) {
                all_kanji_before_i = false;
                break;
              }
            }
            could_be_adjective = all_kanji_before_i;
          }

          // Verify using inflection analysis
          const auto& all_candidates = inflection.analyze(renyokei_surface + "よう");
          float min_confidence = could_be_adjective ? 0.5F : 0.3F;
          float ichidan_confidence = getIchidanConfidence(all_candidates, min_confidence);

          if (ichidan_confidence >= min_confidence) {
            // Negative cost to beat the renyokei + final-particle path.
            constexpr float kVolitionalCost = candidate::verb_cost::kStrongBonus;
            SUZUME_DEBUG_VERBOSE_BLOCK {
              SUZUME_DEBUG_STREAM << "[VERB_CAND] " << surface
                                  << (is_volitional ? " ichidan_volitional lemma=" : " ichidan_imperative lemma=")
                                  << base_form << " conf=" << ichidan_confidence << " cost=" << kVolitionalCost << "\n";
            }
            candidates.push_back(
                makeVerbCandidate(surface, start_pos, volitional_end, kVolitionalCost, base_form,
                                  dictionary::ConjugationType::Ichidan, true, CandidateOrigin::VerbKanji,
                                  ichidan_confidence, is_volitional ? "ichidan_volitional" : "ichidan_imperative",
                                  is_volitional ? core::ExtendedPOS::VerbMizenkei : core::ExtendedPOS::VerbMeireikei));
          }
        }
      }
    }
  }
}

// Try Causative verb renyokei pattern: kanji + ら + せ
// Causative verbs from Godan verbs follow this pattern:
//   知る → 知らせる (causative, Ichidan verb)
//   乗る → 乗らせる (causative, Ichidan verb)
//   終わる → 終わらせる (causative, Ichidan verb)
// The renyokei of these causative verbs ends with せ (e-row):
//   知らせ (renyokei of 知らせる), connects to ます, られる, て, た, etc.
// Pattern: kanji + ら + せ (followed by られ for causative-passive)
void appendCausativeRenyokeiCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t kanji_end,
                                       size_t hiragana_end, const grammar::Inflection& inflection,
                                       const dictionary::DictionaryManager* dict_manager,
                                       const VerbCandidateOptions& verb_opts,
                                       std::vector<UnknownCandidate>& candidates) {
  if (kanji_end + 2 <= hiragana_end) {
    char32_t first_hira = codepoints[kanji_end];
    char32_t second_hira = codepoints[kanji_end + 1];
    // ら + せ pattern (causative renyokei)
    if (first_hira == U'ら' && second_hira == U'せ') {
      std::string original_base = extractSubstring(codepoints, start_pos, kanji_end) + "る";
      // When the underlying Godan verb is attested, preserve the productive
      // mizenkei + causative-auxiliary boundary. The fallback below exists for
      // an otherwise unavailable predicate analysis, not to replace it.
      if (vh::isVerbInDictionary(dict_manager, original_base)) {
        return;
      }
      // Generate causative renyokei when followed by valid ichidan verb endings
      // or causative-passive (られ). This covers:
      //   眠らせた (past), 眠らせて (te-form), 眠らせない (negative),
      //   眠らせます (polite), 眠らせられ (passive)
      bool followed_by_valid = false;
      if (kanji_end + 2 < codepoints.size()) {
        char32_t next_cp = codepoints[kanji_end + 2];
        followed_by_valid = (next_cp == U'ら' || next_cp == U'た' || next_cp == U'て' || next_cp == U'な' ||
                             next_cp == U'ま' || next_cp == U'ず' || next_cp == U'ば');
      }
      // Also allow at end of input (bare renyokei: 眠らせ)
      if (kanji_end + 2 >= codepoints.size()) {
        followed_by_valid = true;
      }
      if (followed_by_valid) {
        size_t renyokei_end = kanji_end + 2;  // kanji + ら + せ
        std::string surface = extractSubstring(codepoints, start_pos, renyokei_end);

        // The causative base form is surface + る (e.g., 知らせ → 知らせる)
        std::string causative_base = surface + "る";

        // Verify this is a valid ichidan verb
        const auto& all_candidates = inflection.analyze(causative_base);
        float ichidan_confidence =
            getIchidanConfidence(all_candidates, candidate::verb_cost::kIchidanDefaultMinConfidence);

        if (ichidan_confidence >= 0.4F) {
          float base_cost = candidate::confidenceScaledCost(verb_opts.bonus_ichidan, ichidan_confidence,
                                                            verb_opts.confidence_cost_scale_small);
          SUZUME_DEBUG_LOG_VERBOSE("[VERB_CAND] " << surface << " causative_renyokei lemma=" << causative_base
                                                  << " conf=" << ichidan_confidence << " cost=" << base_cost << "\n");
          candidates.push_back(makeVerbCandidate(surface, start_pos, renyokei_end, base_cost, causative_base,
                                                 grammar::verbTypeToConjType(grammar::VerbType::Ichidan), true,
                                                 CandidateOrigin::VerbKanji, ichidan_confidence, "causative_renyokei"));
        }
      }
    }
  }
}

// Try Godan passive renyokei pattern: kanji + a-row + れ
// Godan passive verbs (受身形) follow this pattern:
//   言う → 言われる (passive, Ichidan verb)
//   書く → 書かれる (passive, Ichidan verb)
//   読む → 読まれる (passive, Ichidan verb)
// The renyokei of these passive verbs ends with れ (e-row):
//   言われ (renyokei of 言われる), connects to ます, ない, て, た, etc.
// Pattern: kanji + a-row hiragana + れ
void appendGodanPassiveRenyokeiCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t kanji_end,
                                          size_t hiragana_end, const grammar::Inflection& inflection,
                                          const dictionary::DictionaryManager* dict_manager,
                                          const VerbCandidateOptions& verb_opts,
                                          std::vector<UnknownCandidate>& candidates) {
  if (kanji_end + 1 < hiragana_end) {
    char32_t first_hira = codepoints[kanji_end];
    char32_t second_hira = codepoints[kanji_end + 1];
    // A-row + れ pattern (godan passive renyokei)
    if (grammar::isARowCodepoint(first_hira) && second_hira == U'れ') {
      // Skip suru-verb passive pattern: kanji + さ + れ
      // e.g., 処理される should be 処理(noun) + される(aux), not godan passive
      // Also skip single kanji + さ + れ as these are typically not real verbs
      // e.g., 強される is not a verb (強い is adjective, 強 is noun)
      std::string kanji_check = extractSubstring(codepoints, start_pos, kanji_end);
      bool is_suru_passive_pattern = (first_hira == U'さ' && grammar::isAllKanji(kanji_check));
      if (is_suru_passive_pattern) {
        // Skip - this should be handled as noun + される auxiliary
        // Continue to next pattern
      } else {
        size_t renyokei_end = kanji_end + 2;  // kanji + a-row + れ
        std::string surface = extractSubstring(codepoints, start_pos, renyokei_end);

        // Check if this is a valid passive verb stem
        // The passive base form is surface + る (e.g., 言われ → 言われる)
        std::string passive_base = surface + "る";

        // Skip if passive_base is already a known ichidan verb in dictionary.
        // E.g., 生まれる is a standalone ichidan verb, not passive of 生む.
        // The dictionary entry provides the correct candidate with proper lemma.
        if (vh::isVerbInDictionary(dict_manager, passive_base)) {
          // Fall through to end of block - dict entry handles this
        } else {
          // Compute the original base verb lemma by converting A-row to U-row
          // e.g., 言われる: 言 + わ + れる → 言 + う = 言う
          std::string kanji_part = extractSubstring(codepoints, start_pos, kanji_end);
          std::string_view u_row_suffix = grammar::godanBaseSuffixFromARow(first_hira);
          std::string base_lemma = kanji_part + std::string(u_row_suffix);

          // Use analyze() to get all interpretations, not just the best one
          // The best overall interpretation might be Godan (言う + れる), but
          // there should also be an Ichidan interpretation (言われる as verb)
          const auto& all_candidates = inflection.analyze(passive_base);
          float ichidan_confidence =
              getIchidanConfidence(all_candidates, candidate::verb_cost::kIchidanDefaultMinConfidence);

          // Passive verbs are Ichidan conjugation (言われる conjugates like 食べる)
          if (ichidan_confidence >= 0.4F) {
            // Check if followed by べき (classical obligation)
            // For 書かれべき pattern, we want 書か + れべき, not 書かれ + べき
            bool is_beki_pattern = false;
            if (renyokei_end < codepoints.size()) {
              char32_t next_char = codepoints[renyokei_end];
              if (next_char == U'べ') {
                is_beki_pattern = true;
              }
            }

            // Calculate base cost for passive candidates
            // Add a penalty so the grammatical split path (縛ら+れ) can compete.
            // Without this, the merged form (縛られ) has too low a cost (-0.16)
            // and always beats the split path (縛ら(0.1) + れ(aux))
            float base_cost = candidate::confidenceScaledCost(verb_opts.bonus_ichidan, ichidan_confidence,
                                                              verb_opts.confidence_cost_scale_small) +
                              bigram_cost::kMinor;

            // A passive stem before a causative remains split as mizenkei +
            // passive + causative (書か+れ+させる); it is not a lexical
            // renyokei followed directly by させる. Preserve the same rule as
            // the existing classical べき boundary.
            const bool is_passive_causative_chain = vh::causativeSaseFollowsAt(codepoints, renyokei_end);
            if (!is_beki_pattern && !is_passive_causative_chain) {
              candidates.push_back(makeVerbCandidate(
                  surface, start_pos, renyokei_end, base_cost, base_lemma, dictionary::ConjugationType::Ichidan, false,
                  CandidateOrigin::VerbKanji, ichidan_confidence, "godan_passive_renyokei"));
            }

            // NOTE: Passive verb conjugated forms (言われる, 言われた, etc.) are NOT generated
            // as single tokens. MeCab splits them as: 言わ + れ + た
            // The renyokei form (言われ) generated above connects to auxiliary た/て/ない/etc.
          }
        }  // end else (not dict ichidan verb)
      }  // end else (not suru passive pattern)
    }
  }
}

}  // namespace suzume::analysis::kanji_verb_detail
