/**
 * @file verb_candidates_kanji_onbin.cpp
 * @brief Kanji verb onbin candidate patterns
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

// The 促音便 base is lexically ラ/ワ/タ-ambiguous from the っ surface alone, so
// default to ラ行 (閉まる/走る) but prefer a dictionary-verified ワ行 base (向かう
// over the non-word 向かる) when the dictionary carries it. Shared by the
// trailing-っ (extended_sokuonbin) and mid-surface (te_aux_sokuonbin) paths.
struct SokuonbinBase {
  std::string base;
  grammar::VerbType type;
};
SokuonbinBase resolveSokuonbinBase(const dictionary::DictionaryManager* dict_manager, const std::string& stem) {
  std::string godan_wa_base = stem + "う";
  if (vh::isVerbInDictionary(dict_manager, godan_wa_base)) {
    return {godan_wa_base, grammar::VerbType::GodanWa};
  }
  return {stem + "る", grammar::VerbType::GodanRa};
}

bool isGodanTerminalEnding(char32_t codepoint) {
  for (const auto& [verb_type, row] : grammar::Conjugation::getGodanRows()) {
    static_cast<void>(verb_type);
    if (row.base_vowel == codepoint) {
      return true;
    }
  }
  return false;
}

bool hasStandaloneVerbTail(const dictionary::DictionaryManager* dict_manager, const std::vector<char32_t>& codepoints,
                           size_t tail_start, size_t tail_end) {
  if (dict_manager == nullptr || tail_start >= tail_end) {
    return false;
  }
  return vh::isVerbInDictionary(dict_manager, extractSubstring(codepoints, tail_start, tail_end));
}

void appendVerifiedTailGodanTaCompoundCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                                 size_t kanji_end, const dictionary::DictionaryManager* dict_manager,
                                                 std::vector<UnknownCandidate>& candidates) {
  // A non-nominal kanji prefix can productively compound with a known one-kanji
  // GodanTa verb (先立つ, 先立ち, 先立って). The verified tail fixes the conjugation
  // class, while rejecting a dictionary noun prefix preserves object+verb paths
  // such as 本|立つ.
  if (dict_manager == nullptr || kanji_end != start_pos + 2 || kanji_end >= codepoints.size()) {
    return;
  }
  std::string prefix = extractSubstring(codepoints, start_pos, start_pos + 1);
  if (vh::isNounInDictionary(dict_manager, prefix)) {
    return;
  }
  std::string stem = extractSubstring(codepoints, start_pos, kanji_end);
  std::string tail_base = extractSubstring(codepoints, kanji_end - 1, kanji_end) + "つ";
  if (!vh::isVerbInDictionary(dict_manager, tail_base)) {
    return;
  }

  char32_t ending = codepoints[kanji_end];
  size_t end_pos = kanji_end;
  core::ExtendedPOS extended_pos = core::ExtendedPOS::Unknown;
  const char* pattern = nullptr;
  if (ending == U'つ') {
    end_pos = kanji_end + 1;
    extended_pos = core::ExtendedPOS::VerbShuushikei;
    pattern = "tail_godan_ta_shuushikei";
  } else if (ending == U'ち') {
    end_pos = kanji_end + 1;
    extended_pos = core::ExtendedPOS::VerbRenyokei;
    pattern = "tail_godan_ta_renyokei";
  } else if (ending == U'っ' && kanji_end + 1 < codepoints.size() &&
             (codepoints[kanji_end + 1] == U'て' || codepoints[kanji_end + 1] == U'た')) {
    end_pos = kanji_end + 1;
    extended_pos = core::ExtendedPOS::VerbOnbinkei;
    pattern = "tail_godan_ta_sokuonbin";
  }
  if (pattern == nullptr) {
    return;
  }
  std::string surface = extractSubstring(codepoints, start_pos, end_pos);
  std::string base_form = stem + "つ";
  candidates.push_back(makeVerbCandidate(surface, start_pos, end_pos, candidate::kVerifiedTailCompoundVerbBonus,
                                         base_form, grammar::verbTypeToConjType(grammar::VerbType::GodanTa), true,
                                         CandidateOrigin::VerbKanji, candidate::kHighOriginConfidence, pattern,
                                         extended_pos));
}

// Fallback verification for a 促音便 base when it is not in the dictionary:
// only for single-char stems, accept a GodanRa inflection analysis of
// onbin_surface + た with sufficient confidence.
bool sokuonbinInflVerified(const grammar::Inflection& inflection, const std::string& onbin_surface,
                           const std::string& potential_base, size_t hiragana_before_onbin) {
  if (hiragana_before_onbin != 1) {
    return false;
  }
  for (const auto& result : inflection.analyze(onbin_surface + "た")) {
    if (result.verb_type == grammar::VerbType::GodanRa && result.base_form == potential_base &&
        result.confidence >= candidate::verb_cost::kKanjiSokuonbinMinConfidence) {
      return true;
    }
  }
  return false;
}

// Inflection-analysis fallback for a kanji 音便 stem whose base was not found in
// the dictionary: pick the highest-confidence (≥0.5) inflection of full_surface
// whose base_form/type matches one of the onbin candidate types. Shared by the
// い/ん extended-onbin and the て/だ hatsuonbin paths.
struct OnbinInflMatch {
  grammar::VerbType type{grammar::VerbType::Unknown};
  std::string base_form;
};
OnbinInflMatch bestOnbinInflMatch(const grammar::Inflection& inflection, const std::string& full_surface,
                                  const std::string& kanji_stem, grammar::GodanOnbinRange onbin_types) {
  OnbinInflMatch match;
  float best_conf = 0.0F;
  for (const auto& result : inflection.analyze(full_surface)) {
    if (result.confidence >= 0.5F && result.confidence > best_conf) {
      for (const auto& [verb_type, base_suffix] : onbin_types) {
        std::string base_form = kanji_stem + std::string(base_suffix);
        if (result.base_form == base_form && result.verb_type == verb_type) {
          match.type = verb_type;
          match.base_form = base_form;
          best_conf = result.confidence;
          break;
        }
      }
    }
  }
  return match;
}

void appendKanjiOnbinCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t kanji_end,
                                size_t hiragana_end, const grammar::Inflection& inflection,
                                const dictionary::DictionaryManager* dict_manager, bool sokuonbin_stem_verified,
                                const std::string& sokuonbin_lemma, grammar::VerbType sokuonbin_verb_type,
                                std::vector<UnknownCandidate>& candidates) {
  // Generate Godan onbin stem candidates for contraction auxiliary patterns
  // E.g., 読んでる → 読ん (onbin of 読む) + でる (ている contraction)
  //       書いとく → 書い (onbin of 書く) + とく (ておく contraction)
  // Key patterns:
  // - kanji + ん + (ど/じ/で): GodanMa/GodanBa/GodanNa verbs (読んでる, 飛んどく)
  // - kanji + い + (と/ち): GodanKa/GodanGa verbs (書いとく, 泳いちゃう)
  if (kanji_end < hiragana_end) {
    char32_t first_hira = codepoints[kanji_end];
    // Check for hatsuonbin (ん) or ikuon (い) patterns
    bool is_hatsuonbin = (first_hira == U'ん');
    bool is_ikuon = (first_hira == U'い');
    if ((is_hatsuonbin || is_ikuon) && kanji_end + 1 < hiragana_end) {
      char32_t next_char = codepoints[kanji_end + 1];
      bool is_contraction_pattern = false;
      if (is_hatsuonbin) {
        // ん + ど (どく/どいた) or じ (じゃう/じゃった) or で (でる/でた/でて)
        is_contraction_pattern = (next_char == U'ど' || next_char == U'じ' || next_char == U'で');
      } else {
        // い + と (とく/といた) or ち (ちゃう/ちゃった)
        is_contraction_pattern = (next_char == U'と' || next_char == U'ち');
      }
      if (is_contraction_pattern) {
        // Determine candidate verb types based on onbin type
        // Uses centralized GodanRow data instead of manual enumeration
        std::string_view onbin_str = is_hatsuonbin ? "ん" : "い";
        const auto& candidates_to_try = vh::getGodanTypesByOnbin(onbin_str);
        // Get the kanji stem
        std::string kanji_stem = extractSubstring(codepoints, start_pos, kanji_end);
        // First, check dictionary for ALL verb types before falling back to inflection
        // This ensures dictionary-verified verbs take precedence
        // Phase 1: Dictionary check
        auto onbin_match = vh::firstGodanOnbinDictBase(dict_manager, kanji_stem, onbin_str);
        grammar::VerbType matched_verb_type = onbin_match.verb_type;
        std::string matched_base_form = std::move(onbin_match.base_form);
        // Inflection analysis fallback (dictionary lookup above found nothing)
        if (matched_verb_type == grammar::VerbType::Unknown && kanji_end > start_pos) {
          std::string full_surface = extractSubstring(codepoints, start_pos, hiragana_end);
          OnbinInflMatch infl = bestOnbinInflMatch(inflection, full_surface, kanji_stem, candidates_to_try);
          if (infl.type != grammar::VerbType::Unknown) {
            matched_verb_type = infl.type;
            matched_base_form = std::move(infl.base_form);
          }
        }
        if (matched_verb_type == grammar::VerbType::Unknown) {
          // No valid verb found
        } else {
          // Found valid verb - generate onbin stem candidate
          std::string onbin_surface = extractSubstring(codepoints, start_pos, kanji_end + 1);
          constexpr float kOnbinCost = candidate::verb_cost::kStandardBonus;
          SUZUME_DEBUG_VERBOSE_BLOCK {
            SUZUME_DEBUG_STREAM << "[VERB_CAND] " << onbin_surface
                                << " kanji_onbin_contraction lemma=" << matched_base_form << " cost=" << kOnbinCost
                                << "\n";
          }
          const char* pattern = is_hatsuonbin ? "kanji_hatsuonbin" : "kanji_ikuon";
          candidates.push_back(makeVerbCandidate(onbin_surface, start_pos, kanji_end + 1, kOnbinCost, matched_base_form,
                                                 grammar::verbTypeToConjType(matched_verb_type), true,
                                                 CandidateOrigin::VerbKanji, 0.9F, pattern));
        }
      }
    }
  }

  // Generate Godan sokuonbin (っ) candidates for basic te/ta-form splitting
  // E.g., 言って → 言っ (onbin of 言う) + て (particle)
  //       言った → 言っ (onbin of 言う) + た (auxiliary)
  //       待って → 待っ (onbin of 待つ) + て (particle)
  //       買って → 買っ (onbin of 買う) + て (particle)
  // Key patterns:
  // - kanji + っ + て/た/たら/たり: GodanRa/GodanTa/GodanWa verbs
  if (kanji_end < hiragana_end) {
    char32_t first_hira = codepoints[kanji_end];
    // Check for sokuonbin (っ) pattern
    if (first_hira == U'っ' && kanji_end + 1 < hiragana_end) {
      char32_t next_char = codepoints[kanji_end + 1];
      // Basic te/ta form patterns (て, た, たら, たり), ちゃう (ち), and とく (と) contractions
      bool is_te_ta_pattern = (next_char == U'て' || next_char == U'た' || next_char == U'ち' || next_char == U'と');
      if (is_te_ta_pattern) {
        const auto& sokuonbin_types = vh::getGodanTypesByOnbin("っ");
        // Get the kanji stem
        std::string kanji_stem = extractSubstring(codepoints, start_pos, kanji_end);

#ifdef SUZUME_DEBUG
        // TRACE: Collect all candidates for logging (debug builds only)
        std::string onbin_surface_for_log = extractSubstring(codepoints, start_pos, kanji_end + 1);
        struct SokuonbinCandidate {
          grammar::VerbType type;
          std::string base_form;
          bool dict_match;
        };
        std::vector<SokuonbinCandidate> all_sokuonbin_candidates;
#endif

        // First, check dictionary for ALL verb types
        grammar::VerbType matched_verb_type = grammar::VerbType::Unknown;
        std::string matched_base_form;
        bool matched_via_dict = false;
        for (const auto& [verb_type, base_suffix] : sokuonbin_types) {
          std::string base_form = kanji_stem + std::string(base_suffix);
          bool dict_match = vh::isVerbInDictionary(dict_manager, base_form);
#ifdef SUZUME_DEBUG
          all_sokuonbin_candidates.push_back({verb_type, base_form, dict_match});
#endif
          if (dict_match && matched_verb_type == grammar::VerbType::Unknown) {
            matched_verb_type = verb_type;
            matched_base_form = base_form;
            matched_via_dict = true;
          }
        }
        // Sokuonbin compound (突っ走る) whose stem was verified via its embedded verb:
        // the compound itself is absent from the dictionary, so emit the onbin stem
        // (突っ走っ) here with the embedded verb's type/base so た/て split off exactly
        // like a plain verb (走った → 走っ + た), rather than the whole form winning.
        if (matched_verb_type == grammar::VerbType::Unknown && sokuonbin_stem_verified &&
            sokuonbin_verb_type != grammar::VerbType::Unknown && !sokuonbin_lemma.empty()) {
          matched_verb_type = sokuonbin_verb_type;
          matched_base_form = sokuonbin_lemma;
          matched_via_dict = true;
        }
        // Phase 2: Inflection analysis fallback
        // Try progressively shorter surfaces to handle cases where hiragana_end
        // includes particles (e.g., "使っているが" vs "使っている")
        // Skip if kanji stem starts with a dictionary noun (e.g., 昨日買っ → skip)
        // This prevents false compound verb candidates like "昨日買う"
        bool starts_with_dict_noun = false;
        bool remainder_is_dict_verb = false;
        if (dict_manager != nullptr && kanji_end - start_pos >= 2) {
          // Check if any prefix of kanji_stem is a dictionary noun
          for (size_t prefix_len = 1; prefix_len < kanji_end - start_pos; ++prefix_len) {
            std::string prefix = extractSubstring(codepoints, start_pos, start_pos + prefix_len);
            if (verb_helpers::isNounInDictionary(dict_manager, prefix)) {
              starts_with_dict_noun = true;
              SUZUME_DEBUG_LOG_VERBOSE("[VERB_SKIP] \"" << kanji_stem << "\" starts with dict noun \"" << prefix
                                                        << "\"\n");
              break;
            }
          }
          // Also check: if removing single-kanji prefix leaves a valid dict verb
          // E.g., 本買う → 本 + 買う, where 買う is a dict verb
          // This handles patterns like 本買った, 服買った, 車買った
          if (!starts_with_dict_noun && kanji_end - start_pos == 2) {
            // Get the second kanji + verb ending
            std::string remainder_stem = extractSubstring(codepoints, start_pos + 1, kanji_end);
            for (const auto& [verb_type, base_suffix] : sokuonbin_types) {
              std::string remainder_base = remainder_stem + std::string(base_suffix);
              if (vh::isVerbInDictionary(dict_manager, remainder_base)) {
                remainder_is_dict_verb = true;
                SUZUME_DEBUG_LOG_VERBOSE("[VERB_SKIP] \"" << kanji_stem << "\" remainder \"" << remainder_base
                                                          << "\" is dict verb\n");
                break;
              }
            }
          }
        }
        if (matched_verb_type == grammar::VerbType::Unknown && !starts_with_dict_noun && !remainder_is_dict_verb) {
          if (kanji_stem.size() == core::kJapaneseCharBytes) {
            // Single-kanji stem: use inflection analysis of the longer surface
            // (kanji + っ + following chars) to find verb type.
            // Common verbs like 残る, 立つ, 打つ may not be in L2 dictionary.
            // Try surfaces of increasing length to get inflection result.
            for (size_t try_end = kanji_end + 2; try_end <= codepoints.size() && try_end <= kanji_end + 4; ++try_end) {
              std::string try_surface = extractSubstring(codepoints, start_pos, try_end);
              auto infl_result = inflection.analyze(try_surface);
              if (!infl_result.empty()) {
                const auto& best = infl_result[0];
                if (best.confidence >= 0.6F) {
                  for (const auto& [verb_type, base_suffix] : sokuonbin_types) {
                    if (best.verb_type == verb_type) {
                      matched_verb_type = verb_type;
                      matched_base_form = kanji_stem + std::string(base_suffix);
                      SUZUME_DEBUG_LOG_VERBOSE("[VERB_CAND] \"" << kanji_stem << "\" single-kanji sokuonbin → "
                                                                << matched_base_form
                                                                << " (infl, conf=" << best.confidence << ")\n");
                      break;
                    }
                  }
                  if (matched_verb_type != grammar::VerbType::Unknown)
                    break;
                }
              }
            }
          } else {
            SUZUME_DEBUG_LOG_VERBOSE("[VERB_SKIP] \"" << kanji_stem << "\" skip non-dict sokuonbin\n");
          }
        }

#ifdef SUZUME_DEBUG
        // TRACE: Log all sokuonbin candidates
        SUZUME_DEBUG_TRACE_BLOCK {
          SUZUME_DEBUG_STREAM << "[SOKUONBIN_CANDIDATES] \"" << onbin_surface_for_log << "\":\n";
          constexpr float kSokuonbinCost = candidate::verb_cost::kStandardBonus;
          for (const auto& cand : all_sokuonbin_candidates) {
            bool is_selected = (cand.type == matched_verb_type);
            SUZUME_DEBUG_STREAM << "  - " << cand.base_form << " (" << grammar::verbTypeToString(cand.type) << "): "
                                << "dict_match=" << (cand.dict_match ? "YES" : "NO")
                                << ", score=" << (cand.dict_match ? kSokuonbinCost : 0.0F)
                                << (is_selected ? "" : " (skipped)") << "\n";
          }
          if (matched_verb_type != grammar::VerbType::Unknown) {
            SUZUME_DEBUG_STREAM << "  → Selected: " << matched_base_form << " ("
                                << grammar::verbTypeToString(matched_verb_type) << ")\n";
          } else {
            SUZUME_DEBUG_STREAM << "  → No match found\n";
          }
        }
#endif

        if (matched_verb_type != grammar::VerbType::Unknown) {
          // Found valid verb - generate sokuonbin stem candidate
          // Dict-matched verbs get bonus (-0.5) to beat unsplit forms
          // Inflection-only matches get neutral cost (0) to avoid false positives
          // like 像っ (from 像る which is not a real verb)
          std::string onbin_surface = extractSubstring(codepoints, start_pos, kanji_end + 1);
          // Dict-matched verbs get bonus (-0.5) to beat unsplit forms
          // Inflection-only matches (2-kanji stems only) get neutral cost
          const float sokuonbin_cost = matched_via_dict ? candidate::verb_cost::kStandardBonus : bigram_cost::kNeutral;
          SUZUME_DEBUG_VERBOSE_BLOCK {
            SUZUME_DEBUG_STREAM << "[VERB_CAND] " << onbin_surface << " kanji_sokuonbin lemma=" << matched_base_form
                                << " cost=" << sokuonbin_cost << (matched_via_dict ? " (dict)" : " (infl)") << "\n";
          }
          auto candidate =
              makeVerbCandidate(onbin_surface, start_pos, kanji_end + 1, sokuonbin_cost, matched_base_form,
                                grammar::verbTypeToConjType(matched_verb_type), true, CandidateOrigin::VerbKanji, 0.9F,
                                "kanji_sokuonbin", core::ExtendedPOS::VerbOnbinkei);
          // The non-dictionary fallback reaches here only for a one-kanji
          // stem whose complete sokuonbin form was validated by inflection.
          // Preserve that evidence, as the extended and te-auxiliary paths do,
          // so an ordinary verb does not lose to a fabricated noun boundary.
          candidate.lemma_verified = matched_via_dict || kanji_end == start_pos + 1;
          candidates.push_back(std::move(candidate));
        }
      }
    }
  }

  // Generate Godan sokuonbin (っ) candidates for extended patterns
  // E.g., 閉まった → 閉まっ (onbin of 閉まる) + た (auxiliary)
  //       始まった → 始まっ (onbin of 始まる) + た (auxiliary)
  //       決まった → 決まっ (onbin of 決まる) + た (auxiliary)
  // Key patterns:
  // - kanji + hiragana + っ + た/て: GodanRa verbs with multi-char stem
  // Constraints:
  // - Hiragana portion (before っ) should be 1-2 chars (まった, まりった)
  // - Base form must exist in dictionary (prevents false positives)
  // - Require at least kanji + 2 hiragana (e.g., 閉まっ = 閉 + ま + っ)
  if (hiragana_end - kanji_end >= 2) {
    // Check for pattern ending in っ + た/て
    // Look for っ at position hiragana_end - 2 (second to last)
    char32_t second_last = codepoints[hiragana_end - 2];
    char32_t last_char = codepoints[hiragana_end - 1];
    bool is_sokuonbin_te_ta = (second_last == U'っ' && (last_char == U'た' || last_char == U'て'));
    // Hiragana between kanji and っ should be 1-2 chars
    // hiragana_end - kanji_end = total hiragana chars (including っ and た/て)
    // So hiragana before っ = (hiragana_end - kanji_end) - 2
    size_t hiragana_before_onbin = (hiragana_end - kanji_end) - 2;
    bool reasonable_length = (hiragana_before_onbin >= 1 && hiragana_before_onbin <= 2);
    if (is_sokuonbin_te_ta && reasonable_length && hiragana_end - kanji_end >= 3) {
      // We have kanji + 1-2 hiragana + っ + た/て
      // Generate candidate for kanji + hiragana + っ (without the た/て)
      size_t onbin_end = hiragana_end - 1;  // Position after っ
      std::string onbin_surface = extractSubstring(codepoints, start_pos, onbin_end);

      // Skip if hiragana portion is なかっ (negative past pattern: なかっ+た)
      // This prevents false positives like 来なかった → 来なかっ+た (来なかる doesn't exist)
      // The correct split is 来 + なかっ + た (kuru + negative aux + past)
      std::string hiragana_part = extractSubstring(codepoints, kanji_end, onbin_end);
      if (hiragana_part == "なかっ") {
        // This is negative past, not extended sokuonbin - skip
      } else if (hiragana_part == "であっ") {
        // This is copula である pattern (重要であった = 重要 + で + あっ + た)
        // Skip candidate generation to allow proper copula splitting
      } else if (utf8::startsWith(hiragana_part, "といっ")) {
        // Skip と+いっ pattern - this is particle と + verb いう
        // E.g., 友人といった = 友人 + と + いっ + た, not 友人といる
      } else if (hiragana_part == "くなっ") {
        // Skip く+なっ pattern - this is i-adjective adverbial + なる verb
        // E.g., 良くなった = 良く + なっ + た, not 良くなる as single verb
        // MeCab splits: 高くなった → 高く + なっ + た
      } else {
        // Skip godan終止形 + っ + て pattern - this is verb + って(quotative)
        // E.g., 行くって = 行く + って, 食べるって = 食べる + って
        // Godan 終止形 endings: く, す, つ, う, ぐ, ぶ, む, ぬ, る
        // The sokuonbin of godan verbs drops the ending (行く→行っ), not adds っ (行くっ is invalid)
        // Check if hiragana_part ends with godan終止形 + っ
        bool is_quotative_pattern = false;
        if (last_char == U'て' && hiragana_part.size() >= 6 /* at least 2 chars: Xっ */) {
          // Get the character before っ (second to last in hiragana_part)
          // hiragana_part ends with っ (which is at onbin_end - 1)
          // The char before っ is at position onbin_end - 2
          char32_t char_before_sokuon = codepoints[onbin_end - 2];
          is_quotative_pattern =
              (char_before_sokuon == U'く' || char_before_sokuon == U'す' || char_before_sokuon == U'つ' ||
               char_before_sokuon == U'う' || char_before_sokuon == U'ぐ' || char_before_sokuon == U'ぶ' ||
               char_before_sokuon == U'む' || char_before_sokuon == U'ぬ' || char_before_sokuon == U'る');
        }
        if (is_quotative_pattern) {
          // Skip: this is likely quotative って, not extended sokuonbin
        } else {
          // Build potential base form and verify it exists in dictionary or inflection
          // This prevents false positives like 食べてしまる
          std::string stem = extractSubstring(codepoints, start_pos, onbin_end - 1);
          const SokuonbinBase sokuon = resolveSokuonbinBase(dict_manager, stem);
          const std::string& potential_base = sokuon.base;
          const grammar::VerbType onbin_verb_type = sokuon.type;

          // Skip if hiragana before っ is だ (copula pattern)
          // E.g., 本だった = 本 + だっ + た (noun + copula), not 本だる (verb)
          // But 閉まった = 閉まっ + た (verb 閉まる) is valid
          char32_t char_before_sokuon = codepoints[hiragana_end - 3];
          if (char_before_sokuon == U'だ') {
            // This is a copula pattern (NOUN + だった), not a verb
            // Skip candidate generation
          } else {
            // Check dictionary first
            bool in_dict = vh::isVerbInDictionary(dict_manager, potential_base);

            // Fallback: inflection analysis for common patterns like 閉まる
            // (single-char stems only; longer となっ may be noun+particle+verb).
            bool infl_verified =
                !in_dict && sokuonbinInflVerified(inflection, onbin_surface, potential_base, hiragana_before_onbin);
            const bool standalone_verb_tail = hasStandaloneVerbTail(dict_manager, codepoints, kanji_end, onbin_end);

            // Skip if this is an i-adjective katt-form (美しかっ → 美しい, 高かっ → 高い)
            // The stem ends with か, so remove か and add い to get adjective base form
            // E.g., stem="美しか" → adj_base="美しい"
            bool is_adj_katt_form = false;
            if (stem.size() >= core::kTwoJapaneseCharBytes && utf8::endsWith(stem, "か")) {
              std::string adj_base = stem.substr(0, stem.size() - core::kJapaneseCharBytes) + "い";
              if (vh::isAdjectiveInDictionary(dict_manager, adj_base)) {
                is_adj_katt_form = true;
                SUZUME_DEBUG_LOG_VERBOSE("[VERB_CAND] " << onbin_surface << " skip: i-adj \"" << adj_base
                                                        << "\" in dict\n");
              }
            }

            if (!is_adj_katt_form && (in_dict || infl_verified)) {
              // Verified - generate candidate
              constexpr float kExtendedSokuonbinCost = candidate::verb_cost::kModerateBonus;
              SUZUME_DEBUG_VERBOSE_BLOCK {
                SUZUME_DEBUG_STREAM << "[VERB_CAND] " << onbin_surface << " extended_sokuonbin lemma=" << potential_base
                                    << (in_dict ? " [dict]" : " [infl]") << " cost=" << kExtendedSokuonbinCost << "\n";
              }
              auto candidate =
                  makeVerbCandidate(onbin_surface, start_pos, onbin_end, kExtendedSokuonbinCost, potential_base,
                                    grammar::verbTypeToConjType(onbin_verb_type), true, CandidateOrigin::VerbKanji,
                                    0.9F, "extended_sokuonbin", core::ExtendedPOS::VerbOnbinkei);
              // A standalone dictionary verb tail supplies a grammatical
              // boundary, so an inflection-only compound must remain
              // unverified and receive the generic false-positive penalty.
              candidate.lemma_verified =
                  in_dict || (infl_verified && kanji_end == start_pos + 1 && !standalone_verb_tail);
              candidates.push_back(std::move(candidate));
            }
          }  // end else (not copula だ pattern)
        }  // end else (not quotative って pattern)
      }  // end else (not なかっ pattern)
    }
  }

  // Generate sokuonbin (っ) candidates from surfaces containing て+auxiliary chains
  // E.g., 挙がっている → 挙がっ (onbin of 挙がる) + て + いる
  //       集まってくる → 集まっ (onbin of 集まる) + て + くる
  // This handles patterns where っ+て/で is followed by auxiliary verbs,
  // which the basic/extended sokuonbin sections miss (they only handle endings)
  if (hiragana_end - kanji_end >= 3) {
    // Scan for っ in hiragana portion (not at the very end - that's handled above)
    for (size_t pos = kanji_end; pos + 2 < hiragana_end; ++pos) {
      if (codepoints[pos] != U'っ')
        continue;
      char32_t after_sokuon = codepoints[pos + 1];
      if (after_sokuon != U'て' && after_sokuon != U'で')
        continue;
      // Found っ+て/で NOT at end of surface - check if followed by auxiliary
      size_t hiragana_before_onbin = pos - kanji_end;
      if (hiragana_before_onbin < 1 || hiragana_before_onbin > 2)
        continue;

      size_t onbin_end = pos + 1;  // Position after っ
      std::string onbin_surface = extractSubstring(codepoints, start_pos, onbin_end);
      std::string stem = extractSubstring(codepoints, start_pos, pos);
      const SokuonbinBase sokuon = resolveSokuonbinBase(dict_manager, stem);
      const std::string& potential_base = sokuon.base;
      const grammar::VerbType onbin_verb_type = sokuon.type;

      // Check hiragana part for known false patterns
      std::string hiragana_part = extractSubstring(codepoints, kanji_end, onbin_end);
      if (hiragana_part == "なかっ" || hiragana_part == "であっ" || utf8::startsWith(hiragana_part, "といっ") ||
          hiragana_part == "くなっ") {
        continue;
      }

      // A terminal predicate followed by って is a colloquial quotation
      // (読む+っていう), rather than a te-form that continues into an auxiliary.
      if (after_sokuon == U'て' && isGodanTerminalEnding(codepoints[pos - 1])) {
        continue;
      }

      bool in_dict_check = vh::isVerbInDictionary(dict_manager, potential_base);
      bool infl_verified =
          !in_dict_check && sokuonbinInflVerified(inflection, onbin_surface, potential_base, hiragana_before_onbin);
      const bool standalone_verb_tail = hasStandaloneVerbTail(dict_manager, codepoints, kanji_end, onbin_end);

      if (in_dict_check || infl_verified) {
        constexpr float kTeAuxSokuonbinCost = candidate::verb_cost::kModerateBonus;
        SUZUME_DEBUG_VERBOSE_BLOCK {
          SUZUME_DEBUG_STREAM << "[VERB_CAND] " << onbin_surface << " te_aux_sokuonbin lemma=" << potential_base
                              << (in_dict_check ? " [dict]" : " [infl]") << " cost=" << kTeAuxSokuonbinCost << "\n";
        }
        auto candidate =
            makeVerbCandidate(onbin_surface, start_pos, onbin_end, kTeAuxSokuonbinCost, potential_base,
                              grammar::verbTypeToConjType(onbin_verb_type), true, CandidateOrigin::VerbKanji, 0.9F,
                              "te_aux_sokuonbin", core::ExtendedPOS::VerbOnbinkei);
        candidate.lemma_verified =
            in_dict_check || (infl_verified && kanji_end == start_pos + 1 && !standalone_verb_tail);
        candidates.push_back(std::move(candidate));
      }
      break;  // Only process first っ+て/で occurrence
    }
  }

  // Generate Godan hatsuonbin (ん) candidates for basic te/ta-form splitting
  // E.g., 読んだ → 読ん (onbin of 読む) + だ (auxiliary)
  //       読んで → 読ん (onbin of 読む) + で (particle)
  //       飛んだ → 飛ん (onbin of 飛ぶ) + だ (auxiliary)
  //       死んだ → 死ん (onbin of 死ぬ) + だ (auxiliary)
  // Key patterns:
  // - kanji + ん + で/だ: GodanMa/GodanBa/GodanNa verbs
  if (kanji_end < hiragana_end) {
    char32_t first_hira = codepoints[kanji_end];
    // Check for hatsuonbin (ん) pattern
    if (first_hira == U'ん' && kanji_end + 1 < hiragana_end) {
      char32_t next_char = codepoints[kanji_end + 1];
      // Basic te/ta form patterns (で, だ)
      bool is_de_da_pattern = (next_char == U'で' || next_char == U'だ');
      if (is_de_da_pattern) {
        const auto& hatsuonbin_types = vh::getGodanTypesByOnbin("ん");
        // Get the kanji stem
        std::string kanji_stem = extractSubstring(codepoints, start_pos, kanji_end);

        // First, check dictionary for ALL verb types
        auto hatsuonbin_match = vh::firstGodanOnbinDictBase(dict_manager, kanji_stem, "ん");
        grammar::VerbType matched_verb_type = hatsuonbin_match.verb_type;
        std::string matched_base_form = std::move(hatsuonbin_match.base_form);
        // Inflection analysis fallback (dictionary lookup above found nothing)
        if (matched_verb_type == grammar::VerbType::Unknown) {
          std::string full_surface = extractSubstring(codepoints, start_pos, hiragana_end);
          OnbinInflMatch infl = bestOnbinInflMatch(inflection, full_surface, kanji_stem, hatsuonbin_types);
          if (infl.type != grammar::VerbType::Unknown) {
            matched_verb_type = infl.type;
            matched_base_form = std::move(infl.base_form);
          }
        }

        if (matched_verb_type != grammar::VerbType::Unknown) {
          // Found valid verb - generate hatsuonbin stem candidate
          std::string onbin_surface = extractSubstring(codepoints, start_pos, kanji_end + 1);
          constexpr float kHatsuonbinCost = candidate::verb_cost::kStandardBonus;
          SUZUME_DEBUG_VERBOSE_BLOCK {
            SUZUME_DEBUG_STREAM << "[VERB_CAND] " << onbin_surface << " kanji_hatsuonbin lemma=" << matched_base_form
                                << " cost=" << kHatsuonbinCost << "\n";
          }
          candidates.push_back(makeVerbCandidate(onbin_surface, start_pos, kanji_end + 1, kHatsuonbinCost,
                                                 matched_base_form, grammar::verbTypeToConjType(matched_verb_type),
                                                 true, CandidateOrigin::VerbKanji, 0.9F, "kanji_hatsuonbin",
                                                 core::ExtendedPOS::VerbOnbinkei));
        }
      }
    }
  }

  // Generate hatsuonbin candidates for multi-hiragana okurigana and standalone ん
  // Covers cases NOT handled by the de/da handler above:
  // - Multi-hira okurigana: 汗ばんだ → 汗ばん (onbin of 汗ばむ) + だ
  // - Standalone single ん: 死ん (end of token, no following で/だ)
  if (kanji_end < hiragana_end) {
    for (size_t n_pos = kanji_end; n_pos < hiragana_end; ++n_pos) {
      if (codepoints[n_pos] != U'ん')
        continue;

      bool at_end = (n_pos + 1 >= hiragana_end);
      bool followed_by_de_da = (!at_end) && (codepoints[n_pos + 1] == U'で' || codepoints[n_pos + 1] == U'だ');

      // Skip: n_pos == kanji_end && !at_end is already handled by de/da handler above
      if (n_pos == kanji_end && !at_end)
        continue;
      // Only valid: at end of hiragana region, or followed by で/だ
      if (!at_end && !followed_by_de_da)
        continue;

      std::string kanji_stem = extractSubstring(codepoints, start_pos, kanji_end);
      std::string hira_stem = (n_pos > kanji_end) ? extractSubstring(codepoints, kanji_end, n_pos) : "";

      auto n_onbin_match = vh::firstGodanOnbinDictBase(dict_manager, kanji_stem + hira_stem, "ん");
      if (n_onbin_match.matched) {
        std::string onbin_surface = extractSubstring(codepoints, start_pos, n_pos + 1);
        constexpr float kHatsuonbinCost = candidate::verb_cost::kStandardBonus;
        SUZUME_DEBUG_VERBOSE_BLOCK {
          SUZUME_DEBUG_STREAM << "[VERB_CAND] " << onbin_surface
                              << " kanji_hatsuonbin_standalone lemma=" << n_onbin_match.base_form
                              << " cost=" << kHatsuonbinCost << "\n";
        }
        candidates.push_back(
            makeVerbCandidate(onbin_surface, start_pos, n_pos + 1, kHatsuonbinCost, n_onbin_match.base_form,
                              grammar::verbTypeToConjType(n_onbin_match.verb_type), true, CandidateOrigin::VerbKanji,
                              0.9F, "kanji_hatsuonbin", core::ExtendedPOS::VerbOnbinkei));
      }
      break;  // Only process first ん in the region
    }
  }
}

}  // namespace suzume::analysis::kanji_verb_detail
