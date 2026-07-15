/**
 * @file unknown.cpp
 * @brief Unknown word candidate generation orchestrator
 *
 * This file delegates specialized candidate generation to:
 * - suffix_candidates.h: Suffix-based and nominalized noun candidates
 * - adjective_candidates.h: I-adjective and na-adjective candidates
 * - verb_candidates.h: Verb and compound verb candidates
 */

#include "analysis/unknown.h"

#include <algorithm>
#include <cstdint>

#include "adjective_candidates.h"
#include "analysis/scorer_constants.h"
#include "candidate_constants.h"
#include "core/kana_constants.h"
#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "normalize/char_type.h"
#include "normalize/exceptions.h"
#include "normalize/utf8.h"
#include "suffix_candidates.h"
#include "tokenizer_utils.h"
#include "verb_candidates.h"

namespace suzume::analysis {

UnknownWordGenerator::UnknownWordGenerator(const UnknownOptions& options,
                                           const dictionary::DictionaryManager* dict_manager)
    : options_(options), dict_manager_(dict_manager) {}

size_t UnknownWordGenerator::getMaxLength(normalize::CharType ctype) const {
  switch (ctype) {
    case normalize::CharType::Kanji:
      return options_.max_kanji_length;
    case normalize::CharType::Katakana:
      return options_.max_katakana_length;
    case normalize::CharType::Hiragana:
      return options_.max_hiragana_length;
    case normalize::CharType::Alphabet:
      return options_.max_alphabet_length;
    case normalize::CharType::Digit:
      return options_.max_alphanumeric_length;
    default:
      return options_.max_unknown_length;
  }
}

core::PartOfSpeech UnknownWordGenerator::getPosForType(normalize::CharType ctype) {
  switch (ctype) {
    case normalize::CharType::Kanji:
    case normalize::CharType::Katakana:
    case normalize::CharType::Alphabet:
    case normalize::CharType::Digit:
      return core::PartOfSpeech::Noun;
    case normalize::CharType::Hiragana:
      return core::PartOfSpeech::Other;
    default:
      return core::PartOfSpeech::Symbol;
  }
}

float UnknownWordGenerator::getCostForType(normalize::CharType ctype, size_t length) {
  float base_cost = 1.0F;

  switch (ctype) {
    case normalize::CharType::Kanji:
      // Kanji: 2-6 characters are common compound word lengths
      // Keep compounds connected by default - splitting should be driven by
      // PREFIX/SUFFIX markers or dictionary entries, not length heuristics
      // E.g., 自然言語処理 should stay as one token (no evidence to split)
      // But 今夏最高 splits because 今 is marked as PREFIX
      if (length == 1) {
        return base_cost + 0.4F;  // 1.4: prefer over suffix entries (1.5)
      }
      // 2+ chars: all valid compound lengths, no penalty
      // Long compounds like 独立行政法人情報処理推進機構 should stay as one token
      return base_cost;

    case normalize::CharType::Katakana:
      // Katakana: prefer 4+ characters for loanwords (マスカラ, デスクトップ)
      // Penalize short sequences to prevent splits like マ+スカラ
      if (length == 1) {
        return base_cost + 1.5F;  // Strong penalty for 1-char
      }
      if (length == 2) {
        return base_cost + 1.0F;  // Moderate penalty for 2-char
      }
      if (length == 3) {
        return base_cost + 0.3F;  // Light penalty for 3-char
      }
      if (length >= 4 && length <= 10) {
        return base_cost;  // Optimal: 4-10 chars
      }
      return base_cost + 0.3F;  // 11+ chars: light penalty

    case normalize::CharType::Alphabet:
      // Alphabet: prefer longer sequences for identifiers/words
      // Longer sequences (like "getUserData") should not be penalized
      if (length >= 2 && length <= 20) {
        // Give bonus to longer sequences to prefer them over splits
        // This helps keep "getUserData" together vs "getUser" + "Data"
        float length_bonus = (length >= 8) ? -0.3F : 0.0F;
        return base_cost + 0.2F + length_bonus;
      }
      return base_cost + 0.5F;

    case normalize::CharType::Digit:
      // Digits: always reasonable
      return base_cost - 0.2F;

    case normalize::CharType::Hiragana:
      // Hiragana only: usually function words
      // 1-char: high cost (almost always a particle/aux from L1 dict)
      // 2-3 char: moderate cost to compete with particle chains
      //   e.g., はし(1.4) vs は(0.2)+し(0.2)+conn(0.5)=0.9 — still loses,
      //   but closer, and bigram penalties on bad connections can tip it
      // 4+: increasing penalty to force segmentation
      if (length == 1) {
        return base_cost + 1.0F;  // 2.0: original cost
      }
      if (length <= 3) {
        return base_cost + 0.4F;  // 1.4: compete with particle chains
      }
      return base_cost + 0.5F + (static_cast<float>(length) - 3.0F) * 0.5F;

    default:
      return base_cost + 1.5F;
  }
}

std::vector<UnknownCandidate> UnknownWordGenerator::generate(std::string_view text,
                                                             const std::vector<char32_t>& codepoints, size_t start_pos,
                                                             const std::vector<normalize::CharType>& char_types) const {
  std::vector<UnknownCandidate> candidates;

  if (start_pos >= char_types.size()) {
    return candidates;
  }

  // Generate ABAB-type onomatopoeia candidates first (わくわく, きらきら, etc.)
  // This needs to be checked before isNeverVerbStemAtStart filters out わ, etc.
  // Also handles katakana patterns (ニャーニャー, ワンワン, etc.)
  if (char_types[start_pos] == normalize::CharType::Hiragana ||
      char_types[start_pos] == normalize::CharType::Katakana) {
    auto onomatopoeia = generateOnomatopoeiaCandidates(codepoints, start_pos, char_types);
    candidates.insert(candidates.end(), onomatopoeia.begin(), onomatopoeia.end());
  }

  // Generate verb candidates (kanji + hiragana conjugation endings)
  if (char_types[start_pos] == normalize::CharType::Kanji) {
    auto verbs = generateVerbCandidates(text, codepoints, start_pos, char_types);
    candidates.insert(candidates.end(), verbs.begin(), verbs.end());

    // Generate compound verb candidates (kanji + hiragana + kanji + hiragana)
    // e.g., 恐れ入ります, 差し上げます, 申し上げます
    auto compound_verbs = generateCompoundVerbCandidates(text, codepoints, start_pos, char_types);
    candidates.insert(candidates.end(), compound_verbs.begin(), compound_verbs.end());

    // Generate i-adjective candidates (kanji + hiragana conjugation endings)
    auto adjs = generateAdjectiveCandidates(text, codepoints, start_pos, char_types);
    candidates.insert(candidates.end(), adjs.begin(), adjs.end());

    // Generate i-adjective STEM candidates (難し, 美し for 難しそう, 美しすぎる)
    // This enables MeCab-compatible split: 難しそう → 難し(ADJ) + そう(AUX)
    auto adj_stems = generateAdjectiveStemCandidates(text, codepoints, start_pos, char_types);
    candidates.insert(candidates.end(), adj_stems.begin(), adj_stems.end());

    // Generate na-adjective candidates (〜的 patterns)
    auto na_adjs = generateNaAdjectiveCandidates(text, codepoints, start_pos, char_types);
    candidates.insert(candidates.end(), na_adjs.begin(), na_adjs.end());

    // Generate nominalized noun candidates (kanji + short hiragana)
    // e.g., 手助け, 片付け, 引き上げ
    auto nom_nouns = generateNominalizedNounCandidates(text, codepoints, start_pos, char_types);
    candidates.insert(candidates.end(), nom_nouns.begin(), nom_nouns.end());

    // Generate kanji + hiragana compound noun candidates
    // e.g., 玉ねぎ, 水たまり
    // Pass dict_manager to skip compounds when hiragana portion is a known word
    auto compound_nouns = generateKanjiHiraganaCompoundCandidates(codepoints, start_pos, char_types, dict_manager_);
    candidates.insert(candidates.end(), compound_nouns.begin(), compound_nouns.end());

    // Generate counter candidates for numeral + つ patterns
    // e.g., 一つ, 二つ, ..., 九つ (closed class)
    auto counters = generateCounterCandidates(codepoints, start_pos, char_types);
    candidates.insert(candidates.end(), counters.begin(), counters.end());

    // Generate prefix + single kanji compound candidates
    // e.g., 今日, 今週, 本日, 全国 (prefix-like compounds)
    auto prefix_compounds = generatePrefixCompoundCandidates(codepoints, start_pos, char_types, inflection_);
    candidates.insert(candidates.end(), prefix_compounds.begin(), prefix_compounds.end());

    // Generate temporal-noun boundary split candidates (現在|担当者, 昨日|会議)
    auto temporal_boundary_candidates = generateTemporalNounBoundaryCandidates(codepoints, start_pos, char_types);
    candidates.insert(candidates.end(), temporal_boundary_candidates.begin(), temporal_boundary_candidates.end());
  }

  // Generate hiragana verb candidates (pure hiragana verbs like いく, くる)
  if (char_types[start_pos] == normalize::CharType::Hiragana) {
    auto hiragana_verbs = generateHiraganaVerbCandidates(text, codepoints, start_pos, char_types);
    candidates.insert(candidates.end(), hiragana_verbs.begin(), hiragana_verbs.end());

    // Generate hiragana i-adjective candidates (まずい, おいしい, etc.)
    auto hiragana_adjs = generateHiraganaAdjectiveCandidates(text, codepoints, start_pos, char_types);
    candidates.insert(candidates.end(), hiragana_adjs.begin(), hiragana_adjs.end());

    // Generate productive suffix candidates (ありがち, 忘れっぽい, etc.)
    auto productive_suffix = generateProductiveSuffixCandidates(codepoints, start_pos, char_types);
    candidates.insert(candidates.end(), productive_suffix.begin(), productive_suffix.end());
  }

  // Generate katakana verb/adjective candidates (slang: バズる, エモい, etc.)
  if (char_types[start_pos] == normalize::CharType::Katakana) {
    auto kata_verbs = generateKatakanaVerbCandidates(codepoints, start_pos, char_types, inflection_, dict_manager_,
                                                     options_.verb_candidate_options);
    candidates.insert(candidates.end(), kata_verbs.begin(), kata_verbs.end());

    auto kata_adjs = generateKatakanaAdjectiveCandidates(codepoints, start_pos, char_types, inflection_);
    candidates.insert(candidates.end(), kata_adjs.begin(), kata_adjs.end());
  }

  // Generate counter candidates for digit + つ patterns (e.g., 3つ, 10個)
  if (char_types[start_pos] == normalize::CharType::Digit) {
    auto counters = generateCounterCandidates(codepoints, start_pos, char_types);
    candidates.insert(candidates.end(), counters.begin(), counters.end());
  }

  // Generate by same type
  auto same_type = generateBySameType(text, codepoints, start_pos, char_types);
  candidates.insert(candidates.end(), same_type.begin(), same_type.end());

  // Generate alphanumeric sequences
  auto alphanum = generateAlphanumeric(text, codepoints, start_pos, char_types);
  candidates.insert(candidates.end(), alphanum.begin(), alphanum.end());

  // Generate with suffix separation for kanji
  if (options_.separate_suffix && char_types[start_pos] == normalize::CharType::Kanji) {
    auto suffix = generateWithSuffix(text, codepoints, start_pos, char_types);
    candidates.insert(candidates.end(), suffix.begin(), suffix.end());
  }

  // Generate character speech candidates (キャラ語尾)
  if (options_.enable_character_speech) {
    auto char_speech = generateCharacterSpeechCandidates(text, codepoints, start_pos, char_types);
    candidates.insert(candidates.end(), char_speech.begin(), char_speech.end());
  }

  return candidates;
}

std::vector<UnknownCandidate> UnknownWordGenerator::generateAlphanumeric(
    std::string_view /*text*/, const std::vector<char32_t>& codepoints, size_t start_pos,
    const std::vector<normalize::CharType>& char_types) const {
  std::vector<UnknownCandidate> candidates;

  if (start_pos >= char_types.size()) {
    return candidates;
  }

  normalize::CharType start_type = char_types[start_pos];

  // Only for alphabet or digit start
  if (start_type != normalize::CharType::Alphabet && start_type != normalize::CharType::Digit) {
    return candidates;
  }

  // Find mixed alphanumeric sequence (including underscores for identifiers)
  // Supports snake_case identifiers like user_name, first_name, etc.
  size_t end_pos = start_pos;
  bool has_alpha = false;
  bool has_digit = false;
  bool has_underscore = false;

  while (end_pos < char_types.size() && end_pos - start_pos < options_.max_alphanumeric_length) {
    normalize::CharType ctype = char_types[end_pos];
    char32_t ch = codepoints[end_pos];
    if (ctype == normalize::CharType::Alphabet) {
      has_alpha = true;
      ++end_pos;
    } else if (ctype == normalize::CharType::Digit) {
      has_digit = true;
      ++end_pos;
    } else if (ch == U'_') {
      // Include underscore in identifier patterns
      // Only if followed by alphanumeric (avoid trailing underscore)
      if (end_pos + 1 < char_types.size()) {
        normalize::CharType next_type = char_types[end_pos + 1];
        if (next_type == normalize::CharType::Alphabet || next_type == normalize::CharType::Digit) {
          has_underscore = true;
          ++end_pos;
          continue;
        }
      }
      break;
    } else {
      break;
    }
  }

  // Generate candidate if mixed alphanumeric OR identifier with underscore
  // Pure alpha/digit sequences are handled by generateBySameType
  bool is_mixed = has_alpha && has_digit;
  bool is_identifier = has_alpha && has_underscore;
  if ((is_mixed || is_identifier) && end_pos > start_pos + 1) {
    std::string surface = extractSubstring(codepoints, start_pos, end_pos);
    if (!surface.empty()) {
      // Give identifiers with underscores a bonus to prefer them over splits
      float cost = is_identifier ? 0.5F : 0.8F;
      auto cand = makeCandidate(surface, start_pos, end_pos, core::PartOfSpeech::Noun, cost, false,
                                CandidateOrigin::Alphanumeric);
#ifdef SUZUME_DEBUG_INFO
      cand.confidence = 1.0F;
      cand.pattern = is_identifier ? "identifier" : "alphanum_mixed";
#endif
      candidates.push_back(cand);
    }
  }

  return candidates;
}

std::vector<UnknownCandidate> UnknownWordGenerator::generateWithSuffix(
    std::string_view /*text*/, const std::vector<char32_t>& codepoints, size_t start_pos,
    const std::vector<normalize::CharType>& char_types) const {
  // Delegate to the standalone function
  return analysis::generateWithSuffix(codepoints, start_pos, char_types, options_);
}

std::vector<UnknownCandidate> UnknownWordGenerator::generateCompoundVerbCandidates(
    std::string_view /*text*/, const std::vector<char32_t>& codepoints, size_t start_pos,
    const std::vector<normalize::CharType>& char_types) const {
  // Delegate to the standalone function
  return analysis::generateCompoundVerbCandidates(codepoints, start_pos, char_types, inflection_, dict_manager_,
                                                  options_.verb_candidate_options);
}

std::vector<UnknownCandidate> UnknownWordGenerator::generateVerbCandidates(
    std::string_view /*text*/, const std::vector<char32_t>& codepoints, size_t start_pos,
    const std::vector<normalize::CharType>& char_types) const {
  // Delegate to the standalone function
  return analysis::generateVerbCandidates(codepoints, start_pos, char_types, inflection_, dict_manager_,
                                          options_.verb_candidate_options);
}

std::vector<UnknownCandidate> UnknownWordGenerator::generateHiraganaVerbCandidates(
    std::string_view /*text*/, const std::vector<char32_t>& codepoints, size_t start_pos,
    const std::vector<normalize::CharType>& char_types) const {
  // Delegate to the standalone function
  return analysis::generateHiraganaVerbCandidates(codepoints, start_pos, char_types, inflection_, dict_manager_,
                                                  options_.verb_candidate_options);
}

std::vector<UnknownCandidate> UnknownWordGenerator::generateAdjectiveCandidates(
    std::string_view /*text*/, const std::vector<char32_t>& codepoints, size_t start_pos,
    const std::vector<normalize::CharType>& char_types) const {
  // Delegate to the standalone function
  return analysis::generateAdjectiveCandidates(codepoints, start_pos, char_types, inflection_, dict_manager_);
}

std::vector<UnknownCandidate> UnknownWordGenerator::generateAdjectiveStemCandidates(
    std::string_view /*text*/, const std::vector<char32_t>& codepoints, size_t start_pos,
    const std::vector<normalize::CharType>& char_types) const {
  // Delegate to the standalone function
  return analysis::generateAdjectiveStemCandidates(codepoints, start_pos, char_types, inflection_, dict_manager_);
}

std::vector<UnknownCandidate> UnknownWordGenerator::generateHiraganaAdjectiveCandidates(
    std::string_view /*text*/, const std::vector<char32_t>& codepoints, size_t start_pos,
    const std::vector<normalize::CharType>& char_types) const {
  // Delegate to the standalone function
  return analysis::generateHiraganaAdjectiveCandidates(codepoints, start_pos, char_types, inflection_, dict_manager_);
}

std::vector<UnknownCandidate> UnknownWordGenerator::generateNaAdjectiveCandidates(
    std::string_view /*text*/, const std::vector<char32_t>& codepoints, size_t start_pos,
    const std::vector<normalize::CharType>& char_types) const {
  // Delegate to the standalone function
  return analysis::generateNaAdjectiveCandidates(codepoints, start_pos, char_types, options_);
}

std::vector<UnknownCandidate> UnknownWordGenerator::generateNominalizedNounCandidates(
    std::string_view /*text*/, const std::vector<char32_t>& codepoints, size_t start_pos,
    const std::vector<normalize::CharType>& char_types) const {
  // Delegate to the standalone function
  return analysis::generateNominalizedNounCandidates(codepoints, start_pos, char_types, dict_manager_);
}

}  // namespace suzume::analysis
