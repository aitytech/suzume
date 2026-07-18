/**
 * @file verb_candidates_kanji.cpp
 * @brief Kanji-based verb candidate generation (generateVerbCandidates)
 *
 * Handles verb candidate generation for kanji+hiragana patterns.
 * Split from verb_candidates.cpp for maintainability.
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

namespace suzume::analysis {

namespace vh = verb_helpers;
using namespace kanji_verb_detail;

namespace {

bool hasNiSugiNegativeTail(const std::vector<char32_t>& codepoints, size_t pos) {
  return pos + 3 < codepoints.size() && codepoints[pos] == U'に' && codepoints[pos + 1] == U'す' &&
         codepoints[pos + 2] == U'ぎ' && vh::naiNegativeFollowsAt(codepoints, pos + 3);
}

bool isVerifiedFiniteVerb(const dictionary::DictionaryManager* dict_manager, const grammar::Inflection& inflection,
                          const grammar::InflectionCandidate& candidate) {
  if (vh::isVerbInDictionary(dict_manager, candidate.base_form)) {
    return true;
  }
  if (candidate.verb_type == grammar::VerbType::Ichidan) {
    return vh::isVerifiedVerbBase(dict_manager, inflection, candidate.base_form,
                                  candidate::verb_cost::kConstructedVerbMinConfidence, false);
  }
  return grammar::isGodanVerbType(candidate.verb_type) &&
         vh::isVerifiedVerbBase(dict_manager, inflection, candidate.base_form,
                                candidate::verb_cost::kConstructedVerbMinConfidence, true);
}

void appendNiSugiPredicateCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t hiragana_end,
                                     const grammar::Inflection& inflection,
                                     const dictionary::DictionaryManager* dict_manager,
                                     std::vector<UnknownCandidate>& candidates) {
  // The limiting construction V終止形+に+すぎない keeps the finite predicate
  // intact (見るにすぎない, 遅れるにすぎない). It is distinct from the
  // excessive construction V連用形+すぎる, so recognize its particle-delimited
  // tail before the latter's renyokei-specific path is considered.
  for (size_t tail_pos = start_pos + 1; tail_pos < hiragana_end; ++tail_pos) {
    if (!hasNiSugiNegativeTail(codepoints, tail_pos)) {
      continue;
    }
    const std::string surface = extractSubstring(codepoints, start_pos, tail_pos);
    for (const auto& inflected : inflection.analyze(surface)) {
      if (inflected.base_form != surface || inflected.verb_type == grammar::VerbType::IAdjective ||
          !isVerifiedFiniteVerb(dict_manager, inflection, inflected)) {
        continue;
      }
      candidates.push_back(makeVerbCandidate(surface, start_pos, tail_pos, candidate::verb_cost::kStrongBonus,
                                             inflected.base_form, grammar::verbTypeToConjType(inflected.verb_type),
                                             true, CandidateOrigin::VerbKanji, inflected.confidence, "finite_ni_sugi",
                                             core::ExtendedPOS::VerbShuushikei));
      return;
    }
  }
}

void appendNiLimitedIchidanCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t hiragana_end,
                                      const grammar::Inflection& inflection,
                                      std::vector<UnknownCandidate>& candidates) {
  // In the limiting predicate Nに+V連用形+ない, a dictionary-verified
  // Ichidan renyokei must remain available over a homographic Godan reading
  // (本に過ぎない). The preceding case particle and following negative make
  // this a grammatical construction rather than a surface-specific exception.
  if (start_pos == 0 || codepoints[start_pos - 1] != U'に') {
    return;
  }
  for (size_t end_pos = start_pos + 1; end_pos < hiragana_end; ++end_pos) {
    if (!vh::naiNegativeFollowsAt(codepoints, end_pos)) {
      continue;
    }
    const std::string surface = extractSubstring(codepoints, start_pos, end_pos);
    // The bare renyokei can be ambiguous (過ぎ → 過ぐ), while its negative
    // continuation supplies the reliable Ichidan evidence (過ぎない → 過ぎる).
    // Analyze that full inflected form, then emit only its stem as the token.
    const std::string negative_surface = extractSubstring(codepoints, start_pos, hiragana_end);
    for (const auto& inflected : inflection.analyze(negative_surface)) {
      if (inflected.stem != surface || inflected.verb_type != grammar::VerbType::Ichidan ||
          inflected.confidence < candidate::verb_cost::kConstructedVerbMinConfidence) {
        continue;
      }
      candidates.push_back(makeVerbCandidate(surface, start_pos, end_pos, candidate::verb_cost::kStrongBonus,
                                             inflected.base_form, dictionary::ConjugationType::Ichidan, true,
                                             CandidateOrigin::VerbKanji, inflected.confidence, "ni_limited_ichidan",
                                             core::ExtendedPOS::VerbRenyokei));
      return;
    }
  }
}

}  // namespace

std::vector<UnknownCandidate> generateVerbCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                                     const std::vector<normalize::CharType>& char_types,
                                                     const grammar::Inflection& inflection,
                                                     const dictionary::DictionaryManager* dict_manager,
                                                     const VerbCandidateOptions& verb_opts) {
  std::vector<UnknownCandidate> candidates;

  if (start_pos >= char_types.size() || char_types[start_pos] != normalize::CharType::Kanji) {
    return candidates;
  }

  // Find kanji portion (typically 1-2 characters for verbs)
  size_t kanji_end = vh::findCharRegionEnd(char_types, start_pos, 3, normalize::CharType::Kanji);

  if (kanji_end == start_pos) {
    return candidates;
  }

  // Look for hiragana after kanji
  if (kanji_end >= char_types.size() || char_types[kanji_end] != normalize::CharType::Hiragana) {
    return candidates;
  }

  // Sokuonbin-prefixed verb stem: single kanji + っ + kanji run (突っ走, 引っ掻).
  // The sokuon is the contracted renyokei of a prefixing verb (突き -> 突っ); the
  // shape kanji+っ+kanji cannot be represented by the plain "kanji run + okurigana"
  // stem model, so the stem is extended to span the embedded verb when it is a
  // dictionary-verified verb showing conjugation evidence (shuushikei or an aux
  // suffix). The dictionary gate + evidence gate exclude same-shape nominal/
  // adjectival compounds (真っ盛り: 盛る not in dict; 手っ取り早い: 取り is a bare
  // renyokei, nominal). Once extended, all downstream conjugation/aux-split logic
  // treats 突っ走 exactly like any kanji stem.
  // True once the stem has been extended over a dictionary-verified embedded verb;
  // the resulting compound (突っ走る) is itself absent from the dictionary, so this
  // flag lets the confidence-gate and cost logic below treat it as verified.
  bool sokuonbin_stem_verified = false;
  // Correct lemma for the extended compound: sokuon prefix + embedded verb base
  // (吹っ + 飛ぶ = 吹っ飛ぶ). Forced explicitly at emission because the hatsuonbin
  // ん of the full surface is onbin-ambiguous (吹っ飛ん could derive 吹っ飛む), and
  // the embedded analysis is the authoritative source of the base form.
  std::string sokuonbin_lemma;
  grammar::VerbType sokuonbin_verb_type = grammar::VerbType::Unknown;
  if (dict_manager != nullptr && kanji_end == start_pos + 1 && codepoints[kanji_end] == core::hiragana::kSmallTsu &&
      kanji_end + 1 < char_types.size() && char_types[kanji_end + 1] == normalize::CharType::Kanji) {
    size_t kanji2_end = vh::findCharRegionEnd(char_types, kanji_end + 1, 3, normalize::CharType::Kanji);
    // Skip hatsuonbin (ん) continuations: 吹っ飛んだ's ん is onbin-ambiguous (ぶ/む/ぬ)
    // and 漢っ漢+ん compounds are already resolved by the dedicated
    // sokuon_kanji_hatsuonbin suffix candidate with the correct base. The verbs this
    // probe is needed for (走る っ-onbin, 掻く い-onbin) never use ん-onbin.
    if (kanji2_end < char_types.size() && char_types[kanji2_end] == normalize::CharType::Hiragana &&
        codepoints[kanji2_end] != U'ん') {
      size_t probe_end = vh::findCharRegionEnd(char_types, kanji2_end, 12, normalize::CharType::Hiragana);
      std::string embedded = extractSubstring(codepoints, kanji_end + 1, probe_end);
      for (const auto& res : inflection.analyze(embedded)) {
        // Shuushikei (surface == base, 走る) or a multi-character conjugation suffix
        // (った/いた/ります/ろう…) is verbal evidence. A bare renyokei — a single
        // i-row suffix (取り, 盛り) — is nominal and must not pass; standalone
        // renyokei of true compounds is handled by the compound-join path instead.
        bool conjugation_evidence = (embedded == res.base_form) || normalize::utf8Length(res.suffix) >= 2;
        if (conjugation_evidence && vh::isVerbInDictionary(dict_manager, res.base_form)) {
          sokuonbin_lemma = extractSubstring(codepoints, start_pos, kanji_end + 1) + res.base_form;
          sokuonbin_verb_type = res.verb_type;
          kanji_end = kanji2_end;  // stem now spans 突っ走 / 引っ掻; downstream logic unchanged
          sokuonbin_stem_verified = true;
          break;
        }
      }
    }
  }

  // Check if first hiragana is a particle that can NEVER be part of a verb
  // E.g., "領収書を" - を is a particle, not part of a verb
  // Note about が and に:
  // - が can be part of verbs: 上がる, 下がる, 受かる, etc.
  // - が can be mizenkei: 泳がれる (泳ぐ → 泳が + れる)
  // Check if the hiragana after kanji is a particle (not a verb conjugation)
  // e.g., 金がない → 金 + が + ない, not 金ぐ
  // Note about か: excluded - can be part of verb conjugation (書かない, 動かす)
  char32_t first_hiragana = codepoints[kanji_end];
  if (normalize::isNeverVerbStemAfterKanji(first_hiragana)) {
    // Exception 1: A-row hiragana followed by れべき may be mizenkei pattern
    // e.g., 泳がれべき = 泳が (mizenkei) + れべき (passive + classical obligation)
    // Exception 2: A-row hiragana followed by れ is godan passive renyokei
    // e.g., 言われ = 言わ (mizenkei) + れ (passive renyokei of 言われる)
    // Exception 3: が followed by る is godan-ra verb pattern
    // e.g., 上がる, 下がる, 受かる - these are common godan-ra verbs
    // For patterns like 金がない, the が should remain NOUN + PARTICLE + ADJ
    bool is_verb_pattern = false;
    if (grammar::isARowCodepoint(first_hiragana)) {
      size_t next_pos = kanji_end + 1;
      if (next_pos < codepoints.size()) {
        char32_t next_char = codepoints[next_pos];
        if (next_char == U'れ') {
          // A-row + れ pattern: could be passive verb stem (言われ, 書かれ, etc.)
          is_verb_pattern = true;
        } else if (first_hiragana == U'が') {
          // が + る/ら/り/っ pattern: could be godan-ra verb (上がる, 下がる, 受かる)
          // Also handle conjugations: がら(mizenkei), がり(renyokei), がっ(onbin)
          // が + せ/さ/ず: godan-ga verb mizenkei patterns
          // E.g., 脱がせる, 脱がさない, 脱がず
          // が+な: only allow if kanji+ぐ is a known godan-ga verb
          // E.g., 脱がない (脱ぐ exists) vs 金がない (金ぐ doesn't exist)
          if (next_char == U'る' || next_char == U'ら' || next_char == U'り' || next_char == U'っ' ||
              next_char == U'れ' || next_char == U'せ' || next_char == U'さ' || next_char == U'ず') {
            is_verb_pattern = true;
          }
          // が+な: verify kanji+ぐ exists as godan-ga verb in dictionary
          if (!is_verb_pattern && next_char == U'な' && dict_manager != nullptr) {
            std::string kanji_stem = extractSubstring(codepoints, start_pos, kanji_end);
            std::string gu_form = kanji_stem + "ぐ";
            if (vh::isVerbInDictionary(dict_manager, gu_form)) {
              is_verb_pattern = true;
            }
          }
        }
      }
    }
    if (!is_verb_pattern) {
      return candidates;  // Not a verb - these particles follow nouns
    }
  }

  // Find hiragana portion (max 12 for conjugation + aux)
  // Note: We no longer break at particle-like characters here.
  // The inflection module will determine if the full string is a valid
  // conjugated verb. This allows patterns like "飲みながら" (nomi-nagara)
  // where "が" is part of the auxiliary "ながら", not a standalone particle.
  size_t hiragana_end = vh::findCharRegionEnd(char_types, kanji_end, 12, normalize::CharType::Hiragana);

  // Need at least some hiragana for a conjugated verb
  if (hiragana_end <= kanji_end) {
    return candidates;
  }

  // Penalize verb candidates that start in the middle of a kanji run when
  // the preceding kanji and the candidate's first kanji form an exact
  // dictionary word. E.g., in 作画崩壊した a verb candidate 壊し starting at
  // 壊 would split the dictionary word 崩壊 (prefer 作画崩壊+し+た).
  // This mirrors the SPLIT_NV boundary guard in split_candidates.cpp, which
  // cannot cover paths assembled from independent noun and verb candidates.
  float mid_compound_penalty = 0.0F;
  if (start_pos > 0 && dict_manager != nullptr && char_types[start_pos - 1] == normalize::CharType::Kanji) {
    std::string boundary_pair =
        normalize::encodeUtf8(codepoints[start_pos - 1]) + normalize::encodeUtf8(codepoints[start_pos]);
    if (dict_manager->lookupExact(boundary_pair) != nullptr) {
      mid_compound_penalty = bigram_cost::kMinor;
      SUZUME_DEBUG_LOG("[COST_ADJ] verb candidates at pos " << start_pos << " +" << mid_compound_penalty
                                                            << " (boundary pair \"" << boundary_pair
                                                            << "\" is dict word)\n");
    }
  }

  // Detect a kanji verb renyokei followed by the excessive auxiliary すぎ;
  // 書きすぎる is compositional 書き + すぎる, not a single lexical verb.
  // Pattern: kanji + (き/ぎ/し/ち/に/び/み/り/い) + すぎ...
  std::string hira_part = extractSubstring(codepoints, kanji_end, hiragana_end);
  // C++17 compatible: check if hiragana contains "すぎ" (6 bytes)
  bool is_sugi_pattern = (hira_part.find("すぎ") != std::string::npos);

  appendNiSugiPredicateCandidates(codepoints, start_pos, hiragana_end, inflection, dict_manager, candidates);
  appendNiLimitedIchidanCandidates(codepoints, start_pos, hiragana_end, inflection, candidates);

  // Generate verb renyokei candidates when followed by すぎ
  // E.g., 書きすぎた → 書き (renyokei of 書く) + すぎ + た (Godan)
  //       食べすぎた → 食べ (renyokei of 食べる) + すぎ + た (Ichidan)
  if (is_sugi_pattern && candidates.empty() && kanji_end < hiragana_end) {
    char32_t first_hira = codepoints[kanji_end];

    // Pattern 1: Godan verb renyokei (kanji + I-row hiragana + すぎ)
    // き→GodanKa, ぎ→GodanGa, し→GodanSa, ち→GodanTa, に→GodanNa,
    // び→GodanBa, み→GodanMa, り→GodanRa
    if (grammar::isIRowCodepoint(first_hira)) {
      // Verify this is followed by すぎ
      if (kanji_end + 1 < codepoints.size() && codepoints[kanji_end + 1] == U'す' &&
          kanji_end + 2 < codepoints.size() && codepoints[kanji_end + 2] == U'ぎ') {
        // Determine verb type from I-row ending
        grammar::VerbType verb_type = grammar::verbTypeFromIRowCodepoint(first_hira);
        if (verb_type != grammar::VerbType::Unknown) {
          // Get base suffix (e.g., き → く for GodanKa)
          std::string_view base_suffix = grammar::godanBaseSuffixFromIRow(first_hira);
          if (!base_suffix.empty()) {
            // Construct base form: kanji + base_suffix (e.g., 書 + く = 書く)
            std::string kanji_stem = extractSubstring(codepoints, start_pos, kanji_end);
            std::string base_form = kanji_stem + std::string(base_suffix);

            // Verify the base form is a valid verb
            bool is_valid_verb = vh::isVerifiedVerbBase(dict_manager, inflection, base_form,
                                                        candidate::verb_cost::kConstructedVerbMinConfidence, true);

            if (is_valid_verb) {
              size_t renyokei_end = kanji_end + 1;
              std::string surface = extractSubstring(codepoints, start_pos, renyokei_end);
              // Negative cost to beat compound NOUN path
              // Compound NOUNs like 書きすぎた get cost ~1.0, so we need much lower
              constexpr float kCost = candidate::verb_cost::kStrongBonus;
              SUZUME_DEBUG_VERBOSE_BLOCK {
                SUZUME_DEBUG_STREAM << "[VERB_CAND] " << surface << " godan_renyokei_sugi lemma=" << base_form
                                    << " cost=" << kCost << "\n";
              }
              candidates.push_back(makeVerbCandidate(
                  surface, start_pos, renyokei_end, kCost, base_form, grammar::verbTypeToConjType(verb_type), true,
                  CandidateOrigin::VerbKanji, 0.9F, "godan_renyokei_sugi", core::ExtendedPOS::VerbRenyokei));
            }
          }
        }
      }
    }

    // Pattern 2: Ichidan verb renyokei (kanji + E-row hiragana + すぎ)
    // E.g., 食べすぎた → 食べ (renyokei of 食べる) + すぎ + た
    //       見せすぎる → 見せ (renyokei of 見せる) + すぎる
    if (grammar::isERowCodepoint(first_hira)) {
      // Verify this is followed by すぎ
      if (kanji_end + 1 < codepoints.size() && codepoints[kanji_end + 1] == U'す' &&
          kanji_end + 2 < codepoints.size() && codepoints[kanji_end + 2] == U'ぎ') {
        // Construct base form: kanji + first_hira + る (e.g., 食 + べ + る = 食べる)
        std::string kanji_stem = extractSubstring(codepoints, start_pos, kanji_end);
        std::string ichidan_stem = kanji_stem + normalize::encodeUtf8(first_hira);
        std::string base_form = ichidan_stem + "る";

        // Verify the base form is a valid ichidan verb
        bool is_valid_verb = vh::isVerifiedVerbBase(dict_manager, inflection, base_form,
                                                    candidate::verb_cost::kConstructedVerbMinConfidence, false);

        if (is_valid_verb) {
          size_t renyokei_end = kanji_end + 1;
          std::string surface = extractSubstring(codepoints, start_pos, renyokei_end);
          // Negative cost to beat compound NOUN path
          constexpr float kCost = candidate::verb_cost::kStrongBonus;
          SUZUME_DEBUG_VERBOSE_BLOCK {
            SUZUME_DEBUG_STREAM << "[VERB_CAND] " << surface << " ichidan_renyokei_sugi lemma=" << base_form
                                << " cost=" << kCost << "\n";
          }
          candidates.push_back(makeVerbCandidate(surface, start_pos, renyokei_end, kCost, base_form,
                                                 dictionary::ConjugationType::Ichidan, true, CandidateOrigin::VerbKanji,
                                                 0.9F, "ichidan_renyokei_sugi", core::ExtendedPOS::VerbRenyokei));
        }
      }
    }

    // Early return to skip generating full verb forms containing すぎ
    // Prefer the grammatical renyokei + すぎ + auxiliary path.
    if (mid_compound_penalty != 0.0F) {
      for (auto& cand : candidates) {
        cand.cost += mid_compound_penalty;
      }
    }
    return candidates;
  }

  // Godan mizenkei pattern: single-kanji + A-row + れ/せ (passive/causative)
  appendGodanMizenkeiPassiveCausativeCandidates(codepoints, start_pos, kanji_end, hiragana_end, inflection,
                                                dict_manager, candidates);

  // Contracted sa-row mizenkei: kanji + しゃ + れ/せ/し
  appendSaRowContractedMizenkeiCandidates(codepoints, start_pos, kanji_end, hiragana_end, inflection, candidates);

  // Godan mizenkei pattern: kanji + A-row hiragana + ず (classical negative)
  appendGodanMizenkeiZuCandidates(codepoints, start_pos, kanji_end, hiragana_end, inflection, dict_manager, candidates);

  appendAnalyzedKanjiVerbCandidates(codepoints, start_pos, kanji_end, hiragana_end, inflection, dict_manager, verb_opts,
                                    sokuonbin_stem_verified, sokuonbin_lemma, candidates);

  // Try Ichidan renyokei pattern: kanji + e-row/i-row hiragana (+ shuushikei / multi-char stem)
  appendIchidanRenyokeiCandidates(codepoints, start_pos, kanji_end, hiragana_end, inflection, dict_manager, verb_opts,
                                  candidates);

  // Try Godan-Sa renyokei stem pattern: kanji + hiragana ending in し
  appendGodanSaRenyokeiCandidates(codepoints, start_pos, kanji_end, hiragana_end, inflection, dict_manager, verb_opts,
                                  candidates);

  // Try Ichidan verb kateikei (conditional) + volitional stem patterns
  appendIchidanKateikeiVolitionalCandidates(codepoints, start_pos, kanji_end, hiragana_end, inflection, dict_manager,
                                            candidates);

  // Try Godan passive renyokei pattern: kanji + a-row + れ
  appendGodanPassiveRenyokeiCandidates(codepoints, start_pos, kanji_end, hiragana_end, inflection, dict_manager,
                                       verb_opts, candidates);

  // NOTE: Ichidan passive forms (食べられる, 見られる) should split MeCab-style:
  //   食べられる → 食べ + られる (stem + passive auxiliary)
  //   見られる → 見 + られる
  // The ichidan stem candidates are generated in the section below
  // and the られる auxiliary is matched from entries.cpp.
  // We do NOT generate single-token passive candidates here to ensure split wins.

  // Generate Ichidan stem candidates for passive/potential auxiliary patterns (られ+X)
  appendIchidanStemRareCandidates(codepoints, start_pos, kanji_end, hiragana_end, inflection, dict_manager, candidates);

  // Generate single-kanji Ichidan verb candidates for auxiliary patterns
  appendSingleKanjiIchidanCandidates(codepoints, start_pos, kanji_end, hiragana_end, dict_manager, candidates);

  appendKanjiMizenkeiStemCandidates(codepoints, start_pos, kanji_end, hiragana_end, inflection, dict_manager,
                                    candidates);
  appendKanjiOnbinCandidates(codepoints, start_pos, kanji_end, hiragana_end, inflection, dict_manager,
                             sokuonbin_stem_verified, sokuonbin_lemma, sokuonbin_verb_type, candidates);
  appendVerifiedTailGodanTaCompoundCandidates(codepoints, start_pos, kanji_end, dict_manager, candidates);

  // Add emphatic variants (来た → 来たっ, etc.)
  vh::addEmphaticVariants(candidates, codepoints);

  // Do not let an unverified inflection hypothesis replace an exact
  // dictionary function word or deverbal noun.  Kana-final dictionary forms
  // such as 概ね, 答え and 同じ otherwise invite fabricated ichidan/godan
  // lemmas solely because their final kana resembles a renyokei marker.
  // Verified lexical verb forms remain available for genuine homographs.
  candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                  [&](const UnknownCandidate& cand) {
                                    return cand.pos == core::PartOfSpeech::Verb && !cand.lemma_verified &&
                                           ((cand.extended_pos == core::ExtendedPOS::VerbRenyokei &&
                                             grammar::endsWithRenyokeiMarker(cand.surface)) ||
                                            (cand.extended_pos == core::ExtendedPOS::VerbShuushikei &&
                                             cand.surface.compare(cand.lemma) == 0)) &&
                                           vh::hasNonVerbDictionaryEntry(dict_manager, cand.surface);
                                  }),
                   candidates.end());

  // Apply mid-kanji-run dictionary compound penalty (see comment above)
  if (mid_compound_penalty != 0.0F) {
    for (auto& cand : candidates) {
      cand.cost += mid_compound_penalty;
    }
  }

  // Sort by cost and return best candidates
  vh::sortCandidatesByCost(candidates);

  return candidates;
}

}  // namespace suzume::analysis
