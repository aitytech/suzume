/**
 * @file adjective_candidates_hiragana.cpp
 * @brief Hiragana and katakana i-adjective candidate generation
 */

#include <algorithm>

#include "adjective_candidates.h"
#include "adjective_candidates_internal.h"
#include "analysis/candidate_constants.h"
#include "analysis/scorer_constants.h"
#include "core/debug.h"
#include "core/kana_constants.h"
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

namespace {

// Normalize prolonged sound marks (ー) to vowels based on preceding character
// e.g., すごーい → すごおい, やばーい → やばあい
// Also handles consecutive marks: すごーーい → すごおおい
std::string normalizeProlongedSoundMark(const std::vector<char32_t>& codepoints, size_t start, size_t end) {
  std::string result;
  result.reserve((end - start) * 3);  // Japanese chars are typically 3 bytes

  for (size_t i = start; i < end; ++i) {
    char32_t ch = codepoints[i];

    // Check for prolonged sound mark (ー, U+30FC)
    if (normalize::isProlongedSoundMark(ch) && i > start) {
      // Find the first non-ー character before this position
      char32_t prev = 0;
      for (size_t j = i; j > start; --j) {
        if (!normalize::isProlongedSoundMark(codepoints[j - 1])) {
          prev = codepoints[j - 1];
          break;
        }
      }
      char32_t vowel = grammar::getVowelForChar(prev);
      normalize::encodeUtf8(vowel, result);
    } else {
      normalize::encodeUtf8(ch, result);
    }
  }

  return result;
}

// Check if sequence contains a prolonged sound mark
bool containsProlongedSoundMark(const std::vector<char32_t>& codepoints, size_t start, size_t end) {
  for (size_t i = start; i < end; ++i) {
    if (normalize::isProlongedSoundMark(codepoints[i])) {
      return true;
    }
  }
  return false;
}

// Normalize the base form of an adjective by removing extra vowels created by
// prolonged sound mark normalization.
// Two patterns:
// 1. すごーい → すごおい → すごい (ー before final い)
// 2. かわいー → かわいい → かわいい (ー after い, extending the い)
// For consecutive marks:
// 1. すごーーい → すごおおい → すごい
// 2. かわいーー → かわいいい → かわいい
std::string normalizeBaseForm(const std::string& base_form, const std::vector<char32_t>& original_codepoints,
                              size_t start, size_t end) {
  if (end < start + 2) {
    return base_form;
  }

  // Count total prolonged marks in the original
  size_t choon_count = 0;
  size_t first_choon_pos = 0;
  for (size_t i = start; i < end; ++i) {
    if (normalize::isProlongedSoundMark(original_codepoints[i])) {
      if (choon_count == 0) {
        first_choon_pos = i;
      }
      ++choon_count;
    }
  }

  if (choon_count == 0) {
    return base_form;
  }

  // Get the character before the first ー to determine which vowel was extended
  char32_t prev_char = (first_choon_pos > start) ? original_codepoints[first_choon_pos - 1] : 0;
  char32_t extended_vowel = grammar::getVowelForChar(prev_char);

  // If the extended vowel is い (pattern: かわいー, かわいーー)
  // The base form should always be with double い (かわいい)
  if (extended_vowel == U'い') {
    if (choon_count <= 1) {
      return base_form;  // Single ー after い → keep as is (already correct: かわいい)
    }
    // Multiple ー's after い → remove extra い's
    // かわいいい (from かわいーー) → かわいい
    size_t extra_i_count = choon_count - 1;  // How many extra い's to remove
    size_t extra_i_bytes = extra_i_count * core::kJapaneseCharBytes;
    if (base_form.size() > extra_i_bytes) {
      // Verify the end has multiple い's
      bool all_is = true;
      for (size_t i = 0; i < extra_i_count && all_is; ++i) {
        size_t pos = base_form.size() - (i + 1) * core::kJapaneseCharBytes;
        if (base_form.substr(pos, core::kJapaneseCharBytes) != "い") {
          all_is = false;
        }
      }
      if (all_is) {
        return base_form.substr(0, base_form.size() - extra_i_bytes);
      }
    }
    return base_form;
  }

  // Other vowels (pattern: すごーい → すごおい → すごい)
  // Remove the extra vowels from base form
  std::string vowel_str;
  normalize::encodeUtf8(extended_vowel, vowel_str);
  size_t vowel_bytes = vowel_str.size();
  size_t total_extra_bytes = vowel_bytes * choon_count;

  if (base_form.size() >= total_extra_bytes + core::kJapaneseCharBytes) {
    // Check if base_form ends with (vowel * count) + い
    size_t check_pos = base_form.size() - total_extra_bytes - core::kJapaneseCharBytes;
    std::string_view suffix(base_form.data() + check_pos, total_extra_bytes + core::kJapaneseCharBytes);

    std::string expected_suffix;
    for (size_t i = 0; i < choon_count; ++i) {
      expected_suffix += vowel_str;
    }
    expected_suffix += "い";

    if (suffix == expected_suffix) {
      // Remove the extra vowels, keep the い
      return base_form.substr(0, check_pos) + "い";
    }
  }

  return base_form;
}

// Emit a whole-word i-adjective candidate for a spelled-out reduplicated 〜しい
// adjective (バカバカしい, ばかばかしくない). The doubled stem is otherwise pre-empted
// by an onomatopoeia ADV candidate (aa_doubled / abab_pattern) plus a split-off しい
// tail, so this bypasses the particle-boundary and ending gates the regular scanners
// apply and lets inflection analyze the full surface directly. The caller's existing
// ku/katt/ke trim loops spin the conjugation splits (…しく, …しかっ) out of the emitted
// base. Shared by the hiragana and katakana generators (the kanji path handles its own
// stem-length case), so the reduplication rule lives in one place for all three scripts.
void addReduplicatedShiiAdjective(std::vector<UnknownCandidate>& candidates, const std::vector<char32_t>& codepoints,
                                  size_t start_pos, const std::vector<normalize::CharType>& char_types,
                                  const grammar::Inflection& inflection, CandidateOrigin origin) {
  if (!verb_helpers::isReduplicatedShiiAdjectiveHead(codepoints, start_pos)) {
    return;
  }
  // し and every inflection ending after it are hiragana; scan that run.
  size_t shi_pos = start_pos + 4;
  size_t hira_end = verb_helpers::findCharRegionEnd(char_types, shi_pos, 8, normalize::CharType::Hiragana);
  // Longest-first so the full conjugated surface (…しくない) is chosen; the caller's
  // trim loops then derive …しく. Minimum end covers し + い (base form しい).
  for (size_t end_pos = hira_end; end_pos >= shi_pos + 2; --end_pos) {
    std::string surface = extractSubstring(codepoints, start_pos, end_pos);
    for (const auto& cand : inflection.analyze(surface)) {
      if (cand.verb_type != grammar::VerbType::IAdjective || cand.confidence < candidate::kIAdjConfMin) {
        continue;
      }
      float cost = candidate::confidenceScaledCost(candidate::kKanjiAdjBaseCost, cand.confidence,
                                                   candidate::kKanjiAdjConfScale) +
                   candidate::kReduplicatedShiiAdjBonus;
      auto adj = makeIAdjCandidate(surface, start_pos, end_pos, cand.base_form, cost, origin, cand.confidence,
                                   "i_adjective_reduplicated");
      adj.has_suffix = true;  // Morphologically recognized; skip exceeds_dict_length penalty
      candidates.push_back(std::move(adj));
      return;
    }
  }
}

}  // namespace

std::vector<UnknownCandidate> generateHiraganaAdjectiveCandidates(const std::vector<char32_t>& codepoints,
                                                                  size_t start_pos,
                                                                  const std::vector<normalize::CharType>& char_types,
                                                                  const grammar::Inflection& inflection,
                                                                  const dictionary::DictionaryManager* dict_manager) {
  std::vector<UnknownCandidate> candidates;

  if (start_pos >= char_types.size() || char_types[start_pos] != normalize::CharType::Hiragana) {
    return candidates;
  }

  char32_t first_char = codepoints[start_pos];

  // Skip if first character is を (wo) - this is always a particle, never an adjective stem
  // Unlike other particles (は, か, わ, etc.) that can start valid adjectives,
  // を is exclusively an object marker and never begins a Japanese adjective
  if (first_char == U'を') {
    return candidates;
  }

  // Skip if starting with a small kana (拗音・促音: ゃ/ゅ/ょ/っ/ぁ…). No Japanese
  // word starts with a small kana, so an adjective candidate here would cut
  // through the preceding digraph.
  if (kana::isSmallKanaCodepoint(first_char)) {
    return candidates;
  }

  // Fully spelled-out reduplicated 〜しい adjective (ばかばかしい): the doubled stem is
  // otherwise pre-empted by the abab_pattern ADV candidate and the particle-boundary
  // scan below truncates at the internal か. The trim loops later in this function turn
  // the emitted base into …しく for the negative/adverbial forms.
  addReduplicatedShiiAdjective(candidates, codepoints, start_pos, char_types, inflection,
                               CandidateOrigin::AdjectiveIHiragana);

  // STEP 1: Find maximum hiragana sequence (without breaking at particles)
  // This allows us to analyze the full sequence first for adjectives like
  // はなはだしい, かわいい, わびしい that contain particle characters
  size_t max_hiragana_end = start_pos;
  while (max_hiragana_end < char_types.size() &&
         max_hiragana_end - start_pos < 10) {  // Max 10 chars for adjective + endings
    normalize::CharType curr_type = char_types[max_hiragana_end];
    char32_t curr_char = codepoints[max_hiragana_end];

    // Allow hiragana and prolonged sound mark (ー)
    bool is_valid = (curr_type == normalize::CharType::Hiragana);
    if (!is_valid && normalize::isProlongedSoundMark(curr_char)) {
      is_valid = true;
    }

    if (!is_valid) {
      break;
    }
    ++max_hiragana_end;
  }

  // Need at least 3 characters for an i-adjective (e.g., あつい)
  if (max_hiragana_end <= start_pos + 2) {
    return candidates;
  }

  // Add mizenkei (かろ) conjectural candidates (うれしかろう, よかろう) up front, before
  // the particle-boundary early-returns below: よ / な heads are treated as particle
  // starts and would otherwise skip the かろ generation. The inflection analyzer does
  // not emit this form, and it is gated on a decisive i-adjective base to reject the
  // verb-volitional homograph (わかろう).
  appendIAdjKaroCandidates(codepoints, start_pos, start_pos, max_hiragana_end, inflection, dict_manager, candidates);

  // STEP 2: Determine the hiragana_end for candidate generation
  // If first char is a particle, we only allow the full sequence if it's a valid adjective
  // Otherwise, we break at particle boundaries for shorter subsequences
  size_t hiragana_end = max_hiragana_end;
  bool starts_with_particle = normalize::isExtendedParticle(first_char);
  bool has_prolonged = containsProlongedSoundMark(codepoints, start_pos, max_hiragana_end);

  // For particle-starting sequences without prolonged sound marks,
  // we first check if the full sequence is a valid adjective.
  // If not, we'll skip generating candidates (the lattice will find the particle split)
  size_t valid_adj_min_end = start_pos;  // Minimum end position for a valid adjective
  if (starts_with_particle && !has_prolonged) {
    // Check if the full sequence (or any length) forms a valid adjective
    // Use lower threshold (0.50) for particle-starting sequences to catch
    // words like かわいい (confidence=0.51)
    for (size_t end = max_hiragana_end; end > start_pos + 2; --end) {
      std::string test_surface = extractSubstring(codepoints, start_pos, end);

      // Skip patterns ending with just く (adverbial form)
      // This prevents よろしく, わくわく from being validated as adjectives
      if (utf8::endsWith(test_surface, "く") && !utf8::endsWith(test_surface, "くない")) {
        continue;  // Skip - adverbial form, not adjective (くない is valid negative)
      }

      // Skip patterns ending with just ない (negative auxiliary misidentified as adjective)
      // This prevents でもない from being validated as an adjective
      // Valid patterns: くない (adjective negative), but ない alone after particles is auxiliary
      if (utf8::endsWith(test_surface, "ない") && !utf8::endsWith(test_surface, "くない")) {
        continue;  // Skip - likely negative auxiliary, not adjective
      }

      const auto& test_candidates = inflection.analyze(test_surface);
      for (const auto& cand : test_candidates) {
        if (cand.verb_type == grammar::VerbType::IAdjective && cand.confidence >= candidate::kHiraAdjConfParticle) {
          // For particle-starting sequences, require stem length >= 2 characters
          // This prevents に+そうな from being recognized as にい (invalid)
          // Real adjectives have stems of at least 2 chars: あつい, かわいい, etc.
          if (normalize::utf8Length(cand.stem) < 2) {
            continue;  // Stem too short for a valid adjective
          }
          valid_adj_min_end = end;
          break;
        }
      }
      if (valid_adj_min_end > start_pos) {
        break;  // Found a valid adjective length
      }
    }
    // If no valid adjective found, skip this sequence
    // (the lattice will find a better split with the particle)
    if (valid_adj_min_end == start_pos) {
      return candidates;
    }
    // Use the valid adjective length as hiragana_end
    hiragana_end = valid_adj_min_end;
  } else if (!starts_with_particle) {
    // For non-particle-starting sequences, apply particle boundary breaking
    // This handles cases like おいしい where we don't want to extend past particles
    hiragana_end = start_pos;
    while (hiragana_end < max_hiragana_end) {
      char32_t curr_char = codepoints[hiragana_end];

      // Only break at strong particle boundaries after minimum stem length
      if (hiragana_end - start_pos >= 3 && !normalize::isProlongedSoundMark(curr_char)) {
        bool next_is_prolonged =
            (hiragana_end + 1 < char_types.size() && normalize::isProlongedSoundMark(codepoints[hiragana_end + 1]));
        if (!next_is_prolonged) {
          // か heading the i-adjective past connective かっ (…かった/…かっ) is a
          // conjugation, not the question particle — keep scanning so the whole past
          // form becomes one adjective candidate (うれしかった, たのしかった). Without this
          // the scan truncates at か and only the bare stem (うれし) is emitted, letting
          // a fake godan verb (うれしかう) win. A non-adjective tail is still rejected by
          // the inflection confidence gate below. Exclude なかっ (negative auxiliary past):
          // 〜たくなかった/〜くなかった split as aux (たく|なかっ|た), so a な directly before
          // かっ must still break — the rare ない-family adjective (少なかった) is left to the
          // pre-existing split rather than mis-scored as one token.
          bool is_katt_past = curr_char == U'か' && hiragana_end + 1 < codepoints.size() &&
                              codepoints[hiragana_end + 1] == U'っ' && codepoints[hiragana_end - 1] != U'な';
          if (!is_katt_past && (normalize::isExtendedParticle(curr_char) || curr_char == U'や')) {
            break;  // Stop before the particle
          }
        }
      }
      ++hiragana_end;
    }
  }

  // Need at least 3 characters after determining hiragana_end
  if (hiragana_end <= start_pos + 2) {
    return candidates;
  }

  // Try different lengths, starting from longest
  for (size_t end_pos = hiragana_end; end_pos > start_pos + 2; --end_pos) {
    std::string surface = extractSubstring(codepoints, start_pos, end_pos);

    if (surface.empty()) {
      continue;
    }

    // Skip patterns ending with verb passive/potential/causative negative renyokei
    // 〜られなく, 〜れなく, 〜させなく, 〜せなく, 〜されなく are all verb forms,
    // not i-adjectives. E.g., けられなく = ける + られ + ない
    if (grammar::endsWithPassiveCausativeNegativeRenyokei(surface)) {
      continue;  // Skip - passive/potential/causative negative renyokei
    }
    // Skip patterns ending with 〜かなく (verb negative renyokei of godan verbs)
    // E.g., いかなく = いく + ない
    if (grammar::endsWithGodanNegativeRenyokei(surface)) {
      continue;  // Skip - godan negative renyokei
    }

    // Skip patterns ending with just く (adverbial form of i-adjective)
    // This prevents よろしく, わくわく from being recognized as adjectives.
    // Valid i-adjective endings: い, かった, くない, ければ, さ, そう, etc.
    // Note: くない is valid (negative), but just く is adverbial (not adjective POS)
    if (utf8::endsWith(surface, "く") && !utf8::endsWith(surface, "くない")) {
      continue;  // Skip - just く ending (adverbial form)
    }

    // Skip patterns ending with just ない (negative auxiliary misidentified as adjective)
    // This prevents でもない from being recognized as an adjective when starting with particle
    // Valid patterns: くない (adjective negative), but ない alone after particles is auxiliary
    if (starts_with_particle && utf8::endsWith(surface, "ない") && !utf8::endsWith(surface, "くない")) {
      continue;  // Skip - likely negative auxiliary, not adjective
    }

    // Skip short patterns starting with common case particles (で, に, を, と)
    // These are likely particle + adjective splits (でやばい = で + やばい)
    // Longer sequences (5+ chars) are less likely to be splits
    if (starts_with_particle) {
      size_t char_count = end_pos - start_pos;
      char32_t first_char = codepoints[start_pos];
      // Common case particles that frequently precede adjectives
      bool starts_with_case_particle =
          (first_char == U'で' || first_char == U'に' || first_char == U'を' || first_char == U'と');
      if (starts_with_case_particle && char_count <= 4) {
        continue;  // Skip - likely particle + adjective split
      }
    }

    // Normalize prolonged sound marks before analysis
    // e.g., すごーい → すごおい, やばーい → やばあい
    std::string analysis_surface = surface;
    bool has_prolonged = containsProlongedSoundMark(codepoints, start_pos, end_pos);
    if (has_prolonged) {
      analysis_surface = normalizeProlongedSoundMark(codepoints, start_pos, end_pos);
    }

    // Check all candidates for IAdjective, not just the best one
    // This handles cases where Suru interpretation may have higher confidence
    const auto& all_candidates = inflection.analyze(analysis_surface);
    for (const auto& cand : all_candidates) {
      // For hiragana-only adjectives, require higher confidence (0.55) than
      // kanji+hiragana adjectives (0.50) to avoid false positives like しそう → しい
      // For patterns with prolonged sound marks, lower threshold (0.40) since these
      // are intentional colloquial expressions (すごーい, やばーい)
      // Multiple consecutive marks (すごーーい) result in even lower confidence
      // For particle-starting sequences, lower threshold (0.50) since these have
      // already been validated as forming valid adjectives (はなはだしい, かわいい)
      float confidence_threshold = has_prolonged          ? candidate::kHiraAdjConfProlonged
                                   : starts_with_particle ? candidate::kHiraAdjConfParticle
                                                          : candidate::kHiraAdjConfMin;
      if (cand.confidence >= confidence_threshold && cand.verb_type == grammar::VerbType::IAdjective) {
        // For particle-starting sequences, require stem length >= 2 characters
        // This prevents に+そうな from being recognized as にい (invalid)
        if (starts_with_particle && normalize::utf8Length(cand.stem) < 2) {
          continue;  // Stem too short for a valid adjective
        }
        // A particle followed by a dictionary verb in a complete past/te form is
        // not an adjective: にかかった → に + かかる, mis-reconstructed as the
        // non-word base にかい. Gate on a た/て/だ/で ending so a bare renyokei tail
        // (でかい → で + 買い) cannot fire — those are genuine adjectives, not verbs.
        if (starts_with_particle && !isAdjectiveInDictionary(dict_manager, cand.base_form) &&
            (utf8::endsWith(surface, "た") || utf8::endsWith(surface, "て") || utf8::endsWith(surface, "だ") ||
             utf8::endsWith(surface, "で"))) {
          std::string after_particle = extractSubstring(codepoints, start_pos + 1, end_pos);
          bool tail_is_dict_verb = false;
          for (const auto& vres : inflection.analyze(after_particle)) {
            if (vres.verb_type == grammar::VerbType::IAdjective) {
              continue;
            }
            if (isVerbInDictionary(dict_manager, vres.base_form)) {
              tail_is_dict_verb = true;
              break;
            }
          }
          if (tail_is_dict_verb) {
            SUZUME_DEBUG_LOG_VERBOSE("[HIRA_ADJ_SKIP] \"" << surface << "\" particle + dict verb, skipping\n");
            continue;
          }
        }
        // For prolonged sound mark patterns, require normalized stem >= 2 characters
        // The inflection analyzer works on the choon-expanded form (ばーい → ばあい),
        // so cand.stem may be 2+ chars (ばあ). We must check the final normalized
        // base form: normalizeBaseForm removes the duplicate vowel (ばあい → ばい),
        // giving stem "ば" (1 char) which is too short for a valid adjective.
        // e.g., ばーい → ばあい → ばい → stem "ば" (1 char) = invalid, skip
        //       やばーい → やばあい → やばい → stem "やば" (2 chars) = valid
        if (has_prolonged) {
          std::string normalized_base = normalizeBaseForm(cand.base_form, codepoints, start_pos, end_pos);
          // Stem = base form minus trailing い (3 bytes in UTF-8)
          size_t normalized_stem_len = normalize::utf8Length(normalized_base);
          if (normalized_stem_len >= 1) {
            // Subtract 1 for the trailing い
            if (normalized_stem_len - 1 < 2) {
              continue;  // Normalized stem too short for a valid adjective
            }
          }
        }
        // Skip なさそう pattern - should be split as な(ADJ stem) + さ(Suffix) + そう(AUX)
        // This pattern is the nominalization of ない + そう (appearance auxiliary)
        // The inflection analyzer incorrectly treats なさ as stem of なさい (honorific)
        // Check: surface ends with さそう AND (stem ends with さ OR surface is exactly なさそう)
        if (utf8::endsWith(surface, "さそう")) {
          // Check if this is the な+さ+そう pattern (ない nominalization)
          // Pattern: 1 char before さそう (like なさそう where な is the ない stem)
          size_t surface_len = normalize::utf8Length(surface);
          if (surface_len == 4 && utf8::endsWith(cand.stem, "さ")) {
            continue;  // Skip - should be split as な+さ+そう
          }
        }
        // Skip んかった pattern - this is contracted negative (ん) + past (かった)
        // e.g., らんかった would create らんい which is invalid
        // くだらんかった should be くだら+ん+かっ+た, not くだ+らんかっ+た
        if (utf8::endsWith(surface, "んかった")) {
          continue;  // Skip - should be split as ん+かっ+た
        }
        // Skip surfaces that are honorific verb renyokei (ending with さ)
        // e.g., くださ + い = ください is VERB (くださる renyokei), not i-adjective
        // These are typically honorific verb conjugations ending with さ
        if (surface == "くださ" || surface == "なさ" || surface == "いらっしゃ" || surface == "おっしゃ" ||
            surface == "ござ") {
          continue;  // Skip - honorific verb renyokei, not i-adjective
        }
        // Skip hiragana patterns ending with たい - these are verb renyokei + tai (desire)
        // e.g., ねたい should be ね + たい (寝たい), not i-adjective
        //       みたい context-dependent: auxiliary (見たい/似たい) vs mimetic (みたいな)
        //       したい should be し + たい, not i-adjective
        // Note: 痛い (itai) has kanji, so not affected
        if (utf8::endsWith(surface, "たい") && surface != "たい") {
          continue;  // Skip - should be split as verb renyokei + たい
        }
        // Skip pure hiragana patterns ending with さ - these are almost always part of
        // honorific suffix さん/さま, not i-adjective forms
        // e.g., おじさ, おばさ, おねえさ should not be recognized as i-adjectives
        if (utf8::endsWith(surface, "さ") && surface.size() >= 9) {  // 3+ chars ending with さ
          continue;  // Skip - likely part of honorific suffix さん/さま
        }
        // Skip さそう patterns (adj nominalization + appearance auxiliary)
        // e.g., よさそうに → よ + さ + そう + に, not よさい (invalid adj)
        //       なさそう → な + さ + そう (handled separately)
        if (utf8::endsWith(surface, "さそう") || utf8::endsWith(surface, "さそうに") ||
            utf8::endsWith(surface, "さそうな") || utf8::endsWith(surface, "さそうだ")) {
          continue;  // Skip - should be split as adj-stem + さ + そう
        }
        // Skip candidates containing て/で in stem - indicates verb te-form boundary
        // No genuine i-adjective has て or で in its stem
        // e.g., さましてほしい should be さまし+て+ほしい, not a single i-adj
        {
          auto stem = surface.substr(0, surface.size() - 3);  // Remove trailing い (3 bytes)
          if (stem.find("て") != std::string::npos || stem.find("で") != std::string::npos) {
            continue;
          }
        }
        // Skip adj renyokei + なる patterns — these are adj く-form + auxiliary verb なる
        // e.g., なくなった = なく(adj renyokei) + なっ(なる) + た, not a single adjective
        //       よくなった = よく(adj renyokei) + なっ(なる) + た
        if (verb_helpers::containsKuNaruPattern(surface)) {
          continue;
        }
        // Base cost for hiragana i-adjective candidates
        // Use slightly elevated base to avoid fragments like ろしい beating
        // kanji adjectives like 恐ろしい (kanji adj base=0.2F)
        float cost = candidate::confidenceScaledCost(candidate::kHiraganaAdjBaseCost, cand.confidence,
                                                     candidate::kHiraganaAdjConfScale);
        if (has_prolonged) {
          cost += candidate::kProlongedSoundBonus;  // Bonus for colloquial patterns like すごーい
          SUZUME_DEBUG_LOG_VERBOSE("[COST_ADJ] \"" << surface << "\" -0.1 (prolonged_sound_bonus)\n");
        }
        // Length-based bonus for adjectives starting with particle characters
        // Short sequences (3-4 chars like につい, でやばい) are likely splits
        // Longer sequences (5+ chars like かわいい, はなはだしい) are real adjectives
        if (starts_with_particle) {
          size_t char_count = end_pos - start_pos;
          if (char_count >= 5) {
            cost += candidate::kLongParticleAdjBonus;  // Strong bonus for long adjectives (はなはだしい)
            SUZUME_DEBUG_LOG_VERBOSE("[COST_ADJ] \"" << surface << "\" -0.5 (long_particle_adj_bonus)\n");
          }
          // No bonus for 3-4 char sequences (につい, でやばい) - likely particle + adjective split
        }
        // Set lemma to base form from inflection analysis
        // For prolonged sound mark patterns, normalize the base form
        // e.g., すごおい → すごい, やばあい → やばい
        std::string lemma =
            has_prolonged ? normalizeBaseForm(cand.base_form, codepoints, start_pos, end_pos) : cand.base_form;
        const char* pattern = has_prolonged ? "i_adjective_hira_choon" : "i_adjective_hira";
        candidates.push_back(makeIAdjCandidate(surface, start_pos, end_pos, lemma, cost,
                                               CandidateOrigin::AdjectiveIHiragana, cand.confidence, pattern));
        break;  // Only add one adjective candidate per surface
      }
    }
  }

  // Add emphatic variants (まずい → まずいっ, etc.)
  addEmphaticVariants(candidates, codepoints);

  // Add ku-form candidates for kunai patterns
  // This enables MeCab-compatible split: しんどくない → しんどく + ない
  // For each candidate ending with くない, generate a ku-form variant ending with く
  std::vector<UnknownCandidate> ku_form_candidates;
  for (const auto& cand : candidates) {
    // Check if surface ends with くない
    if (utf8::endsWith(cand.surface, "くない")) {
      // Generate ku-form variant: しんどくない → しんどく
      ku_form_candidates.push_back(makeTrimmedAdjVariant(cand, 2, candidate::kAdjKuSplitBonusWeak,
                                                         core::ExtendedPOS::AdjRenyokei, "i_adjective_hira_ku"));
    }
    // Also check for くなかった pattern (negative past)
    // E.g., 良くなかった → 良く + なかった
    else if (utf8::endsWith(cand.surface, "くなかった")) {
      // Generate ku-form variant: 良くなかった → 良く
      ku_form_candidates.push_back(makeTrimmedAdjVariant(cand, 4, candidate::kAdjKuSplitBonusWeak,
                                                         core::ExtendedPOS::AdjRenyokei, "i_adjective_ku_nakatta"));
    }
    // Also check for くなかっ pattern (negative past before た)
    // E.g., 良くなかっ → 良く + なかっ
    else if (utf8::endsWith(cand.surface, "くなかっ")) {
      // Generate ku-form variant: 良くなかっ → 良く
      ku_form_candidates.push_back(makeTrimmedAdjVariant(cand, 3, candidate::kAdjKuSplitBonusWeak,
                                                         core::ExtendedPOS::AdjRenyokei, "i_adjective_ku_nakatt"));
    }
  }

  // Add all ku-form candidates
  for (auto& var : ku_form_candidates) {
    candidates.push_back(std::move(var));
  }

  // Add katt-form candidates for katta patterns (BUG-036)
  // This enables MeCab-compatible split: よかったです → よかっ + た + です
  // For each candidate ending with かった, generate a katt-form variant ending with かっ
  std::vector<UnknownCandidate> katt_form_candidates;
  for (const auto& cand : candidates) {
    // Check if surface ends with かった (i-adjective past form)
    if (utf8::endsWith(cand.surface, "かった")) {
      // Skip んかった pattern - this is contracted negative (ん) + past (かった)
      // e.g., くだらんかった = くだら+ん+かっ+た, NOT くだ+らんかっ+た
      if (utf8::endsWith(cand.surface, "んかった")) {
        continue;
      }
      // Generate katt-form variant: よかった → よかっ (連用タ接続; AdjKatt→AuxTenseTa)
      katt_form_candidates.push_back(makeTrimmedAdjVariant(cand, 1, candidate::kAdjKattSplitBonus,
                                                           core::ExtendedPOS::AdjKatt, "i_adjective_hira_katt"));
    }
  }

  // Add all katt-form candidates
  for (auto& var : katt_form_candidates) {
    candidates.push_back(std::move(var));
  }

  // Add ke-form candidates for kereba patterns
  // This enables MeCab-compatible split: よければ → よけれ + ば
  // For each candidate ending with ければ, generate a ke-form variant ending with けれ
  std::vector<UnknownCandidate> ke_form_candidates;
  for (const auto& cand : candidates) {
    // Check if surface ends with ければ (i-adjective conditional form)
    if (utf8::endsWith(cand.surface, "ければ")) {
      // Generate ke-form variant: よければ → よけれ (仮定形; AdjKeForm→ParticleConj)
      ke_form_candidates.push_back(makeTrimmedAdjVariant(cand, 1, candidate::kAdjKeSplitBonus,
                                                         core::ExtendedPOS::AdjKeForm, "i_adjective_hira_kere"));
    }
  }

  // Add all ke-form candidates
  for (auto& var : ke_form_candidates) {
    candidates.push_back(std::move(var));
  }

  // Add stem candidates for pure hiragana adjective + auxiliary patterns
  // This handles patterns like おいしそう → おいし (stem) + そう (aux)
  // Similar to the kanji adjective stem logic at lines 1673-1785
  // Check for しそう, しすぎ patterns (adjective stem + auxiliary)
  static const std::vector<std::string_view> kHiraStemAuxPatterns = {
      "しそう",    // appearance: おいしそう
      "しそうだ",  // appearance + copula
      "しそうな",  // attributive
      "しそうに",  // adverbial
      "しすぎ",    // excessive: おいしすぎ
      "しすぎる",  // excessive + dictionary form
  };

  // Start from maximum hiragana sequence
  std::string full_surface = extractSubstring(codepoints, start_pos, max_hiragana_end);
  for (const auto& aux_pattern : kHiraStemAuxPatterns) {
    if (full_surface.size() >=
            aux_pattern.size() + core::kTwoJapaneseCharBytes &&  // Need at least 2 chars before pattern
        full_surface.find(aux_pattern) != std::string::npos) {
      // Find where the pattern starts
      size_t pattern_pos = full_surface.find(aux_pattern);
      if (pattern_pos < core::kTwoJapaneseCharBytes) {
        continue;  // Stem too short (need at least 2 chars like おいし, うれし)
      }

      // The stem is everything before the auxiliary pattern, including the し
      std::string stem = full_surface.substr(0, pattern_pos + 3);  // +3 for し
      std::string base_form = stem + "い";                         // e.g., おいし → おいしい

      // Validate that this forms a valid i-adjective
      const auto& adj_results = inflection.analyze(base_form);
      const float adj_confidence =
          adj_detail::firstConfidenceAtLeast(adj_results, grammar::VerbType::IAdjective, candidate::kIAdjConfMin);

      if (adj_confidence == 0.0F) {
        continue;
      }

      // Check that this is NOT a verb renyokei (e.g., 話し from 話す)
      // For pure hiragana, check if stem + す would be a valid verb
      // We compare adjective vs verb confidence - if adjective is significantly higher, prefer it
      std::string verb_stem = stem.substr(0, stem.size() - 3);  // Remove し
      std::string verb_form = verb_stem + "す";                 // e.g., おい + す = おいす (not real)

      // Check verb confidence from inflection analyzer
      const auto& verb_results = inflection.analyze(verb_form);
      const float verb_confidence =
          adj_detail::maxConfidenceFor(verb_results, {grammar::VerbType::GodanSa, grammar::VerbType::Suru});

      // Require adjective confidence to be higher than verb confidence
      // This filters out false positives like 話しそう (話す renyokei + そう)
      // but keeps valid adjectives like おいしそう (おいしい stem + そう)
      // Note: Both おいしい (0.66) and おいす (0.62) have similar confidence,
      // so we just need adj >= verb for pure hiragana patterns.
      if (verb_confidence > 0.0F && adj_confidence < verb_confidence) {
        SUZUME_DEBUG_LOG_VERBOSE("[ADJ_STEM_HIRA] skip: adj_conf=" << adj_confidence << " verb_conf=" << verb_confidence
                                                                   << "\n");
        continue;  // Verb confidence higher, likely verb renyokei
      }

      // Calculate position
      size_t stem_char_count = normalize::utf8Length(stem);
      size_t stem_end = start_pos + stem_char_count;

      // Generate stem candidate with strong bonus
      // おい (INTJ) has cost -1, so stem needs very low cost to win
      float cost =
          candidate::confidenceScaledCost(candidate::kAdjStemExtCost, adj_confidence, candidate::kAdjStemConfScale);
      SUZUME_DEBUG_LOG("[ADJ_STEM_HIRA] ✓ candidate stem=\"" << stem << "\" base=\"" << base_form << "\" cost=" << cost
                                                             << "\n");
      candidates.push_back(makeIAdjStemCandidate(stem, start_pos, stem_end, base_form, cost,
                                                 CandidateOrigin::AdjectiveIHiragana, adj_confidence,
                                                 "adj_stem_hira_sou"));
      break;  // Only one stem candidate per pattern
    }
  }

  // Sort by cost
  verb_helpers::sortCandidatesByCost(candidates);

  return candidates;
}

std::vector<UnknownCandidate> generateKatakanaAdjectiveCandidates(const std::vector<char32_t>& codepoints,
                                                                  size_t start_pos,
                                                                  const std::vector<normalize::CharType>& char_types,
                                                                  const grammar::Inflection& inflection) {
  std::vector<UnknownCandidate> candidates;

  // Only process katakana-starting positions
  if (start_pos >= char_types.size() || char_types[start_pos] != normalize::CharType::Katakana) {
    return candidates;
  }

  // Find katakana portion (1-6 characters for slang adjective stems)
  // e.g., エモ, キモ, ウザ, ダサ, etc.
  size_t kata_end = findCharRegionEnd(char_types, start_pos, 6, normalize::CharType::Katakana);

  // Need at least 1 katakana character
  if (kata_end == start_pos) {
    return candidates;
  }

  // Fully spelled-out reduplicated 〜しい adjective (バカバカしい): the doubled katakana
  // stem is otherwise pre-empted by the aa_doubled ADV candidate, and its しい ending
  // starts with し, which the ending gate below rejects. Emit the whole-word adjective
  // here; the trim loops after the main loop derive the …しく split.
  addReduplicatedShiiAdjective(candidates, codepoints, start_pos, char_types, inflection, CandidateOrigin::AdjectiveI);

  // The main loop only runs for a katakana stem followed by a valid i-adjective ending
  // start. When it does not apply (e.g. the reduplicated しい handled above), fall through
  // to the shared emphatic/trim/sort tail so the emitted candidate still gets its splits.
  if (kata_end < char_types.size() && char_types[kata_end] == normalize::CharType::Hiragana) {
    // I-adjective endings: い, か(った), く(ない/て), け(れば), さ(そう), そ(う) etc.
    char32_t first_hira = codepoints[kata_end];
    size_t kata_len = kata_end - start_pos;
    bool valid_ending_start = (first_hira == U'い' || first_hira == U'か' || first_hira == U'く' ||
                               first_hira == U'け' || first_hira == U'さ' || first_hira == U'そ');
    // For さ (nominalization), restrict to short katakana stems (2 chars max)
    // Valid: エモさ, キモさ, ウザさ, ダサさ (2-char stems)
    // Invalid: レイプさ (3-char stem, レイプい doesn't exist)
    if (valid_ending_start && !(first_hira == U'さ' && kata_len > 2)) {
      // Find hiragana portion (up to 8 chars for conjugation endings)
      size_t hira_end = findCharRegionEnd(char_types, kata_end, 8, normalize::CharType::Hiragana);

      // Try different ending lengths, starting from longest
      for (size_t end_pos = hira_end; end_pos > kata_end; --end_pos) {
        std::string surface = extractSubstring(codepoints, start_pos, end_pos);

        if (surface.empty()) {
          continue;
        }

        // Check all candidates for IAdjective
        const auto& all_candidates = inflection.analyze(surface);
        for (const auto& cand : all_candidates) {
          // Require confidence >= 0.5 for i-adjectives
          if (cand.confidence >= candidate::kIAdjConfMin && cand.verb_type == grammar::VerbType::IAdjective) {
            // Lower cost than pure katakana noun to prefer adjective reading
            // Cost: 0.2-0.35 based on confidence (lower = better)
            float cost = candidate::confidenceScaledCost(candidate::kKanjiAdjBaseCost, cand.confidence,
                                                         candidate::kKanjiAdjConfScale);
            auto adj_cand = makeIAdjCandidate(surface, start_pos, end_pos, cand.base_form, cost,
                                              CandidateOrigin::AdjectiveI, cand.confidence, "i_adjective_kata");
            // Skip exceeds_dict_length penalty - this is a morphologically recognized pattern
            adj_cand.has_suffix = true;
            candidates.push_back(std::move(adj_cand));
            break;  // Only add one adjective candidate per surface
          }
        }
      }
    }
  }

  // Add emphatic variants (エグい → エグいっ, etc.)
  addEmphaticVariants(candidates, codepoints);

  // Add katt-form candidates for katta patterns
  // This enables MeCab-compatible split: エモかった → エモかっ + た
  std::vector<UnknownCandidate> katt_form_candidates;
  for (const auto& cand : candidates) {
    if (utf8::endsWith(cand.surface, "かった")) {
      katt_form_candidates.push_back(makeTrimmedAdjVariant(cand, 1, candidate::kAdjKattSplitBonus,
                                                           core::ExtendedPOS::AdjKatt, "i_adjective_kata_katt"));
    }
  }
  for (auto& var : katt_form_candidates) {
    candidates.push_back(std::move(var));
  }

  // Add ku-form candidates for kute patterns (te-form split)
  // This enables MeCab-compatible split: ウザくて → ウザく + て
  std::vector<UnknownCandidate> ku_te_candidates;
  for (const auto& cand : candidates) {
    if (utf8::endsWith(cand.surface, "くて")) {
      // AdjRenyokei→ParticleConj
      ku_te_candidates.push_back(makeTrimmedAdjVariant(cand, 1, candidate::kAdjKuSplitBonus,
                                                       core::ExtendedPOS::AdjRenyokei, "i_adjective_kata_ku_te"));
    }
  }
  for (auto& var : ku_te_candidates) {
    candidates.push_back(std::move(var));
  }

  // Add ku-form candidates for kunai patterns (negative form split)
  // This enables MeCab-compatible split: エモくない → エモく + ない
  std::vector<UnknownCandidate> ku_nai_candidates;
  for (const auto& cand : candidates) {
    if (utf8::endsWith(cand.surface, "くない")) {
      // AdjRenyokei→AuxNegativeNai
      ku_nai_candidates.push_back(makeTrimmedAdjVariant(cand, 2, candidate::kAdjKuSplitBonusWeak,
                                                        core::ExtendedPOS::AdjRenyokei, "i_adjective_kata_ku_nai"));
    }
  }
  for (auto& var : ku_nai_candidates) {
    candidates.push_back(std::move(var));
  }

  // Add ke-form candidates for kereba patterns
  // This enables MeCab-compatible split: エモければ → エモけれ + ば
  std::vector<UnknownCandidate> ke_form_candidates;
  for (const auto& cand : candidates) {
    if (utf8::endsWith(cand.surface, "ければ")) {
      ke_form_candidates.push_back(makeTrimmedAdjVariant(cand, 1, candidate::kAdjKeSplitBonus,
                                                         core::ExtendedPOS::AdjKeForm, "i_adjective_kata_kere"));
    }
  }
  for (auto& var : ke_form_candidates) {
    candidates.push_back(std::move(var));
  }

  // Add stem candidates for sou patterns (appearance auxiliary)
  // This enables MeCab-compatible split: キモそう → キモ + そう
  std::vector<UnknownCandidate> stem_sou_candidates;
  for (const auto& cand : candidates) {
    if (utf8::endsWith(cand.surface, "そう")) {
      // Skip if trimming そう would leave an empty stem.
      if (cand.surface.size() <= core::kTwoJapaneseCharBytes) {
        continue;
      }
      // Trim そう (2 chars) → AdjStem for the AdjStem→AuxAppearanceSou bigram.
      stem_sou_candidates.push_back(makeTrimmedAdjVariant(cand, 2, candidate::kAdjStemSplitBonus,
                                                          core::ExtendedPOS::AdjStem, "i_adjective_kata_stem_sou"));
    }
  }
  for (auto& var : stem_sou_candidates) {
    candidates.push_back(std::move(var));
  }

  // Sort by cost
  verb_helpers::sortCandidatesByCost(candidates);

  return candidates;
}

}  // namespace suzume::analysis
