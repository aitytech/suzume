/**
 * @file verb_candidates_helpers.cpp
 * @brief Implementation of internal helpers for verb candidate generation
 */

#include "verb_candidates_helpers.h"

#include <algorithm>

#include "analysis/candidate_constants.h"
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

bool endsWithParticleTailOfPos(const dictionary::DictionaryManager* dict_manager,
                               const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos,
                               core::ExtendedPOS particle_pos) {
  if (dict_manager == nullptr || end_pos <= start_pos || end_pos > codepoints.size()) {
    return false;
  }
  // Strip a trailing negative auxiliary (ない / なかっ / なかった).
  size_t tail_end = end_pos;
  size_t total_len = end_pos - start_pos;
  if (total_len >= 4 && codepoints[end_pos - 4] == U'な' && codepoints[end_pos - 3] == U'か' &&
      codepoints[end_pos - 2] == U'っ' && codepoints[end_pos - 1] == U'た') {
    tail_end = end_pos - 4;
  } else if (total_len >= 3 && codepoints[end_pos - 3] == U'な' && codepoints[end_pos - 2] == U'か' &&
             codepoints[end_pos - 1] == U'っ') {
    tail_end = end_pos - 3;
  } else if (total_len >= 2 && codepoints[end_pos - 2] == U'な' && codepoints[end_pos - 1] == U'い') {
    tail_end = end_pos - 2;
  }
  // Probe particle suffixes of 2+ codepoints, keeping a non-empty prefix.
  for (size_t particle_len = 2; start_pos + particle_len < tail_end; ++particle_len) {
    std::string suffix = extractSubstring(codepoints, tail_end - particle_len, tail_end);
    const dictionary::DictionaryEntry* suffix_entry = dict_manager->lookupExact(suffix);
    if (suffix_entry != nullptr && suffix_entry->extended_pos == particle_pos) {
      return true;
    }
  }
  return false;
}

bool endsWithFocusParticleTail(const dictionary::DictionaryManager* dict_manager,
                               const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos) {
  return endsWithParticleTailOfPos(dict_manager, codepoints, start_pos, end_pos,
                                   core::ExtendedPOS::ParticleAdverbial) ||
         endsWithParticleTailOfPos(dict_manager, codepoints, start_pos, end_pos, core::ExtendedPOS::ParticleBinding);
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

EmphaticSuffixMatch matchEmphaticSuffix(const std::vector<char32_t>& codepoints, size_t base_end,
                                        core::PartOfSpeech base_pos, SokuonOnsetPolicy policy) {
  EmphaticSuffixMatch match;
  match.end = base_end;
  if (base_end == 0 || base_end > codepoints.size()) {
    return match;
  }

  while (match.end < codepoints.size() && isEmphaticChar(codepoints[match.end])) {
    const char32_t codepoint = codepoints[match.end];
    if (policy == SokuonOnsetPolicy::Candidate && (codepoint == core::hiragana::kSmallTsu || codepoint == U'ッ') &&
        isSuppressedSokuonOnset(codepoints, match.end, base_pos, codepoints[base_end - 1], policy)) {
      break;
    }
    match.suffix += normalize::encodeUtf8(codepoint);
    ++match.standard_char_count;
    ++match.end;
  }

  if (policy == SokuonOnsetPolicy::DictionaryEntry && match.suffix == "っ" && match.end < codepoints.size() &&
      isSuppressedSokuonOnset(codepoints, base_end, base_pos, codepoints[base_end - 1], policy)) {
    match.suffix.clear();
    match.standard_char_count = 0;
    return match;
  }

  const char32_t repeated_vowel = getHiraganaVowel(codepoints[base_end - 1]);
  if (repeated_vowel == 0 || match.end >= codepoints.size()) {
    return match;
  }

  const size_t vowel_start = match.end;
  while (match.end < codepoints.size() && codepoints[match.end] == repeated_vowel) {
    ++match.repeated_vowel_count;
    ++match.end;
  }
  if (match.repeated_vowel_count < candidate::kEmphaticMinRepeatedVowels) {
    match.repeated_vowel_count = 0;
    match.end = vowel_start;
    return match;
  }

  for (size_t idx = 0; idx < match.repeated_vowel_count; ++idx) {
    match.suffix += normalize::encodeUtf8(repeated_vowel);
  }

  return match;
}

float emphaticCostAdjustment(const EmphaticSuffixMatch& match) {
  if (match.repeated_vowel_count >= candidate::kEmphaticMinRepeatedVowels) {
    const auto char_count = static_cast<float>(match.standard_char_count + match.repeated_vowel_count);
    return candidate::kEmphaticRepeatedVowelBonus + candidate::kEmphaticRepeatedVowelLengthPenalty * char_count;
  }
  return candidate::kEmphaticCharacterPenalty * static_cast<float>(match.standard_char_count);
}

void addEmphaticVariants(std::vector<UnknownCandidate>& candidates, const std::vector<char32_t>& codepoints) {
  std::vector<UnknownCandidate> emphatic_variants;

  for (const auto& cand : candidates) {
    // Only extend verb and adjective candidates
    if (cand.pos != core::PartOfSpeech::Verb && cand.pos != core::PartOfSpeech::Adjective) {
      continue;
    }

    const auto emphatic = matchEmphaticSuffix(codepoints, cand.end, cand.pos);

    // Add emphatic variant if we found any emphatic characters
    if (!emphatic.empty()) {
      UnknownCandidate emphatic_cand = cand;
      emphatic_cand.surface += emphatic.suffix;
      emphatic_cand.end = emphatic.end;
      emphatic_cand.cost += emphaticCostAdjustment(emphatic);
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
         surface.find("くなる") != std::string::npos || surface.find("くなれ") != std::string::npos ||
         surface.find("くなら") != std::string::npos;
}

bool isReduplicatedShiiAdjectiveHead(const std::vector<char32_t>& codepoints, size_t start_pos) {
  if (start_pos + 5 >= codepoints.size()) {
    return false;
  }
  // Doubled two-character unit XYXY, compared by codepoint so the same rule
  // serves kanji and both kana scripts.
  if (codepoints[start_pos] != codepoints[start_pos + 2] || codepoints[start_pos + 1] != codepoints[start_pos + 3]) {
    return false;
  }
  if (codepoints[start_pos + 4] != U'し') {
    return false;
  }
  // い/く/か/け start the i-adjective inflection endings after し:
  // しい, しく(ない/て), しかっ(た)/しかろ(う), しけれ(ば).
  const char32_t onset = codepoints[start_pos + 5];
  return onset == U'い' || onset == U'く' || onset == U'か' || onset == U'け';
}

grammar::GodanOnbinRange getGodanTypesByOnbin(std::string_view onbin) {
  return grammar::Conjugation::getGodanTypesByOnbin(onbin);
}

GodanOnbinDictMatch firstGodanOnbinDictBase(const dictionary::DictionaryManager* dict_manager, std::string_view stem,
                                            std::string_view onbin) {
  for (const auto& [verb_type, base_suffix] : getGodanTypesByOnbin(onbin)) {
    std::string base_form = std::string(stem) + std::string(base_suffix);
    if (isVerbInDictionary(dict_manager, base_form)) {
      return GodanOnbinDictMatch{verb_type, std::move(base_form), base_suffix, true};
    }
  }
  return GodanOnbinDictMatch{};
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
  // E.g., 聞かせられた → 聞か + せ + られ + た.
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
  return utf8::containsAny(surface, scorer::kTeFormAuxPenaltyPatterns);
}

bool containsCausativeAuxPattern(std::string_view surface) {
  return utf8::containsAny(surface, scorer::kCausativeAuxPenaltyPatterns);
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
