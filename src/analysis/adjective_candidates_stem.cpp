/**
 * @file adjective_candidates_stem.cpp
 * @brief I-adjective stem candidate generation
 */

#include <algorithm>

#include "adjective_candidates.h"
#include "adjective_candidates_internal.h"
#include "analysis/candidate_constants.h"
#include "analysis/scorer_constants.h"
#include "core/debug.h"
#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "grammar/patterns.h"
#include "normalize/char_type.h"
#include "normalize/exceptions.h"
#include "normalize/utf8.h"
#include "suffix_candidates.h"
#include "unknown.h"
#include "verb_candidates_helpers.h"

namespace suzume::analysis {

using verb_helpers::addEmphaticVariants;
using verb_helpers::findCharRegionEnd;
using verb_helpers::isAdjectiveInDictionary;
using verb_helpers::isEmphaticChar;
using verb_helpers::isVerbInDictionary;

using adj_detail::makeIAdjCandidate;
using adj_detail::makeIAdjStemCandidate;
using adj_detail::makeTrimmedAdjVariant;

std::vector<UnknownCandidate> generateAdjectiveStemCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                                              const std::vector<normalize::CharType>& char_types,
                                                              const grammar::Inflection& inflection,
                                                              const dictionary::DictionaryManager* dict_manager) {
  std::vector<UnknownCandidate> candidates;

  // Must start with kanji
  if (start_pos >= char_types.size() || char_types[start_pos] != normalize::CharType::Kanji) {
    return candidates;
  }

  // Find kanji portion (1-2 characters for adjective stem)
  size_t kanji_end = findCharRegionEnd(char_types, start_pos, 2, normalize::CharType::Kanji);

  if (kanji_end == start_pos) {
    return candidates;
  }

  // Look for hiragana after kanji
  if (kanji_end >= char_types.size() || char_types[kanji_end] != normalize::CharType::Hiragana) {
    return candidates;
  }

  // Find hiragana ending with し + auxiliary pattern (そう, すぎ, etc.)
  size_t hiragana_end = findCharRegionEnd(char_types, kanji_end, 8, normalize::CharType::Hiragana);

  if (hiragana_end <= kanji_end) {
    return candidates;
  }

  std::string hiragana_part = extractSubstring(codepoints, kanji_end, hiragana_end);
  std::string kanji_part = extractSubstring(codepoints, start_pos, kanji_end);
  SUZUME_DEBUG_LOG_VERBOSE("[ADJ_STEM] pos=" << start_pos << " kanji=\"" << kanji_part << "\" hiragana=\""
                                             << hiragana_part << "\"\n");

  // =============================================================================
  // Pattern 1: Regular i-adjective stem + すぎる/がる/さ/そう (ガル接続)
  // =============================================================================
  // MeCab handles regular i-adjectives (高い, 尊い, 寒い) differently from しい-adjectives.
  // For patterns like 高すぎる, MeCab splits as: 高(ADJ, ガル接続) + すぎる(VERB)
  // The adjective stem is just the kanji portion (without い).
  //
  // Patterns handled:
  // - 高すぎる → 高 (ADJ stem) + すぎる (VERB)
  // - 尊すぎて → 尊 (ADJ stem) + すぎ (VERB) + て (PARTICLE)
  // - 高がる → 高 (ADJ stem) + がる (VERB)
  // - 高さ → 高 (ADJ stem) + さ (NOUN/SUFFIX)
  // - 高そう → 高 (ADJ stem) + そう (AUX)
  static const std::vector<std::string_view> kIAdjGaruPatterns = {
      "すぎ",  // excessive: 高すぎる, 高すぎ, 高すぎて
      "がる",  // emotional verb: 高がる, 怖がる
      "がり",  // nominalized: 怖がり
      "がっ",  // te/ta form: 怖がって, 怖がった
      "さ",    // nominalization: 高さ, 重さ (1 char)
      "そう",  // appearance: 高そう (2 chars)
      "み",    // nominalization: 痛み, 深み (1 char)
  };

  // Check if hiragana starts with a garu-connection pattern
  for (const auto& pattern : kIAdjGaruPatterns) {
    if (hiragana_part.size() >= pattern.size() && hiragana_part.substr(0, pattern.size()) == pattern) {
      SUZUME_DEBUG_LOG_VERBOSE("[ADJ_STEM]   pattern=\"" << pattern << "\" matched, hiragana=\"" << hiragana_part
                                                         << "\"\n");

      // Check for サ変 passive/causative pattern: さ + れ/せ
      // E.g., 処理される, 勉強させる - these are NOT adjective nominalization
      if (std::string_view(pattern) == "さ" && hiragana_part.size() > 3) {
        std::string after_sa = hiragana_part.substr(3);  // Skip さ (3 bytes)
        if (after_sa.size() >= 3 && (after_sa.substr(0, 3) == "れ" || after_sa.substr(0, 3) == "せ")) {
          SUZUME_DEBUG_LOG_VERBOSE("[ADJ_STEM]   skip: サ変 passive/causative (さ+" << after_sa.substr(0, 3) << ")\n");
          continue;  // Skip - this is likely サ変 passive/causative, not adjective
        }
      }

      // Check if hiragana_part is a suffix in dictionary (さん, さま, etc.)
      // E.g., 姉さん = 姉 + さん (NOUN + SUFFIX), not 姉 + さ (ADJ stem + nominalization)
      // EXCEPT: "さ" alone is valid for adjective nominalization (高さ, 明るさ, 優しさ)
      if (hiragana_part != "さ" &&
          verb_helpers::hasDictionaryEntry(dict_manager, hiragana_part, core::PartOfSpeech::Suffix)) {
        SUZUME_DEBUG_LOG_VERBOSE("[ADJ_STEM]   skip: suffix \"" << hiragana_part << "\" in dict\n");
        continue;  // Skip - hiragana_part is a dictionary suffix
      }

      // Check for compound adjective pattern: み + やすい/にくい/がたい
      // E.g., 読みやすい, 使いにくい - these are verb renyokei + auxiliary adjective
      // NOT kanji stem + み nominalization
      if (std::string_view(pattern) == "み" && verb_helpers::isCompoundAdjectivePattern(hiragana_part)) {
        SUZUME_DEBUG_VERBOSE_BLOCK {
          // Extract the compound suffix for detailed logging
          const char* compound_suffix = "compound";
          if (hiragana_part.find("やすい") != std::string::npos || hiragana_part.find("やすく") != std::string::npos) {
            compound_suffix = "やすい";
          } else if (hiragana_part.find("にくい") != std::string::npos ||
                     hiragana_part.find("にくく") != std::string::npos) {
            compound_suffix = "にくい";
          } else if (hiragana_part.find("がたい") != std::string::npos ||
                     hiragana_part.find("がたく") != std::string::npos) {
            compound_suffix = "がたい";
          }
          SUZUME_DEBUG_STREAM << "[ADJ_STEM]   skip: compound adjective (contains \"" << compound_suffix << "\")\n";
        }
        continue;  // Skip - this is likely verb + やすい/にくい, not adjective + み
      }

      // For 1-char patterns (み, さ), skip if the hiragana portion starts with
      // a known dictionary word of 2+ chars. This prevents splitting known words.
      // E.g., 像+みんな → みんな is PRON, so み is not nominalization suffix
      if (pattern.size() <= 3 && hiragana_part.size() > pattern.size() && dict_manager) {
        auto hira_results = dict_manager->lookup(hiragana_part, 0);
        bool has_longer_dict_word = false;
        for (const auto& result : hira_results) {
          if (result.entry && result.entry->surface.size() > 3) {
            has_longer_dict_word = true;
            break;
          }
        }
        if (has_longer_dict_word) {
          SUZUME_DEBUG_LOG_VERBOSE("[ADJ_STEM]   skip: hiragana starts with dict word\n");
          continue;
        }
      }

      // Found potential i-adjective stem + garu-connection pattern
      // The stem is just the kanji portion (e.g., 高, 尊, 寒)
      std::string stem = extractSubstring(codepoints, start_pos, kanji_end);
      std::string base_form = stem + "い";  // e.g., 高 → 高い

      // Validate that stem + い is a real i-adjective
      // Use lower threshold (0.35) for garu-connection patterns because:
      // - Single-kanji adjectives like 高い get lower confidence (0.42)
      // - The presence of すぎる/がる/さ strongly indicates adjective interpretation
      const auto& adj_results = inflection.analyze(base_form);
      bool is_valid_adjective = false;
      float adj_confidence = 0.0F;
      // A single-kanji stem is validated by inflection shape alone too easily: 上い
      // (conf 0.42) looks like an i-adjective but is really the godan verb stem of
      // 上がる. Require dictionary confirmation for single-kanji stems (real ones —
      // 寒い, 高い, 痛い — are all registered), while multi-kanji/extended stems
      // (恥ずかしい) keep the inflection path.
      const bool single_kanji_stem = (kanji_end - start_pos == 1);
      for (const auto& result : adj_results) {
        if (result.verb_type == grammar::VerbType::IAdjective && result.confidence >= candidate::kGaruAdjConfMin) {
          if (single_kanji_stem && !isAdjectiveInDictionary(dict_manager, base_form)) {
            continue;
          }
          is_valid_adjective = true;
          adj_confidence = result.confidence;
          break;
        }
      }

      SUZUME_DEBUG_LOG_VERBOSE("[ADJ_STEM]   base=\"" << base_form << "\" is_valid=" << is_valid_adjective
                                                      << " conf=" << adj_confidence << "\n");

      // Dictionary fallback: if inflection analysis gives low confidence but
      // the adjective exists in the dictionary, accept it.
      // E.g., 可愛い has conf=0 from inflection (all-kanji stem) but is in L2 dict.
      if (!is_valid_adjective) {
        if (isAdjectiveInDictionary(dict_manager, base_form)) {
          is_valid_adjective = true;
          adj_confidence = candidate::kDictFallbackAdjConfidence;
          SUZUME_DEBUG_LOG_VERBOSE("[ADJ_STEM]   dict fallback: \"" << base_form << "\" found in dictionary\n");
        } else {
          continue;
        }
      }

      // Check for false positives: single-kanji stems that are also verb renyokei
      // E.g., 落ちすぎ could be 落ち(verb renyokei) + すぎ(verb)
      // We should prefer the verb renyokei interpretation if kanji+ちる/きる/etc. is a verb
      if (kanji_end - start_pos == 1) {
        // Check if stem + る, stem + す, etc. forms a verb
        bool is_likely_verb_stem = false;
        for (const auto& suffix : {"ちる", "きる", "ぎる", "しる", "びる", "みる", "りる"}) {
          std::string verb_form = stem + suffix;
          if (isVerbInDictionary(dict_manager, verb_form)) {
            SUZUME_DEBUG_LOG_VERBOSE("[ADJ_STEM]   skip: ichidan verb \"" << verb_form << "\" exists in dict\n");
            is_likely_verb_stem = true;
            break;
          }
        }
        if (is_likely_verb_stem) {
          continue;  // Skip - likely verb renyokei, not adjective stem
        }
      }

      // Skip adjective stem when the full kanji+hiragana surface is a known verb
      // E.g., 下さい(=ください) is a verb, not adjective stem 下 + nominalization さ + い
      std::string full_surface = extractSubstring(codepoints, start_pos, hiragana_end);
      if (isVerbInDictionary(dict_manager, full_surface)) {
        SUZUME_DEBUG_LOG_VERBOSE("[ADJ_STEM]   skip: full surface \"" << full_surface << "\" is dict verb\n");
        continue;
      }

      // Low cost to compete with single-token verb path (高すぎる as VERB/ADJ)
      // Use strong negative cost to prefer ADJ_stem + すぎる split over compound
      // Need: stem + connection(0.5) + すぎる(0.4) < compound(0.35)
      // Required: stem < 0.35 - 0.5 - 0.4 = -0.55
      float cost =
          adj_detail::confidenceScaledCost(candidate::kAdjStemBaseCost, adj_confidence, candidate::kAdjStemConfScale);
      SUZUME_DEBUG_LOG("[ADJ_STEM]   ✓ candidate stem=\"" << stem << "\" cost=" << cost << "\n");
      candidates.push_back(makeIAdjStemCandidate(stem, start_pos, kanji_end, base_form, cost,
                                                 CandidateOrigin::AdjectiveI, adj_confidence, "adj_stem_garu_conn"));
      // Don't break - allow multiple patterns to generate candidates
    }
  }

  // =============================================================================
  // Pattern 1b: Extended adjective stem + garu-connection
  // =============================================================================
  // For adjectives like 恥ずかしい where the stem has extended okurigana.
  // E.g., 恥ずかしがってる → 恥ずかし (ADJ stem) + がっ + てる
  // E.g., 恥ずかしすぎる → 恥ずかし (ADJ stem) + すぎる
  //
  // Scan hiragana_part for garu patterns at non-zero positions.
  // If kanji + hiragana_prefix + い is a dict adjective, generate stem candidate.
  if (hiragana_part.size() >= 6) {  // Need at least 2 hiragana chars (prefix + pattern)
    // Garu patterns to look for within hiragana_part
    static const std::vector<std::string_view> kExtGaruPatterns = {
        "すぎ", "がる", "がり", "がっ", "がれ", "がろ", "そう", "さ",
    };

    for (const auto& pattern : kExtGaruPatterns) {
      // Search for pattern at each hiragana character boundary (3-byte aligned for UTF-8)
      for (size_t byte_pos = 3; byte_pos + pattern.size() <= hiragana_part.size(); byte_pos += 3) {
        if (hiragana_part.substr(byte_pos, pattern.size()) == pattern) {
          // Found pattern at byte_pos within hiragana_part
          std::string ext_okurigana = hiragana_part.substr(0, byte_pos);
          std::string stem = kanji_part + ext_okurigana;
          std::string base_form = stem + "い";

          if (isAdjectiveInDictionary(dict_manager, base_form)) {
            // Count hiragana chars in okurigana for stem_end calculation
            size_t okurigana_chars = byte_pos / 3;
            size_t stem_end = kanji_end + okurigana_chars;

            float cost = candidate::kAdjStemExtCost;
            SUZUME_DEBUG_LOG("[ADJ_STEM]   ✓ ext_garu candidate stem=\""
                             << stem << "\" base=\"" << base_form << "\" pattern=\"" << pattern << "\" cost=" << cost
                             << "\n");
            candidates.push_back(makeIAdjStemCandidate(stem, start_pos, stem_end, base_form, cost,
                                                       CandidateOrigin::AdjectiveI, 1.0F, "adj_stem_ext_garu"));
            goto ext_garu_done;  // Found a match, skip remaining patterns
          }
        }
      }
    }
  ext_garu_done:;
  }

  // Check for しそう, しすぎ patterns (adjective stem + auxiliary)
  // The stem ends with し, and is followed by そう/すぎる/etc.
  // E.g., 難しそう → 難し (stem) + そう
  // E.g., 美しすぎる → 美し (stem) + すぎる
  static const std::vector<std::string_view> kAdjStemAuxPatterns = {
      "しそう",    // appearance: 難しそう, 美しそう
      "しそうだ",  // appearance + copula
      "しそうな",  // attributive
      "しそうに",  // adverbial
      "しすぎ",    // excessive: 難しすぎ, 美しすぎ
      "しすぎる",  // excessive + dictionary form
      "しすぎた",  // excessive + past
      "きそう",    // appearance: 大きそう
      "きそうだ",  // appearance + copula
      "きそうな",  // attributive
      "きそうに",  // adverbial
      "きすぎ",    // excessive: 大きすぎ
      "きすぎる",  // excessive + dictionary form
      "きすぎた",  // excessive + past
  };

  for (const auto& pattern : kAdjStemAuxPatterns) {
    if (hiragana_part.size() >= pattern.size() && hiragana_part.substr(0, pattern.size()) == pattern) {
      SUZUME_DEBUG_LOG_VERBOSE("[ADJ_STEM]   shii pattern=\"" << pattern << "\" matched\n");

      // Found adjective stem + auxiliary pattern
      // The stem is: kanji + し
      size_t stem_end = kanji_end + 1;  // kanji + し (one hiragana)

      std::string stem = extractSubstring(codepoints, start_pos, stem_end);
      std::string base_form = stem + "い";  // e.g., 難し → 難しい

      // Validate that this looks like a real adjective
      const auto& adj_results = inflection.analyze(base_form);
      const float adj_confidence =
          adj_detail::firstConfidenceAtLeast(adj_results, grammar::VerbType::IAdjective, candidate::kIAdjConfMin);
      const bool is_valid_adjective = adj_confidence != 0.0F;

      SUZUME_DEBUG_LOG_VERBOSE("[ADJ_STEM]   base=\"" << base_form << "\" is_valid=" << is_valid_adjective
                                                      << " conf=" << adj_confidence << "\n");

      if (!is_valid_adjective) {
        continue;
      }

      // Also check that this is NOT a verb renyokei (話し from 話す)
      // by comparing adjective vs verb confidence
      // The verb form would be: kanji_stem + す (e.g., 話 + す = 話す)
      std::string kanji_stem = extractSubstring(codepoints, start_pos, kanji_end);
      std::string verb_form = kanji_stem + "す";  // e.g., 話す (not 話しす)
      const auto& verb_results = inflection.analyze(verb_form);
      const float verb_confidence =
          adj_detail::maxConfidenceFor(verb_results, {grammar::VerbType::GodanSa, grammar::VerbType::Suru});

      // Check if the verb form (kanji + す) is in the dictionary
      // If it is, this is likely a verb renyokei, not an adjective stem
      // E.g., 話す is in dictionary → 話し is verb renyokei, not adjective
      // E.g., 難す is NOT in dictionary → 難し could be adjective stem
      bool is_dict_verb = isVerbInDictionary(dict_manager, verb_form);
      SUZUME_DEBUG_LOG_VERBOSE("[ADJ_STEM]   verb_form=\"" << verb_form << "\" is_dict_verb=" << is_dict_verb << "\n");
      if (is_dict_verb) {
        SUZUME_DEBUG_LOG_VERBOSE("[ADJ_STEM]   skip: verb in dictionary\n");
        continue;  // Skip - this is a dictionary verb renyokei
      }

      // Check if the adjective form (kanji + し + い) is in the dictionary
      // If it is, we trust the dictionary entry over confidence comparison
      // E.g., 美味しい is in dictionary → 美味し is adjective stem (skip conf check)
      // E.g., 難しい is in dictionary → 難し is adjective stem (skip conf check)
      bool is_dict_adjective = isAdjectiveInDictionary(dict_manager, base_form);
      SUZUME_DEBUG_LOG_VERBOSE("[ADJ_STEM]   is_dict_adj=" << is_dict_adjective << "\n");

      // Confidence-based fallback when adjective is not in dictionary
      // Only generate adjective stem if adjective confidence is SIGNIFICANTLY higher
      // than verb confidence. This prevents generating stems for verb renyokei
      // patterns like 話し (from 話す) where both get similar confidence.
      if (!is_dict_adjective) {
        float diff = adj_confidence - verb_confidence;
        SUZUME_DEBUG_LOG_VERBOSE("[ADJ_STEM]   conf_diff=" << diff << " (adj=" << adj_confidence
                                                           << " verb=" << verb_confidence
                                                           << " threshold=" << candidate::kAdjVerbConfDiffMin << ")\n");
        if (diff < candidate::kAdjVerbConfDiffMin) {
          SUZUME_DEBUG_LOG_VERBOSE("[ADJ_STEM]   skip: conf_diff < threshold\n");
          continue;
        }
      }

      // Low cost to compete with VERB path and single-token conjugated forms
      // Dictionary adjectives get strong bonus to prefer MeCab-compatible split
      // (美味しそう → 美味し + そう)
      // Need stronger negative cost like garu-connection pattern
      float cost =
          adj_detail::confidenceScaledCost(candidate::kAdjStemBaseCost, adj_confidence, candidate::kAdjStemConfScale);
      SUZUME_DEBUG_LOG("[ADJ_STEM]   ✓ candidate stem=\"" << stem << "\" cost=" << cost << "\n");
      candidates.push_back(makeIAdjStemCandidate(stem, start_pos, stem_end, base_form, cost,
                                                 CandidateOrigin::AdjectiveI, adj_confidence, "adj_stem_shii"));
      break;  // Only one stem candidate per pattern
    }
  }

  // =============================================================================
  // Pattern 3: Extended stem (kanji + hiragana) + さ nominalization
  // =============================================================================
  // For adjectives like 明るい, 優しい, 暗い where stem is kanji + hiragana.
  // E.g., 明るさ → 明る (stem) + さ (nominalization suffix)
  // E.g., 優しさ → 優し (stem) + さ (nominalization suffix)
  // E.g., 暖かさ → 暖か (stem) + さ (nominalization suffix)
  //
  // Check if hiragana_part contains "さ" and the prefix forms a valid adjective
  // Handles both final さ (明るさ) and medial さ (気持ちよさそう)
  // Scan for さ positions in hiragana_part (must be at least 2 chars from start)
  for (size_t sa_byte = 3; sa_byte < hiragana_part.size(); sa_byte += 3) {
    if (hiragana_part.substr(sa_byte, 3) != "さ")
      continue;

    // hiragana before さ becomes part of the stem
    std::string stem_suffix = hiragana_part.substr(0, sa_byte);
    std::string stem = kanji_part + stem_suffix;
    std::string base_form = stem + "い";

    SUZUME_DEBUG_LOG_VERBOSE("[ADJ_STEM]   ext_stem pattern: stem=\"" << stem << "\" base=\"" << base_form << "\"\n");

    // Validate: stem+い must be a dictionary i-adjective
    // No inflection fallback — too permissive for kanji+hiragana+さ pattern
    // (would accept nonsense like 像くだい as adjective)
    bool is_dict_adj = isAdjectiveInDictionary(dict_manager, base_form);
    if (!is_dict_adj) {
      SUZUME_DEBUG_LOG_VERBOSE("[ADJ_STEM]   ext_stem: skip (not dict adj)\n");
      continue;
    }

    // Calculate stem end position
    // sa_byte / 3 = number of hiragana chars before さ
    size_t stem_char_count = sa_byte / core::kJapaneseCharBytes;
    size_t stem_end = kanji_end + stem_char_count;

    // Use strong bonus for dictionary-verified compound adjectives
    float cost = is_dict_adj ? candidate::kAdjStemExtCost : candidate::kAdjStemBaseCost;
    SUZUME_DEBUG_LOG("[ADJ_STEM]   ✓ ext_stem candidate stem=\"" << stem << "\" cost=" << cost
                                                                 << " dict=" << is_dict_adj << "\n");
    candidates.push_back(makeIAdjStemCandidate(stem, start_pos, stem_end, base_form, cost, CandidateOrigin::AdjectiveI,
                                               is_dict_adj ? 1.0F : 0.7F, "adj_stem_ext_sa"));
    break;  // Only first valid match
  }

  // =============================================================================
  // Pattern 4: Extended adjective stem (kanji + multi-char hiragana)
  // =============================================================================
  // For adjectives like 懐かしい where the okurigana extends beyond しい.
  // E.g., 懐かしアニメ → 懐かし (ADJ stem) + アニメ (NOUN)
  // E.g., 勇ましい → 勇まし (stem) used in adnominal form
  //
  // Check if kanji + full hiragana_part + い is a dictionary adjective.
  // Only applies when hiragana_part is 2+ chars (Pattern 2 handles 1-char "し").
  if (hiragana_part.size() >= 6) {  // 2+ hiragana chars (6+ bytes)
    std::string stem = kanji_part + hiragana_part;
    std::string base_form = stem + "い";

    bool is_dict_adj = isAdjectiveInDictionary(dict_manager, base_form);
    if (is_dict_adj) {
      float cost = candidate::kAdjStemExtCost;
      SUZUME_DEBUG_LOG("[ADJ_STEM]   ✓ ext_adj candidate stem=\"" << stem << "\" base=\"" << base_form
                                                                  << "\" cost=" << cost << "\n");
      candidates.push_back(makeIAdjStemCandidate(stem, start_pos, hiragana_end, base_form, cost,
                                                 CandidateOrigin::AdjectiveI, 1.0F, "adj_stem_ext_adj"));
    }
  }

  return candidates;
}

void appendIAdjKaroCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t scan_start,
                              size_t scan_end, const grammar::Inflection& inflection,
                              const dictionary::DictionaryManager* dict_manager,
                              std::vector<UnknownCandidate>& candidates) {
  for (size_t karo_pos = scan_start; karo_pos + 1 < scan_end; ++karo_pos) {
    if (karo_pos <= start_pos) {
      continue;  // The stem before かろ must be non-empty
    }
    if (codepoints[karo_pos] != U'か' || codepoints[karo_pos + 1] != U'ろ') {
      continue;
    }
    // Require a following う (推量): Xかろ+う. Without う, Xかろ is far more likely
    // a verb form, so leave it to the verb candidate paths.
    if (karo_pos + 2 >= codepoints.size() || codepoints[karo_pos + 2] != U'う') {
      continue;
    }
    std::string lemma = extractSubstring(codepoints, start_pos, karo_pos) + "い";
    // ない is both the adjective 無い and the negative auxiliary; in the かろ form
    // (〜ではなかろうか) the auxiliary reading dominates, so leave なかろ to the
    // auxiliary path rather than tagging it Adjective.
    if (lemma == "ない") {
      continue;
    }
    // Decisive lexical signal: the reconstructed base is a dictionary adjective,
    // or the inflection analyzer recognizes it as an i-adjective. This rejects the
    // verb-volitional homograph (分かろう → 分か+い is not an adjective).
    bool is_adjective = isAdjectiveInDictionary(dict_manager, lemma);
    if (!is_adjective) {
      for (const auto& cand : inflection.analyze(lemma)) {
        if (cand.verb_type == grammar::VerbType::IAdjective && cand.confidence >= candidate::kHiraAdjConfMin) {
          is_adjective = true;
          break;
        }
      }
    }
    if (!is_adjective) {
      continue;
    }
    UnknownCandidate miz_cand;
    miz_cand.surface = extractSubstring(codepoints, start_pos, karo_pos + 2);
    miz_cand.start = start_pos;
    miz_cand.end = karo_pos + 2;
    miz_cand.pos = core::PartOfSpeech::Adjective;
    miz_cand.lemma = lemma;
    // Verified adjective: make the 未然形 win over fake verb interpretations
    // (ichidan Xかる etc.), mirroring the ke-form handling.
    miz_cand.cost = candidate::verb_cost::kStrongBonus;
    miz_cand.has_suffix = true;                              // Conjugated form (未然ウ接続)
    miz_cand.extended_pos = core::ExtendedPOS::AdjMizenkei;  // For bigram: AdjMizenkei→AuxVolitional
#ifdef SUZUME_DEBUG_INFO
    miz_cand.origin = CandidateOrigin::AdjectiveI;
    miz_cand.confidence = candidate::kIAdjKaroConfidence;
    miz_cand.pattern = "i_adjective_karo";
#endif
    candidates.push_back(std::move(miz_cand));
  }
}

}  // namespace suzume::analysis
