/**
 * @file verb_candidates_hiragana_mizenkei.cpp
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

// Godan mizenkei forms derived from an a-row (未然形) ending. Shared by the
// ん / ない / なきゃ mizenkei candidate loops, which each add their own
// validation, cost, and skip rules on top of this common derivation.
struct GodanMizenkeiForms {
  char32_t a_row_char;
  grammar::VerbType verb_type;
  std::string_view base_suffix;
  std::string mizenkei_surface;
  std::string stem;
  std::string base_form;
};

// Derive the godan mizenkei forms for the a-row char at codepoints[mizenkei_end-1].
// Returns false when that char is not a recognized a-row godan mizenkei ending.
bool deriveGodanMizenkeiForms(const std::vector<char32_t>& codepoints, size_t start_pos, size_t mizenkei_end,
                              GodanMizenkeiForms& out) {
  out.a_row_char = codepoints[mizenkei_end - 1];
  if (!grammar::isARowCodepoint(out.a_row_char)) {
    return false;
  }
  out.verb_type = grammar::verbTypeFromARowCodepoint(out.a_row_char);
  out.base_suffix = grammar::godanBaseSuffixFromARow(out.a_row_char);
  if (out.verb_type == grammar::VerbType::Unknown || out.base_suffix.empty()) {
    return false;
  }
  out.mizenkei_surface = extractSubstring(codepoints, start_pos, mizenkei_end);
  out.stem = extractSubstring(codepoints, start_pos, mizenkei_end - 1);
  out.base_form = out.stem + std::string(out.base_suffix);
  return true;
}

// Detect a formal-noun prefix boundary inside an unverified hiragana verb stem.
// A stem that begins with a dictionary formal noun (わけ, こと, もの, ところ, ...)
// followed by a remainder of two or more characters is usually a noun + verb
// sequence (わけ + わから), not a single verb (わけわかる is not a word).
// Formal nouns form independent word boundaries, so callers add a
// split-preference penalty when this returns true.
bool hasFormalNounPrefixBoundary(const dictionary::DictionaryManager* dict_manager,
                                 const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos) {
  if (dict_manager == nullptr || end_pos <= start_pos) {
    return false;
  }
  const size_t total_len = end_pos - start_pos;
  // Both the noun prefix and the verb remainder need at least two characters
  if (total_len < 4) {
    return false;
  }
  for (size_t prefix_len = 2; prefix_len + 2 <= total_len; ++prefix_len) {
    std::string prefix = extractSubstring(codepoints, start_pos, start_pos + prefix_len);
    auto results = dict_manager->lookup(prefix, 0);
    for (const auto& result : results) {
      if (result.entry != nullptr && result.entry->surface == prefix && result.entry->pos == core::PartOfSpeech::Noun &&
          result.entry->extended_pos == core::ExtendedPOS::NounFormal) {
        return true;
      }
    }
  }
  return false;
}

// True when an unverified hiragana mizenkei surface is really [verb] + [adverbial
// particle] rather than a single verb. しか is a 副助詞 and there is no godan verb
// 〜しく, so みるしか / やるしか must split as verb + しか, never be fabricated as the
// 未然形 of the non-word 〜しく (which would then absorb しか and connect cheaply to
// ない). The suffix is matched against ParticleAdverbial only (しか/とか) so that
// case/final particles that legitimately follow a real mizenkei are never swept
// up. A single-mora prefix (る in るしか) is a bare verb-ending fragment, never a
// word, so the whole candidate is garbage; a longer prefix must itself be a verb.
// @see fabricated closed-class absorption guards (verb_candidates_helpers.h)
bool endsWithParticleAfterVerb(const dictionary::DictionaryManager* dict_manager, const grammar::Inflection& inflection,
                               const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos) {
  if (dict_manager == nullptr || end_pos <= start_pos) {
    return false;
  }
  const size_t total_len = end_pos - start_pos;
  if (total_len < 3) {  // need a 1+ char prefix and a 2+ char particle suffix
    return false;
  }
  for (size_t prefix_len = 1; prefix_len + 2 <= total_len; ++prefix_len) {
    size_t split = start_pos + prefix_len;
    std::string suffix = extractSubstring(codepoints, split, end_pos);
    const dictionary::DictionaryEntry* suffix_entry = dict_manager->lookupExact(suffix);
    if (suffix_entry == nullptr || suffix_entry->extended_pos != core::ExtendedPOS::ParticleAdverbial) {
      continue;
    }
    if (prefix_len == 1) {
      return true;
    }
    // Probe the prefix for a verb. Strip a leading te-form particle first, since
    // て/で + verb + しか (てみるしか = て + みる + しか) is a subsidiary-verb
    // sequence whose verb sits after て.
    size_t probe_start = start_pos;
    if (codepoints[start_pos] == U'て' || codepoints[start_pos] == U'で') {
      probe_start = start_pos + 1;
    }
    std::string probe = extractSubstring(codepoints, probe_start, split);
    if (vh::isVerbInDictionary(dict_manager, probe)) {
      return true;
    }
    const auto& analysis = inflection.analyze(probe);
    if (!analysis.empty() && analysis[0].verb_type != grammar::VerbType::Unknown &&
        analysis[0].confidence >= candidate::verb_cost::kConstructedVerbMinConfidence) {
      return true;
    }
  }
  return false;
}

// True when a pronoun word ends exactly at @p pos (誰/何/だれ/なに/どこ/いつ …).
// A following か is then the particle か (誰か + いる), never the 2nd mora of a
// godan-wa verb stem, so a か…renyokei candidate at @p pos must be discouraged.
bool pronounEndsAt(const dictionary::DictionaryManager* dict_manager, const std::vector<char32_t>& codepoints,
                   size_t pos) {
  if (dict_manager == nullptr || pos == 0) {
    return false;
  }
  const size_t max_len = pos < 3 ? pos : 3;
  for (size_t len = 1; len <= max_len; ++len) {
    std::string word = extractSubstring(codepoints, pos - len, pos);
    for (const auto& result : dict_manager->lookup(word, 0)) {
      if (result.entry != nullptr && result.length == len && result.entry->pos == core::PartOfSpeech::Pronoun) {
        return true;
      }
    }
  }
  return false;
}

// Passive mizenkei candidates for pure-hiragana verbs (いわれる → いわ + れる).
// Splits at the A-row mizenkei before れ for MeCab compatibility.
void appendPassiveMizenkeiCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t hiragana_end,
                                     const grammar::Inflection& inflection,
                                     const dictionary::DictionaryManager* dict_manager,
                                     std::vector<UnknownCandidate>& candidates) {
  // Key insight: A-row hiragana (わ,か,さ,た,な,ま,ら,が,etc.) + れ pattern
  for (size_t end_pos = hiragana_end; end_pos > start_pos + 2; --end_pos) {
    // Check if position end_pos-1 is A-row hiragana (mizenkei ending)
    // and position end_pos is れ (passive pattern start)
    size_t mizenkei_end = end_pos - 1;  // Position after A-row char
    if (mizenkei_end <= start_pos)
      continue;

    char32_t a_row_char = codepoints[mizenkei_end - 1];  // The A-row character
    char32_t next_char = codepoints[mizenkei_end];       // Should be れ

    // Check for A-row followed by passive pattern (れる, れた, れて, etc.)
    if (!grammar::isARowCodepoint(a_row_char) || next_char != U'れ') {
      continue;
    }

    // Check for passive patterns after れ
    // All passive patterns split at mizenkei (いわ + れる/れ) for MeCab compatibility
    // Loose ま-branch: bare ま (れます, れました, れません, れませんでした) qualifies.
    bool is_passive_pattern = vh::isPassiveAuxContinuation(codepoints, mizenkei_end + 1, /*strict_masu=*/false);

    if (!is_passive_pattern) {
      continue;
    }

    // Derive VerbType from the A-row ending (e.g., わ → GodanWa)
    grammar::VerbType verb_type = grammar::verbTypeFromARowCodepoint(a_row_char);
    if (verb_type == grammar::VerbType::Unknown) {
      continue;
    }

    // Get base suffix (e.g., わ → う for GodanWa)
    std::string_view base_suffix = grammar::godanBaseSuffixFromARow(a_row_char);
    if (base_suffix.empty()) {
      continue;
    }

    // Construct base form and mizenkei surface
    // E.g., for いわれる: mizenkei = いわ, stem = い, base_suffix = う → base_form = いう
    std::string mizenkei_surface = extractSubstring(codepoints, start_pos, mizenkei_end);
    std::string stem = extractSubstring(codepoints, start_pos, mizenkei_end - 1);
    std::string base_form = stem + std::string(base_suffix);

    // Check if mizenkei surface exists in dictionary as a verb
    // This handles cases like いわ which is registered with lemma いう
    // OR check if base form exists (for kanji compounds like 言わ)
    bool is_valid_verb = vh::isVerbInDictionary(dict_manager, mizenkei_surface);
    if (!is_valid_verb) {
      // Fallback: check the reconstructed base form (kanji compounds like 言わ→言う)
      is_valid_verb = vh::isVerbInDictionary(dict_manager, base_form);
    }
    // Fallback for GodanSa: use inflection analysis for causative verb patterns
    // E.g., やらされた = やらさ (mizenkei of やらす) + れ + た
    // やらす is the causative form of やる but not in dictionary
    if (!is_valid_verb && verb_type == grammar::VerbType::GodanSa) {
      is_valid_verb = vh::isVerifiedVerbBase(dict_manager, inflection, base_form,
                                             candidate::verb_cost::kConstructedVerbMinConfidence, true);
    }

    if (!is_valid_verb) {
      continue;
    }

    // For GodanSa passive, check if this is causative+passive pattern
    // E.g., やらさ+れ+た = causative+passive of やる (not passive of やらす)
    // If the stem (without さ) is a valid godan verb mizenkei, penalize
    // the merged candidate so the decomposed path (やら+さ+れ+た) can compete
    float causative_passive_penalty = 0.0F;
    if (verb_type == grammar::VerbType::GodanSa && dict_manager != nullptr && mizenkei_end - start_pos >= 2) {
      char32_t stem_last = codepoints[mizenkei_end - 2];
      auto inner_suffix = grammar::godanBaseSuffixFromARow(stem_last);
      if (!inner_suffix.empty()) {
        std::string inner_stem = extractSubstring(codepoints, start_pos, mizenkei_end - 2);
        std::string inner_base = inner_stem + std::string(inner_suffix);
        if (vh::isVerbInDictionary(dict_manager, inner_base)) {
          causative_passive_penalty = bigram_cost::kStrong;
        }
      }
    }

    // Skip GodanRa passive for known ichidan verbs (いる, きる, ねる, etc.)
    // These 2-char verbs ending in る are ichidan, not godan-ra.
    // The ichidan path (い+られる) is handled separately.
    // Only skip when stem is 1 hiragana char (3 bytes) = base form is 2 chars (6 bytes)
    if (verb_type == grammar::VerbType::GodanRa && stem.size() == 3) {
      // Known ichidan 2-char verbs (stem is 1 char before ら):
      // いる, きる, みる, ねる, でる, にる, ひる, etc.
      // Godan-ra 2-char verbs: やる, なる, ある, とる, のる, etc.
      char32_t stem_char = codepoints[start_pos];
      // ichidan stems: い,き,み,ね,で,に,ひ,び (E-row or I-row before る)
      bool is_known_ichidan =
          (stem_char == U'い' || stem_char == U'き' || stem_char == U'み' || stem_char == U'ね' || stem_char == U'で' ||
           stem_char == U'に' || stem_char == U'ひ' || stem_char == U'び' || stem_char == U'え');
      if (is_known_ichidan) {
        continue;
      }
    }

    // Get lemma from dictionary entry if mizenkei is registered
    // Otherwise use constructed base form
    std::string lemma = vh::lookupVerbLemma(dict_manager, mizenkei_surface, base_form);

    // Always split at mizenkei (いわ + れる/れ) for MeCab compatibility
    // MeCab splits: いわれません → いわ + れ + ませ + ん (4 tokens)
    // Previous strategy of splitting at passive renyokei (いわれ + ません) was incorrect
    size_t split_end = mizenkei_end;
    std::string surface = extractSubstring(codepoints, start_pos, split_end);
    const char* pattern_name = "passive_mizenkei";

    float cost = candidate::verb_cost::kStandardBonus + causative_passive_penalty;
    SUZUME_DEBUG_VERBOSE_BLOCK {
      SUZUME_DEBUG_STREAM << "[VERB_CAND] " << surface << " hiragana_" << pattern_name << " lemma=" << lemma
                          << " cost=" << cost << "\n";
    }
    candidates.push_back(makeVerbCandidate(surface, start_pos, split_end, cost, lemma,
                                           grammar::verbTypeToConjType(verb_type), true, CandidateOrigin::VerbHiragana,
                                           0.9F, "hiragana_passive_mizenkei"));
    break;  // Only generate one passive candidate per length
  }
}

// Ichidan renyokei candidates before られ (potential/passive) for pure hiragana
// (いられる → い + られる). Splits at the ichidan stem.
void appendIchidanRareruCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t hiragana_end,
                                   const grammar::Inflection& inflection,
                                   const dictionary::DictionaryManager* dict_manager,
                                   std::vector<UnknownCandidate>& candidates) {
  // Pattern: ichidan stem (E-row ending or い/え) + られ + る/た/て
  // Search for られ starting at positions from start_pos+1 to hiragana_end-2
  for (size_t ra_pos = start_pos + 1; ra_pos + 2 < hiragana_end; ++ra_pos) {
    // Check for られ pattern at this position
    if (codepoints[ra_pos] != U'ら' || codepoints[ra_pos + 1] != U'れ') {
      continue;
    }

    // Check for られる, られた, られて, られな, られま patterns
    // れ sits at ra_pos+1, so the continuation index is ra_pos+2 (loose ま-branch).
    bool is_potential_passive_pattern = vh::isPassiveAuxContinuation(codepoints, ra_pos + 2, /*strict_masu=*/false);

    if (!is_potential_passive_pattern) {
      continue;
    }

    // The stem is everything before ら
    size_t stem_end = ra_pos;  // Exclusive end of stem
    if (stem_end <= start_pos)
      continue;

    std::string stem = extractSubstring(codepoints, start_pos, stem_end);

    // Skip stems containing て or で - these are te-form + subsidiary verb patterns
    // E.g., しておられた → stem=してお is actually して(te-form)+おる(subsidiary), not ichidan しておる
    //       つないでおられた → stem=つないでお is つないで(te-form)+おる, not ichidan
    if (stem.find("て") != std::string::npos || stem.find("で") != std::string::npos) {
      continue;
    }

    std::string base_form = stem + "る";  // Ichidan base form = stem + る

    // Validate: check if base form is a known ichidan verb
    // For pure hiragana like いる, check the dictionary
    bool is_valid_ichidan = vh::isVerbInDictionary(dict_manager, base_form);

    // Special case: common hiragana ichidan verbs (いる, おきる, みる, etc.)
    // These may not always be in the L2 dictionary but are valid
    if (!is_valid_ichidan) {
      // Check if inflection analysis recognizes base_form as ichidan
      const auto& analysis = inflection.analyze(base_form);
      if (!analysis.empty() && analysis[0].verb_type == grammar::VerbType::Ichidan &&
          analysis[0].confidence >= candidate::verb_cost::kConstructedVerbMinConfidence) {
        is_valid_ichidan = true;
      }
    }

    if (!is_valid_ichidan) {
      continue;
    }

    // Get lemma from dictionary if available
    std::string lemma = vh::lookupVerbLemma(dict_manager, stem, base_form);

    // Generate the ichidan renyokei candidate
    // Negative cost to beat the single-word verb candidate
    constexpr float kCost = candidate::verb_cost::kStandardBonus;
    SUZUME_DEBUG_VERBOSE_BLOCK {
      SUZUME_DEBUG_STREAM << "[VERB_CAND] " << stem << " hiragana_ichidan_rareru lemma=" << lemma << " cost=" << kCost
                          << "\n";
    }
    candidates.push_back(makeVerbCandidate(stem, start_pos, stem_end, kCost, lemma,
                                           dictionary::ConjugationType::Ichidan, true, CandidateOrigin::VerbHiragana,
                                           0.9F, "hiragana_ichidan_rareru"));
    break;  // Only generate one ichidan rareru candidate per starting position
  }
}

// Godan mizenkei + contracted negative ん (くだらん → くだら + ん). Requires a
// dictionary-confirmed base form for short stems to avoid honorific さん splits.
void appendMizenkeiNCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t hiragana_end,
                               const dictionary::DictionaryManager* dict_manager,
                               std::vector<UnknownCandidate>& candidates) {
  // Pattern: A-row hiragana (mizenkei ending) + ん
  // NOTE: Skip さ+ん pattern - さん/さま are honorific suffixes, not verb 未然形+ん
  for (size_t end_pos = hiragana_end; end_pos > start_pos + 1; --end_pos) {
    // Check if the string ends with ん at this position
    if (end_pos >= codepoints.size() || codepoints[end_pos] != U'ん') {
      continue;
    }

    // Check if position end_pos-1 is A-row hiragana (mizenkei ending)
    size_t mizenkei_end = end_pos;  // Position of ん (exclusive end of mizenkei)
    if (mizenkei_end <= start_pos)
      continue;

    GodanMizenkeiForms forms;
    if (!deriveGodanMizenkeiForms(codepoints, start_pos, mizenkei_end, forms)) {
      continue;
    }
    // Skip さ+ん pattern - さん is almost always an honorific suffix, not verb 未然形+ん
    // E.g., おねえさん, おかあさん, おじさん should not be parsed as verb + contracted negative
    if (forms.a_row_char == U'さ') {
      continue;
    }
    grammar::VerbType verb_type = forms.verb_type;
    const std::string& mizenkei_surface = forms.mizenkei_surface;
    const std::string& base_form = forms.base_form;

    // Validate: check if base form exists in dictionary
    // The inflection analysis is too permissive and will match almost any input,
    // so we require dictionary confirmation to avoid false positives
    // like おねえさん → おねえさ + ん (おねえす is not a real verb)
    bool is_valid_verb = vh::isVerbInDictionary(dict_manager, base_form);

    // Minimum stem length check: need at least 2 chars in mizenkei to be meaningful
    // This prevents false positives like "かん" → "か" + "ん"
    if (!is_valid_verb && mizenkei_surface.size() < 6) {  // 2 chars = 6 bytes in UTF-8
      continue;
    }

    // Get lemma from dictionary entry if available
    std::string lemma = vh::lookupVerbLemma(dict_manager, mizenkei_surface, base_form);

    // Generate mizenkei candidate with explicit VerbMizenkei EPOS for bigram connection
    // Use negative cost for valid verbs (to beat unsplit form)
    // Use positive cost for unconfirmed verbs (long hiragana that might be verbs)
    // This prevents false positives like おねえさん → おねえさ + ん
    float cost = is_valid_verb ? -0.5F : 1.0F;
    // Unverified stems starting with a formal noun are noun + verb sequences
    // (わけわから+ん should split as わけ + わから + ん)
    if (!is_valid_verb && hasFormalNounPrefixBoundary(dict_manager, codepoints, start_pos, mizenkei_end)) {
      cost += bigram_cost::kStrong;
    }
    SUZUME_DEBUG_VERBOSE_BLOCK {
      SUZUME_DEBUG_STREAM << "[VERB_CAND] " << mizenkei_surface << " hiragana_mizenkei_n lemma=" << lemma
                          << " cost=" << cost << "\n";
    }
    candidates.push_back(makeVerbCandidate(mizenkei_surface, start_pos, mizenkei_end, cost, lemma,
                                           grammar::verbTypeToConjType(verb_type), true, CandidateOrigin::VerbHiragana,
                                           0.9F, "hiragana_mizenkei_n", core::ExtendedPOS::VerbMizenkei));
    break;  // Only generate one candidate per position
  }
}

// Godan mizenkei + negative auxiliary ない (わからない → わから + ない).
// Loop includes end_pos == start_pos + 2 for 2-char stems like いか (いく).
void appendMizenkeiNaiCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t hiragana_end,
                                 const grammar::Inflection& inflection,
                                 const dictionary::DictionaryManager* dict_manager,
                                 std::vector<UnknownCandidate>& candidates) {
  // Pattern: A-row hiragana (mizenkei ending) + ない
  for (size_t end_pos = hiragana_end; end_pos >= start_pos + 2; --end_pos) {
    // Check if the string ends with ない at this position
    if (end_pos + 2 > codepoints.size())
      continue;
    if (codepoints[end_pos] != U'な' || codepoints[end_pos + 1] != U'い') {
      continue;
    }

    // Check if position end_pos-1 is A-row hiragana (mizenkei ending)
    size_t mizenkei_end = end_pos;  // Position of な (exclusive end of mizenkei)
    if (mizenkei_end <= start_pos)
      continue;

    GodanMizenkeiForms forms;
    if (!deriveGodanMizenkeiForms(codepoints, start_pos, mizenkei_end, forms)) {
      continue;
    }
    grammar::VerbType verb_type = forms.verb_type;
    const std::string& mizenkei_surface = forms.mizenkei_surface;
    const std::string& stem = forms.stem;
    const std::string& base_form = forms.base_form;

    // Validate: analyze the full form (including ない) to check if it's a valid verb
    std::string full_form = mizenkei_surface + "ない";
    const auto& analysis = inflection.analyze(full_form);
    bool is_valid_verb = false;
    for (const auto& cand : analysis) {
      if (cand.verb_type == verb_type && cand.base_form == base_form) {
        is_valid_verb = true;
        break;
      }
    }

    // Also check if base form exists in dictionary
    bool is_in_dict = vh::isVerbInDictionary(dict_manager, base_form);
    if (!is_valid_verb) {
      is_valid_verb = is_in_dict;
    }
    if (verb_type == grammar::VerbType::GodanSa && !is_in_dict && grammar::isPureHiragana(stem)) {
      continue;
    }
    // Reject a fabricated mizenkei that merely absorbs a trailing adverbial
    // particle (みるしか / やるしか = verb + しか, never the 未然形 of a non-word).
    if (!is_in_dict && endsWithParticleAfterVerb(dict_manager, inflection, codepoints, start_pos, mizenkei_end)) {
      continue;
    }

    // Minimum stem length check: need at least 2 chars in mizenkei to be meaningful
    // This prevents false positives like "かない" → "か" + "ない"
    if (!is_valid_verb && mizenkei_surface.size() < 6) {  // 2 chars = 6 bytes in UTF-8
      continue;
    }

    // Get lemma from dictionary entry if available
    std::string lemma = vh::lookupVerbLemma(dict_manager, mizenkei_surface, base_form);

    // Dict-verified verbs get standard bonus; unverified get weaker cost
    // to prevent false hiragana verb candidates (e.g., はいか from はいく)
    // from beating particle+verb splits (は+いか)
    float cost_nai = candidate::verb_cost::kStandardBonus;  // -0.5
    if (!is_in_dict && mizenkei_surface.size() >= 6) {      // 2+ char stems
      cost_nai = 0.5F;                                      // Positive cost for unverified candidates
    }
    // Unverified stems starting with a formal noun are noun + verb sequences
    // (わけわから+ない should split as わけ + わから + ない)
    if (!is_in_dict && hasFormalNounPrefixBoundary(dict_manager, codepoints, start_pos, mizenkei_end)) {
      cost_nai += bigram_cost::kStrong;
    }
    SUZUME_DEBUG_VERBOSE_BLOCK {
      SUZUME_DEBUG_STREAM << "[VERB_CAND] " << mizenkei_surface << " hiragana_mizenkei_nai lemma=" << lemma
                          << " cost=" << cost_nai << "\n";
    }
    candidates.push_back(makeVerbCandidate(mizenkei_surface, start_pos, mizenkei_end, cost_nai, lemma,
                                           grammar::verbTypeToConjType(verb_type), true, CandidateOrigin::VerbHiragana,
                                           0.9F, "hiragana_mizenkei_nai", core::ExtendedPOS::VerbMizenkei));
    break;  // Only generate one candidate per position
  }
}

// Godan mizenkei before なきゃ/なければ (やらなきゃ → やら + なきゃ). The
// contraction is an unambiguous mizenkei signal, so the candidate gets a bonus.
void appendMizenkeiNakyaCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t hiragana_end,
                                   const grammar::Inflection& inflection,
                                   const dictionary::DictionaryManager* dict_manager,
                                   std::vector<UnknownCandidate>& candidates) {
  // Pattern: A-row hiragana (mizenkei ending) + なきゃ OR + なけれ(ば)
  for (size_t end_pos = hiragana_end; end_pos >= start_pos + 2; --end_pos) {
    // Follow pattern begins at end_pos (the mizenkei is start_pos..end_pos)
    // なきゃ = な + き + ゃ ; なけれ = な + け + れ
    if (end_pos + 3 > codepoints.size() || codepoints[end_pos] != U'な') {
      continue;
    }
    bool is_nakya = (codepoints[end_pos + 1] == U'き' && codepoints[end_pos + 2] == U'ゃ');
    bool is_nakere = (codepoints[end_pos + 1] == U'け' && codepoints[end_pos + 2] == U'れ');
    if (!is_nakya && !is_nakere) {
      continue;
    }

    // Check if position end_pos-1 is A-row hiragana (godan mizenkei ending)
    size_t mizenkei_end = end_pos;
    if (mizenkei_end <= start_pos) {
      continue;
    }
    GodanMizenkeiForms forms;
    if (!deriveGodanMizenkeiForms(codepoints, start_pos, mizenkei_end, forms)) {
      continue;
    }
    grammar::VerbType verb_type = forms.verb_type;
    const std::string& mizenkei_surface = forms.mizenkei_surface;
    const std::string& stem = forms.stem;
    const std::string& base_form = forms.base_form;

    // Validate: analyze the equivalent ない form to confirm it is a valid verb.
    // E.g., for やら validate やらない → やる (godan-ra). Dictionary is a fallback.
    std::string full_form = mizenkei_surface + "ない";
    const auto& analysis = inflection.analyze(full_form);
    bool is_valid_verb = false;
    for (const auto& cand : analysis) {
      if (cand.verb_type == verb_type && cand.base_form == base_form) {
        is_valid_verb = true;
        break;
      }
    }
    bool is_in_dict = vh::isVerbInDictionary(dict_manager, base_form);
    if (!is_valid_verb) {
      is_valid_verb = is_in_dict;
    }
    // GodanSa mizenkei on pure hiragana is almost always spurious (さ is the
    // causative marker or さん honorific), require dictionary confirmation.
    if (verb_type == grammar::VerbType::GodanSa && !is_in_dict && grammar::isPureHiragana(stem)) {
      continue;
    }
    if (!is_valid_verb) {
      continue;
    }

    // Get lemma from dictionary entry if the mizenkei surface is registered
    std::string lemma = vh::lookupVerbLemma(dict_manager, mizenkei_surface, base_form);

    // The なきゃ/なければ contraction is an unambiguous mizenkei signal, so give a
    // bonus (verified verbs stronger) to beat the particle split や + らなきゃ.
    float cost = is_in_dict ? candidate::verb_cost::kStrongBonus : candidate::verb_cost::kStandardBonus;
    // Unverified stems starting with a formal noun are noun + verb sequences
    // (わけわから+なきゃ should split as わけ + わから + なきゃ)
    if (!is_in_dict && hasFormalNounPrefixBoundary(dict_manager, codepoints, start_pos, mizenkei_end)) {
      cost += bigram_cost::kStrong;
    }
    SUZUME_DEBUG_VERBOSE_BLOCK {
      SUZUME_DEBUG_STREAM << "[VERB_CAND] " << mizenkei_surface << " hiragana_mizenkei_nakya lemma=" << lemma
                          << " cost=" << cost << "\n";
    }
    candidates.push_back(makeVerbCandidate(mizenkei_surface, start_pos, mizenkei_end, cost, lemma,
                                           grammar::verbTypeToConjType(verb_type), true, CandidateOrigin::VerbHiragana,
                                           0.9F, "hiragana_mizenkei_nakya", core::ExtendedPOS::VerbMizenkei));
    break;  // Only generate one candidate per position
  }
}

// Godan-ra ん音便 + negative ない (たまんない → たまん + ない), where stem + る
// is a godan-ra verb.
void appendNOnbinNaiCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t hiragana_end,
                               const grammar::Inflection& inflection, const dictionary::DictionaryManager* dict_manager,
                               std::vector<UnknownCandidate>& candidates) {
  // Pattern: stem + ん + ない where stem + る is a godan-ra verb
  for (size_t n_pos = start_pos + 1; n_pos < hiragana_end; ++n_pos) {
    if (codepoints[n_pos] != U'ん')
      continue;

    // Check if followed by ない
    if (n_pos + 2 >= codepoints.size())
      continue;
    if (codepoints[n_pos + 1] != U'な' || codepoints[n_pos + 2] != U'い')
      continue;

    // Get stem (part before ん) — need at least 1 char
    if (n_pos <= start_pos)
      continue;
    std::string stem = extractSubstring(codepoints, start_pos, n_pos);

    // Construct base form: stem + る (godan-ra)
    std::string base_form = stem + "る";

    // Validate: check if the standard form (stem + らない) is a valid verb
    std::string standard_form = stem + "らない";
    const auto& analysis = inflection.analyze(standard_form);
    bool is_valid_verb = false;
    for (const auto& cand : analysis) {
      if (cand.verb_type == grammar::VerbType::GodanRa && cand.base_form == base_form) {
        is_valid_verb = true;
        break;
      }
    }

    // Also check if base form exists in dictionary
    bool is_in_dict = vh::isVerbInDictionary(dict_manager, base_form);
    if (!is_valid_verb) {
      is_valid_verb = is_in_dict;
    }

    if (!is_valid_verb)
      continue;

    // Surface: stem + ん (the ん音便 form)
    std::string onbin_surface = stem + "ん";
    size_t onbin_end = n_pos + 1;

    // Get lemma from dictionary if available
    std::string standard_mizenkei = stem + "ら";
    std::string lemma = vh::lookupVerbLemma(dict_manager, standard_mizenkei, base_form);

    float cost_n_onbin = candidate::verb_cost::kStandardBonus;
    // Unverified stems starting with a formal noun are noun + verb sequences
    // (わけわかん+ない should split as わけ + わかん + ない)
    if (!is_in_dict && hasFormalNounPrefixBoundary(dict_manager, codepoints, start_pos, onbin_end)) {
      cost_n_onbin += bigram_cost::kStrong;
    }
    SUZUME_DEBUG_VERBOSE_BLOCK {
      SUZUME_DEBUG_STREAM << "[VERB_CAND] " << onbin_surface << " hiragana_n_onbin_nai lemma=" << lemma
                          << " cost=" << cost_n_onbin << "\n";
    }
    candidates.push_back(makeVerbCandidate(onbin_surface, start_pos, onbin_end, cost_n_onbin, lemma,
                                           grammar::verbTypeToConjType(grammar::VerbType::GodanRa), true,
                                           CandidateOrigin::VerbHiragana, 0.9F, "hiragana_n_onbin_nai",
                                           core::ExtendedPOS::VerbMizenkei));
    break;
  }
}

// Godan onbin stems before contraction/tense auxiliaries. Handles
// っ + と/ち/た/て (GodanRa/Ta/Wa) and ん + ど/じ/で/だ (GodanMa/Ba/Na).

}  // namespace suzume::analysis::hiragana_verb_detail
