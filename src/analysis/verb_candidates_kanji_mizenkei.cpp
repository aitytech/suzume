/**
 * @file verb_candidates_kanji_mizenkei.cpp
 * @brief Kanji verb mizenkei candidate patterns
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

void appendGodanMizenkeiPassiveCausativeCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                                   size_t kanji_end, size_t hiragana_end,
                                                   const grammar::Inflection& inflection,
                                                   const dictionary::DictionaryManager* dict_manager,
                                                   std::vector<UnknownCandidate>& candidates) {
  if (kanji_end - start_pos == 1 && kanji_end < hiragana_end && grammar::isARowCodepoint(codepoints[kanji_end])) {
    char32_t a_row = codepoints[kanji_end];
    size_t after_a_pos = kanji_end + 1;
    if (after_a_pos < codepoints.size()) {
      char32_t after_a = codepoints[after_a_pos];
      // A-row + れ (passive) or A-row + せ (causative)
      if (after_a == U'れ' || after_a == U'せ') {
        grammar::VerbType verb_type = grammar::verbTypeFromARowCodepoint(a_row);
        std::string_view base_suffix = grammar::godanBaseSuffixFromARow(a_row);
        if (verb_type != grammar::VerbType::Unknown && !base_suffix.empty()) {
          std::string kanji_stem = extractSubstring(codepoints, start_pos, kanji_end);
          std::string base_form = kanji_stem + std::string(base_suffix);
          std::string surface = extractSubstring(codepoints, start_pos, kanji_end + 1);

          // Verify via inflection analysis of base form
          const auto& results = inflection.analyze(base_form);
          bool is_valid = false;
          for (const auto& cand : results) {
            if (cand.verb_type == verb_type && cand.confidence >= 0.4F) {
              is_valid = true;
              break;
            }
          }

          if (is_valid) {
            // Skip if kanji+A-row+る is a known godan-ra verb in dictionary
            // (potential form conflict). E.g., 泊まれる = potential of
            // 泊まる (godan-ra), not passive of 泊む (godan-ma).
            // 囲まれる = passive of 囲む is OK because 囲まる is not
            // in the dictionary.
            //
            // Also skip if kanji+A-row+れる is a known ichidan verb in
            // dictionary. E.g., 生まれる is ichidan, not passive of 生む.
            // Without this check, 生ま(mizenkei)+れ(passive) would
            // incorrectly win over the dictionary ichidan entry.
            bool has_competing_verb = false;
            if (after_a == U'れ') {
              std::string ra_form = surface + "る";
              has_competing_verb = vh::isVerbInDictionary(dict_manager, ra_form);
              if (!has_competing_verb) {
                std::string ichidan_form = surface + "れる";
                has_competing_verb = vh::isVerbInDictionary(dict_manager, ichidan_form);
              }
            }

            if (!has_competing_verb) {
              constexpr float kCost = candidate::verb_cost::kWeakPenalty;
              SUZUME_DEBUG_LOG("[VERB_CAND] " << surface << " godan_mizenkei_passive lemma=" << base_form
                                              << " cost=" << kCost << "\n");
              candidates.push_back(makeVerbCandidate(
                  surface, start_pos, kanji_end + 1, kCost, base_form, grammar::verbTypeToConjType(verb_type), true,
                  CandidateOrigin::VerbKanji, 0.8F, "godan_mizenkei_passive", core::ExtendedPOS::VerbMizenkei));
            }
          }
        }
      }
    }
  }
}

// Contracted sa-row mizenkei: kanji + しゃ + れ/せ/し
// Colloquial contraction さ→しゃ in passive/causative/emphatic negation
// E.g., 殺しゃれる → 殺しゃ (contracted mizenkei of 殺す) + れる (passive)
//       話しゃれる → 話しゃ (contracted mizenkei of 話す) + れる (passive)
//       出しゃしない → 出しゃ (contracted) + し + ない (emphatic neg)
// Only single-kanji stems (same constraint as godan-sa mizenkei above)
void appendSaRowContractedMizenkeiCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                             size_t kanji_end, size_t hiragana_end,
                                             const grammar::Inflection& inflection,
                                             std::vector<UnknownCandidate>& candidates) {
  if (kanji_end - start_pos == 1 && kanji_end + 1 < hiragana_end && codepoints[kanji_end] == U'し' &&
      codepoints[kanji_end + 1] == U'ゃ') {
    size_t after_sha = kanji_end + 2;
    if (after_sha < codepoints.size()) {
      char32_t after = codepoints[after_sha];
      // しゃ + れ (passive) or しゃ + せ (causative) or しゃ + し (emphatic)
      if (after == U'れ' || after == U'せ' || after == U'し') {
        std::string kanji_stem = extractSubstring(codepoints, start_pos, kanji_end);
        std::string base_form = kanji_stem + "す";
        std::string surface = kanji_stem + "しゃ";

        const auto& sa_results = inflection.analyze(base_form);
        bool is_valid_godan_sa = false;
        for (const auto& cand : sa_results) {
          if (cand.verb_type == grammar::VerbType::GodanSa && cand.confidence >= 0.4F) {
            is_valid_godan_sa = true;
            break;
          }
        }

        if (is_valid_godan_sa) {
          constexpr float kCost = candidate::verb_cost::kWeakPenalty;
          SUZUME_DEBUG_LOG("[VERB_CAND] " << surface << " godan_sa_contracted_mizenkei lemma=" << base_form
                                          << " cost=" << kCost << "\n");
          candidates.push_back(makeVerbCandidate(surface, start_pos, kanji_end + 2, kCost, base_form,
                                                 grammar::verbTypeToConjType(grammar::VerbType::GodanSa), false,
                                                 CandidateOrigin::VerbKanji, 0.8F, "godan_sa_contracted_mizenkei",
                                                 core::ExtendedPOS::VerbMizenkei));
        }
      }
    }
  }
}

// Godan mizenkei pattern: kanji + A-row hiragana + ず (classical negative)
// E.g., 抜かずに → 抜か (mizenkei of 抜く) + ず + に
//       行かずに → 行か (mizenkei of 行く) + ず + に
//       書かずに → 書か (mizenkei of 書く) + ず + に
// The main loop skips single A-row hiragana as particle (か, etc.)
// so we generate mizenkei candidates explicitly when followed by ず.
void appendGodanMizenkeiZuCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t kanji_end,
                                     size_t hiragana_end, const grammar::Inflection& inflection,
                                     const dictionary::DictionaryManager* dict_manager,
                                     std::vector<UnknownCandidate>& candidates) {
  if (kanji_end + 1 < hiragana_end && codepoints[kanji_end + 1] == U'ず') {
    char32_t first_hira = codepoints[kanji_end];
    if (grammar::isARowCodepoint(first_hira)) {
      grammar::VerbType verb_type = grammar::verbTypeFromARowCodepoint(first_hira);
      if (verb_type != grammar::VerbType::Unknown) {
        std::string_view base_suffix = grammar::godanBaseSuffixFromARow(first_hira);
        if (!base_suffix.empty()) {
          std::string kanji_stem = extractSubstring(codepoints, start_pos, kanji_end);
          std::string base_form = kanji_stem + std::string(base_suffix);
          std::string surface = extractSubstring(codepoints, start_pos, kanji_end + 1);

          // Verify via dictionary or inflection analysis of conjugated form
          bool is_valid = vh::isVerbInDictionary(dict_manager, base_form);
          if (!is_valid) {
            // Analyze mizenkei+ない form (standard negative) for better confidence
            // Base form alone (e.g., 躊躇う) may not be recognized
            std::string neg_form = surface + "ない";
            const auto& infl_results = inflection.analyze(neg_form);
            for (const auto& cand : infl_results) {
              if (cand.base_form == base_form && cand.verb_type == verb_type && cand.confidence >= 0.3F) {
                is_valid = true;
                break;
              }
            }
          }

          if (is_valid) {
            // Skip if verb+ず or verb+ずに is a dictionary entry (e.g., 思わず=ADV)
            // In that case, the dictionary entry should win over the split path
            bool dict_has_zu_form = false;
            if (dict_manager != nullptr) {
              std::string zu_form = surface + "ず";
              std::string zuni_form = surface + "ずに";
              dict_has_zu_form =
                  dict_manager->lookupExact(zu_form) != nullptr || dict_manager->lookupExact(zuni_form) != nullptr;
            }
            if (!dict_has_zu_form) {
              constexpr float kCost = candidate::verb_cost::kWeakPenalty;
              SUZUME_DEBUG_LOG("[VERB_CAND] " << surface << " godan_mizenkei_zu lemma=" << base_form
                                              << " cost=" << kCost << "\n");
              candidates.push_back(makeVerbCandidate(
                  surface, start_pos, kanji_end + 1, kCost, base_form, grammar::verbTypeToConjType(verb_type), true,
                  CandidateOrigin::VerbKanji, 0.8F, "godan_mizenkei_zu", core::ExtendedPOS::VerbMizenkei));
            }
          }
        }
      }
    }
  }
}

// Try Ichidan renyokei pattern: kanji + e-row/i-row hiragana
// 下一段 (shimo-ichidan): e-row ending (食べ, 見せ, 教え)
// 上一段 (kami-ichidan): i-row ending (感じ, 見, 居)
// These are standalone verb forms that connect to ます, ましょう, etc.
// The stem IS the entire surface (no conjugation suffix)

void appendKanjiMizenkeiStemCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t kanji_end,
                                       size_t hiragana_end, const grammar::Inflection& inflection,
                                       const dictionary::DictionaryManager* dict_manager,
                                       std::vector<UnknownCandidate>& candidates) {
  // Generate Godan mizenkei stem candidates for auxiliary separation
  // E.g., 書か (from 書く), 読ま (from 読む), 話さ (from 話す)
  // These connect to passive (れる), causative (せる), negative (ない)
  if (kanji_end < hiragana_end) {
    char32_t first_hira = codepoints[kanji_end];
    // A-row hiragana: あ, か, さ, た, な, ま, ら, わ, が, ざ, だ, ば, ぱ
    if (grammar::isARowCodepoint(first_hira)) {
      size_t mizenkei_end = kanji_end + 1;
      // Check if followed by passive/causative auxiliary pattern
      if (mizenkei_end < hiragana_end) {
        char32_t next_char = codepoints[mizenkei_end];
        // Generate mizenkei candidates for:
        // 1. Classical べき patterns: 書かれべき, 読まれべき
        // 2. Classical negation ぬ: 揃わぬ, 知らぬ, 行かぬ
        // 3. Passive patterns: 書か+れる, 言わ+れ+た
        bool is_beki_pattern = false;
        bool is_nu_pattern = false;
        bool is_passive_pattern = false;
        if (next_char == U'れ') {
          if (mizenkei_end + 2 < codepoints.size() && codepoints[mizenkei_end + 1] == U'べ' &&
              codepoints[mizenkei_end + 2] == U'き') {
            // Check for れべき pattern
            is_beki_pattern = true;
          } else {
            // Check for passive patterns: れる, れた, れて, れない, れます
            // E.g., 言われる → 言わ (mizenkei) + れる (passive AUX)
            // Strict ま-branch: bare ま requires a following す/せ (れます/れません).
            is_passive_pattern = vh::isPassiveAuxContinuation(codepoints, mizenkei_end + 1, /*strict_masu=*/true);
          }
        }
        // Check for classical negation ぬ pattern
        // E.g., 揃わぬ → 揃わ (mizenkei) + ぬ (AUX)
        if (next_char == U'ぬ') {
          is_nu_pattern = true;
        }
        // Check for colloquial contracted negative ん pattern
        // E.g., 行かん → 行か (mizenkei) + ん (contracted negative AUX)
        //       言わん → 言わ (mizenkei) + ん
        // Skip single-kanji + さ + ん pattern (honorific さん suffix)
        // E.g., 姉さん should be 姉 + さん (noun + suffix), not 姉さ + ん (verb + AUX)
        bool is_n_pattern = false;
        if (next_char == U'ん') {
          // Skip if single kanji + さ (potential さん honorific)
          bool is_honorific_san = (kanji_end == start_pos + 1 && first_hira == U'さ');
          if (!is_honorific_san) {
            is_n_pattern = true;
          }
        }
        // Check for standard negative ない pattern
        // E.g., 行かない → 行か (mizenkei) + ない (negative AUX)
        //       書かない → 書か (mizenkei) + ない
        bool is_nai_pattern = false;
        if (next_char == U'な' && mizenkei_end + 1 < codepoints.size() && codepoints[mizenkei_end + 1] == U'い') {
          is_nai_pattern = true;
        }
        // Check for the past-negative auxiliary stem なかっ.
        // E.g., 書かなかった → 書か (mizenkei) + なかっ (negative past AUX) + た
        //       行かなかった → 行か (mizenkei) + なかっ + た
        bool is_nakatt_pattern = false;
        if (next_char == U'な' && mizenkei_end + 3 < codepoints.size() && codepoints[mizenkei_end + 1] == U'か' &&
            codepoints[mizenkei_end + 2] == U'っ') {
          is_nakatt_pattern = true;
        }
        // Check for the negative adverbial なく (ない's 連用形).
        // E.g., 行かなくて → 行か (mizenkei) + なく (negative adj) + て
        //       食べなくなる counterpart is handled elsewhere; here we split the godan
        //       mizenkei so なく does not get absorbed into a spurious verb form.
        bool is_naku_pattern = false;
        if (next_char == U'な' && mizenkei_end + 1 < codepoints.size() && codepoints[mizenkei_end + 1] == U'く') {
          is_naku_pattern = true;
        }
        // Check for the causative auxiliary せ.
        // E.g., 聞かせられた → 聞か (mizenkei) + せ (causative AUX) + られ + た
        //       書かせる → 書か (mizenkei) + せる (causative AUX)
        bool is_causative_pattern = false;
        if (next_char == U'せ') {
          // せ followed by られ, る, た, て, etc.
          if (mizenkei_end + 1 < codepoints.size()) {
            char32_t after_se = codepoints[mizenkei_end + 1];
            // せら (せられる, せられた)
            if (after_se == U'ら') {
              is_causative_pattern = true;
            }
            // せる, せた, せて
            else if (after_se == U'る' || after_se == U'た' || after_se == U'て') {
              is_causative_pattern = true;
            }
            // せな (せない)
            else if (after_se == U'な' && mizenkei_end + 2 < codepoints.size() &&
                     codepoints[mizenkei_end + 2] == U'い') {
              is_causative_pattern = true;
            }
          }
        }
        if (is_beki_pattern || is_nu_pattern || is_n_pattern || is_nai_pattern || is_nakatt_pattern ||
            is_naku_pattern || is_passive_pattern || is_causative_pattern) {
          // Derive VerbType from the A-row ending (e.g., か → GodanKa)
          grammar::VerbType verb_type = grammar::verbTypeFromARowCodepoint(first_hira);
          if (verb_type != grammar::VerbType::Unknown) {
            // Skip GodanSa mizenkei for all-kanji stems (likely サ変名詞 + される)
            // E.g., 装飾さ should be 装飾 + される, not 装飾す mizenkei
            bool is_suru_verb_pattern = false;
            if (verb_type == grammar::VerbType::GodanSa) {
              std::string kanji_stem = extractSubstring(codepoints, start_pos, kanji_end);
              if (grammar::isAllKanji(kanji_stem) && kanji_stem.size() >= 6) {
                // This is likely a Suru verb pattern (2+ kanji followed by される)
                // The connection rules will handle 装飾 + される instead
                // Note: 6 bytes = 2 kanji characters in UTF-8
                is_suru_verb_pattern = true;
              }
              // Skip single-kanji GodanSa + causative pattern (likely ichidan verb + させ)
              // E.g., 見させられた = 見 + させ + られ + た (ichidan 見る + causative)
              //       Not: 見さ + せ + られ + た (godan 見す doesn't exist)
              // Real godan-sa verbs (話す, 出す, 消す) have multi-char stems (話さ, 出さ, 消さ)
              if (is_causative_pattern && kanji_stem.size() == 3) {  // 3 bytes = 1 kanji
                is_suru_verb_pattern = true;                         // Skip generation
              }
            }
            if (is_suru_verb_pattern) {
              // Skip mizenkei generation for Suru verb patterns
            } else {
              // Get base suffix (e.g., か → く for GodanKa)
              std::string_view base_suffix = grammar::godanBaseSuffixFromARow(first_hira);
              if (!base_suffix.empty()) {
                // Construct base form: stem + base_suffix (e.g., 書 + く = 書く)
                std::string kanji_stem = extractSubstring(codepoints, start_pos, kanji_end);
                std::string base_form = kanji_stem + std::string(base_suffix);

                // Verify the base form is a valid verb
                // First check dictionary, then fall back to inflection analysis
                // IMPORTANT: For passive pattern, require dictionary check only for
                // most verb rows. The inflection analyzer is too permissive and will
                // accept patterns like 泊む (from 泊まれる) which don't exist.
                // EXCEPTIONS that allow inflection fallback:
                // - WA-row (わ行): passive (奪われる) doesn't conflict with potential
                // - RA-row (ら行): Xらる is not a valid modern verb, so Xられる
                //   is always passive of Xる (e.g., 縛られる = passive of 縛る)
                bool is_valid_verb = vh::isVerbInDictionary(dict_manager, base_form);
                // For passive pattern, allow inflection fallback for WA-row and RA-row
                bool allow_inflection_fallback = !is_passive_pattern || first_hira == U'わ' || first_hira == U'ら';
                if (!is_valid_verb && allow_inflection_fallback) {
                  // For non-passive patterns (ない, ぬ, etc.), allow inflection fallback
                  // For WA-row passive, also allow with higher confidence threshold
                  float threshold = is_passive_pattern ? candidate::verb_cost::kConstructedVerbPassiveMinConfidence
                                                       : candidate::verb_cost::kConstructedVerbMinConfidence;
                  is_valid_verb = vh::isVerifiedVerbBase(dict_manager, inflection, base_form, threshold, true);
                }

                // Skip irregular verb 来る for passive — its passive is 来+られる, not 来ら+れる
                if (is_valid_verb && is_passive_pattern && base_form == "来る") {
                  is_valid_verb = false;
                }

                // Skip godan mizenkei passive when the surface + れる is a known
                // ichidan verb in the dictionary. E.g., 囚われる is ichidan,
                // not passive of 囚う. The dictionary entry provides the correct
                // candidate with proper lemma.
                if (is_valid_verb && is_passive_pattern) {
                  std::string ichidan_form = extractSubstring(codepoints, start_pos, mizenkei_end) + "れる";
                  if (vh::isVerbInDictionary(dict_manager, ichidan_form)) {
                    is_valid_verb = false;
                  }
                }

                if (is_valid_verb) {
                  std::string surface = extractSubstring(codepoints, start_pos, mizenkei_end);
                  // Cost varies by pattern:
                  // - ぬ pattern: negative cost (-0.5F) to beat combined verb form
                  //   揃わぬ(VERB) gets ~-0.1 total, so split needs lower cost
                  // - ん pattern: negative cost (-0.5F) for contracted negative
                  //   行かん(VERB) should split to 行か + ん
                  // - ない pattern: negative cost (-0.5F) for standard negative
                  //   行かない(VERB) should split to 行か + ない
                  // - passive pattern: negative cost (-0.5F) for the mizenkei boundary
                  //   言われる(VERB) gets ~0.15, so split (言わ+れる) needs lower cost
                  // - べき pattern: moderate cost (0.2F) for classical obligation
                  float cost = 0.2F;  // default for beki
                  if (is_nu_pattern || is_n_pattern || is_nai_pattern) {
                    cost = -0.5F;
                  } else if (is_passive_pattern) {
                    cost = -0.5F;
                  }
                  const char* debug_pattern = is_nu_pattern        ? "nu"
                                              : is_n_pattern       ? "n"
                                              : is_nai_pattern     ? "nai"
                                              : is_passive_pattern ? "passive"
                                                                   : "beki";
                  SUZUME_DEBUG_VERBOSE_BLOCK {
                    SUZUME_DEBUG_STREAM << "[VERB_CAND] " << surface << " godan_mizenkei lemma=" << base_form
                                        << " cost=" << cost << " pattern=" << debug_pattern << "\n";
                  }
                  const char* info_pattern = is_nu_pattern        ? "godan_mizenkei_nu"
                                             : is_n_pattern       ? "godan_mizenkei_n"
                                             : is_nai_pattern     ? "godan_mizenkei_nai"
                                             : is_nakatt_pattern  ? "godan_mizenkei_nakatt"
                                             : is_passive_pattern ? "godan_mizenkei_passive"
                                                                  : "godan_mizenkei";
                  // Use explicit VerbMizenkei EPOS for negative/passive patterns to enable bigram connection
                  core::ExtendedPOS epos =
                      (is_nu_pattern || is_n_pattern || is_nai_pattern || is_nakatt_pattern || is_passive_pattern)
                          ? core::ExtendedPOS::VerbMizenkei
                          : core::ExtendedPOS::Unknown;
                  candidates.push_back(makeVerbCandidate(surface, start_pos, mizenkei_end, cost, base_form,
                                                         grammar::verbTypeToConjType(verb_type), true,
                                                         CandidateOrigin::VerbKanji, 0.9F, info_pattern, epos));
                }
              }
            }  // else (not Suru verb pattern)
          }
        }
      }
    }
  }

  // Generate mizenkei candidates for verbs with multiple okurigana + negative patterns
  // E.g., 分からない → 分から (mizenkei of 分かる) + ない
  //       分からなかった → 分から (mizenkei of 分かる) + なかっ + た
  //       始まらない → 始まら (mizenkei of 始まる) + ない
  // These are Godan verbs where the okurigana includes 2+ hiragana before the A-row ending
  if (kanji_end < hiragana_end && hiragana_end >= kanji_end + 3) {
    // Look for A-row hiragana + negative patterns (ない, なかっ, or ん)
    for (size_t scan_pos = kanji_end + 1; scan_pos < hiragana_end - 1; ++scan_pos) {
      char32_t cur_char = codepoints[scan_pos];
      char32_t next_char = codepoints[scan_pos + 1];
      // Check if cur_char is A-row and followed by negative pattern
      bool is_nai_pattern = grammar::isARowCodepoint(cur_char) && next_char == U'な' &&
                            scan_pos + 2 < codepoints.size() && codepoints[scan_pos + 2] == U'い';
      bool is_nakatt_pattern = grammar::isARowCodepoint(cur_char) && next_char == U'な' &&
                               scan_pos + 3 < codepoints.size() && codepoints[scan_pos + 2] == U'か' &&
                               codepoints[scan_pos + 3] == U'っ';
      // Check for contracted negative ん pattern (分からん, 始まらん)
      // ん must be at the end of the string (hiragana_end == scan_pos + 2)
      bool is_n_pattern = grammar::isARowCodepoint(cur_char) && next_char == U'ん' && scan_pos + 2 == hiragana_end;
      // Check for classical negative ぬ pattern (分からぬ, 変わらぬ)
      bool is_nu_pattern = grammar::isARowCodepoint(cur_char) && next_char == U'ぬ';
      if (is_nai_pattern || is_nakatt_pattern || is_n_pattern || is_nu_pattern) {
        // Found A-row + negative pattern at scan_pos
        // The mizenkei would be from start_pos to scan_pos + 1
        size_t multi_miz_end = scan_pos + 1;
        grammar::VerbType verb_type = grammar::verbTypeFromARowCodepoint(cur_char);
        if (verb_type != grammar::VerbType::Unknown) {
          // Construct the base form
          // E.g., 分から → 分かる (replace A-row ending with U-row)
          std::string_view base_suffix = grammar::godanBaseSuffixFromARow(cur_char);
          if (!base_suffix.empty()) {
            std::string stem = extractSubstring(codepoints, start_pos, scan_pos);
            std::string base_form = stem + std::string(base_suffix);
            // Verify this is a valid verb
            bool is_valid_verb = vh::isVerifiedVerbBase(dict_manager, inflection, base_form,
                                                        candidate::verb_cost::kConstructedVerbMinConfidence, true);
            // Reject a fabricated mizenkei that merely absorbs a trailing
            // binding particle (係助詞): 水すらない is noun + すら + ない, never
            // the mizenkei of a non-word godan-ra verb 水する. Only すら ends in
            // an a-row mora among binding particles, and no genuine godan verb
            // ends in 〜する, so this cannot suppress a real conjugation.
            // @see fabricated closed-class absorption guards (verb_candidates_helpers.h)
            if (is_valid_verb && !vh::isVerbInDictionary(dict_manager, base_form) &&
                vh::endsWithParticleTailOfPos(dict_manager, codepoints, start_pos, multi_miz_end,
                                              core::ExtendedPOS::ParticleBinding)) {
              SUZUME_DEBUG_LOG("[VERB_SKIP] \"" << extractSubstring(codepoints, start_pos, multi_miz_end)
                                                << "\" fabricated mizenkei absorbing binding particle\n");
              is_valid_verb = false;
            }
            if (is_valid_verb) {
              std::string surface = extractSubstring(codepoints, start_pos, multi_miz_end);
              constexpr float kCost = candidate::verb_cost::kStandardBonus;  // Same as other negative patterns
              const char* pattern = is_nakatt_pattern ? "multi_mizenkei_nakatt"
                                    : is_n_pattern    ? "multi_mizenkei_n"
                                    : is_nu_pattern   ? "multi_mizenkei_nu"
                                                      : "multi_mizenkei_nai";
              SUZUME_DEBUG_VERBOSE_BLOCK {
                SUZUME_DEBUG_STREAM << "[VERB_CAND] " << surface << " " << pattern << " lemma=" << base_form
                                    << " cost=" << kCost << "\n";
              }
              candidates.push_back(makeVerbCandidate(
                  surface, start_pos, multi_miz_end, kCost, base_form, grammar::verbTypeToConjType(verb_type), true,
                  CandidateOrigin::VerbKanji, 0.9F, pattern, core::ExtendedPOS::VerbMizenkei));
            }
          }
        }
        break;  // Only generate one candidate per position
      }
    }
  }
}

}  // namespace suzume::analysis::kanji_verb_detail
