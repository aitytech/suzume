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

float getIchidanConfidence(const std::vector<grammar::InflectionCandidate>& candidates, float min_threshold) {
  float best = candidate::kNoConfidence;
  for (const auto& candidate : candidates) {
    if (candidate.verb_type == grammar::VerbType::Ichidan && candidate.confidence >= min_threshold) {
      best = std::max(best, candidate.confidence);
    }
  }
  return best;
}

void appendIchidanRenyokeiCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t kanji_end,
                                     size_t hiragana_end, const grammar::Inflection& inflection,
                                     const dictionary::DictionaryManager* dict_manager,
                                     const VerbCandidateOptions& verb_opts, std::vector<UnknownCandidate>& candidates) {
  // A numeral followed by a temporal counter establishes a quantity boundary
  // (十年|余り, 三日|余り).  An unknown Ichidan proposal must not absorb that
  // quantity and reinterpret it as a verb merely because the following kana
  // happens to be an e/i-row continuative.
  size_t numeral_end = start_pos;
  while (numeral_end < kanji_end && normalize::isNumeralCodepoint(codepoints[numeral_end])) {
    ++numeral_end;
  }
  if (numeral_end > start_pos && numeral_end < kanji_end &&
      normalize::isTemporalCounterKanji(codepoints[numeral_end])) {
    return;
  }

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
        // An A-row stem followed by させ is already a Godan mizenkei plus
        // causative auxiliary (聞か+せ, 読ま+せ).  Only non-A-row stems use
        // that evidence to recover an Ichidan lexical stem.
        const bool causative_follows =
            vh::causativeSaseFollowsAt(codepoints, renyokei_end) && !grammar::isARowCodepoint(first_hira);
        const bool passive_follows = renyokei_end + 1 < codepoints.size() && codepoints[renyokei_end] == U'ら' &&
                                     codepoints[renyokei_end + 1] == U'れ' &&
                                     vh::isPassiveAuxContinuation(codepoints, renyokei_end + 2, /*strict_masu=*/true);
        // Skip if there's a suru-verb or godan-verb candidate with higher confidence
        // e.g., 勉強し has suru conf=0.82 vs ichidan conf=0.3 - prefer suru
        // e.g., 走り has godan conf=0.61 vs ichidan conf=0.3 - prefer godan
        const bool ichidan_base_is_dict = vh::isVerbInDictionary(dict_manager, ichidan_cand.base_form);
        const bool godan_base_is_dict = vh::isVerbInDictionary(dict_manager, godan_cand.base_form);
        const bool comma_clause_chaining =
            vh::isCommaClauseChainingRenyokei(codepoints, start_pos, renyokei_end, dict_manager);
        bool prefer_suru = !causative_follows && !passive_follows && !ichidan_base_is_dict &&
                           (suru_cand.confidence > ichidan_cand.confidence);
        bool prefer_godan = !causative_follows && !passive_follows && !ichidan_base_is_dict &&
                            (!comma_clause_chaining || godan_base_is_dict) &&
                            (godan_cand.confidence > ichidan_cand.confidence);
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
                                vh::masuAuxFollowsAt(codepoints, renyokei_end) || causative_follows || passive_follows;
        // A dictionary-verified single-kanji Ichidan base followed by て and
        // a contracted progressive or past marker is a te-form boundary
        // (見+てる, 見+て+た), not a fabricated verb such as 見てる.
        const bool single_kanji_te_form = is_single_kanji && first_hira == U'て' &&
                                          (continuation == U'た' || continuation == U'る') &&
                                          vh::isSingleKanjiIchidan(codepoints[start_pos]);
        const bool negative_aux_follows =
            continuation == U'な' && renyokei_end + 1 < codepoints.size() && codepoints[renyokei_end + 1] == U'い';
        // An Ichidan verb uses the same stem before the classical negative
        // auxiliaries ぬ/ず/ざる/ざれ as it does before ない.  In this
        // environment the candidate surface is the full stem, so recover its
        // lemma by appending る rather than asking the standalone inflection
        // analyzer to interpret a final て as a te-form suffix.
        const bool classical_negative_aux_follows =
            first_hira != U'せ' && (continuation == U'ぬ' || continuation == U'ず' ||
                                    (continuation == U'ざ' && renyokei_end + 1 < codepoints.size() &&
                                     (codepoints[renyokei_end + 1] == U'る' || codepoints[renyokei_end + 1] == U'れ')));
        // A multi-kanji nominal stem followed by せ+ん is the literary
        // irrealis of する (解決+せ+ん), not an unverified Ichidan verb
        // ending in ～せる.  Dictionary-verified lexical verbs such as
        // 見せる remain eligible.
        const bool unverified_multi_kanji_suru_mizen =
            !ichidan_base_is_dict && kanji_end - start_pos >= 2 && first_hira == U'せ' && continuation == U'ん';
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
        // A productive suffix boundary must not be hidden inside an
        // unverified Ichidan proposal (中+抜き, not fabricated 中抜きる).
        // Check every trailing span because lexical suffixes can begin with
        // either kanji or hiragana.
        bool trailing_span_is_dict_suffix = false;
        if (!ichidan_base_is_dict && dict_manager != nullptr && kanji_end > start_pos + 1) {
          for (size_t split = start_pos + 1; split < renyokei_end; ++split) {
            const std::string suffix = extractSubstring(codepoints, split, renyokei_end);
            if (vh::hasDictionaryEntry(dict_manager, suffix, core::PartOfSpeech::Suffix)) {
              trailing_span_is_dict_suffix = true;
              SUZUME_DEBUG_LOG("[VERB_SKIP] \"" << surface << "\" trailing suffix \"" << suffix
                                                << "\", skipping ichidan_renyokei\n");
              break;
            }
          }
        }
        // An unverified multi-kanji Ichidan proposal must not hide a
        // noun + Godan continuative boundary before the classical
        // conjectural auxiliary. The auxiliary supplies grammatical evidence
        // for the final-kanji verb without requiring either open-class word
        // to be registered in the compact dictionary.
        bool suffix_is_godan_before_classical_auxiliary = false;
        if (!ichidan_base_is_dict && kanji_end > start_pos + 1 &&
            grammar::startsClassicalConjecturalAuxiliary(
                extractSubstring(codepoints, renyokei_end, codepoints.size()))) {
          const std::string suffix_surface = extractSubstring(codepoints, kanji_end - 1, renyokei_end);
          for (const auto& suffix_candidate : inflection.analyze(suffix_surface)) {
            if (grammar::isGodanVerbType(suffix_candidate.verb_type)) {
              suffix_is_godan_before_classical_auxiliary = true;
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
        // A bare, unverified multi-kanji Ichidan continuative immediately
        // before a closed temporal nominal is itself a deverbal temporal noun
        // (夜明け+前, 夕暮れ+どき), not evidence for a fabricated predicate such
        // as 夜明ける. Dictionary-attested verbs and single-kanji predicates
        // retain their ordinary continuative candidates.
        const bool unverified_before_temporal_nominal =
            !ichidan_base_is_dict && kanji_end > start_pos + 1 &&
            grammar::startsClosedTemporalNominal(extractSubstring(codepoints, renyokei_end, codepoints.size()));
        if (!prefer_suru && !prefer_godan && ichidan_cand.confidence > conf_threshold && !surface_is_dict_noun &&
            !single_kanji_te_form && !suffix_is_dict_verb && !trailing_span_is_dict_suffix &&
            !suffix_is_godan_before_classical_auxiliary && !adj_homograph_blocked &&
            !unverified_multi_kanji_suru_mizen && !unverified_before_temporal_nominal) {
          // Negative cost to strongly favor split over combined analysis
          // Combined forms get optimal_length bonus (-0.5), so we need to be lower
          float base_cost = (causative_follows || passive_follows)
                                ? candidate::verb_cost::kStrongBonus
                                : candidate::confidenceScaledCost(verb_opts.bonus_ichidan, ichidan_cand.confidence,
                                                                  verb_opts.confidence_cost_scale_small);
          // An unverified multi-kanji stem is far more often a noun plus a
          // separate predicate than a real Ichidan verb (花散り is 花 + 散り,
          // ご飯食べ is ご飯 + 食べ). The terminal path already charges such a
          // hypothesis; charge the continuative path identically so a negative
          // cost only ever reaches a dictionary-backed stem.
          if (!ichidan_base_is_dict && kanji_end - start_pos >= 2) {
            base_cost += bigram_cost::kRare;
            SUZUME_DEBUG_LOG_VERBOSE("[COST_ADJ] \"" << surface << "\" +" << bigram_cost::kRare
                                                     << " (ichidan_renyokei_multi_kanji_non_dict)\n");
          }
          // Ichidan renyokei stems are valid morphological units, so mark the
          // candidate as suffixed to avoid the generic length penalty.
          // The whole surface is the Ichidan stem by construction here (kanji
          // run plus one e/i-row kana), so the base form is the stem plus る
          // regardless of the stem's final row. Asking the standalone analyzer
          // instead mis-reads a final て/で as a te-form auxiliary and rebuilds
          // the lemma from the truncated stem (立て → 立る).
          const std::string lemma = surface + "る";
          auto renyokei_candidate = makeVerbCandidate(
              surface, start_pos, renyokei_end, base_cost, lemma, grammar::verbTypeToConjType(ichidan_cand.verb_type),
              true, CandidateOrigin::VerbKanji, ichidan_cand.confidence, "ichidan_renyokei",
              (negative_aux_follows || classical_negative_aux_follows || causative_follows)
                  ? core::ExtendedPOS::VerbMizenkei
                  : core::ExtendedPOS::VerbRenyokei);
          renyokei_candidate.lemma_verified = ichidan_base_is_dict;
          candidates.push_back(std::move(renyokei_candidate));
          SUZUME_DEBUG_LOG_VERBOSE("[VERB_CAND] " << surface << " ichidan_renyokei lemma=" << lemma
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
              candidates.push_back(makeVerbCandidate(shuushi_surface, start_pos, shuushi_end, shuushi_cost, lemma,
                                                     grammar::verbTypeToConjType(ichidan_cand.verb_type), true,
                                                     CandidateOrigin::VerbKanji, ichidan_cand.confidence,
                                                     "ichidan_shuushikei"));
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
      const bool causative_follows = vh::causativeSaseFollowsAt(codepoints, renyokei_end);
      const bool passive_follows = renyokei_end + 1 < codepoints.size() && codepoints[renyokei_end] == U'ら' &&
                                   codepoints[renyokei_end + 1] == U'れ' &&
                                   vh::isPassiveAuxContinuation(codepoints, renyokei_end + 2, /*strict_masu=*/true);
      const bool classical_negative_follows =
          renyokei_end < codepoints.size() && (codepoints[renyokei_end] == U'ず' || codepoints[renyokei_end] == U'ぬ');
      bool follows_symbol_after_case_particle = false;
      if (renyokei_end < codepoints.size() &&
          normalize::classifyChar(codepoints[renyokei_end]) == normalize::CharType::Symbol && start_pos > 0 &&
          dict_manager != nullptr) {
        const auto* preceding_particle = dict_manager->lookupExact(
            extractSubstring(codepoints, start_pos - 1, start_pos), core::PartOfSpeech::Particle);
        follows_symbol_after_case_particle =
            preceding_particle != nullptr && preceding_particle->extended_pos == core::ExtendedPOS::ParticleCase;
      }
      bool has_ichidan_continuation =
          renyokei_end < codepoints.size() &&
          (codepoints[renyokei_end] == U'る' || codepoints[renyokei_end] == U'て' ||
           codepoints[renyokei_end] == U'た' || codepoints[renyokei_end] == U'ま' ||
           codepoints[renyokei_end] == U'な' || codepoints[renyokei_end] == U'ず' ||
           codepoints[renyokei_end] == U'ぬ' ||
           (codepoints[renyokei_end] == U'れ' && renyokei_end + 1 < codepoints.size() &&
            codepoints[renyokei_end + 1] == U'ば') ||
           follows_kanji_sahen_predicate || causative_follows || passive_follows || follows_symbol_after_case_particle);
      if (!first_is_single_stem_ending && has_ichidan_continuation &&
          (grammar::isERowCodepoint(second_hira) || grammar::isIRowCodepoint(second_hira))) {
        std::string surface = extractSubstring(codepoints, start_pos, renyokei_end);
        const auto& all_cands = inflection.analyze(surface);
        vh::VerbClassBests bests = vh::bestByVerbClass(all_cands);
        const grammar::InflectionCandidate& ichidan_cand = bests.ichidan;
        const grammar::InflectionCandidate& suru_cand = bests.suru;
        const grammar::InflectionCandidate& godan_cand = bests.godan;
        bool prefer_suru = !causative_follows && !passive_follows && (suru_cand.confidence > ichidan_cand.confidence);
        bool prefer_godan = !causative_follows && !passive_follows && (godan_cand.confidence > ichidan_cand.confidence);
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
            float base_cost = (causative_follows || passive_follows)
                                  ? candidate::verb_cost::kStrongBonus
                                  : candidate::confidenceScaledCost(verb_opts.bonus_ichidan, ichidan_cand.confidence,
                                                                    verb_opts.confidence_cost_scale_small);
            auto renyokei_candidate =
                makeVerbCandidate(surface, start_pos, renyokei_end, base_cost, ichidan_cand.base_form,
                                  grammar::verbTypeToConjType(ichidan_cand.verb_type), true, CandidateOrigin::VerbKanji,
                                  ichidan_cand.confidence, "ichidan_renyokei_multi",
                                  (causative_follows || classical_negative_follows) ? core::ExtendedPOS::VerbMizenkei
                                                                                    : core::ExtendedPOS::VerbRenyokei);
            renyokei_candidate.lemma_verified = base_is_dict_verb;
            candidates.push_back(std::move(renyokei_candidate));

            if (codepoints[renyokei_end] == U'れ' && renyokei_end + 1 < codepoints.size() &&
                codepoints[renyokei_end + 1] == U'ば') {
              std::string kateikei_surface = extractSubstring(codepoints, start_pos, renyokei_end + 1);
              float kateikei_confidence = getIchidanConfidence(inflection.analyze(kateikei_surface),
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

      // A dictionary-backed single-kanji する verb has the same 連用形 shape
      // as an unknown GodanSa verb (反し: 反する vs fabricated 反す).  The
      // generic inflection scorer deliberately disfavors single-kanji Suru
      // stems, so recover this closed ambiguity from the attested base form
      // before falling back to productive GodanSa generation.  Multi-kanji
      // Sahen predicates remain compositional search units (確認+し).
      if (kanji_end == start_pos + 1 && renyokei_end == kanji_end + 1) {
        const std::string suru_base = extractSubstring(codepoints, start_pos, kanji_end) + "する";
        if (vh::isVerbInDictionary(dict_manager, suru_base)) {
          auto suru_candidate = makeVerbCandidate(
              surface, start_pos, renyokei_end, candidate::verb_cost::kStrongBonus, suru_base,
              dictionary::ConjugationType::Suru, true, CandidateOrigin::VerbKanji, candidate::kVerifiedConfidence,
              "verified_single_kanji_suru_renyokei", core::ExtendedPOS::VerbRenyokei);
          suru_candidate.lemma_verified = true;
          candidates.push_back(std::move(suru_candidate));
          SUZUME_DEBUG_LOG_VERBOSE("[VERB_CAND] "
                                   << surface << " verified single-kanji suru renyokei lemma=" << suru_base << "\n");
          continue;
        }
      }

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

      if (utf8::endsWith(surface, "くし")) {
        const std::string adjective_base = std::string(utf8::dropLast2Chars(surface)) + "い";
        if (vh::isAdjectiveInDictionary(dict_manager, adjective_base)) {
          continue;
        }
      }

      if (isInterrogativeKanji(codepoints[start_pos])) {
        continue;
      }

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
              vh::hasCaseParticleDictionaryEntry(dict_manager,
                                                 extractSubstring(codepoints, kanji_end, kanji_end + 1))) {
            SUZUME_DEBUG_LOG("[VERB_SKIP] \"" << surface << "\" godan_sa case-particle+する pattern\n");
            continue;
          }
          if (hira_chars <= 1) {
            const std::string preceding_surface =
                start_pos > 0 ? extractSubstring(codepoints, start_pos - 1, start_pos) : std::string();
            const auto* preceding_particle =
                dict_manager != nullptr && !preceding_surface.empty()
                    ? dict_manager->lookupExact(preceding_surface, core::PartOfSpeech::Particle)
                    : nullptr;
            const bool follows_case_particle =
                preceding_particle != nullptr && preceding_particle->extended_pos == core::ExtendedPOS::ParticleCase;
            const bool sahen_past_after_ichidan_stem =
                hira_chars == 1 && codepoints[kanji_end] == U'し' && renyokei_end < codepoints.size() &&
                codepoints[renyokei_end] == U'た' && vh::isSingleKanjiIchidan(codepoints[start_pos]) &&
                !follows_case_particle;
            if (sahen_past_after_ichidan_stem) {
              continue;
            }
            // Single kanji + 1 hiragana (呈し, 訴え, 課し etc.).
            // Many one-kanji 漢語サ変動詞 stems exist; the verb candidate must
            // be available so it can compete with NOUN nominalization. Use
            // strong non-dict penalty so 悲し (no real 悲す verb) still loses to
            // NOUN/adjective interpretations, while 呈し (followed by 、/aux)
            // can win via context.
            // A connective or past auxiliary provides direct inflectional
            // evidence, so retain the lexical-verb candidate for forms such
            // as 押して while keeping the stronger guard before nominal-like
            // continuations such as 話しそう.
            const bool inflectional_continuation =
                renyokei_end < codepoints.size() &&
                (codepoints[renyokei_end] == U'て' || codepoints[renyokei_end] == U'た');
            non_dict_penalty = inflectional_continuation ? bigram_cost::kMinor : bigram_cost::kStrong;
          } else {
            // Block kanji+まし pattern (false godan-sa from verb+ます renyoukei)
            // E.g., 来まし → 来ます (false), 出まし → 出ます (false)
            if (codepoints[kanji_end] == U'ま' && vh::isSingleKanjiPoliteStem(codepoints[start_pos])) {
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
      auto renyokei_candidate = makeVerbCandidate(surface, start_pos, renyokei_end, base_cost, best_sa.base_form,
                                                  grammar::verbTypeToConjType(best_sa.verb_type), true,
                                                  CandidateOrigin::VerbKanji, best_sa.confidence, "godan_sa_renyokei");
      renyokei_candidate.lemma_verified = vh::isVerbInDictionary(dict_manager, best_sa.base_form);
      candidates.push_back(std::move(renyokei_candidate));
    }
  }
}

// Generate Ichidan stem candidates for passive/potential auxiliary patterns
// E.g., 信じられべき (信じ + られべき), 認められた (認め + られた)
// These connect to られ+X (passive/potential auxiliary forms)
// Unlike Godan mizenkei which uses れ+X, Ichidan uses られ+X

}  // namespace suzume::analysis::kanji_verb_detail
