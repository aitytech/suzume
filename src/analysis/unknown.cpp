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
#include <utility>

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

namespace {

void appendCandidates(std::vector<suzume::analysis::UnknownCandidate>& destination,
                      std::vector<suzume::analysis::UnknownCandidate>&& source) {
  destination.reserve(destination.size() + source.size());
  for (auto& candidate : source) {
    destination.push_back(std::move(candidate));
  }
}

// A closed-class conjunction is a hard lexical boundary.  Unknown candidates
// may not consume its first character (本又|は, 本若しく|は), even when their
// own surface stops before the conjunction's final character.
bool spansConjunctionStart(const suzume::analysis::UnknownCandidate& candidate, const std::vector<char32_t>& codepoints,
                           const suzume::dictionary::DictionaryManager* dict_manager) {
  if (dict_manager == nullptr || candidate.end <= candidate.start + 1) {
    return false;
  }

  constexpr size_t kConjunctionWindowChars = 8;
  for (size_t boundary = candidate.start; boundary < candidate.end; ++boundary) {
    size_t window_end = std::min(codepoints.size(), boundary + kConjunctionWindowChars);
    for (size_t conjunction_end = boundary + 1; conjunction_end <= window_end; ++conjunction_end) {
      std::string conjunction = suzume::analysis::extractSubstring(codepoints, boundary, conjunction_end);
      if (dict_manager->lookupExact(conjunction, suzume::core::PartOfSpeech::Conjunction) != nullptr &&
          (boundary > candidate.start || conjunction_end > candidate.end)) {
        // A two-kanji content noun can overlap the first kanji of a
        // conjunction whose last mora is an independent case particle
        // (変更+に, not 変+更に).  Keep that noun candidate so the lattice can
        // evaluate the grammatical case boundary.  Topic-final conjunctions
        // such as 又は remain protected by the ordinary hard boundary below.
        const bool conjunction_leaves_one_case_particle =
            boundary + 1 == candidate.end && conjunction_end == candidate.end + 1;
        if (conjunction_leaves_one_case_particle) {
          const std::string trailing = suzume::analysis::extractSubstring(codepoints, candidate.end, conjunction_end);
          const auto* particle = dict_manager->lookupExact(trailing, suzume::core::PartOfSpeech::Particle);
          if (particle != nullptr && particle->extended_pos == suzume::core::ExtendedPOS::ParticleCase) {
            continue;
          }
        }
        return true;
      }
    }
  }
  return false;
}

// A generic kanji noun candidate may end by consuming the stem kanji of a
// Godan continuative compound (確認申|し上げる). Preserve the boundary when the
// final kanji plus the following i-row ending reconstructs an attested verb
// and a second kanji verb follows. Dictionary verification prevents an
// arbitrary noun-final kanji followed by し from triggering the rule.
bool endsInsideVerifiedCompoundVerb(const suzume::analysis::UnknownCandidate& candidate,
                                    const std::vector<char32_t>& codepoints,
                                    const std::vector<suzume::normalize::CharType>& char_types,
                                    const suzume::dictionary::DictionaryManager* dict_manager) {
  if (dict_manager == nullptr || candidate.origin != suzume::core::CandidateOrigin::SameType ||
      candidate.pos != suzume::core::PartOfSpeech::Noun || candidate.end <= candidate.start + 1 ||
      candidate.end + 1 >= codepoints.size() || char_types[candidate.start] != suzume::normalize::CharType::Kanji ||
      char_types[candidate.end] != suzume::normalize::CharType::Hiragana ||
      char_types[candidate.end + 1] != suzume::normalize::CharType::Kanji) {
    return false;
  }

  const std::string_view base_suffix = suzume::grammar::godanBaseSuffixFromIRow(codepoints[candidate.end]);
  if (base_suffix.empty()) {
    return false;
  }
  const std::string verb_base =
      suzume::analysis::extractSubstring(codepoints, candidate.end - 1, candidate.end) + std::string(base_suffix);
  return dict_manager->lookupExact(verb_base, suzume::core::PartOfSpeech::Verb) != nullptr;
}

}  // namespace

namespace suzume::analysis {

UnknownCandidate makeVerbCandidate(const std::string& surface, size_t start, size_t end, float cost,
                                   const std::string& lemma, dictionary::ConjugationType conj_type, bool has_suffix,
                                   CandidateOrigin origin, [[maybe_unused]] float confidence,
                                   [[maybe_unused]] const char* pattern, core::ExtendedPOS extended_pos,
                                   [[maybe_unused]] const char* epos_source) {
  UnknownCandidate candidate;
  candidate.surface = surface;
  candidate.start = start;
  candidate.end = end;
  candidate.pos = core::PartOfSpeech::Verb;
  candidate.extended_pos =
      extended_pos != core::ExtendedPOS::Unknown ? extended_pos : core::detectVerbForm(surface, {});
  candidate.cost = cost;
  candidate.lemma = lemma;
  candidate.conj_type = conj_type;
  candidate.has_suffix = has_suffix;
  candidate.origin = origin;
#ifdef SUZUME_DEBUG_INFO
  candidate.confidence = confidence;
  if (pattern != nullptr) {
    candidate.pattern = pattern;
  }
  if (epos_source != nullptr) {
    candidate.epos_source = epos_source;
  } else if (extended_pos != core::ExtendedPOS::Unknown) {
    candidate.epos_source = "verb_cand_explicit";
  } else {
    candidate.epos_source = "verb_cand_auto";
  }
#endif
  return candidate;
}

UnknownCandidate makeNounCandidate(const std::string& surface, size_t start, size_t end, float cost, bool has_suffix,
                                   CandidateOrigin origin, core::ExtendedPOS extended_pos,
                                   [[maybe_unused]] const char* epos_source) {
  UnknownCandidate candidate;
  candidate.surface = surface;
  candidate.start = start;
  candidate.end = end;
  candidate.pos = core::PartOfSpeech::Noun;
  candidate.extended_pos = extended_pos != core::ExtendedPOS::Unknown ? extended_pos : core::ExtendedPOS::Noun;
  candidate.cost = cost;
  candidate.has_suffix = has_suffix;
  candidate.origin = origin;
#ifdef SUZUME_DEBUG_INFO
  if (epos_source != nullptr) {
    candidate.epos_source = epos_source;
  } else if (extended_pos != core::ExtendedPOS::Unknown) {
    candidate.epos_source = "noun_cand_explicit";
  } else {
    candidate.epos_source = "noun_cand_default";
  }
#endif
  return candidate;
}

UnknownCandidate makeCandidate(const std::string& surface, size_t start, size_t end, core::PartOfSpeech pos, float cost,
                               bool has_suffix, CandidateOrigin origin, core::ExtendedPOS extended_pos,
                               [[maybe_unused]] const char* epos_source) {
  UnknownCandidate candidate;
  candidate.surface = surface;
  candidate.start = start;
  candidate.end = end;
  candidate.pos = pos;
  candidate.extended_pos = extended_pos != core::ExtendedPOS::Unknown ? extended_pos : core::posToExtendedPos(pos);
  candidate.cost = cost;
  candidate.has_suffix = has_suffix;
  candidate.origin = origin;
#ifdef SUZUME_DEBUG_INFO
  if (epos_source != nullptr) {
    candidate.epos_source = epos_source;
  } else if (extended_pos != core::ExtendedPOS::Unknown) {
    candidate.epos_source = "make_cand_explicit";
  } else {
    candidate.epos_source = "make_cand_default";
  }
#endif
  return candidate;
}

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
    appendCandidates(candidates, generateOnomatopoeiaCandidates(codepoints, start_pos, char_types));
  }

  // Generate verb candidates (kanji + hiragana conjugation endings)
  if (char_types[start_pos] == normalize::CharType::Kanji) {
    appendCandidates(candidates, generateVerbCandidates(text, codepoints, start_pos, char_types));

    // Generate compound verb candidates (kanji + hiragana + kanji + hiragana)
    // e.g., 恐れ入ります, 差し上げます, 申し上げます
    appendCandidates(candidates, generateCompoundVerbCandidates(text, codepoints, start_pos, char_types));

    // Generate i-adjective candidates (kanji + hiragana conjugation endings)
    appendCandidates(candidates, generateAdjectiveCandidates(text, codepoints, start_pos, char_types));

    // Generate i-adjective STEM candidates (難し, 美し for 難しそう, 美しすぎる)
    // This preserves the adjective stem and appearance-auxiliary boundary.
    appendCandidates(candidates, generateAdjectiveStemCandidates(text, codepoints, start_pos, char_types));

    // Generate productive nominal-base suffix verbs (春めく、謎めく).
    appendCandidates(candidates, generateProductiveSuffixVerbCandidates(codepoints, start_pos, char_types));

    // Generate na-adjective candidates (〜的 patterns)
    appendCandidates(candidates, generateNaAdjectiveCandidates(text, codepoints, start_pos, char_types));

    // Generate nominalized noun candidates (kanji + short hiragana)
    // e.g., 手助け, 片付け, 引き上げ
    appendCandidates(candidates, generateNominalizedNounCandidates(text, codepoints, start_pos, char_types));

    // Generate kanji + hiragana compound noun candidates
    // e.g., 玉ねぎ, 水たまり
    // Pass dict_manager to skip compounds when hiragana portion is a known word
    appendCandidates(candidates,
                     generateKanjiHiraganaCompoundCandidates(codepoints, start_pos, char_types, dict_manager_));

    // Generate counter candidates for numeral + つ patterns
    // e.g., 一つ, 二つ, ..., 九つ (closed class)
    appendCandidates(candidates, generateCounterCandidates(codepoints, start_pos, char_types, dict_manager_));

    // Generate prefix + single kanji compound candidates
    // e.g., 今日, 今週, 本日, 全国 (prefix-like compounds)
    appendCandidates(candidates, generatePrefixCompoundCandidates(codepoints, start_pos, char_types, inflection_));

    // Generate temporal-noun boundary split candidates (現在|担当者, 昨日|会議)
    appendCandidates(candidates, generateTemporalNounBoundaryCandidates(codepoints, start_pos, char_types));
  }

  // Generate hiragana verb candidates (pure hiragana verbs like いく, くる)
  if (char_types[start_pos] == normalize::CharType::Hiragana) {
    appendCandidates(candidates, generateHiraganaVerbCandidates(text, codepoints, start_pos, char_types));

    // Generate hiragana i-adjective candidates (まずい, おいしい, etc.)
    appendCandidates(candidates, generateHiraganaAdjectiveCandidates(text, codepoints, start_pos, char_types));

    // Generate productive suffix candidates (ありがち, 忘れっぽい, etc.)
    appendCandidates(candidates, generateProductiveSuffixCandidates(codepoints, start_pos, char_types));
  }

  // Generate katakana verb/adjective candidates (slang: バズる, エモい, etc.)
  if (char_types[start_pos] == normalize::CharType::Katakana) {
    appendCandidates(candidates, generateKatakanaVerbCandidates(codepoints, start_pos, char_types, inflection_,
                                                                dict_manager_, options_.verb_candidate_options));

    appendCandidates(candidates, generateKatakanaAdjectiveCandidates(codepoints, start_pos, char_types, inflection_));
  }

  // Generate counter candidates for digit + つ patterns (e.g., 3つ, 10個)
  if (char_types[start_pos] == normalize::CharType::Digit) {
    appendCandidates(candidates, generateCounterCandidates(codepoints, start_pos, char_types, dict_manager_));
  }

  // Generate by same type
  appendCandidates(candidates, generateBySameType(text, codepoints, start_pos, char_types));

  // Generate alphanumeric sequences
  appendCandidates(candidates, generateAlphanumeric(text, codepoints, start_pos, char_types));

  // Generate with suffix separation for kanji
  if (options_.separate_suffix && char_types[start_pos] == normalize::CharType::Kanji) {
    appendCandidates(candidates, generateWithSuffix(text, codepoints, start_pos, char_types));
  }

  // Generate character speech candidates (キャラ語尾)
  if (options_.enable_character_speech) {
    appendCandidates(candidates, generateCharacterSpeechCandidates(text, codepoints, start_pos, char_types));
  }

  candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                  [&](const UnknownCandidate& candidate) {
                                    if (spansConjunctionStart(candidate, codepoints, dict_manager_)) {
                                      return true;
                                    }
                                    return endsInsideVerifiedCompoundVerb(candidate, codepoints, char_types,
                                                                          dict_manager_);
                                  }),
                   candidates.end());

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
