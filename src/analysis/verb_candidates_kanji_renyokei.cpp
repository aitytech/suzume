/**
 * @file verb_candidates_kanji_renyokei.cpp
 * @brief Kanji verb renyokei candidate patterns
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

void appendIchidanRenyokeiCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t kanji_end,
                                     size_t hiragana_end, const grammar::Inflection& inflection,
                                     const dictionary::DictionaryManager* dict_manager,
                                     const VerbCandidateOptions& verb_opts, std::vector<UnknownCandidate>& candidates) {
  if (kanji_end < hiragana_end) {
    char32_t first_hira = codepoints[kanji_end];
    // E-row hiragana: え, け, せ, て, ね, へ, め, れ, げ, ぜ, で, べ, ぺ
    // I-row hiragana: い, き, し, ち, に, ひ, み, り, ぎ, じ, ぢ, び, ぴ
    if (grammar::isERowCodepoint(first_hira) || grammar::isIRowCodepoint(first_hira)) {
      // Skip hiragana commonly used as particles after single kanji
      // で (te-form/particle), に (particle), へ (particle) are rarely Ichidan stem endings
      // These almost always represent kanji + particle (雨で→雨+で, 本に→本+に)
      // Also skip い (i) - this is almost always an i-adjective suffix (面白い, 高い)
      // not an ichidan verb renyoukei. The closed set of kami-ichidan
      // renyokei stems ending in い (率い, 用い, ...) is exempted.
      bool is_common_particle = (first_hira == U'で' || first_hira == U'に' || first_hira == U'へ');
      bool is_i_adjective_suffix = (first_hira == U'い') && !grammar::inflection::isValidKanjiIStemException(
                                                                extractSubstring(codepoints, start_pos, kanji_end + 1));
      bool is_single_kanji = (kanji_end == start_pos + 1);
      // Skip kuru irregular verb: 来 + て/た should not be treated as ichidan
      // 来る is kuru irregular, not ichidan (来て should have lemma 来る, not 来てる)
      bool is_kuru_verb = is_single_kanji && codepoints[start_pos] == U'来';
      if ((is_common_particle && is_single_kanji) || is_i_adjective_suffix || is_kuru_verb) {
        // Skip this pattern - almost certainly noun + particle, i-adjective, or kuru verb
      } else {
        // Surface is kanji + first e/i-row hiragana only (e.g., 食べ from 食べます, 感じ from 感じる)
        size_t renyokei_end = kanji_end + 1;
        std::string surface = extractSubstring(codepoints, start_pos, renyokei_end);
        // Get all inflection candidates, not just the best
        // This is important for ambiguous cases like 入れ (godan 入る imperative vs ichidan 入れる renyoukei)
        const auto& all_cands = inflection.analyze(surface);
        // Find the best Ichidan, Suru, and Godan candidates
        vh::VerbClassBests bests = vh::bestByVerbClass(all_cands);
        const grammar::InflectionCandidate& ichidan_cand = bests.ichidan;
        const grammar::InflectionCandidate& suru_cand = bests.suru;
        const grammar::InflectionCandidate& godan_cand = bests.godan;
        // Skip if there's a suru-verb or godan-verb candidate with higher confidence
        // e.g., 勉強し has suru conf=0.82 vs ichidan conf=0.3 - prefer suru
        // e.g., 走り has godan conf=0.61 vs ichidan conf=0.3 - prefer godan
        const bool ichidan_base_is_dict = vh::isVerbInDictionary(dict_manager, ichidan_cand.base_form);
        bool prefer_suru = !ichidan_base_is_dict && (suru_cand.confidence > ichidan_cand.confidence);
        bool prefer_godan = !ichidan_base_is_dict && (godan_cand.confidence > ichidan_cand.confidence);
        // Use different thresholds for e-row vs i-row patterns:
        // - I-row (じ, み, etc.): lower threshold (0.28) - these are distinctively verb stems
        //   and get penalized by ichidan_kanji_i_row_stem, so need lower threshold
        // - E-row (べ, れ, etc.): use 0.28 threshold to catch renyoukei like 入れ (conf=0.3)
        //   while avoiding too many false positives
        float conf_threshold = verb_opts.confidence_ichidan_dict;
        // Skip if surface is registered as NOUN in dictionary
        // This prevents nominalized verb forms (売り上げ, 楽しみ, 晴れ) from being tokenized as VERB
        // when they are explicitly registered as nouns.
        // Exception: a following ます-family, た/て-family, or causative させ auxiliary attaches
        // only to a verb renyokei/mizenkei, so the verb reading of a noun homograph
        // must survive (感じます → 感じ(VERB) + ます; 感じさせる → 感じ(VERB) + させる,
        // not 感じ(NOUN) + さ + せる); standalone 感じ stays NOUN.
        const char32_t continuation = renyokei_end < codepoints.size() ? codepoints[renyokei_end] : U'\0';
        bool verb_aux_follows = continuation == U'た' || continuation == U'て' ||
                                vh::masuAuxFollowsAt(codepoints, renyokei_end) ||
                                vh::causativeSaseFollowsAt(codepoints, renyokei_end);
        bool surface_is_dict_noun = !verb_aux_follows && vh::isNounInDictionary(dict_manager, surface);
        if (surface_is_dict_noun) {
          SUZUME_DEBUG_LOG("[VERB_SKIP] \"" << surface << "\" is dict NOUN, skipping ichidan_renyokei\n");
        }
        // Skip if splitting at a kanji boundary yields a known dictionary verb
        // E.g., 血浴び → 血 + 浴び(る) — 浴びる is a dict verb, so 血浴びる is not a real verb
        bool suffix_is_dict_verb = false;
        if (dict_manager != nullptr && kanji_end > start_pos + 1) {
          for (size_t split = start_pos + 1; split < kanji_end; ++split) {
            std::string remainder = extractSubstring(codepoints, split, renyokei_end);
            std::string remainder_base = remainder + "る";
            if (vh::isVerbInDictionary(dict_manager, remainder_base)) {
              suffix_is_dict_verb = true;
              SUZUME_DEBUG_LOG("[VERB_SKIP] \"" << surface << "\" suffix \"" << remainder_base
                                                << "\" is dict verb, skipping ichidan_renyokei\n");
              break;
            }
          }
        }
        // A surface that is also a dictionary i-adjective (強い) is verbal
        // only in conjugation contexts: renyokei + た/て or mizenkei + られ/させ.
        // Elsewhere (predicate/attributive use: 力が強い, 強い風) the adjective
        // reading is correct, so skip the verb candidate.
        bool adj_homograph_blocked = false;
        if (vh::isAdjectiveInDictionary(dict_manager, surface)) {
          char32_t next_cp = (renyokei_end < codepoints.size()) ? codepoints[renyokei_end] : U'\0';
          adj_homograph_blocked = !(next_cp == U'た' || next_cp == U'て' || next_cp == U'ら' || next_cp == U'さ' ||
                                    next_cp == U'る' || next_cp == U'れ');
        }
        if (!prefer_suru && !prefer_godan && ichidan_cand.confidence > conf_threshold && !surface_is_dict_noun &&
            !suffix_is_dict_verb && !adj_homograph_blocked) {
          // Negative cost to strongly favor split over combined analysis
          // Combined forms get optimal_length bonus (-0.5), so we need to be lower
          float base_cost = candidate::confidenceScaledCost(verb_opts.bonus_ichidan, ichidan_cand.confidence,
                                                            verb_opts.confidence_cost_scale_small);
          // Ichidan renyokei stems are valid morphological units, so mark the
          // candidate as suffixed to avoid the generic length penalty.
          // Set lemma to the base form (e.g., 入れ → 入れる, 論じ → 論じる)
          // This is critical for correct lemmatization when the surface is ambiguous
          // (e.g., 入れ could be godan 入る imperative or ichidan 入れる renyoukei)
          auto renyokei_candidate =
              makeVerbCandidate(surface, start_pos, renyokei_end, base_cost, ichidan_cand.base_form,
                                grammar::verbTypeToConjType(ichidan_cand.verb_type), true, CandidateOrigin::VerbKanji,
                                ichidan_cand.confidence, "ichidan_renyokei");
          renyokei_candidate.lemma_verified = ichidan_base_is_dict;
          candidates.push_back(std::move(renyokei_candidate));
          SUZUME_DEBUG_LOG_VERBOSE("[VERB_CAND] " << surface << " ichidan_renyokei lemma=" << ichidan_cand.base_form
                                                  << " cost=" << base_cost << "\n");
          // Also generate shuushikei (dictionary form) if followed by る
          // E.g., 捨てるわけ → 捨てる (VERB shuushikei) + わけ (NOUN)
          // Without this, compound noun 捨てるわけ wins over split path
          // Restricted to single-kanji stems or dict-verified verbs to avoid
          // false merges like 間+炒める → 間炒める (suffix + verb)
          if (renyokei_end < codepoints.size() && codepoints[renyokei_end] == U'る') {
            bool is_single_kanji = (kanji_end - start_pos == 1);
            bool is_in_dict = (dict_manager != nullptr && vh::isVerbInDictionary(dict_manager, ichidan_cand.base_form));
            if (is_single_kanji || is_in_dict) {
              size_t shuushi_end = renyokei_end + 1;
              std::string shuushi_surface = extractSubstring(codepoints, start_pos, shuushi_end);
              float shuushi_cost = base_cost + 0.1F;  // Slightly higher than renyokei
              candidates.push_back(
                  makeVerbCandidate(shuushi_surface, start_pos, shuushi_end, shuushi_cost, ichidan_cand.base_form,
                                    grammar::verbTypeToConjType(ichidan_cand.verb_type), true,
                                    CandidateOrigin::VerbKanji, ichidan_cand.confidence, "ichidan_shuushikei"));
            }
          }
        }
      }
    }
    // Try multi-char hiragana ichidan renyokei: kanji + 2 hiragana ending in e/i-row.
    // This covers stems such as 聞こえ and 踏まえ. The first hiragana alone is
    // not a sufficient signal, so require an actual ichidan continuation after
    // the stem before generating an unknown-word candidate.
    if (hiragana_end >= kanji_end + 2) {
      char32_t first_hira = codepoints[kanji_end];
      char32_t second_hira = codepoints[kanji_end + 1];
      size_t renyokei_end = kanji_end + 2;
      bool first_is_single_stem_ending = grammar::isERowCodepoint(first_hira) || grammar::isIRowCodepoint(first_hira);
      size_t following_kanji_end = renyokei_end;
      while (following_kanji_end < codepoints.size() && normalize::isKanjiCodepoint(codepoints[following_kanji_end])) {
        ++following_kanji_end;
      }
      bool follows_kanji_sahen_predicate =
          following_kanji_end > renyokei_end && following_kanji_end + 1 < codepoints.size() &&
          codepoints[following_kanji_end] == U'す' && codepoints[following_kanji_end + 1] == U'る';
      bool has_ichidan_continuation = renyokei_end < codepoints.size() &&
                                      (codepoints[renyokei_end] == U'る' || codepoints[renyokei_end] == U'て' ||
                                       codepoints[renyokei_end] == U'た' || codepoints[renyokei_end] == U'ま' ||
                                       codepoints[renyokei_end] == U'な' ||
                                       (codepoints[renyokei_end] == U'れ' && renyokei_end + 1 < codepoints.size() &&
                                        codepoints[renyokei_end + 1] == U'ば') ||
                                       follows_kanji_sahen_predicate);
      if (!first_is_single_stem_ending && has_ichidan_continuation &&
          (grammar::isERowCodepoint(second_hira) || grammar::isIRowCodepoint(second_hira))) {
        std::string surface = extractSubstring(codepoints, start_pos, renyokei_end);
        const auto& all_cands = inflection.analyze(surface);
        vh::VerbClassBests bests = vh::bestByVerbClass(all_cands);
        const grammar::InflectionCandidate& ichidan_cand = bests.ichidan;
        const grammar::InflectionCandidate& suru_cand = bests.suru;
        const grammar::InflectionCandidate& godan_cand = bests.godan;
        bool prefer_suru = (suru_cand.confidence > ichidan_cand.confidence);
        bool prefer_godan = (godan_cand.confidence > ichidan_cand.confidence);
        // Higher confidence threshold for multi-char stems to avoid false positives
        constexpr float kMultiCharIchidanThreshold = 0.45F;
        // Skip surfaces ending in ない — almost always adjective (少ない) or negative suffix
        bool ends_in_nai = (second_hira == U'い' && first_hira == U'な');
        // A-row + せ/れ before an auxiliary continuation is a Godan voice
        // stem (読ま+せる, 読ま+れる). Keep a genuinely lexicalized Ichidan
        // verb such as 泳がせる, but do not generate an unverified long
        // Ichidan candidate that absorbs a causative or passive chain.
        bool is_unverified_godan_voice = grammar::isARowCodepoint(first_hira) &&
                                         (second_hira == U'せ' || second_hira == U'れ') &&
                                         !vh::isVerbInDictionary(dict_manager, ichidan_cand.base_form);
        if (!prefer_suru && !prefer_godan && !ends_in_nai && !is_unverified_godan_voice &&
            ichidan_cand.confidence > kMultiCharIchidanThreshold) {
          bool surface_is_dict_entry = vh::isNounOrAdjectiveInDictionary(dict_manager, surface);
          bool base_is_dict_verb = vh::isVerbInDictionary(dict_manager, ichidan_cand.base_form);
          // A non-dictionary multi-kana stem must not absorb a closed-class
          // focus particle: 本+さえ and 水+すら are nominal phrases, not
          // renyokei of fabricated verbs. Dictionary-verified verbs remain
          // available for genuine lexical surfaces that happen to end alike.
          // @see fabricated closed-class absorption guards (verb_candidates_helpers.h)
          bool absorbs_focus_particle =
              !base_is_dict_verb && vh::endsWithFocusParticleTail(dict_manager, codepoints, start_pos, renyokei_end);
          if ((!surface_is_dict_entry || base_is_dict_verb) && !absorbs_focus_particle) {
            float base_cost = candidate::confidenceScaledCost(verb_opts.bonus_ichidan, ichidan_cand.confidence,
                                                              verb_opts.confidence_cost_scale_small);
            auto renyokei_candidate =
                makeVerbCandidate(surface, start_pos, renyokei_end, base_cost, ichidan_cand.base_form,
                                  grammar::verbTypeToConjType(ichidan_cand.verb_type), true, CandidateOrigin::VerbKanji,
                                  ichidan_cand.confidence, "ichidan_renyokei_multi");
            renyokei_candidate.lemma_verified = base_is_dict_verb;
            candidates.push_back(std::move(renyokei_candidate));

            if (codepoints[renyokei_end] == U'れ' && renyokei_end + 1 < codepoints.size() &&
                codepoints[renyokei_end + 1] == U'ば') {
              std::string kateikei_surface = extractSubstring(codepoints, start_pos, renyokei_end + 1);
              float kateikei_confidence = vh::getIchidanConfidence(inflection.analyze(kateikei_surface),
                                                                   candidate::verb_cost::kIchidanKateikeiMinConfidence);
              if (kateikei_confidence >= candidate::verb_cost::kIchidanKateikeiMinConfidence) {
                candidates.push_back(makeVerbCandidate(
                    kateikei_surface, start_pos, renyokei_end + 1, candidate::verb_cost::kStrongBonus, surface + "る",
                    dictionary::ConjugationType::Ichidan, true, CandidateOrigin::VerbKanji, kateikei_confidence,
                    "ichidan_kateikei_multi", core::ExtendedPOS::VerbKateikei));
              }
            }

            if (codepoints[renyokei_end] == U'る') {
              candidates.push_back(
                  makeVerbCandidate(extractSubstring(codepoints, start_pos, renyokei_end + 1), start_pos,
                                    renyokei_end + 1, base_cost + candidate::verb_cost::kWeakPenalty,
                                    ichidan_cand.base_form, grammar::verbTypeToConjType(ichidan_cand.verb_type), true,
                                    CandidateOrigin::VerbKanji, ichidan_cand.confidence, "ichidan_shuushikei_multi"));
            }
          }
        }
      }
    }
  }
}

// Try Godan-Sa renyokei stem pattern: kanji + hiragana ending in し
// E.g., 過ごし (過ごす), 話し (話す), 取り消し (取り消す)
// These are needed when the verb is not in the dictionary, to enable
// correct splitting at て-form boundaries (過ごし+て+み+たい)
// Check positions kanji_end+1 through kanji_end+3 for し-ending godan-sa renyokei
void appendGodanSaRenyokeiCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t kanji_end,
                                     size_t hiragana_end, const grammar::Inflection& inflection,
                                     const dictionary::DictionaryManager* dict_manager,
                                     const VerbCandidateOptions& verb_opts, std::vector<UnknownCandidate>& candidates) {
  if (hiragana_end > kanji_end) {
    size_t max_renyokei_end = std::min(kanji_end + 4, hiragana_end + 1);
    for (size_t renyokei_end = kanji_end + 1; renyokei_end <= max_renyokei_end && renyokei_end <= codepoints.size();
         ++renyokei_end) {
      // Must end in し (godan-sa renyokei marker)
      if (codepoints[renyokei_end - 1] != U'し')
        continue;

      std::string surface = extractSubstring(codepoints, start_pos, renyokei_end);
      const auto& all_cands = inflection.analyze(surface);

      // Find best godan-sa candidate
      grammar::InflectionCandidate best_sa;
      best_sa.confidence = 0.0F;
      for (const auto& cand : all_cands) {
        if (cand.has_explanatory_suffix)
          continue;
        if (cand.verb_type == grammar::VerbType::GodanSa && cand.confidence > best_sa.confidence) {
          best_sa = cand;
        }
      }

      if (best_sa.confidence <= 0.5F)
        continue;

      // Skip if surface is a dictionary NOUN (exact match)
      if (vh::isNounInDictionary(dict_manager, surface))
        continue;

      // For short godan-sa patterns, require dict verification to avoid
      // false positives like 悲し (not a real verb 悲す) or 春らし (not 春らす).
      // Multi-kanji verbs (過ごし, 見逃し) are more likely real verbs.
      float non_dict_penalty = 0.0F;
      size_t kanji_chars = kanji_end - start_pos;  // actual kanji count
      size_t hira_chars = renyokei_end - kanji_end;
      if (kanji_chars <= 1 && dict_manager != nullptr) {
        if (!vh::isVerbInDictionary(dict_manager, best_sa.base_form)) {
          // A one-mora case particle followed by し and the て/た form is a
          // productive noun + particle + する construction, not an unknown
          // GodanSa verb. This covers short-noun contexts such as 本+として
          // and 本+とした without suppressing real stems such as 話し or 尽くし.
          if (hira_chars == 2 && renyokei_end < codepoints.size() &&
              (codepoints[renyokei_end] == U'て' || codepoints[renyokei_end] == U'た') &&
              vh::hasParticleDictionaryEntry(dict_manager, extractSubstring(codepoints, kanji_end, kanji_end + 1))) {
            SUZUME_DEBUG_LOG("[VERB_SKIP] \"" << surface << "\" godan_sa case-particle+する pattern\n");
            continue;
          }
          if (hira_chars <= 1) {
            // Single kanji + 1 hiragana (呈し, 訴え, 課し etc.).
            // Many one-kanji 漢語サ変動詞 stems exist; the verb candidate must
            // be available so it can compete with NOUN nominalization. Use
            // strong non-dict penalty so 悲し (no real 悲す verb) still loses to
            // NOUN/adjective interpretations, while 呈し (followed by 、/aux)
            // can win via context.
            non_dict_penalty = bigram_cost::kStrong;
          } else {
            // Block kanji+まし pattern (false godan-sa from verb+ます renyoukei)
            // E.g., 来まし → 来ます (false), 出まし → 出ます (false)
            if (codepoints[kanji_end] == U'ま') {
              SUZUME_DEBUG_LOG("[VERB_SKIP] \"" << surface << "\" godan_sa kanji+まし pattern (likely verb+ます)\n");
              continue;
            }
            // Block [renyokei vowel]+し followed by a する-auxiliary: the surface
            // is a verb renyokei + する renyokei (お伝えします → 伝え+し+ます,
            // お待ちします → 待ち+し+ます), not a godan-sa stem. Real godan-sa
            // verbs keep し directly after the kanji (貸し←貸す, 話し←話す), so
            // the char before し is a kanji there; a hiragana i-row (godan) or
            // e-row (ichidan) renyokei vowel before し means the godan-sa base
            // (待ちす/伝えす) is fabricated and would glue the humble form.
            char32_t before_shi = codepoints[renyokei_end - 2];
            if ((grammar::isIRowCodepoint(before_shi) || grammar::isERowCodepoint(before_shi)) &&
                renyokei_end < codepoints.size() && vh::isSuruAuxiliaryStarter(codepoints[renyokei_end])) {
              SUZUME_DEBUG_LOG("[VERB_SKIP] \"" << surface << "\" godan_sa verb-renyokei+し+する-aux pattern\n");
              continue;
            }
            // 2+ hiragana non-ます pattern (尽くし) — allow with penalty
            non_dict_penalty = bigram_cost::kMinor;
          }
        }
      }

      float base_cost = candidate::confidenceScaledCost(verb_opts.bonus_ichidan, best_sa.confidence,
                                                        verb_opts.confidence_cost_scale_small) +
                        non_dict_penalty;
      SUZUME_DEBUG_VERBOSE_BLOCK {
        SUZUME_DEBUG_STREAM << "[VERB_CAND] " << surface << " godan_sa_renyokei lemma=" << best_sa.base_form
                            << " conf=" << best_sa.confidence << " cost=" << base_cost << "\n";
      }
      candidates.push_back(makeVerbCandidate(surface, start_pos, renyokei_end, base_cost, best_sa.base_form,
                                             grammar::verbTypeToConjType(best_sa.verb_type), true,
                                             CandidateOrigin::VerbKanji, best_sa.confidence, "godan_sa_renyokei"));
    }
  }
}

// Try Ichidan verb kateikei (conditional) + volitional stem patterns.
// Kateikei: renyokei + れ + ば (食べれば → 食べれ + ば).
// Volitional: renyokei + よ + う (食べよう → 食べよ + う).
// MeCab splits these; generate the stem candidate for the split.
void appendIchidanKateikeiVolitionalCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                               size_t kanji_end, size_t hiragana_end,
                                               const grammar::Inflection& inflection,
                                               const dictionary::DictionaryManager* dict_manager,
                                               std::vector<UnknownCandidate>& candidates) {
  // A single-kanji ichidan stem can attach directly to よう (見よう, 着よう).
  // Unlike 食べよう, there is no e-row renyokei kana before よ, so emit the
  // mizenkei stem separately after inflection confirms the full form.
  if (kanji_end == start_pos + 1 && kanji_end + 1 < codepoints.size() && codepoints[kanji_end] == U'よ' &&
      codepoints[kanji_end + 1] == U'う') {
    std::string full_surface = extractSubstring(codepoints, start_pos, kanji_end + 2);
    float confidence =
        vh::getIchidanConfidence(inflection.analyze(full_surface), candidate::verb_cost::kIchidanKateikeiMinConfidence);
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
        float ichidan_confidence = vh::getIchidanConfidence(all_candidates, 0.3F);

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
          float ichidan_confidence = vh::getIchidanConfidence(all_candidates, min_confidence);

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
                                       const VerbCandidateOptions& verb_opts,
                                       std::vector<UnknownCandidate>& candidates) {
  if (kanji_end + 2 <= hiragana_end) {
    char32_t first_hira = codepoints[kanji_end];
    char32_t second_hira = codepoints[kanji_end + 1];
    // ら + せ pattern (causative renyokei)
    if (first_hira == U'ら' && second_hira == U'せ') {
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
        float ichidan_confidence = vh::getIchidanConfidence(all_candidates);

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
          float ichidan_confidence = vh::getIchidanConfidence(all_candidates);

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

// Generate Ichidan stem candidates for passive/potential auxiliary patterns
// E.g., 信じられべき (信じ + られべき), 認められた (認め + られた)
// These connect to られ+X (passive/potential auxiliary forms)
// Unlike Godan mizenkei which uses れ+X, Ichidan uses られ+X

}  // namespace suzume::analysis::kanji_verb_detail
