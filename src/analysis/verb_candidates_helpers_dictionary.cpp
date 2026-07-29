/**
 * @file verb_candidates_helpers_dictionary.cpp
 * @brief Dictionary-backed verb candidate helpers
 */

#include "core/debug.h"
#include "normalize/char_type.h"
#include "normalize/utf8.h"
#include "verb_candidates_helpers.h"

namespace suzume::analysis::verb_helpers {

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

bool hasNominalHostBefore(const std::vector<char32_t>& codepoints, size_t start_pos) {
  if (start_pos == 0 || start_pos > codepoints.size()) {
    return false;
  }
  const normalize::CharType host_end = normalize::classifyChar(codepoints[start_pos - 1]);
  return host_end == normalize::CharType::Kanji || host_end == normalize::CharType::Katakana;
}

bool isBoundSuffixAfterNominalHost(const dictionary::DictionaryManager* dict_manager,
                                   const std::vector<char32_t>& codepoints, size_t start_pos,
                                   std::string_view surface) {
  return hasNominalHostBefore(codepoints, start_pos) &&
         hasDictionaryEntry(dict_manager, surface, core::PartOfSpeech::Suffix);
}

bool hasDictionaryEntry(const dictionary::DictionaryManager* dict_manager, std::string_view surface,
                        core::PartOfSpeech pos) {
  if (dict_manager == nullptr || surface.empty()) {
    return false;
  }
  const auto* entry = dict_manager->lookupExact(surface, pos);
  if (entry != nullptr) {
    SUZUME_DEBUG_LOG_TRACE("[DICT] \"" << surface << "\" (" << core::posToString(pos) << "/"
                                       << core::extendedPosToString(entry->extended_pos) << ") = FOUND\n");
    return true;
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
  return dict_manager != nullptr && dict_manager->lookupExact(surface, core::PartOfSpeech::Particle) != nullptr;
}

bool hasCaseParticleDictionaryEntry(const dictionary::DictionaryManager* dict_manager, std::string_view surface) {
  if (dict_manager == nullptr) {
    return false;
  }
  const auto* entry = dict_manager->lookupExact(surface, core::PartOfSpeech::Particle);
  return entry != nullptr && entry->extended_pos == core::ExtendedPOS::ParticleCase;
}

bool isCommaClauseChainingRenyokei(const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos,
                                   const dictionary::DictionaryManager* dict_manager) {
  if (dict_manager == nullptr || start_pos == 0 || end_pos >= codepoints.size() || codepoints[end_pos] != U'、') {
    return false;
  }
  const std::string particle_surface = extractSubstring(codepoints, start_pos - 1, start_pos);
  const auto* particle = dict_manager->lookupExact(particle_surface, core::PartOfSpeech::Particle);
  const bool follows_argument = particle != nullptr && particle->extended_pos == core::ExtendedPOS::ParticleCase &&
                                particle_surface != "と" && particle_surface != "で";
  if (follows_argument) {
    return true;
  }

  // A quantified focus phrase also supplies a predicate boundary
  // (何度も+試み、). Restrict this to the closed counter property so a
  // noun in an enumerated …も、 sequence does not become verbal evidence.
  return start_pos >= 2 && codepoints[start_pos - 1] == U'も' && normalize::isCounterKanji(codepoints[start_pos - 2]);
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

bool embedsAuxiliaryOnOnbinStem(const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos,
                                const dictionary::DictionaryManager* dict_manager) {
  if (dict_manager == nullptr || end_pos > codepoints.size()) {
    return false;
  }
  for (size_t aux_start = start_pos + 1; aux_start < end_pos; ++aux_start) {
    const char32_t onbin = codepoints[aux_start - 1];
    if (onbin != U'い' && onbin != U'ん' && onbin != U'っ') {
      continue;
    }
    for (size_t aux_end = aux_start + 1; aux_end <= end_pos; ++aux_end) {
      if (dict_manager->lookupExact(extractSubstring(codepoints, aux_start, aux_end), core::PartOfSpeech::Auxiliary) !=
          nullptr) {
        return true;
      }
    }
  }
  return false;
}

bool auxiliaryFollowsAt(const dictionary::DictionaryManager* dict_manager, const std::vector<char32_t>& codepoints,
                        size_t pos, bool (*accept)(core::ExtendedPOS)) {
  if (dict_manager == nullptr || pos >= codepoints.size()) {
    return false;
  }
  constexpr size_t kAuxiliaryProbe = 3;
  const size_t max_end = std::min(codepoints.size(), pos + kAuxiliaryProbe);
  for (size_t aux_end = pos + 1; aux_end <= max_end; ++aux_end) {
    const auto* entry =
        dict_manager->lookupExact(extractSubstring(codepoints, pos, aux_end), core::PartOfSpeech::Auxiliary);
    if (entry != nullptr && accept(entry->extended_pos)) {
      return true;
    }
  }
  return false;
}

bool classicalAuxiliaryFollowsAt(const dictionary::DictionaryManager* dict_manager,
                                 const std::vector<char32_t>& codepoints, size_t pos) {
  if (dict_manager == nullptr || pos >= codepoints.size()) {
    return false;
  }
  constexpr size_t kAuxiliaryProbe = 3;
  const size_t max_end = std::min(codepoints.size(), pos + kAuxiliaryProbe);
  for (size_t aux_end = pos + 1; aux_end <= max_end; ++aux_end) {
    const std::string span = extractSubstring(codepoints, pos, aux_end);
    const auto* entry = dict_manager->lookupExact(span, core::PartOfSpeech::Auxiliary);
    if (entry == nullptr || !core::isClassicalAuxiliaryType(entry->extended_pos)) {
      continue;
    }
    // A final particle closes a clause after any word class, so a spelling that
    // can be one says nothing about what precedes it: the 已然形 ね of ぬ and the
    // 終助詞 ね are the same mora, and only the latter stands after a noun.
    const auto* particle = dict_manager->lookupExact(span, core::PartOfSpeech::Particle);
    if (particle != nullptr && particle->extended_pos == core::ExtendedPOS::ParticleFinal) {
      continue;
    }
    return true;
  }
  return false;
}

bool predicateAuxiliaryFollowsAt(const dictionary::DictionaryManager* dict_manager,
                                 const std::vector<char32_t>& codepoints, size_t pos) {
  if (dict_manager == nullptr || pos >= codepoints.size()) {
    return false;
  }
  constexpr size_t kAuxiliaryProbe = 4;
  const size_t max_end = std::min(codepoints.size(), pos + kAuxiliaryProbe);
  for (size_t aux_end = pos + 1; aux_end <= max_end; ++aux_end) {
    const auto* entry =
        dict_manager->lookupExact(extractSubstring(codepoints, pos, aux_end), core::PartOfSpeech::Auxiliary);
    if (entry != nullptr && entry->extended_pos != core::ExtendedPOS::AuxCopulaDa &&
        entry->extended_pos != core::ExtendedPOS::AuxCopulaDesu) {
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

bool startsWithFocusParticleHead(const dictionary::DictionaryManager* dict_manager,
                                 const std::vector<char32_t>& codepoints, size_t hiragana_start, size_t end_pos) {
  if (dict_manager == nullptr || end_pos < hiragana_start + 2 || end_pos > codepoints.size()) {
    return false;
  }
  // Longest focus particle in the closed class is four codepoints (どころか).
  constexpr size_t kMaxParticleLen = 4;
  const size_t max_len = std::min(kMaxParticleLen, end_pos - hiragana_start);
  for (size_t particle_len = 2; particle_len <= max_len; ++particle_len) {
    const size_t particle_end = hiragana_start + particle_len;
    const dictionary::DictionaryEntry* entry =
        dict_manager->lookupExact(extractSubstring(codepoints, hiragana_start, particle_end));
    if (entry == nullptr || (entry->extended_pos != core::ExtendedPOS::ParticleAdverbial &&
                             entry->extended_pos != core::ExtendedPOS::ParticleBinding)) {
      continue;
    }
    // An adjective past keeps っ right after the coinciding kana (美味しかっ +
    // た), so that sequence is genuine okurigana rather than a particle.
    if (particle_end < end_pos && codepoints[particle_end] == U'っ') {
      continue;
    }
    return true;
  }
  return false;
}

bool embedsCaseParticle(const dictionary::DictionaryManager* dict_manager, const std::vector<char32_t>& codepoints,
                        size_t start_pos, size_t end_pos) {
  if (dict_manager == nullptr || end_pos < start_pos + 3 || end_pos > codepoints.size()) {
    return false;
  }
  // Longest case particle in the closed class is three codepoints (からの/より).
  constexpr size_t kMaxParticleLen = 3;
  for (size_t particle_start = start_pos + 1; particle_start + 1 < end_pos; ++particle_start) {
    // が opens the productive derivational suffixes がまし〜 / がる / がたい, which
    // attach straight to a nominal or a continuative and so put the same mora
    // inside a single derived word (未練がましい, 恩着せがましさ, 欲しがる). Its
    // ambiguity is lexical rather than structural, so it is left to the
    // confidence model instead of being rejected outright here.
    if (codepoints[particle_start] == U'が') {
      continue;
    }
    const size_t max_len = std::min(kMaxParticleLen, end_pos - particle_start - 1);
    for (size_t particle_len = 1; particle_len <= max_len; ++particle_len) {
      const auto* entry =
          dict_manager->lookupExact(extractSubstring(codepoints, particle_start, particle_start + particle_len));
      if (entry != nullptr && entry->extended_pos == core::ExtendedPOS::ParticleCase) {
        return true;
      }
    }
  }
  return false;
}

size_t negativeAuxiliaryLengthAt(const dictionary::DictionaryManager* dict_manager,
                                 const std::vector<char32_t>& codepoints, size_t pos) {
  if (dict_manager == nullptr || pos >= codepoints.size()) {
    return 0;
  }
  // Longest negative auxiliary in the closed class is four codepoints (なけりゃ).
  constexpr size_t kMaxAuxLen = 4;
  constexpr size_t kMinAuxLen = 2;
  const size_t max_len = std::min(kMaxAuxLen, codepoints.size() - pos);
  for (size_t aux_len = max_len; aux_len >= kMinAuxLen; --aux_len) {
    const auto* entry =
        dict_manager->lookupExact(extractSubstring(codepoints, pos, pos + aux_len), core::PartOfSpeech::Auxiliary);
    if (entry != nullptr && (entry->extended_pos == core::ExtendedPOS::AuxNegativeNai ||
                             entry->extended_pos == core::ExtendedPOS::AuxNegativeNu)) {
      return aux_len;
    }
  }
  return 0;
}

bool opensOnClosedClassWordTail(const dictionary::DictionaryManager* dict_manager,
                                const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos) {
  if (dict_manager == nullptr || start_pos == 0 || end_pos < start_pos + 2 || end_pos > codepoints.size()) {
    return false;
  }
  // The closed class holds nothing longer than a handful of morae, so the scan
  // back is bounded rather than running to the start of the sentence.
  constexpr size_t kMaxClosedClassLen = 5;
  const size_t scan_start = start_pos - std::min(start_pos, kMaxClosedClassLen - 1);
  for (size_t word_start = scan_start; word_start < start_pos; ++word_start) {
    const size_t max_end = std::min(end_pos - 1, word_start + kMaxClosedClassLen);
    for (size_t word_end = start_pos + 1; word_end <= max_end; ++word_end) {
      const std::string word = extractSubstring(codepoints, word_start, word_end);
      if (dict_manager->lookupExact(word, core::PartOfSpeech::Auxiliary) != nullptr ||
          dict_manager->lookupExact(word, core::PartOfSpeech::Particle) != nullptr) {
        return true;
      }
    }
  }
  return false;
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
  const std::string remaining = extractClosedClassProbe(codepoints, pos);
  return lookupResultsHaveExtendedPOS(dict_manager->lookup(remaining, 0), core::ExtendedPOS::NounFormal);
}

std::string lookupVerbLemma(const dictionary::DictionaryManager* dict_manager, std::string_view surface,
                            std::string_view fallback) {
  if (dict_manager != nullptr) {
    const auto* entry = dict_manager->lookupExact(surface, core::PartOfSpeech::Verb);
    if (entry != nullptr && !entry->lemma.empty()) {
      return entry->lemma;
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

}  // namespace suzume::analysis::verb_helpers
