/**
 * @file suffix_candidates_nominalized.cpp
 * @brief Nominalized noun candidate generation
 */

#include "candidate_constants.h"
#include "core/debug.h"
#include "core/utf8_constants.h"
#include "dictionary/dictionary.h"
#include "grammar/char_patterns.h"
#include "grammar/conjugation.h"
#include "grammar/inflection.h"
#include "normalize/char_type.h"
#include "normalize/exceptions.h"
#include "normalize/utf8.h"
#include "suffix_candidates.h"
#include "tokenizer_utils.h"
#include "unknown.h"
#include "verb_candidates_helpers.h"

namespace suzume::analysis {

namespace {

#ifdef SUZUME_DEBUG_INFO
// Reported confidence shared by the nominalized-noun candidates; debug metadata
// only, it never reaches the lattice score.
constexpr float kNominalizedNounReportedConfidence = 0.6F;
#endif

bool hasNominalizedNounParticleContinuation(const std::vector<char32_t>& codepoints, size_t end_pos,
                                            const dictionary::DictionaryManager* dict_manager) {
  return end_pos < codepoints.size() && normalize::isParticleCodepoint(codepoints[end_pos]) &&
         codepoints[end_pos] != U'て' && codepoints[end_pos] != U'で' &&
         !startsLongerNonParticleEntry(codepoints, end_pos, dict_manager);
}

// A genitive/adnominal の followed by a continuative-shaped word that closes
// at a clause boundary forms a complete noun phrase (雨の匂い。). Requiring
// both sides keeps attributive predicates such as 父の残した手紙 verbal.
bool isGenitiveClauseFinalNominal(const std::vector<char32_t>& codepoints,
                                  const std::vector<normalize::CharType>& char_types, size_t start_pos,
                                  size_t end_pos) {
  return start_pos > 0 && codepoints[start_pos - 1] == U'の' &&
         (end_pos == codepoints.size() ||
          (end_pos < char_types.size() && char_types[end_pos] == normalize::CharType::Symbol));
}

// A closed suffix beginning inside a mixed kanji+hiragana span is a stronger
// morpheme boundary than the generic one-hiragana nominalization candidate.
// The host is deliberately unrestricted: audience and derivational suffixes
// attach to an open class of nouns.  A lexical noun registered for the whole
// span remains intact.
bool hasClosedSuffixBoundary(const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos,
                             const dictionary::DictionaryManager* dict_manager) {
  if (dict_manager == nullptr) {
    return false;
  }
  const std::string whole = extractSubstring(codepoints, start_pos, end_pos);
  if (dict_manager->lookupExact(whole, core::PartOfSpeech::Noun) != nullptr) {
    return false;
  }
  for (size_t split = start_pos + 1; split < end_pos; ++split) {
    const std::string suffix = extractSubstring(codepoints, split, end_pos);
    if (dict_manager->lookupExact(suffix, core::PartOfSpeech::Suffix) != nullptr) {
      return true;
    }
  }
  return false;
}

}  // namespace

void generateNominalizedNounCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                       const std::vector<normalize::CharType>& char_types,
                                       const dictionary::DictionaryManager* dict_manager,
                                       std::vector<UnknownCandidate>& candidates) {
  if (start_pos >= char_types.size() || char_types[start_pos] != normalize::CharType::Kanji) {
    return;
  }

  // Find kanji portion (typically 1-3 characters for nominalized nouns)
  size_t kanji_end = findCharRegionEnd(char_types, start_pos, 4, normalize::CharType::Kanji);

  // Need at least 1 kanji
  if (kanji_end == start_pos) {
    return;
  }

  // Look for 1-2 hiragana after kanji (nominalization endings)
  if (kanji_end >= char_types.size() || char_types[kanji_end] != normalize::CharType::Hiragana) {
    return;
  }

  char32_t first_hiragana = codepoints[kanji_end];

  // Skip particles that never form nominalizations
  if (normalize::isParticleCodepoint(first_hiragana)) {
    return;
  }

  // Common nominalization endings (renyokei stems)
  bool is_nominalization_ending =
      (first_hiragana == U'け' || first_hiragana == U'げ' || first_hiragana == U'せ' || first_hiragana == U'い' ||
       first_hiragana == U'り' || first_hiragana == U'ち' || first_hiragana == U'き' || first_hiragana == U'ぎ' ||
       first_hiragana == U'し' || first_hiragana == U'ま' || first_hiragana == U'み' || first_hiragana == U'び' ||
       first_hiragana == U'え' || first_hiragana == U'れ' || first_hiragana == U'め');

  if (!is_nominalization_ending) {
    return;
  }

  // Do not turn the first mora of a dictionary particle into a nominalized
  // noun. The particle candidate owns the whole span (最後|まで, not 最後ま|で).
  bool begins_particle = false;
  if (dict_manager != nullptr) {
    size_t probe_end = std::min(codepoints.size(), kanji_end + static_cast<size_t>(4));
    std::string particle_probe = extractSubstring(codepoints, kanji_end, probe_end);
    for (const auto& match : dict_manager->lookup(particle_probe, 0)) {
      if (match.entry != nullptr && match.entry->pos == core::PartOfSpeech::Particle &&
          normalize::utf8Length(match.entry->surface) > 1) {
        begins_particle = true;
        break;
      }
    }
  }
  if (begins_particle) {
    return;
  }

  // Skip potential suru-verb patterns: 漢字2字+し followed by suru-auxiliary
  // e.g., 勉強しちゃった → 勉強 + し + ちゃっ + た (not 勉強し + ちゃった)
  size_t kanji_count = kanji_end - start_pos;
  // For sahen-compatible 2+ kanji nouns, せ is mizenkei (勉強せよ), not a
  // nominalization ending. Skip nominalized noun candidate here so the
  // 勉強+せよ dictionary path can win.
  if (first_hiragana == U'せ' && kanji_count >= 2) {
    size_t next_pos = kanji_end + 1;
    if (next_pos < codepoints.size()) {
      char32_t next_char = codepoints[next_pos];
      // せ followed by imperative よ, passive ら/れ, causative ら, etc.
      if (next_char == U'よ' || next_char == U'ら' || next_char == U'れ' || next_char == U'ず') {
        return;
      }
    }
  }
  if (first_hiragana == U'し' && kanji_count >= 2) {
    // Check for suru-auxiliary patterns following し
    size_t next_pos = kanji_end + 1;
    if (next_pos < codepoints.size()) {
      char32_t next_char = codepoints[next_pos];
      if (verb_helpers::isSuruAuxiliaryStarter(next_char)) {
        // This looks like a suru-verb pattern - skip nominalization
        return;
      }
      // Kanji after し indicates suru-verb renyoukei + kanji verb/noun
      // e.g., 解決し得ない → 解決+し+得+ない (not 解決し+得ない)
      if (next_pos < char_types.size() && char_types[next_pos] == normalize::CharType::Kanji) {
        return;
      }
    }
  }
  // A kanji+し token that is NOT a genuine deverbal noun (last kanji + す ∉ dict)
  // is a sahen renyokei that must split off, not a nominalized noun. Apply this
  // only to a multi-kanji stem (遅刻し→遅刻+し, 遅刻す∉dict) or a fragment starting
  // mid kanji-run (刻し inside 遅刻し, 刻す∉dict); a standalone single kanji + し is
  // left alone so the classical adjective-stem nominalization stays a noun
  // (寒し, 美し — both 寒す/美す ∉ dict). Deverbal compounds keep the noun reading
  // regardless of position (丸出し→出す, 手渡し→渡す, 年越し→越す, 話し→話す).
  const bool preceded_by_kanji = start_pos > 0 && char_types[start_pos - 1] == normalize::CharType::Kanji;
  if (first_hiragana == U'し' && dict_manager != nullptr && (kanji_count >= 2 || preceded_by_kanji)) {
    std::string_view base_ending = grammar::godanBaseSuffixFromIRow(first_hiragana);
    std::string verb_base = normalize::encodeUtf8(codepoints[kanji_end - 1]) + std::string(base_ending);
    if (!verb_helpers::isVerbInDictionary(dict_manager, verb_base)) {
      return;
    }
  }

  // Check for 1 or 2 hiragana (e.g., け or 上げ)
  size_t hiragana_end = kanji_end + 1;

  // Check for 2-hiragana patterns if second char is also valid
  if (hiragana_end < char_types.size() && char_types[hiragana_end] == normalize::CharType::Hiragana) {
    char32_t second_hiragana = codepoints[hiragana_end];
    const bool second_starts_classical_conjectural_auxiliary =
        grammar::startsClassicalConjecturalAuxiliary(extractSubstring(codepoints, hiragana_end, codepoints.size()));
    // Common 2-char nominalization endings
    // Note: い is excluded — kanji+2hira ending in い is overwhelmingly
    // i-adjective (美しい, 正しい, 激しい), not nominalized noun
    if (second_hiragana == U'げ' || second_hiragana == U'け' || second_hiragana == U'り' || second_hiragana == U'え' ||
        second_hiragana == U'し') {
      // Trailing し followed by a suru-auxiliary (or kanji) is ichidan renyokei
      // + する (お伝えします, お届けして), not a nominalization — skip the noun
      // so the verb split can win. し at end of text or before a particle keeps
      // the noun candidate (genuine nominalizations survive).
      bool trailing_shi_is_suru = false;
      if (second_hiragana == U'し') {
        size_t after_shi_pos = hiragana_end + 1;
        if (after_shi_pos < codepoints.size()) {
          char32_t after_shi = codepoints[after_shi_pos];
          if (verb_helpers::isSuruAuxiliaryStarter(after_shi) ||
              (after_shi_pos < char_types.size() && char_types[after_shi_pos] == normalize::CharType::Kanji)) {
            trailing_shi_is_suru = true;
          }
        }
      }
      // Generate 2-hiragana candidate
      if (!trailing_shi_is_suru && !second_starts_classical_conjectural_auxiliary) {
        std::string surface = extractSubstring(codepoints, start_pos, hiragana_end + 1);
        if (!surface.empty()) {
          float nom2_cost = 0.8F;
          const bool has_particle_continuation =
              hasNominalizedNounParticleContinuation(codepoints, hiragana_end + 1, dict_manager);
          if (has_particle_continuation ||
              isGenitiveClauseFinalNominal(codepoints, char_types, start_pos, hiragana_end + 1)) {
            nom2_cost += candidate::kNominalizedNounParticleBonus;
          }
          auto cand = makeCandidate(surface, start_pos, hiragana_end + 1, core::PartOfSpeech::Noun, nom2_cost,
                                    has_particle_continuation, CandidateOrigin::NominalizedNoun);
#ifdef SUZUME_DEBUG_INFO
          cand.confidence = 0.8F;
          cand.pattern = "nominalized_2hira";
#endif
          candidates.push_back(cand);
        }
      }
    }
  }

  // Generate 1-hiragana candidate
  bool skip_single_char =
      grammar::startsClassicalConjecturalAuxiliary(extractSubstring(codepoints, kanji_end + 1, codepoints.size()));
  if (kanji_end + 1 < char_types.size() && char_types[kanji_end + 1] == normalize::CharType::Hiragana) {
    char32_t next_char = codepoints[kanji_end + 1];
    if (next_char == U'な') {
      skip_single_char = true;
    }
  }
  // Skip kanji+い when kanji ends with 的 (teki na-adjective suffix)
  // 理性的い, 経済的い don't make sense — 的 forms na-adjectives, not i-adjectives
  if (first_hiragana == U'い' && kanji_end > start_pos) {
    char32_t last_kanji = codepoints[kanji_end - 1];
    if (last_kanji == U'的') {
      skip_single_char = true;
    }
  }
  // Skip kanji+い followed by た/て: this い is godan-ka i-onbin forming a
  // past/te-form verb (続いた, 書いて), not a nominalized renyokei. True
  // nominalized nouns (間違い, 度合い) never take past た directly.
  // だ/で are intentionally excluded: copula after a real nominalization
  // (度合いだ) must keep the noun candidate, and godan-ga onbin (泳いだ)
  // is on the だ/で side as well.
  if (first_hiragana == U'い' && kanji_end + 1 < codepoints.size()) {
    char32_t after_i = codepoints[kanji_end + 1];
    if (after_i == U'た' || after_i == U'て') {
      skip_single_char = true;
    }
  }
  // A dictionary i-adjective (甘い、辛い) is not a deverbal noun merely
  // because its final mora is also an i-row renyokei ending.
  if (first_hiragana == U'い' && dict_manager != nullptr) {
    const std::string adjective_surface = extractSubstring(codepoints, start_pos, kanji_end + 1);
    if (dict_manager->lookupExact(adjective_surface, core::PartOfSpeech::Adjective) != nullptr) {
      skip_single_char = true;
    }
  }

  // A deverbal noun is built on the continuative stem (読み, 調べ), never on
  // the irrealis. An a-row tail is therefore a conjugational boundary rather
  // than a nominalization, and fabricating a noun across it swallows the verb
  // together with its host (水飲ま+ね instead of 水/飲ま/ね). Fossilized a-row
  // nominals are not deverbal at all and are carried by their own entries
  // (自ら, 半ば), so the dictionary reading still wins where one exists.
  if (grammar::isARowCodepoint(first_hiragana)) {
    const std::string nominal_surface = extractSubstring(codepoints, start_pos, kanji_end + 1);
    if (dict_manager == nullptr || dict_manager->lookupExact(nominal_surface, core::PartOfSpeech::Noun) == nullptr) {
      skip_single_char = true;
    }
  }

  // A long kanji sequence ending in an attested godan stem normally contains a
  // nominal boundary (東京+行き, 翌月+払い, ご確認+願い), rather than one unknown
  // nominalization. The boundary shows up both before a particle and before a
  // verbal continuation, so the trailing hiragana carries the verb: 確認願います
  // is 確認 + 願い + ます, never a 確認願い noun. Copula だ and a non-hiragana or
  // final position leave the nominal reading intact (翌月払いだ). Two-kanji
  // deverbal compounds are handled by the verified compound path below.
  if (kanji_count >= 3 && dict_manager != nullptr && kanji_end + 1 < codepoints.size() &&
      char_types[kanji_end + 1] == normalize::CharType::Hiragana && codepoints[kanji_end + 1] != U'だ') {
    const std::string_view base_ending = grammar::godanBaseSuffixFromIRow(first_hiragana);
    if (!base_ending.empty()) {
      const std::string verb_base = normalize::encodeUtf8(codepoints[kanji_end - 1]) + std::string(base_ending);
      if (verb_helpers::isVerbInDictionary(dict_manager, verb_base)) {
        skip_single_char = true;
      }
    }
  }

  if (!skip_single_char) {
    std::string surface = extractSubstring(codepoints, start_pos, kanji_end + 1);
    if (!surface.empty()) {
      // Scale cost higher for long kanji sequences to prevent absorbing
      // following tokens (e.g., 触手画像み should not beat 触手画像+みんな)
      float nom1_cost = 1.2F;
      if (kanji_count >= 3) {
        nom1_cost += static_cast<float>(kanji_count - 2) * 0.5F;
      }
      // A following particle makes the renyokei a nominalized search unit:
      // 答えは, 始まりは, 決まりを.  Prefer that productive noun reading over
      // a finite-verb candidate whose continuation is grammatically absent.
      const bool has_particle_continuation =
          hasNominalizedNounParticleContinuation(codepoints, kanji_end + 1, dict_manager);
      const bool has_temporal_nominal_continuation =
          grammar::startsClosedTemporalNominal(extractSubstring(codepoints, kanji_end + 1, codepoints.size()));
      if (has_particle_continuation || isGenitiveClauseFinalNominal(codepoints, char_types, start_pos, kanji_end + 1) ||
          has_temporal_nominal_continuation) {
        nom1_cost += candidate::kNominalizedNounParticleBonus;
      }
      // Deverbal compound noun bonus (連用形転成名詞の複合):
      // [N kanji]+[V kanji]+[godan renyokei hiragana] where the trailing
      // kanji + base ending is a dictionary verb (丸出し→出す, 恩返し→返す,
      // 山登り→登る). These N+V-renyokei compounds are productive nominal
      // units, so prefer them over splitting off the renyokei verb.
      // Restricted to exactly 2 kanji: longer runs usually contain a real
      // word boundary inside the kanji sequence (翌月+払い, not 翌月払い).
      // Apply only in nominal context — followed by a particle, copula だ,
      // a non-hiragana character, or end of text — so verbal continuations
      // (ながら, ます, たい...) keep the verb reading.
      if (kanji_count == 2 && dict_manager != nullptr) {
        std::string_view base_ending = grammar::godanBaseSuffixFromIRow(first_hiragana);
        if (!base_ending.empty()) {
          bool nominal_context = true;
          size_t after_pos = kanji_end + 1;
          if (after_pos < char_types.size() && char_types[after_pos] == normalize::CharType::Hiragana) {
            char32_t after_char = codepoints[after_pos];
            nominal_context = normalize::isParticleCodepoint(after_char) || after_char == U'だ';
          }
          if (nominal_context) {
            std::string verb_base = normalize::encodeUtf8(codepoints[kanji_end - 1]) + std::string(base_ending);
            if (verb_helpers::isVerbInDictionary(dict_manager, verb_base)) {
              nom1_cost -= 0.6F;
            }
          }
        }
      }
      // A one-kanji i-adjective may use the classical terminal -し form at
      // the end of a predicate. Keep that attested terminal form as one
      // lexical unit instead of reanalyzing its final し as a suru stem.
      const bool is_classical_iadjective_terminal =
          kanji_count == 1 && first_hiragana == U'し' && kanji_end + 1 == codepoints.size() &&
          verb_helpers::isAdjectiveInDictionary(dict_manager,
                                                extractSubstring(codepoints, start_pos, kanji_end) + "い");
      if (is_classical_iadjective_terminal) {
        nom1_cost += candidate::kClassicalIAdjectiveTerminalNounBonus;
      }
      // Single kanji + し followed by sentence punctuation (、。) is almost
      // always 一字漢語サ変動詞 renyokei in formal/literary text (呈し、訴し、),
      // not a nominalized noun. Skip to let the VERB candidate win.
      bool skip_nom_single_kanji_shi = false;
      if (kanji_count == 1 && first_hiragana == U'し' && kanji_end + 1 < codepoints.size()) {
        char32_t after = codepoints[kanji_end + 1];
        if (after == U'、' || after == U'。') {
          skip_nom_single_kanji_shi = true;
        }
      }
      const bool crosses_closed_suffix = hasClosedSuffixBoundary(codepoints, start_pos, kanji_end + 1, dict_manager);
      if (!skip_nom_single_kanji_shi && !crosses_closed_suffix) {
        auto cand = makeCandidate(surface, start_pos, kanji_end + 1, core::PartOfSpeech::Noun, nom1_cost,
                                  has_particle_continuation, CandidateOrigin::NominalizedNoun);
#ifdef SUZUME_DEBUG_INFO
        cand.confidence = kNominalizedNounReportedConfidence;
        cand.pattern = "nominalized_1hira";
#endif
        candidates.push_back(cand);
      }
    }
  }

  // Deverbal compound noun: a continuative verb form written as kanji plus one
  // okurigana, followed by a single-kanji noun, is a productive nominal compound
  // (笑い声, 泣き声, 立ち姿, 聞き耳, 呼び声).  The continuative must reconstruct a
  // dictionary verb, the noun must be a kanji run of exactly one character so a
  // longer kanji compound keeps its own left boundary (読み文章 stays split), and
  // the whole span must close in nominal context so 連用中止法 before an object
  // (水を飲み薬を出す) is not fused either.
  if (kanji_count == 1 && dict_manager != nullptr && kanji_end + 2 <= char_types.size() &&
      char_types[kanji_end + 1] == normalize::CharType::Kanji &&
      (kanji_end + 2 == char_types.size() || char_types[kanji_end + 2] != normalize::CharType::Kanji)) {
    // The continuative shape alone is not enough: 高い山 and 及び水 have it too,
    // and only an attested verb base separates them from a real compound. The
    // reconstructed base therefore has to be a known verb, on either the godan
    // (i-row okurigana) or the ichidan (whole stem + る) paradigm.
    const std::string stem = extractSubstring(codepoints, start_pos, kanji_end + 1);
    const std::string_view base_ending = grammar::godanBaseSuffixFromIRow(first_hiragana);
    const bool is_verb_continuative =
        (!base_ending.empty() &&
         verb_helpers::isVerbInDictionary(
             dict_manager, normalize::encodeUtf8(codepoints[kanji_end - 1]) + std::string(base_ending))) ||
        (grammar::isERowCodepoint(first_hiragana) && verb_helpers::isVerbInDictionary(dict_manager, stem + "る"));
    // A closed suffix on the right is its own morpheme (書き|先, 崩し|的), so it
    // never becomes the second half of a lexical compound.
    const bool crosses_suffix = hasClosedSuffixBoundary(codepoints, start_pos, kanji_end + 2, dict_manager);
    // A span that is already lexicalized as a closed-class word keeps that
    // reading even when its shape also parses as a continuative (及び, 従って).
    bool stem_is_closed_class = false;
    for (const auto& match : dict_manager->lookup(stem, 0)) {
      if (match.entry != nullptr && match.entry->surface.size() == stem.size() &&
          match.entry->pos != core::PartOfSpeech::Verb && match.entry->pos != core::PartOfSpeech::Noun) {
        stem_is_closed_class = true;
        break;
      }
    }
    bool nominal_context = true;
    if (kanji_end + 2 < char_types.size() && char_types[kanji_end + 2] == normalize::CharType::Hiragana) {
      const char32_t after = codepoints[kanji_end + 2];
      nominal_context = normalize::isParticleCodepoint(after) || after == U'だ';
    }
    // A lexical entry reaching past the compound owns the span: the second kanji
    // is then the head of a compound verb, not a noun (取り逃がす, not 取り逃+が+す).
    bool longer_entry_starts_here = false;
    {
      constexpr size_t kLexicalProbe = 6;
      const size_t probe_end = std::min(codepoints.size(), start_pos + kLexicalProbe);
      for (const auto& match : dict_manager->lookup(extractSubstring(codepoints, start_pos, probe_end), 0)) {
        if (match.entry != nullptr && normalize::utf8Length(match.entry->surface) > kanji_end + 2 - start_pos) {
          longer_entry_starts_here = true;
          break;
        }
      }
    }
    if (is_verb_continuative && nominal_context && !crosses_suffix && !stem_is_closed_class &&
        !longer_entry_starts_here) {
      auto cand = makeCandidate(extractSubstring(codepoints, start_pos, kanji_end + 2), start_pos, kanji_end + 2,
                                core::PartOfSpeech::Noun, candidate::kDeverbalCompoundNounCost, true,
                                CandidateOrigin::NominalizedNoun);
#ifdef SUZUME_DEBUG_INFO
      cand.confidence = kNominalizedNounReportedConfidence;
      cand.pattern = "deverbal_compound_noun";
#endif
      candidates.push_back(cand);
    }
  }

  return;
}

void generateReciprocalActionNounCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                            const std::vector<normalize::CharType>& char_types,
                                            const dictionary::DictionaryManager* dict_manager,
                                            std::vector<UnknownCandidate>& candidates) {
  if (dict_manager == nullptr || start_pos >= char_types.size() ||
      char_types[start_pos] != normalize::CharType::Hiragana) {
    return;
  }
  // The nominalizer needs a continuative long enough to be one on its own: a
  // one-mora head would turn the opening kana of a lexical word into a stem.
  constexpr size_t kMinStemLength = 2;
  // Long enough for a compound continuative (追いかけっこ) without running past
  // the construction.
  constexpr size_t kMaxStemLength = 4;
  const size_t max_stem_end = std::min(codepoints.size(), start_pos + kMaxStemLength);
  for (size_t stem_end = start_pos + kMinStemLength; stem_end <= max_stem_end; ++stem_end) {
    const size_t end_pos = stem_end + 2;
    if (end_pos > codepoints.size() || codepoints[stem_end] != core::hiragana::kSmallTsu ||
        codepoints[stem_end + 1] != U'こ') {
      continue;
    }
    if (!hasExactPartOfSpeech(*dict_manager, extractSubstring(codepoints, start_pos, stem_end),
                              partOfSpeechMask(core::PartOfSpeech::Verb))) {
      continue;
    }
    std::string surface = extractSubstring(codepoints, start_pos, end_pos);
    auto cand = makeCandidate(surface, start_pos, end_pos, core::PartOfSpeech::Noun,
                              candidate::kReciprocalActionNounCost, false, CandidateOrigin::NominalizedNoun);
    cand.lemma = surface;
#ifdef SUZUME_DEBUG_INFO
    cand.pattern = "reciprocal_action_kko";
#endif
    candidates.push_back(cand);
  }
}

void generateHumbleNominalCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                     const grammar::Inflection& inflection,
                                     const dictionary::DictionaryManager* /*dict_manager*/,
                                     std::vector<UnknownCandidate>& candidates) {
  if (start_pos == 0 || start_pos >= codepoints.size()) {
    return;
  }
  if (!grammar::isHonorificPrefix(extractSubstring(codepoints, start_pos - 1, start_pos))) {
    return;
  }
  // A one-mora stem carries no continuative evidence of its own and would turn
  // the opening kana of a lexical word into a nominal (おいしい).
  constexpr size_t kMinStemLength = 2;
  // Long enough for a compound continuative (問い合わせ) without scanning past
  // the frame.
  constexpr size_t kMaxStemLength = 5;
  const size_t max_end = std::min(codepoints.size(), start_pos + kMaxStemLength);
  for (size_t end_pos = start_pos + kMinStemLength; end_pos <= max_end; ++end_pos) {
    if (end_pos >= codepoints.size()) {
      break;
    }
    // する closes the frame in its dictionary form or through its continuative
    // し, which carries the polite and past chains (お伝えします, おかけした).
    const char32_t suru_head = codepoints[end_pos];
    const bool closes_frame = suru_head == U'し' || (suru_head == U'す' && end_pos + 1 < codepoints.size() &&
                                                     codepoints[end_pos + 1] == U'る');
    if (!closes_frame) {
      continue;
    }
    // A kanji-headed continuative already carries its own deverbal-noun and
    // verbal candidates inside this frame, so only the pure-hiragana stem is
    // left with nothing to oppose the fabricated verb.
    const std::string stem = extractSubstring(codepoints, start_pos, end_pos);
    if (!grammar::isPureHiragana(stem)) {
      continue;
    }
    // A continuative ends in an e-row or i-row mora. Without this the a-row
    // tail of an ordinary lexical word (おこがましい, ございました) reads as a
    // stem, because the inflection analyzer reconstructs a nominal ichidan
    // paradigm for any kana run at its floor confidence.
    const char32_t stem_end = codepoints[end_pos - 1];
    if (!grammar::isERowCodepoint(stem_end) && !grammar::isIRowCodepoint(stem_end)) {
      continue;
    }
    const auto& stem_analysis = inflection.analyze(stem);
    const std::string ichidan_base = stem + "る";
    const grammar::VerbType godan_type = grammar::verbTypeFromIRowCodepoint(codepoints[end_pos - 1]);
    std::string godan_base;
    if (godan_type != grammar::VerbType::Unknown) {
      godan_base = extractSubstring(codepoints, start_pos, end_pos - 1) +
                   std::string(grammar::godanBaseSuffixFromIRow(codepoints[end_pos - 1]));
    }
    bool is_continuative = false;
    for (const auto& cand : stem_analysis) {
      if (cand.confidence <= candidate::kHumbleNominalStemMinConfidence) {
        continue;
      }
      if ((cand.verb_type == grammar::VerbType::Ichidan && cand.base_form == ichidan_base) ||
          (cand.verb_type == godan_type && !godan_base.empty() && cand.base_form == godan_base)) {
        is_continuative = true;
        break;
      }
    }
    if (!is_continuative) {
      continue;
    }
    auto cand = makeCandidate(stem, start_pos, end_pos, core::PartOfSpeech::Noun,
                              candidate::kHumbleNominalCandidateBonus, true, CandidateOrigin::NominalizedNoun);
#ifdef SUZUME_DEBUG_INFO
    cand.pattern = "humble_nominal";
#endif
    candidates.push_back(cand);
  }
}

}  // namespace suzume::analysis
