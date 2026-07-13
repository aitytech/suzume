/**
 * @file verb_candidates_helpers.cpp
 * @brief Implementation of internal helpers for verb candidate generation
 */

#include "verb_candidates_helpers.h"

#include <algorithm>

#include "analysis/scorer_constants.h"
#include "core/debug.h"
#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "grammar/conjugation.h"
#include "normalize/utf8.h"

namespace suzume::analysis::verb_helpers {

// =============================================================================
// Single-kanji Ichidan verbs
// =============================================================================

namespace {
constexpr char32_t kSingleKanjiIchidanList[] = {U'見', U'居', U'着', U'寝', U'煮', U'似',
                                                U'経', U'干', U'射', U'得', U'出', U'鋳'};
}  // namespace

bool isSingleKanjiIchidan(char32_t c) {
  for (char32_t k : kSingleKanjiIchidanList) {
    if (c == k)
      return true;
  }
  return false;
}

bool isSingleKanjiIchidanSurface(std::string_view surface) {
  if (normalize::utf8Length(surface) != 1) {
    return false;
  }
  auto codepoints = normalize::toCodepoints(surface);
  return !codepoints.empty() && isSingleKanjiIchidan(codepoints[0]);
}

// =============================================================================
// Dictionary Lookup Helpers
// =============================================================================

bool hasDictionaryEntry(const dictionary::DictionaryManager* dict_manager, std::string_view surface,
                        core::PartOfSpeech pos) {
  if (dict_manager == nullptr || surface.empty()) {
    return false;
  }
  auto results = dict_manager->lookup(surface, 0);
  for (const auto& result : results) {
    if (result.entry != nullptr && result.entry->surface == surface && result.entry->pos == pos) {
      SUZUME_DEBUG_LOG_TRACE("[DICT] \"" << surface << "\" (" << core::posToString(pos) << "/"
                                         << core::extendedPosToString(result.entry->extended_pos) << ") = FOUND\n");
      return true;
    }
  }
  SUZUME_DEBUG_LOG_TRACE("[DICT] \"" << surface << "\" (" << core::posToString(pos) << ") = NOT_FOUND\n");
  return false;
}

bool hasNonVerbDictionaryEntry(const dictionary::DictionaryManager* dict_manager, std::string_view surface) {
  if (dict_manager == nullptr) {
    return false;
  }
  auto results = dict_manager->lookup(surface, 0);
  for (const auto& result : results) {
    if (result.entry != nullptr && result.entry->surface == surface && result.entry->pos != core::PartOfSpeech::Verb) {
      return true;
    }
  }
  return false;
}

bool hasParticleDictionaryEntry(const dictionary::DictionaryManager* dict_manager, std::string_view surface) {
  if (dict_manager == nullptr) {
    return false;
  }
  auto results = dict_manager->lookup(surface, 0);
  for (const auto& result : results) {
    if (result.entry != nullptr && result.entry->surface == surface &&
        result.entry->pos == core::PartOfSpeech::Particle) {
      return true;
    }
  }
  return false;
}

std::string lookupVerbLemma(const dictionary::DictionaryManager* dict_manager, std::string_view surface,
                            std::string_view fallback) {
  if (dict_manager != nullptr) {
    auto results = dict_manager->lookup(surface, 0);
    for (const auto& result : results) {
      if (result.entry != nullptr && result.entry->surface == surface &&
          result.entry->pos == core::PartOfSpeech::Verb && !result.entry->lemma.empty()) {
        return result.entry->lemma;
      }
    }
  }
  return std::string(fallback);
}

bool isVerifiedVerbBase(const dictionary::DictionaryManager* dict_manager, const grammar::Inflection& inflection,
                        std::string_view base_form, float min_confidence, bool require_godan) {
  if (isVerbInDictionary(dict_manager, base_form)) {
    return true;
  }
  auto infl_result = inflection.getBest(base_form);
  bool type_ok = require_godan ? grammar::isGodanVerbType(infl_result.verb_type)
                               : infl_result.verb_type == grammar::VerbType::Ichidan;
  return infl_result.confidence > min_confidence && type_ok;
}

// =============================================================================
// Candidate Sorting
// =============================================================================

void sortCandidatesByCost(std::vector<UnknownCandidate>& candidates) {
  std::sort(candidates.begin(), candidates.end(),
            [](const UnknownCandidate& lhs, const UnknownCandidate& rhs) { return lhs.cost < rhs.cost; });
}

// =============================================================================
// Emphatic Pattern Helpers
// =============================================================================

bool isEmphaticChar(char32_t c) {
  return c == core::hiragana::kSmallTsu ||  // っ
         c == U'ッ' ||                      // katakana sokuon
         c == U'ー' ||                      // chouon
         // Small hiragana vowels
         c == U'ぁ' || c == U'ぃ' || c == U'ぅ' || c == U'ぇ' || c == U'ぉ' ||
         // Small katakana vowels
         c == U'ァ' || c == U'ィ' || c == U'ゥ' || c == U'ェ' || c == U'ォ';
}

char32_t getHiraganaVowel(char32_t c) {
  // Hiragana range: U+3041 (ぁ) to U+3096 (ゖ)
  constexpr char32_t kHiraganaStart = 0x3041;
  constexpr char32_t kHiraganaEnd = 0x3096;

  if (c < kHiraganaStart || c > kHiraganaEnd) {
    return 0;  // Not hiragana
  }

  // Vowel table: 'a'/'i'/'u'/'e'/'o' or 0 for no-vowel (ん, っ)
  // Index = codepoint - 0x3041, covers ぁ through ゖ (86 chars)
  static constexpr char kVowelTable[86] = {
      'a', 'a', 'i', 'i', 'u', 'u', 'e', 'e', 'o', 'o', 'a', 'a', 'i', 'i', 'u',       // ぁ-く
      'u', 'e', 'e', 'o', 'o', 'a', 'a', 'i', 'i', 'u', 'u', 'e', 'e', 'o', 'o',       // ぐ-ぞ
      'a', 'a', 'i', 'i', 0,   'u', 'u', 'e', 'e', 'o', 'o', 'a', 'i', 'u', 'e',       // た-ね
      'o', 'a', 'a', 'a', 'i', 'i', 'i', 'u', 'u', 'u', 'e', 'e', 'e', 'o', 'o',       // の-ぼ
      'o', 'a', 'i', 'u', 'e', 'o', 'a', 'a', 'u', 'u', 'o', 'o', 'a', 'i', 'u', 'e',  // ぽ-れ
      'o', 'a', 'a', 'i', 'e', 'o', 0,   'u', 'a', 'e',                                // ろ-ゖ
  };

  char vowel = kVowelTable[c - kHiraganaStart];
  switch (vowel) {
    case 'a':
      return U'あ';
    case 'i':
      return U'い';
    case 'u':
      return U'う';
    case 'e':
      return U'え';
    case 'o':
      return U'お';
    default:
      return 0;
  }
}

void addEmphaticVariants(std::vector<UnknownCandidate>& candidates, const std::vector<char32_t>& codepoints) {
  std::vector<UnknownCandidate> emphatic_variants;

  for (const auto& cand : candidates) {
    // Only extend verb and adjective candidates
    if (cand.pos != core::PartOfSpeech::Verb && cand.pos != core::PartOfSpeech::Adjective) {
      continue;
    }

    // Check if there are emphatic characters after the candidate
    size_t emphatic_end = cand.end;
    std::string emphatic_suffix;

    while (emphatic_end < codepoints.size()) {
      char32_t c = codepoints[emphatic_end];
      if (isEmphaticChar(c)) {
        // Stop before a sokuon that begins a te/ta-form, colloquial auxiliary
        // (っす/っさ/っせ) or Godan quotative (っと): it is a separate morpheme.
        if ((c == core::hiragana::kSmallTsu || c == U'ッ') && cand.end > 0 &&
            isSuppressedSokuonOnset(codepoints, emphatic_end, cand.pos, codepoints[cand.end - 1])) {
          break;
        }
        emphatic_suffix += normalize::encodeUtf8(c);
        ++emphatic_end;
      } else {
        break;
      }
    }

    // Track standard emphatic chars separately for cost calculation
    size_t standard_emphatic_chars = emphatic_suffix.size() / core::kJapaneseCharBytes;

    // Also check for repeated vowels matching the final character's vowel
    size_t vowel_repeat_count = 0;
    if (cand.end > 0 && emphatic_end < codepoints.size()) {
      char32_t final_char = codepoints[cand.end - 1];
      char32_t expected_vowel = getHiraganaVowel(final_char);

      if (expected_vowel != 0) {
        size_t vowel_start = emphatic_end;

        // Count consecutive occurrences of the expected vowel
        while (emphatic_end < codepoints.size() && codepoints[emphatic_end] == expected_vowel) {
          ++vowel_repeat_count;
          ++emphatic_end;
        }

        // Require at least 2 repeated vowels for emphatic pattern
        if (vowel_repeat_count >= 2) {
          for (size_t i = 0; i < vowel_repeat_count; ++i) {
            emphatic_suffix += normalize::encodeUtf8(expected_vowel);
          }
        } else {
          // Not enough repetition, reset position
          emphatic_end = vowel_start;
          vowel_repeat_count = 0;
        }
      }
    }

    // Add emphatic variant if we found any emphatic characters
    if (!emphatic_suffix.empty()) {
      UnknownCandidate emphatic_cand = cand;
      emphatic_cand.surface += emphatic_suffix;
      emphatic_cand.end = emphatic_end;
      float cost_adjustment;

      if (vowel_repeat_count >= 2) {
        // Give a BONUS for vowel repetition to compete with split alternatives
        float char_count = static_cast<float>(emphatic_suffix.size() / core::kJapaneseCharBytes);
        cost_adjustment = -0.5F + 0.05F * char_count;
      } else {
        // Standard emphatic chars (sokuon/chouon/small vowels) use penalty
        cost_adjustment = 0.3F * static_cast<float>(standard_emphatic_chars);
      }
      emphatic_cand.cost += cost_adjustment;
#ifdef SUZUME_DEBUG_INFO
      emphatic_cand.pattern += "_emphatic";
#endif
      emphatic_variants.push_back(std::move(emphatic_cand));
    }
  }

  // Add all emphatic variants
  for (auto& var : emphatic_variants) {
    candidates.push_back(std::move(var));
  }
}

// =============================================================================
// Pattern Skip Helpers
// =============================================================================

bool shouldSkipMasuAuxPattern(std::string_view surface, grammar::VerbType verb_type) {
  // Check if surface ends with ます/ました/ましょう/ません
  bool has_masu_aux = utf8::endsWith(surface, "ましょう") || utf8::endsWith(surface, "ました") ||
                      utf8::endsWith(surface, "ません") || utf8::endsWith(surface, "ます");

  if (!has_masu_aux) {
    return false;
  }

  // Don't skip suru-verb passive/causative patterns (され, させ)
  bool is_suru_passive_causative =
      (verb_type == grammar::VerbType::Suru && utf8::containsAny(surface, {"され", "させ"}));

  return !is_suru_passive_causative;
}

bool shouldSkipSouPattern(std::string_view surface, grammar::VerbType verb_type) {
  // Check for そう/そうです/そうだ at end
  bool has_sou_pattern = utf8::endsWith(surface, "そうです") || utf8::endsWith(surface, "そうだ") ||
                         utf8::endsWith(surface, scorer::kSuffixSou);

  // Don't skip i-adjective patterns
  return has_sou_pattern && verb_type != grammar::VerbType::IAdjective;
}

bool isCompoundAdjectivePattern(std::string_view surface) {
  if (surface.size() < core::kFourJapaneseCharBytes) {
    return false;
  }
  // Check for auxiliary adjective patterns in various conjugation forms
  if (utf8::containsAny(surface, {
                                     "にくい", "にくく", "にくか", "にくけ", "にくさ",  // difficult to do
                                     "やすい", "やすく", "やすか", "やすけ", "やすさ",  // easy to do
                                     "がたい", "がたく", "がたか", "がたけ", "がたさ"   // hard to do
                                 })) {
    return true;
  }
  // Also check stem forms at end of surface (e.g., 使いにく for 使いにく+い split)
  return utf8::endsWith(surface, "にく") || utf8::endsWith(surface, "やす") || utf8::endsWith(surface, "がた");
}

bool containsKuNaruPattern(std::string_view surface) {
  return surface.find("くなっ") != std::string::npos || surface.find("くなり") != std::string::npos ||
         surface.find("くなる") != std::string::npos || surface.find("くなれ") != std::string::npos;
}

bool endsWithKuNaruPattern(std::string_view surface) {
  return utf8::endsWith(surface, "くなる") || utf8::endsWith(surface, "くなっ") || utf8::endsWith(surface, "くなり") ||
         utf8::endsWith(surface, "くなれ") || utf8::endsWith(surface, "くなら") ||
         utf8::endsWith(surface, "くなった") || utf8::endsWith(surface, "くなって");
}

const std::vector<std::pair<grammar::VerbType, std::string_view>>& getGodanTypesByOnbin(std::string_view onbin) {
  return grammar::Conjugation::getGodanTypesByOnbin(onbin);
}

bool shouldSkipPassiveAuxPattern(std::string_view surface, grammar::VerbType verb_type) {
  // Skip patterns containing classical passive + べき
  if (utf8::endsWith(surface, "れべき")) {
    return true;
  }

  // Only apply remaining checks to Godan verbs
  if (!grammar::isGodanVerbType(verb_type)) {
    return false;
  }

  // Passive patterns: れる, れた, れて, れない, れます, れたい, れたく
  return utf8::endsWith(surface, "れる") || utf8::endsWith(surface, "れた") || utf8::endsWith(surface, "れて") ||
         utf8::endsWith(surface, "れない") || utf8::endsWith(surface, "れます") || utf8::endsWith(surface, "れたい") ||
         utf8::endsWith(surface, "れたく");
}

bool isPassiveAuxContinuation(const std::vector<char32_t>& codepoints, size_t pos_after_re, bool strict_masu) {
  if (pos_after_re >= codepoints.size()) {
    return false;
  }
  char32_t after_re = codepoints[pos_after_re];
  // れる, れた, れて
  if (after_re == U'る' || after_re == U'た' || after_re == U'て') {
    return true;
  }
  // れな (れない, れなかった)
  if (after_re == U'な' && pos_after_re + 1 < codepoints.size() && codepoints[pos_after_re + 1] == U'い') {
    return true;
  }
  // れま (れます, れました); the strict form requires す/せ (excludes bare ま)
  if (after_re == U'ま') {
    if (!strict_masu) {
      return true;
    }
    return pos_after_re + 1 < codepoints.size() &&
           (codepoints[pos_after_re + 1] == U'す' || codepoints[pos_after_re + 1] == U'せ');
  }
  return false;
}

bool shouldSkipCausativeAuxPattern(std::string_view surface, grammar::VerbType verb_type) {
  // Suru verb causative/passive: stay as single tokens
  if (verb_type == grammar::VerbType::Suru) {
    return false;
  }

  // Godan causative: せる, せた, せて
  if (grammar::isGodanVerbType(verb_type)) {
    return utf8::endsWith(surface, "せる") || utf8::endsWith(surface, "せた") || utf8::endsWith(surface, "せて");
  }

  // Causative-passive patterns for all verb types (including Ichidan)
  // E.g., 聞かせられた → 聞か + せ + られ + た (MeCab-compatible split)
  // These look like Ichidan verbs but contain causative+passive auxiliary chain
  if (utf8::endsWith(surface, "せられる") || utf8::endsWith(surface, "せられた") ||
      utf8::endsWith(surface, "せられて") || utf8::endsWith(surface, "せられない")) {
    return true;
  }
  return false;
}

namespace {

// Check if a hiragana tail analyzes as a conjugation of する with an auxiliary
// chain (して, しました, してもらっている); bare し and plain する have none.
bool isSuruAuxChainTail(std::string_view tail, const grammar::Inflection& inflection) {
  // Empty-stem する conjugations start with し/す/せ; される/させる need the
  // mizenkei さ with a stem (whole-surface check in the caller covers them)
  if (!utf8::startsWithAny(tail, {"し", "す", "せ"})) {
    return false;
  }
  if (utf8::equalsAny(tail, {"しろ", "せよ"})) {  // Imperatives carry no auxiliary chain
    return true;
  }
  for (const auto& cand : inflection.analyze(tail)) {
    if (cand.verb_type == grammar::VerbType::Suru && cand.stem.empty() && !cand.morphemes.empty()) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool shouldSkipSuruVerbAuxPattern(std::string_view surface, size_t kanji_count, const grammar::Inflection& inflection) {
  // Only apply to patterns with 2+ kanji (typical サ変 noun stems: 勉強, 対応)
  if (kanji_count < 2) {
    return false;
  }
  // Scan codepoint suffixes of the hiragana tail after the kanji run for a
  // する-auxiliary chain (勉強して, 空回りして) — ends-with semantics
  size_t tail_start = normalize::charToByteOffset(surface, kanji_count);
  std::string_view tail = surface.substr(std::min(tail_start, surface.size()));
  for (size_t pos = 0; pos < tail.size(); normalize::decodeUtf8(tail, pos)) {
    if (isSuruAuxChainTail(tail.substr(pos), inflection)) {
      return true;
    }
  }
  // される/させる need the mizenkei さ with a stem: use a whole-surface サ変
  // parse whose conjugated part starts with さ (対応される, 実行させた)
  for (const auto& cand : inflection.analyze(surface)) {
    if (cand.verb_type == grammar::VerbType::Suru && !cand.morphemes.empty() && utf8::startsWith(cand.suffix, "さ")) {
      return true;
    }
  }
  return false;
}

// =============================================================================
// Verb Type / Stem Analysis Helpers
// =============================================================================

std::string baseFormSuffix(grammar::VerbType verb_type) {
  if (verb_type == grammar::VerbType::Ichidan) {
    return "る";
  }
  const auto* row = grammar::Conjugation::getGodanRow(verb_type);
  if (row == nullptr) {
    return "";
  }
  return normalize::encodeUtf8(row->base_vowel);
}

bool isValidIRowIchidanStem(std::string_view stem) {
  if (stem.size() < 2 * core::kJapaneseCharBytes) {
    return false;
  }
  std::string_view last_char(stem.data() + stem.size() - core::kJapaneseCharBytes, core::kJapaneseCharBytes);
  if (!grammar::endsWithIRow(last_char)) {
    return false;
  }
  std::string_view kanji_part(stem.data(), stem.size() - core::kJapaneseCharBytes);
  bool is_single_kanji_i = (kanji_part.size() == core::kJapaneseCharBytes && last_char == "い");
  return !is_single_kanji_i;
}

bool containsTeFormAuxPattern(std::string_view surface) {
  for (size_t i = 0; i < scorer::kTeFormAuxPenaltyPatternsSize; ++i) {
    if (utf8::contains(surface, scorer::kTeFormAuxPenaltyPatterns[i])) {
      return true;
    }
  }
  return false;
}

bool containsCausativeAuxPattern(std::string_view surface) {
  for (size_t i = 0; i < scorer::kCausativeAuxPenaltyPatternsSize; ++i) {
    if (utf8::contains(surface, scorer::kCausativeAuxPenaltyPatterns[i])) {
      return true;
    }
  }
  return false;
}

VerbClassBests bestByVerbClass(const std::vector<grammar::InflectionCandidate>& candidates) {
  // Value-initialize so every field (including each accumulator's confidence) starts
  // at zero; the loop then keeps the highest-confidence candidate per verb class.
  VerbClassBests bests{};
  for (const auto& cand : candidates) {
    if (cand.has_explanatory_suffix) {
      continue;
    }
    if (cand.verb_type == grammar::VerbType::Ichidan && cand.confidence > bests.ichidan.confidence) {
      bests.ichidan = cand;
    }
    if (cand.verb_type == grammar::VerbType::Suru && cand.confidence > bests.suru.confidence) {
      bests.suru = cand;
    }
    if (grammar::isGodanVerbType(cand.verb_type) && cand.confidence > bests.godan.confidence) {
      bests.godan = cand;
    }
  }
  return bests;
}

}  // namespace suzume::analysis::verb_helpers
