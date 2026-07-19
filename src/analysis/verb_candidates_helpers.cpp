/**
 * @file verb_candidates_helpers.cpp
 * @brief Implementation of internal helpers for verb candidate generation
 */

#include "verb_candidates_helpers.h"

#include <utility>

#include "analysis/candidate_constants.h"
#include "analysis/scorer_constants.h"
#include "core/debug.h"
#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "grammar/conjugation.h"
#include "normalize/char_type.h"
#include "normalize/utf8.h"

namespace suzume::analysis::verb_helpers {

bool embedsTeFormAuxiliary(std::string_view surface) {
  static constexpr std::string_view kPatterns[] = {
      "ていく", "ていっ", "ていけ", "ていか",                // 〜ていく directional aspect
      "てもら", "てくれ", "てあげ", "てほしい", "てくださ",  // benefactive / request
      "てある", "である",                                    // completed-state existential
  };
  for (const std::string_view pattern : kPatterns) {
    if (surface.find(pattern) != std::string_view::npos) {
      return true;
    }
  }
  return false;
}

bool embedsTeFormMiruAuxiliary(const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos) {
  if (end_pos > codepoints.size()) {
    return false;
  }
  for (size_t pos = start_pos + 1; pos + 1 < end_pos; ++pos) {
    if ((codepoints[pos] == core::hiragana::kTe || codepoints[pos] == U'で') && codepoints[pos + 1] == U'み') {
      return true;
    }
  }
  return false;
}

bool masuAuxFollowsAt(const std::vector<char32_t>& codepoints, size_t pos) {
  if (pos + 1 >= codepoints.size() || codepoints[pos] != U'ま') {
    return false;
  }
  const char32_t next = codepoints[pos + 1];
  return next == U'す' || next == U'し' || next == U'せ';
}

bool causativeSaseFollowsAt(const std::vector<char32_t>& codepoints, size_t pos) {
  return pos + 1 < codepoints.size() && codepoints[pos] == U'さ' && codepoints[pos + 1] == U'せ';
}

bool isSuruAuxiliaryStarter(char32_t next_char) {
  return next_char == U'ち' || next_char == U'て' || next_char == U'た' || next_char == U'な' || next_char == U'ま' ||
         next_char == U'よ' || next_char == U'ろ' || next_char == U'そ' || next_char == U'と' || next_char == U'か' ||
         next_char == U'つ';
}

bool naiNegativeFollowsAt(const std::vector<char32_t>& codepoints, size_t pos) {
  if (pos + 1 >= codepoints.size() || codepoints[pos] != U'な') {
    return false;
  }
  const char32_t second = codepoints[pos + 1];
  if (second == U'い' || second == U'く') {
    return true;
  }
  if (pos + 2 >= codepoints.size()) {
    return false;
  }
  const char32_t third = codepoints[pos + 2];
  return (second == U'か' && third == U'っ') || (second == U'け' && (third == U'れ' || third == U'り')) ||
         (second == U'き' && third == U'ゃ');
}

bool naiConditionalFollowsAt(const std::vector<char32_t>& codepoints, size_t pos) {
  return pos + 2 < codepoints.size() && codepoints[pos] == U'な' && codepoints[pos + 1] == U'け' &&
         codepoints[pos + 2] == U'れ';
}

bool itadakuParadigmStartsAt(const std::vector<char32_t>& codepoints, size_t pos) {
  if (pos + 3 >= codepoints.size() || codepoints[pos] != U'い' || codepoints[pos + 1] != U'た' ||
      codepoints[pos + 2] != U'だ') {
    return false;
  }
  const char32_t inflected = codepoints[pos + 3];
  return inflected == U'か' || inflected == U'き' || inflected == U'く' || inflected == U'け' || inflected == U'こ' ||
         inflected == U'い';
}

bool hasInternalVerbChainBoundary(const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos,
                                  const grammar::Inflection& inflection,
                                  const dictionary::DictionaryManager* dict_manager) {
  if (dict_manager == nullptr || end_pos <= start_pos + 3) {
    return false;
  }
  auto is_verified_verb_form = [&](size_t form_start, size_t form_end) {
    const std::string form = extractSubstring(codepoints, form_start, form_end);
    if (hasDictionaryEntry(dict_manager, form, core::PartOfSpeech::Verb) ||
        hasDictionaryEntry(dict_manager, form, core::PartOfSpeech::Auxiliary)) {
      return true;
    }
    if (form_end > form_start + 1 &&
        (codepoints[form_end - 1] == U'っ' || codepoints[form_end - 1] == U'ん' || codepoints[form_end - 1] == U'い')) {
      const std::string stem = extractSubstring(codepoints, form_start, form_end - 1);
      const std::string onbin = extractSubstring(codepoints, form_end - 1, form_end);
      if (firstGodanOnbinDictBase(dict_manager, stem, onbin).matched) {
        return true;
      }
    }
    for (const auto& candidate : inflection.analyze(form)) {
      if (candidate.verb_type != grammar::VerbType::IAdjective &&
          (isVerbInDictionary(dict_manager, candidate.base_form) ||
           hasDictionaryEntry(dict_manager, candidate.base_form, core::PartOfSpeech::Auxiliary))) {
        return true;
      }
    }
    if (form_end > form_start) {
      const std::string_view base_suffix = grammar::godanBaseSuffixFromARow(codepoints[form_end - 1]);
      if (!base_suffix.empty()) {
        const std::string stem = extractSubstring(codepoints, form_start, form_end - 1);
        if (isVerbInDictionary(dict_manager, stem + std::string(base_suffix))) {
          return true;
        }
      }
    }
    return false;
  };

  for (size_t connective_pos = start_pos + 2; connective_pos + 2 < end_pos; ++connective_pos) {
    if (codepoints[connective_pos] != U'て' && codepoints[connective_pos] != U'で') {
      continue;
    }
    if (is_verified_verb_form(start_pos, connective_pos) && is_verified_verb_form(connective_pos + 1, end_pos)) {
      return true;
    }
  }
  for (size_t negative_pos = start_pos + 2; negative_pos + 1 < end_pos; ++negative_pos) {
    if (codepoints[negative_pos] == U'ず' && is_verified_verb_form(start_pos, negative_pos) &&
        is_verified_verb_form(negative_pos + 1, end_pos)) {
      return true;
    }
  }
  return false;
}

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

bool isVerbInDictionary(const dictionary::DictionaryManager* dict_manager, std::string_view base_form) {
  return hasDictionaryEntry(dict_manager, base_form, core::PartOfSpeech::Verb);
}

bool isAdjectiveInDictionary(const dictionary::DictionaryManager* dict_manager, std::string_view base_form) {
  return hasDictionaryEntry(dict_manager, base_form, core::PartOfSpeech::Adjective);
}

bool isNounInDictionary(const dictionary::DictionaryManager* dict_manager, std::string_view surface) {
  return hasDictionaryEntry(dict_manager, surface, core::PartOfSpeech::Noun);
}

bool isNounOrAdjectiveInDictionary(const dictionary::DictionaryManager* dict_manager, std::string_view surface) {
  return hasDictionaryEntry(dict_manager, surface, core::PartOfSpeech::Noun) ||
         hasDictionaryEntry(dict_manager, surface, core::PartOfSpeech::Adjective);
}

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

bool startsInsideDictionaryParticle(const std::vector<char32_t>& codepoints, size_t start_pos,
                                    const dictionary::DictionaryManager* dict_manager) {
  if (dict_manager == nullptr || start_pos == 0) {
    return false;
  }
  constexpr size_t kParticleLookback = 4;
  constexpr size_t kParticleProbe = 5;
  size_t first_start = start_pos > kParticleLookback ? start_pos - kParticleLookback : 0;
  size_t probe_end = std::min(codepoints.size(), start_pos + kParticleProbe);
  for (size_t particle_start = first_start; particle_start < start_pos; ++particle_start) {
    std::string probe = extractSubstring(codepoints, particle_start, probe_end);
    for (const auto& match : dict_manager->lookup(probe, 0)) {
      if (match.entry != nullptr && match.entry->pos == core::PartOfSpeech::Particle &&
          particle_start + normalize::utf8Length(match.entry->surface) > start_pos) {
        return true;
      }
    }
  }
  return false;
}

bool startsWithMultiMoraDictionaryParticle(const std::vector<char32_t>& codepoints, size_t start_pos,
                                           const dictionary::DictionaryManager* dict_manager) {
  if (dict_manager == nullptr || start_pos >= codepoints.size()) {
    return false;
  }
  constexpr size_t kMinimumParticleLength = 2;
  constexpr size_t kParticleProbe = 4;
  const size_t probe_end = std::min(codepoints.size(), start_pos + kParticleProbe);
  for (size_t particle_end = start_pos + kMinimumParticleLength; particle_end <= probe_end; ++particle_end) {
    const auto* entry = dict_manager->lookupExact(extractSubstring(codepoints, start_pos, particle_end));
    if (entry != nullptr && entry->extended_pos == core::ExtendedPOS::ParticleBinding) {
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
  // Strip a trailing inflecting auxiliary. A focus particle can precede a
  // negative (本だけない) or a copula (本だけだ / 本だけだった); neither
  // sequence belongs inside a fabricated lexical candidate.
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
  total_len = tail_end - start_pos;
  if (total_len >= 2 && codepoints[tail_end - 2] == U'だ' && codepoints[tail_end - 1] == U'っ') {
    tail_end -= 2;
  } else if (total_len >= 1 && codepoints[tail_end - 1] == U'だ') {
    --tail_end;
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

bool hasAuxiliaryNegativeBoundary(const dictionary::DictionaryManager* dict_manager,
                                  const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos) {
  if (dict_manager == nullptr || end_pos <= start_pos + 2 || end_pos > codepoints.size()) {
    return false;
  }
  auto has_exact_epos = [&](size_t span_start, size_t span_end, core::ExtendedPOS epos) {
    const auto* entry = dict_manager->lookupExact(extractSubstring(codepoints, span_start, span_end));
    return entry != nullptr && entry->extended_pos == epos;
  };
  for (size_t boundary = start_pos + 1; boundary + 1 < end_pos; ++boundary) {
    const std::string prefix = extractSubstring(codepoints, start_pos, boundary);
    const auto* prefix_entry = dict_manager->lookupExact(prefix);
    const bool is_closed_class_prefix =
        prefix_entry != nullptr && (prefix_entry->pos == core::PartOfSpeech::Auxiliary ||
                                    prefix_entry->extended_pos == core::ExtendedPOS::AuxExcessive);
    if (!is_closed_class_prefix) {
      continue;
    }
    for (size_t negative_end = boundary + 1; negative_end <= end_pos; ++negative_end) {
      if (!has_exact_epos(boundary, negative_end, core::ExtendedPOS::AuxNegativeNai)) {
        continue;
      }
      if (negative_end == end_pos || has_exact_epos(negative_end, end_pos, core::ExtendedPOS::AuxTenseTa)) {
        return true;
      }
    }
  }
  return false;
}

bool formalNounFollowsAt(const dictionary::DictionaryManager* dict_manager, const std::vector<char32_t>& codepoints,
                         size_t pos) {
  if (dict_manager == nullptr || pos >= codepoints.size()) {
    return false;
  }
  const std::string remaining = extractSubstring(codepoints, pos, codepoints.size());
  for (const auto& result : dict_manager->lookup(remaining, 0)) {
    if (result.entry != nullptr && result.entry->extended_pos == core::ExtendedPOS::NounFormal) {
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
  // Candidate lists are small and already close to generation order. A stable
  // insertion sort avoids pulling the generic introsort implementation into
  // WASM while keeping equal-cost candidates deterministic.
  for (size_t idx = 1; idx < candidates.size(); ++idx) {
    UnknownCandidate candidate = std::move(candidates[idx]);
    size_t insert_at = idx;
    while (insert_at > 0 && candidates[insert_at - 1].cost > candidate.cost) {
      candidates[insert_at] = std::move(candidates[insert_at - 1]);
      --insert_at;
    }
    candidates[insert_at] = std::move(candidate);
  }
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

namespace {

bool isSuppressedSokuonOnset(const std::vector<char32_t>& codepoints, size_t sokuon_pos, core::PartOfSpeech base_pos,
                             char32_t base_final, SokuonOnsetPolicy policy) {
  if (sokuon_pos + 1 >= codepoints.size()) {
    return false;
  }
  const char32_t next = codepoints[sokuon_pos + 1];
  if (next == U'す' || next == U'さ' || next == U'せ') {
    return true;
  }
  const bool u_row_verb = base_pos == core::PartOfSpeech::Verb && normalize::isURowHiragana(base_final);
  if (next == U'と') {
    return u_row_verb;
  }
  if (policy == SokuonOnsetPolicy::DictionaryEntry) {
    return next == core::hiragana::kTe && u_row_verb;
  }
  return next == core::hiragana::kTe || next == core::hiragana::kTa;
}

}  // namespace

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
      // A run of emphasis marks is itself a searchable colloquial form.
      // Keep modest elongation normalized (すごーい, やばいいい), but preserve
      // longer runs such as すごーーい and すごいいいい as their own lemma.
      if (cand.pos == core::PartOfSpeech::Adjective &&
          (emphatic.standard_char_count >= 2 || emphatic.repeated_vowel_count >= 3)) {
        emphatic_cand.lemma = emphatic_cand.surface;
      }
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

  // Sahen predicates are search-tokenized as a nominal stem plus the する
  // mizenkei and passive auxiliary: 勉強+さ+れる. Retaining a unified
  // 勉強される candidate hides that grammatical chain.
  if (verb_type == grammar::VerbType::Suru) {
    return utf8::endsWith(surface, "される") || utf8::endsWith(surface, "された") ||
           utf8::endsWith(surface, "されて") || utf8::endsWith(surface, "されない") ||
           utf8::endsWith(surface, "されます") || utf8::endsWith(surface, "されたい") ||
           utf8::endsWith(surface, "されたく");
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

  // An unverified Ichidan candidate ending in A-row + せ is the stem of a
  // Godan causative (読ま+せ, 書か+せ), not an independent verb. Dictionary
  // candidates remain available for lexicalized derivatives such as 泳がせる.
  if (verb_type == grammar::VerbType::Ichidan) {
    const auto codepoints = normalize::utf8::decode(surface);
    if (codepoints.size() >= 2 && grammar::isARowCodepoint(codepoints[codepoints.size() - 2]) &&
        codepoints.back() == U'せ') {
      return true;
    }
  }

  // Causative-passive and passive-causative patterns for all verb types
  // (including Ichidan). These look like Ichidan verbs but contain a voice
  // auxiliary chain, so retain each auxiliary boundary.
  // E.g., 聞かせられた → 聞か + せ + られ + た;
  //       書かれさせる → 書か + れ + させる.
  if (utf8::endsWith(surface, "せられる") || utf8::endsWith(surface, "せられた") ||
      utf8::endsWith(surface, "せられて") || utf8::endsWith(surface, "せられない") ||
      utf8::containsAny(surface, {"れさせ", "られさせ"})) {
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

bool containsPassiveCausativeAuxPattern(std::string_view surface) {
  return utf8::containsAny(surface, {"れさせ", "られさせ"});
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
