/**
 * @file suffix_candidates_compound.cpp
 * @brief Suffix-based unknown word candidate generation
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

std::vector<UnknownCandidate> generateNominalizedNounCandidates(const std::vector<char32_t>& codepoints,
                                                                size_t start_pos,
                                                                const std::vector<normalize::CharType>& char_types,
                                                                const dictionary::DictionaryManager* dict_manager) {
  std::vector<UnknownCandidate> candidates;

  if (start_pos >= char_types.size() || char_types[start_pos] != normalize::CharType::Kanji) {
    return candidates;
  }

  // Find kanji portion (typically 1-3 characters for nominalized nouns)
  size_t kanji_end = findCharRegionEnd(char_types, start_pos, 4, normalize::CharType::Kanji);

  // Need at least 1 kanji
  if (kanji_end == start_pos) {
    return candidates;
  }

  // Look for 1-2 hiragana after kanji (nominalization endings)
  if (kanji_end >= char_types.size() || char_types[kanji_end] != normalize::CharType::Hiragana) {
    return candidates;
  }

  char32_t first_hiragana = codepoints[kanji_end];

  // Skip particles that never form nominalizations
  if (normalize::isParticleCodepoint(first_hiragana)) {
    return candidates;
  }

  // Common nominalization endings (renyokei stems)
  bool is_nominalization_ending =
      (first_hiragana == U'け' || first_hiragana == U'げ' || first_hiragana == U'せ' || first_hiragana == U'い' ||
       first_hiragana == U'り' || first_hiragana == U'ち' || first_hiragana == U'き' || first_hiragana == U'ぎ' ||
       first_hiragana == U'し' || first_hiragana == U'ま' || first_hiragana == U'み' || first_hiragana == U'び' ||
       first_hiragana == U'え' || first_hiragana == U'れ' || first_hiragana == U'め');

  if (!is_nominalization_ending) {
    return candidates;
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
    return candidates;
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
        return candidates;
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
        return candidates;
      }
      // Kanji after し indicates suru-verb renyoukei + kanji verb/noun
      // e.g., 解決し得ない → 解決+し+得+ない (not 解決し+得ない)
      if (next_pos < char_types.size() && char_types[next_pos] == normalize::CharType::Kanji) {
        return candidates;
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
      return candidates;
    }
  }

  // Check for 1 or 2 hiragana (e.g., け or 上げ)
  size_t hiragana_end = kanji_end + 1;

  // Check for 2-hiragana patterns if second char is also valid
  if (hiragana_end < char_types.size() && char_types[hiragana_end] == normalize::CharType::Hiragana) {
    char32_t second_hiragana = codepoints[hiragana_end];
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
      if (!trailing_shi_is_suru) {
        std::string surface = extractSubstring(codepoints, start_pos, hiragana_end + 1);
        if (!surface.empty()) {
          float nom2_cost = 0.8F;
          if (hiragana_end + 1 < char_types.size() && normalize::isParticleCodepoint(codepoints[hiragana_end + 1]) &&
              codepoints[hiragana_end + 1] != U'て' && codepoints[hiragana_end + 1] != U'で') {
            nom2_cost += candidate::kNominalizedNounParticleBonus;
          }
          auto cand = makeCandidate(surface, start_pos, hiragana_end + 1, core::PartOfSpeech::Noun, nom2_cost, false,
                                    CandidateOrigin::NominalizedNoun);
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
  bool skip_single_char = false;
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
      if (kanji_end + 1 < char_types.size() && normalize::isParticleCodepoint(codepoints[kanji_end + 1]) &&
          codepoints[kanji_end + 1] != U'て' && codepoints[kanji_end + 1] != U'で') {
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
      if (!skip_nom_single_kanji_shi) {
        auto cand = makeCandidate(surface, start_pos, kanji_end + 1, core::PartOfSpeech::Noun, nom1_cost, false,
                                  CandidateOrigin::NominalizedNoun);
#ifdef SUZUME_DEBUG_INFO
        cand.confidence = 0.6F;
        cand.pattern = "nominalized_1hira";
#endif
        candidates.push_back(cand);
      }
    }
  }

  return candidates;
}

std::vector<UnknownCandidate> generateKanjiHiraganaCompoundCandidates(
    const std::vector<char32_t>& codepoints, size_t start_pos, const std::vector<normalize::CharType>& char_types,
    const dictionary::DictionaryManager* dict_manager) {
  std::vector<UnknownCandidate> candidates;

  if (start_pos >= char_types.size() || char_types[start_pos] != normalize::CharType::Kanji) {
    return candidates;
  }

  // Skip if this kanji is preceded by another kanji - it's likely the tail end
  // of a longer kanji compound, not the start of a new kanji+hiragana word.
  // E.g., in 魔法少女まどか, skip generating 女まど at pos=3.
  // Dictionary entries (玉ねぎ etc.) are handled separately as dict candidates.
  if (start_pos > 0 && char_types[start_pos - 1] == normalize::CharType::Kanji) {
    return candidates;
  }

  // Find kanji portion (1 character only for compound nouns)
  size_t kanji_end = findCharRegionEnd(char_types, start_pos, 1, normalize::CharType::Kanji);

  size_t kanji_len = kanji_end - start_pos;
  if (kanji_len == 0) {
    return candidates;
  }

  // Need hiragana after kanji
  if (kanji_end >= char_types.size() || char_types[kanji_end] != normalize::CharType::Hiragana) {
    return candidates;
  }

  // Find hiragana portion (2-4 characters)
  size_t hiragana_end = kanji_end;
  while (hiragana_end < char_types.size() && hiragana_end - kanji_end < 4 &&
         char_types[hiragana_end] == normalize::CharType::Hiragana) {
    char32_t ch = codepoints[hiragana_end];
    if (normalize::isParticleCodepoint(ch)) {
      break;
    }
    ++hiragana_end;
  }

  size_t hiragana_len = hiragana_end - kanji_end;
  char32_t first_hira = codepoints[kanji_end];

  // A kanji numeral followed by つ is already a complete native counter
  // (一つ, 二つ). Do not extend it into an invented kanji-hiragana compound
  // when another hiragana word follows (一つ|ひとつ), because the counter
  // generator emits the natural boundary separately.
  if (normalize::isNumeralCodepoint(codepoints[start_pos]) && first_hira == U'つ') {
    return candidates;
  }

  // Handle sokuon (っ) pattern FIRST, before the hiragana_len check
  // Pattern: 漢字 + っ + (漢字 or 平仮名) - e.g., 横っ面, 取っ手, 引っ込む
  // These are valid compound words where hiragana portion may be just 1 char (っ)
  if (first_hira == U'っ') {
    // Need at least one more character after っ
    size_t sokuon_pos = kanji_end;  // Position of っ
    if (sokuon_pos + 1 < char_types.size()) {
      normalize::CharType next_type = char_types[sokuon_pos + 1];

      if (next_type == normalize::CharType::Kanji) {
        // Pattern: 漢字 + っ + 漢字 (e.g., 横っ面, 取っ手)
        size_t kanji2_end = findCharRegionEnd(char_types, sokuon_pos + 1, 3, normalize::CharType::Kanji);

        // Generate candidates for each length
        for (size_t end_pos = sokuon_pos + 2; end_pos <= kanji2_end; ++end_pos) {
          std::string surface = extractSubstring(codepoints, start_pos, end_pos);
          if (!surface.empty()) {
            auto cand = makeCandidate(surface, start_pos, end_pos, core::PartOfSpeech::Noun, 0.5F, false,
                                      CandidateOrigin::KanjiHiraganaCompound);
#ifdef SUZUME_DEBUG_INFO
            cand.confidence = 0.9F;
            cand.pattern = "kanji_sokuon_kanji";
#endif
            candidates.push_back(cand);
          }
        }

        // Check for hatsuonbin verb: 漢字+っ+漢字+ん (e.g., 吹っ飛ん from 吹っ飛ぶ)
        // When the second kanji is followed by ん, check if kanji2+ぶ/む/ぬ is in dict
        if (kanji2_end < codepoints.size() && codepoints[kanji2_end] == U'ん' && dict_manager != nullptr) {
          std::string kanji2_stem = extractSubstring(codepoints, sokuon_pos + 1, kanji2_end);

          auto hatsuonbin_match = verb_helpers::firstGodanOnbinDictBase(dict_manager, kanji2_stem, "ん");
          if (hatsuonbin_match.matched) {
            size_t onbin_end = kanji2_end + 1;  // Include ん
            std::string onbin_surface = extractSubstring(codepoints, start_pos, onbin_end);
            constexpr float kHatsuonbinCost = -0.5F;
            auto cand = makeCandidate(onbin_surface, start_pos, onbin_end, core::PartOfSpeech::Verb, kHatsuonbinCost,
                                      false, CandidateOrigin::KanjiHiraganaCompound);
            // Full base form includes the first kanji + っ
            std::string full_kanji = extractSubstring(codepoints, start_pos, kanji2_end);
            cand.lemma = full_kanji + std::string(hatsuonbin_match.base_suffix);
            cand.conj_type = grammar::verbTypeToConjType(hatsuonbin_match.verb_type);
            cand.extended_pos = core::ExtendedPOS::VerbOnbinkei;
#ifdef SUZUME_DEBUG_INFO
            cand.confidence = 0.9F;
            cand.pattern = "sokuon_kanji_hatsuonbin";
#endif
            SUZUME_DEBUG_LOG("[SUFFIX_CAND] " << onbin_surface << " sokuon_kanji_hatsuonbin lemma=" << cand.lemma
                                              << " cost=" << kHatsuonbinCost << "\n");
            candidates.push_back(cand);
          }
        }
      } else if (next_type == normalize::CharType::Hiragana) {
        // Pattern: 漢字 + っ + 平仮名 (e.g., 引っ込む, 突っ走る)
        // BUT skip if っ is followed by た/て (verb conjugation endings)
        // e.g., 減った, 勝って are verb forms, not compound nouns
        char32_t next_hira = codepoints[sokuon_pos + 1];
        if (next_hira == U'た' || next_hira == U'て') {
          return candidates;  // Skip - this is a verb conjugation, not a compound noun
        }
        size_t hira2_end = sokuon_pos + 1;
        while (hira2_end < char_types.size() && hira2_end - (sokuon_pos + 1) < 4 &&
               char_types[hira2_end] == normalize::CharType::Hiragana) {
          char32_t ch = codepoints[hira2_end];
          if (normalize::isParticleCodepoint(ch)) {
            break;
          }
          ++hira2_end;
        }

        if (hira2_end > sokuon_pos + 1) {
          // A registered adjective beginning at the sokuon is a productive
          // suffix boundary (e.g. noun + っぽ + さ). Do not fabricate a
          // single compound noun across it; the dictionary candidates retain
          // the suffix inflection and any following nominalizer.
          std::string suffix_portion = extractSubstring(codepoints, sokuon_pos, hira2_end);
          if (dict_manager != nullptr) {
            for (const auto& entry : dict_manager->lookup(suffix_portion, 0)) {
              if (entry.entry != nullptr && entry.entry->pos == core::PartOfSpeech::Adjective) {
                const size_t ppoi_end = sokuon_pos + 2;
                const std::string base = extractSubstring(codepoints, start_pos, sokuon_pos);
                // An i-adjective stem productively forms 〜っぽい.  Keep its
                // stem before the following nominalizer (安っぽ+さ), while a
                // nominal base such as 男 retains the ordinary noun+suffix
                // boundary.  The dictionary gate is on the adjective base,
                // not on individual derived words.
                if (ppoi_end <= codepoints.size() && extractSubstring(codepoints, sokuon_pos, ppoi_end) == "っぽ") {
                  if (dict_manager->lookupExact(base + "い", core::PartOfSpeech::Adjective) != nullptr) {
                    auto stem = makeCandidate(extractSubstring(codepoints, start_pos, ppoi_end), start_pos, ppoi_end,
                                              core::PartOfSpeech::Adjective, candidate::kCompoundAdjBaseCost, true,
                                              CandidateOrigin::KanjiHiraganaCompound, core::ExtendedPOS::AdjStem);
                    stem.lemma = base + "っぽい";
                    stem.conj_type = dictionary::ConjugationType::IAdjective;
                    candidates.push_back(std::move(stem));
                  }
                }
                return candidates;
              }
            }
          }

          std::string surface = extractSubstring(codepoints, start_pos, hira2_end);
          if (!surface.empty()) {
            auto cand = makeCandidate(surface, start_pos, hira2_end, core::PartOfSpeech::Noun, 1.0F, false,
                                      CandidateOrigin::KanjiHiraganaCompound);
#ifdef SUZUME_DEBUG_INFO
            cand.confidence = 0.7F;
            cand.pattern = "kanji_sokuon_hira";
#endif
            candidates.push_back(cand);
          }
        }
      }
    }
    // Return after handling sokuon - don't continue to normal hiragana logic
    return candidates;
  }

  if (hiragana_len < 2) {
    return candidates;
  }
  char32_t second_hira = (hiragana_len >= 2) ? codepoints[kanji_end + 1] : 0;

  // A kanji verb continuative stem productively combines with the resemblance
  // suffix っぽい to form one i-adjective search unit (忘れっぽい, 飽きっぽい).
  // This is morphology, not a per-word lexicon: i-row marks Godan
  // continuative stems and e-row marks Ichidan continuative stems.
  const std::string hiragana_candidate = extractSubstring(codepoints, kanji_end, hiragana_end);
  if (utf8::endsWith(hiragana_candidate, "っぽい") &&
      (grammar::isIRowCodepoint(first_hira) || grammar::isERowCodepoint(first_hira))) {
    const std::string derived = extractSubstring(codepoints, start_pos, hiragana_end);
    auto adjective = makeCandidate(derived, start_pos, hiragana_end, core::PartOfSpeech::Adjective,
                                   candidate::kProductivePpoiAdjCost, false, CandidateOrigin::KanjiHiraganaCompound,
                                   core::ExtendedPOS::AdjBasic);
    adjective.lemma = derived;
    adjective.conj_type = dictionary::ConjugationType::IAdjective;
    candidates.push_back(std::move(adjective));
    return candidates;
  }

  // Skip small kana at start - morphologically invalid
  // EXCEPTION: っ (sokuon) can appear in compound patterns like 横っ面, 取っ手, 引っ込む
  // These are valid words where kanji + っ + (kanji or hiragana) forms a compound
  if (first_hira == U'ゃ' || first_hira == U'ゅ' || first_hira == U'ょ' || first_hira == U'ぁ' || first_hira == U'ぃ' ||
      first_hira == U'ぅ' || first_hira == U'ぇ' || first_hira == U'ぉ') {
    return candidates;
  }

  // Skip patterns ending with ん - likely honorific suffixes
  // e.g., さん, くん, ちゃん, たん should split as NOUN + SUFFIX
  // This is a grammatical pattern: hiragana ending with ん after single kanji
  // is typically an honorific suffix, not a compound noun
  if (kanji_len == 1 && hiragana_len >= 2) {
    char32_t last_hira = codepoints[hiragana_end - 1];
    if (last_hira == U'ん') {
      return candidates;
    }
  }

  // Check if pattern looks like a grammatical suffix
  // These get high cost to let verb/adjective candidates win
  bool looks_like_aux = false;

  if (hiragana_len >= 2) {
    // te/ta form, copula patterns
    if (second_hira == U'て' || second_hira == U'た' || second_hira == U'で' || second_hira == U'だ') {
      looks_like_aux = true;
    }
    // ます, ない
    if ((first_hira == U'ま' && second_hira == U'す') || (first_hira == U'な' && second_hira == U'い')) {
      looks_like_aux = true;
    }
    // れる, られる, せる, させる
    if ((first_hira == U'れ' && second_hira == U'る') || (first_hira == U'せ' && second_hira == U'る')) {
      looks_like_aux = true;
    }
    // だった, だろう
    if (first_hira == U'だ' && (second_hira == U'っ' || second_hira == U'ろ')) {
      looks_like_aux = true;
    }
    // なら, なかった
    if (first_hira == U'な' && (second_hira == U'ら' || second_hira == U'か')) {
      looks_like_aux = true;
    }
    // Godan verb shuushikei (終止形) pattern
    // e.g., 休む, 行く, 泳ぐ, 話す, 立つ, 死ぬ, 飛ぶ, 取る
    // If first hiragana is a godan verb ending, kanji+first hiragana likely forms
    // a complete verb, and the rest starts a new word
    // 休むこと → 休む(VERB) + こと(NOUN), not 休むこ(NOUN) + と(PARTICLE)
    bool is_godan_shuushikei = (first_hira == U'む' || first_hira == U'う' || first_hira == U'く' ||
                                first_hira == U'ぐ' || first_hira == U'す' || first_hira == U'つ' ||
                                first_hira == U'ぬ' || first_hira == U'ぶ' || first_hira == U'る');
    if (is_godan_shuushikei) {
      // The 終止形 split hypothesis (kanji+first_hira is a complete verb, the rest starts
      // a new word) is only sound when the stranded remainder is lexically realizable.
      // When exactly one hiragana would be orphaned (hiragana_len == 2), require that a
      // dictionary word can start there; otherwise the "verb" reading strands junk (宝く|じ)
      // and we must keep the kanji+hiragana noun (宝くじ) whole. Standalone single hiragana
      // are a closed class (final particles よ/ね/な, copula, …) all in L1, and formal-noun
      // continuations (こと) are caught by scanning across the particle break — so 休むこと,
      // 飲むな, 帰るね, 行くよ still split as before.
      bool orphan_split_viable = true;
      if (hiragana_len == 2 && dict_manager != nullptr) {
        size_t orphan_pos = kanji_end + 1;
        size_t ctx_end = orphan_pos;
        while (ctx_end < char_types.size() && ctx_end - orphan_pos < 3 &&
               char_types[ctx_end] == normalize::CharType::Hiragana) {
          ++ctx_end;
        }
        std::string orphan_ctx = extractSubstring(codepoints, orphan_pos, ctx_end);
        orphan_split_viable = !dict_manager->lookup(orphan_ctx, 0).empty();
      }
      if (orphan_split_viable) {
        looks_like_aux = true;
      }
    }
    // Renyokei + そう/たい/ます
    // For godan verbs: し,み,き,ぎ,ち,り,い,び (i-row)
    // For ichidan verbs: べ,め,け,せ,て,ね,れ,え (e-row) - these are verb stems
    bool is_renyokei = (first_hira == U'し' || first_hira == U'み' || first_hira == U'き' || first_hira == U'ぎ' ||
                        first_hira == U'ち' || first_hira == U'り' || first_hira == U'い' || first_hira == U'び');
    bool is_ichidan_stem = (first_hira == U'べ' || first_hira == U'め' || first_hira == U'け' || first_hira == U'せ' ||
                            first_hira == U'て' || first_hira == U'ね' || first_hira == U'れ' || first_hira == U'え' ||
                            first_hira == U'げ' || first_hira == U'ぜ' || first_hira == U'で' || first_hira == U'へ' ||
                            first_hira == U'ぺ');
    if ((is_renyokei || is_ichidan_stem) && (second_hira == U'そ' || second_hira == U'た' || second_hira == U'ま')) {
      looks_like_aux = true;
    }
    // Negative + 様態 そう (なさそう): the negative auxiliary ない nominalized as
    // なさ, carrying 様態 そう. Attaches to a verb stem (見なさそう = 見 + なさそう,
    // 食べなさそう = 食べ + なさそう) and is never a compound noun. This is the
    // negative counterpart of the renyokei + そう handling above, so let the
    // verb + な + さ + そう decomposition win instead of merging into one noun.
    if (hiragana_len >= 3) {
      std::string hira_portion = extractSubstring(codepoints, kanji_end, hiragana_end);
      if (hira_portion.find("なさそ") != std::string::npos) {
        looks_like_aux = true;
      }
    }
    // Renyokei + なさい (polite imperative)
    // e.g., 書きなさい, 起きなさい - these should split as verb + なさい
    if ((is_renyokei || is_ichidan_stem) && hiragana_len >= 4) {
      // Check if hiragana portion ends with "さい" (last 2 chars of なさい)
      char32_t h_minus2 = codepoints[hiragana_end - 2];
      char32_t h_minus1 = codepoints[hiragana_end - 1];
      if (h_minus2 == U'さ' && h_minus1 == U'い') {
        looks_like_aux = true;
      }
    }
    // Renyokei + べき (classical auxiliary)
    // e.g., 読むべき, 食べるべき - these should split as verb + べき
    if (hiragana_len >= 3) {
      char32_t h_minus2 = codepoints[hiragana_end - 2];
      char32_t h_minus1 = codepoints[hiragana_end - 1];
      if (h_minus2 == U'べ' && h_minus1 == U'き') {
        looks_like_aux = true;
      }
    }
    // Patterns containing くださ (part of ください auxiliary)
    // e.g., 待ちくださ, 行きくださ - these should be verb + ください
    // Check if hiragana portion contains くださ
    if (hiragana_len >= 3) {
      std::string hira_portion = extractSubstring(codepoints, kanji_end, hiragana_end);
      if (hira_portion.find("くださ") != std::string::npos || hira_portion.find("ください") != std::string::npos) {
        looks_like_aux = true;
      }
    }
  }

  // Ichidan verb pattern (e-row + る)
  bool is_e_row =
      (first_hira == U'え' || first_hira == U'け' || first_hira == U'げ' || first_hira == U'せ' ||
       first_hira == U'て' || first_hira == U'ね' || first_hira == U'べ' || first_hira == U'め' || first_hira == U'れ');
  if (is_e_row && hiragana_len == 2 && second_hira == U'る') {
    looks_like_aux = true;
  }

  // Patterns ending with る
  char32_t last_hira = codepoints[hiragana_end - 1];
  if (last_hira == U'る' && hiragana_len >= 2) {
    looks_like_aux = true;
  }

  // Patterns ending with るそう (verb dictionary form + hearsay そう)
  // e.g., 食べるそう, 降るそう - these are verb終止形 + そう(hearsay), not compound nouns
  // Valid i-adj+そう like 美味しそう are handled separately (don't have る before そう)
  if (hiragana_len >= 3 && last_hira == U'う') {
    char32_t h_minus2 = codepoints[hiragana_end - 2];
    char32_t h_minus3 = (hiragana_end >= 3) ? codepoints[hiragana_end - 3] : U'\0';
    // Check for るそう pattern (verb終止形 + hearsay)
    if (h_minus2 == U'そ' && h_minus3 == U'る') {
      looks_like_aux = true;
    }
    // Check for くそう pattern (godan-ku終止形 + hearsay: 行くそう)
    if (h_minus2 == U'そ' && h_minus3 == U'く') {
      looks_like_aux = true;
    }
    // Check for すそう pattern (godan-sa終止形 + hearsay: 話すそう, するそう)
    if (h_minus2 == U'そ' && h_minus3 == U'す') {
      looks_like_aux = true;
    }
  }

  // Patterns ending with て/で (verb te-form)
  // e.g., 基づいて, 考えて - these are verb conjugations, not compound nouns
  if ((last_hira == U'て' || last_hira == U'で') && hiragana_len >= 2) {
    looks_like_aux = true;
  }

  // Patterns ending with お (prefix marker)
  // e.g., 一つお should be 一つ + お(PREFIX), not 一つお(NOUN)
  // お is very commonly used as honorific prefix, so it should not be absorbed
  // into compound nouns
  if (last_hira == U'お') {
    looks_like_aux = true;
  }

  // Skip NOUN generation for pure auxiliary patterns
  // These should always be verb stem + auxiliary, never a compound noun
  // e.g., 寝ます should be 寝(VERB) + ます(AUX), not 寝ます(NOUN)
  if (hiragana_len == 2) {
    using namespace suzume::core::hiragana;
    char32_t h1 = codepoints[kanji_end];
    char32_t h2 = codepoints[kanji_end + 1];
    // ます, ない - pure polite/negative auxiliaries
    if ((h1 == kMa && h2 == kSu) || (h1 == kNa && h2 == kI)) {
      return candidates;  // Skip NOUN generation entirely
    }
  }

  // Check if the hiragana portion is a known dictionary word (exact match)
  // If so, skip compound generation to let the split path win
  // E.g., 火だるま: if だるま is in dictionary, don't generate compound
  // Only skip for exact matches - partial matches (like た in たまり) don't count
  std::string hiragana_portion = extractSubstring(codepoints, kanji_end, hiragana_end);
  if (dict_manager != nullptr && !hiragana_portion.empty()) {
    auto entries = dict_manager->lookup(hiragana_portion, 0);
    for (const auto& entry : entries) {
      // Check for exact match: entry length must equal the hiragana portion length
      if (entry.length == hiragana_len) {
        // Exact match found - skip compound candidate
        // This allows split like 火+だるま to win
        return candidates;
      }
    }
  }

  // Skip compound generation if the full surface is a known verb in dictionary
  // E.g., 下さい is dict verb (くださる), not compound noun
  {
    std::string full_surface = extractSubstring(codepoints, start_pos, hiragana_end);
    if (verb_helpers::isVerbInDictionary(dict_manager, full_surface)) {
      return candidates;  // Skip - dict verb should win
    }
  }

  // Skip when the hiragana portion ends in a focus particle (副助詞/係助詞)
  // tail, optionally followed by ない: 金さえない is noun + 係助詞 さえ + ない,
  // never a single compound noun. A hiragana portion that IS exactly a
  // particle (先ほど, 中ほど) was already skipped by the exact-dictionary-word
  // check above, so this only rejects particle + negative absorption blobs.
  // @see fabricated closed-class absorption guards (verb_candidates_helpers.h)
  if (verb_helpers::endsWithFocusParticleTail(dict_manager, codepoints, start_pos, hiragana_end)) {
    return candidates;  // Skip - noun + focus particle split should win
  }

  // Generate candidate with cost based on pattern
  std::string surface = extractSubstring(codepoints, start_pos, hiragana_end);
  if (!surface.empty()) {
    float cost = looks_like_aux ? 3.5F : 1.0F;
    auto cand = makeCandidate(surface, start_pos, hiragana_end, core::PartOfSpeech::Noun, cost, false,
                              CandidateOrigin::KanjiHiraganaCompound);
#ifdef SUZUME_DEBUG_INFO
    cand.confidence = looks_like_aux ? 0.3F : 0.8F;
    cand.pattern = looks_like_aux ? "aux_like" : "compound";
#endif
    candidates.push_back(cand);
  }

  return candidates;
}

}  // namespace suzume::analysis
