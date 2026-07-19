/**
 * @file verb_candidates_hiragana_onbin.cpp
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

namespace {

bool isClearTeFormBeforeSubsidiary(const std::vector<char32_t>& codepoints, size_t start_pos, bool allow_emphatic_mo) {
  if (start_pos == 0) {
    return false;
  }
  if (codepoints[start_pos - 1] == core::hiragana::kTe) {
    return true;
  }
  // Emphatic ても/でも keeps the same te-form attachment (思ってもみる,
  // 読んでもみる). Retain the voiced-onbin guard for で so an ordinary
  // case-particle sequence such as 外でもみる stays lexical.
  if (allow_emphatic_mo && codepoints[start_pos - 1] == U'も' && start_pos >= 2) {
    if (codepoints[start_pos - 2] == core::hiragana::kTe) {
      return true;
    }
    return start_pos >= 3 && codepoints[start_pos - 2] == U'で' &&
           (codepoints[start_pos - 3] == core::hiragana::kI || codepoints[start_pos - 3] == U'ん');
  }
  // A voiced te-form before みる comes from い/ん音便 (泳いでみる,
  // 読んでみる). Requiring the onbin keeps an ordinary case-particle で in
  // 外でみる from being reinterpreted as a te-form boundary.
  return start_pos >= 2 && codepoints[start_pos - 1] == U'で' &&
         (codepoints[start_pos - 2] == core::hiragana::kI || codepoints[start_pos - 2] == U'ん');
}

bool grammaticalStemFollowerStartsAt(const std::vector<char32_t>& codepoints, size_t start_pos,
                                     const dictionary::DictionaryManager* dict_manager) {
  if (dict_manager == nullptr || start_pos >= codepoints.size()) {
    return false;
  }
  const std::string remaining = extractSubstring(codepoints, start_pos, codepoints.size());
  for (const auto& result : dict_manager->lookup(remaining, 0)) {
    if (result.entry == nullptr) {
      continue;
    }
    if (result.entry->pos == core::PartOfSpeech::Auxiliary ||
        result.entry->extended_pos == core::ExtendedPOS::ParticleConj) {
      return true;
    }
  }
  return false;
}

// A dictionary hit alone is insufficient for an euphonic Godan candidate:
// the reconstructed base must also have the Godan class that licenses the
// observed onbin. This excludes non-Godan homographs such as する from a
// fabricated すっ+て path.
bool hasMatchingGodanInflection(const grammar::Inflection& inflection, std::string_view base_form,
                                grammar::VerbType expected_type) {
  for (const auto& analysis : inflection.analyze(base_form)) {
    if (analysis.base_form == base_form && analysis.verb_type == expected_type) {
      return true;
    }
  }
  return false;
}

void appendContextualSubsidiaryCandidate(const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos,
                                         std::string_view lemma, dictionary::ConjugationType conj_type,
                                         core::ExtendedPOS extended_pos, const char* pattern, float candidate_cost,
                                         std::vector<UnknownCandidate>& candidates,
                                         core::PartOfSpeech pos = core::PartOfSpeech::Auxiliary) {
  const std::string surface = extractSubstring(codepoints, start_pos, end_pos);
  auto candidate = makeCandidate(surface, start_pos, end_pos, pos, candidate_cost, true, CandidateOrigin::VerbHiragana,
                                 extended_pos, pattern);
  candidate.lemma = lemma;
  candidate.conj_type = conj_type;
  candidates.push_back(std::move(candidate));
}

// The contextual Ichidan subsidiary forms share one shape: a continuative
// stem is allowed only before a grammatical follower, while its finite,
// conditional, imperative, and volitional forms retain the whole surface.
// Keeping this in the onbin owner preserves the caller-specific te-form gate.
void appendContextualIchidanSubsidiaryForms(const std::vector<char32_t>& codepoints, size_t start_pos, size_t stem_end,
                                            std::string_view lemma, const char* pattern,
                                            const dictionary::DictionaryManager* dict_manager,
                                            std::vector<UnknownCandidate>& candidates) {
  if (stem_end >= codepoints.size()) {
    return;
  }

  // The Ichidan volitional is stem + よう, never the bare renyokei stem + よう.
  const bool is_volitional_stem = codepoints[stem_end] == core::hiragana::kYo;
  if (!is_volitional_stem && grammaticalStemFollowerStartsAt(codepoints, stem_end, dict_manager)) {
    appendContextualSubsidiaryCandidate(codepoints, start_pos, stem_end, lemma, dictionary::ConjugationType::Ichidan,
                                        core::ExtendedPOS::AuxAspectMiru, pattern, bigram_cost::kMinor, candidates);
  }

  const char32_t ending = codepoints[stem_end];
  if (ending == core::hiragana::kRu || ending == core::hiragana::kRe || ending == U'ろ' ||
      ending == core::hiragana::kYo) {
    appendContextualSubsidiaryCandidate(codepoints, start_pos, stem_end + 1, lemma,
                                        dictionary::ConjugationType::Ichidan, core::ExtendedPOS::AuxAspectMiru, pattern,
                                        bigram_cost::kMinor, candidates);
  }
}

}  // namespace

void appendOnbinContractionCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t hiragana_end,
                                      const grammar::Inflection& inflection,
                                      const dictionary::DictionaryManager* dict_manager,
                                      std::vector<UnknownCandidate>& candidates) {
  for (size_t onbin_pos = start_pos + 1; onbin_pos < hiragana_end; ++onbin_pos) {
    char32_t onbin_char = codepoints[onbin_pos];

    // Check for sokuonbin (っ) or hatsuonbin (ん)
    bool is_sokuonbin = (onbin_char == U'っ');
    bool is_hatsuonbin = (onbin_char == U'ん');
    if (!is_sokuonbin && !is_hatsuonbin) {
      continue;
    }

    // Check if followed by contraction auxiliary starter
    if (onbin_pos + 1 >= hiragana_end) {
      continue;
    }
    char32_t next_char = codepoints[onbin_pos + 1];

    bool is_contraction_pattern = false;
    bool is_tense_pattern = false;  // っ+た/て (past/te-form)
    if (is_sokuonbin) {
      // っ + と (とく/といた/といて) or ち (ちゃう/ちゃった/ちゃって)
      is_contraction_pattern = (next_char == U'と' || next_char == U'ち');
      // っ + た/て (past/te-form for GodanWa/Ra/Ta)
      // E.g., かった → かっ + た (from かう), やった → やっ + た (from やる)
      is_tense_pattern = (next_char == U'た' || next_char == U'て');
    } else {
      // ん + ど (どく/どいた/どいて) or じ (じゃう/じゃった/じゃって) or で (でる/でた/でて)
      is_contraction_pattern = (next_char == U'ど' || next_char == U'じ' || next_char == U'で');
      // ん + だ/で (past/te-form for GodanMa/Ba/Na)
      // E.g., 読んだ → 読ん + だ (from 読む), 飛んだ → 飛ん + だ (from 飛ぶ)
      is_tense_pattern = (next_char == U'だ' || next_char == U'で');
    }

    if (!is_contraction_pattern && !is_tense_pattern) {
      continue;
    }

    // Get the stem (part before onbin character)
    std::string stem = extractSubstring(codepoints, start_pos, onbin_pos);
    if (stem.empty()) {
      continue;
    }

    // Check if stem starts with common case particles (と、を、に、で、が、は、へ)
    // Used later to skip short particle+verb patterns unless dictionary-verified
    // E.g., となっ (stem=とな, 2 chars) → skip, particle + なる is more likely
    //       はじまっ (stem=はじま, 3 chars) → allow, longer stems are more likely verbs
    bool starts_with_short_particle_stem = false;
    size_t stem_char_count = suzume::normalize::utf8Length(stem);
    if (stem_char_count == 2) {  // Only skip 2-char stems
      char32_t first_char = codepoints[start_pos];
      starts_with_short_particle_stem =
          (first_char == U'と' || first_char == U'を' || first_char == U'に' || first_char == U'で' ||
           first_char == U'が' || first_char == U'は' || first_char == U'へ');
    }

    // Try different verb types based on onbin type
    const auto& candidates_to_try = vh::getGodanTypesByOnbin(is_sokuonbin ? "っ" : "ん");

    // Try each verb type and check dictionary or inflection analysis
    for (const auto& [verb_type, base_suffix] : candidates_to_try) {
      std::string base_form = stem + std::string(base_suffix);

      // Check if base form exists in dictionary as this verb type
      bool is_valid_verb = !grammar::isSuruBaseForm(base_form) && vh::isVerbInDictionary(dict_manager, base_form) &&
                           hasMatchingGodanInflection(inflection, base_form, verb_type);
      // Capture dictionary attestation before the inflection fallback below may
      // set is_valid_verb on a non-dictionary base.
      const bool lemma_dict_verified = is_valid_verb;

      // For sokuonbin tense and contraction patterns (っ+た/て, っとく), also
      // use inflection analysis. It validates common verbs like かう and やる
      // that may not be in the dictionary.
      // Hatsuonbin (ん) is deliberately EXCLUDED: its godan type is rule-ambiguous
      // (む/ぶ/ぬ are table-identical, all endorsed at equal confidence), so the
      // fallback would validate a table-first NON-WORD base (あそんで→あそむ). The
      // ん-onbin lemma must come from the dictionary path instead (bounded L2 +
      // the dedicated hatsuonbin generator), never from an inflection guess.
      // Exception: skip short particle-starting stems (となっ should be と+なっ);
      // longer stems like はじまっ (3+ chars) are allowed.
      if (!is_valid_verb && (is_tense_pattern || is_contraction_pattern) && is_sokuonbin &&
          !starts_with_short_particle_stem) {
        // A contraction is validated through its uncontracted te-form
        // (やっとく -> やって); tense patterns retain their observed suffix.
        std::string_view suffix = is_contraction_pattern                       ? "て"
                                  : (next_char == U'た' || next_char == U'だ') ? "た"
                                                                               : "て";
        std::string full_form = stem + "っ" + std::string(suffix);
        const auto& analysis = inflection.analyze(full_form);
        for (const auto& cand : analysis) {
          // Lower threshold (0.25) for short stems like かっ, やっ
          // since godan_single_hiragana_stem penalty reduces confidence
          if (cand.verb_type == verb_type && cand.base_form == base_form &&
              cand.confidence >= candidate::verb_cost::kShortHiraganaSokuonbinMinConfidence) {
            is_valid_verb = true;
            break;
          }
        }
      }

      if (!is_valid_verb) {
        continue;
      }

      // Found a valid verb - generate onbin stem candidate
      std::string onbin_surface = extractSubstring(codepoints, start_pos, onbin_pos + 1);
      // For tense patterns, use higher cost to avoid false positives for short stems
      // Contraction patterns (っとく, っちゃう) are more reliable, use lower cost
      const float cost =
          is_contraction_pattern ? candidate::verb_cost::kContractedOnbinBonus : candidate::verb_cost::kMinorPenalty;
      SUZUME_DEBUG_VERBOSE_BLOCK {
        SUZUME_DEBUG_STREAM << "[VERB_CAND] " << onbin_surface
                            << (is_tense_pattern ? " hiragana_onbin_tense" : " hiragana_onbin_contraction")
                            << " lemma=" << base_form << " cost=" << cost << "\n";
      }
      const char* pattern = is_sokuonbin ? "hiragana_sokuonbin" : "hiragana_hatsuonbin";
      auto onbin_cand = makeVerbCandidate(onbin_surface, start_pos, onbin_pos + 1, cost, base_form,
                                          grammar::verbTypeToConjType(verb_type), true, CandidateOrigin::VerbHiragana,
                                          0.9F, pattern, core::ExtendedPOS::VerbOnbinkei);
      // The reconstructed base is dictionary-attested, irrespective of
      // whether the inflected surface also has a dictionary entry.
      onbin_cand.lemma_verified = lemma_dict_verified;
      candidates.push_back(std::move(onbin_cand));
      break;  // Found valid candidate for this position
    }
  }
}

// Irregular 来る (カ変) mizenkei こ before a ない-family negative (こない,
// こなかった, こなくて, こなければ, こなきゃ), or before the passive/potential
// auxiliary (てこられる, でこられない). Its volitional stem こよ is likewise
// emitted only in the directional auxiliary context (読んでこよう). The short
// surfaces are far too frequent as unconditional dictionary entries (こと,
// これ, きのこ, ...), so each candidate requires its following inflection.
// The reading is chosen from the preceding context:
//   - after a clear て/で form: directional auxiliary てくる → Auxiliary / AuxAspectKuru
//   - otherwise: main verb 来る before negation → Verb / VerbMizenkei
// Emitting a single context-appropriate reading avoids relying on a broad
// AuxAspectKuru connection rule that could mis-flip other subsidiary verbs.
void appendKkoNegativeConjectureCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                           std::vector<UnknownCandidate>& candidates) {
  // The colloquial negative-conjecture construction V連用形+っ+こ+ない
  // (読みっこない, 食べっこなかった) has two one-mora dependent elements.
  // Generate the first only when the following こ and a ない-family ending
  // prove this construction; a standalone っ must never become a token.
  if (start_pos == 0 || start_pos + 2 >= codepoints.size() || codepoints[start_pos] != U'っ' ||
      codepoints[start_pos + 1] != U'こ' || !vh::naiNegativeFollowsAt(codepoints, start_pos + 2)) {
    return;
  }

  std::string surface = extractSubstring(codepoints, start_pos, start_pos + 1);
  auto candidate = makeCandidate(surface, start_pos, start_pos + 1, core::PartOfSpeech::Auxiliary,
                                 candidate::verb_cost::kStrongBonus, true, CandidateOrigin::VerbHiragana,
                                 core::ExtendedPOS::AuxNegativeMai, "hiragana_kko_negative_conjecture");
  candidate.lemma = "く";
  candidates.push_back(std::move(candidate));
}

void appendSuruInabilityCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                   std::vector<UnknownCandidate>& candidates) {
  // The inability subsidiary かねる productively follows the sahen
  // renyokei し (確認し|かねない, 対応し|かねます). In this exact
  // grammatical environment, make the closed-class する stem competitive
  // with the homographic binding particle しか.
  if (start_pos + 2 >= codepoints.size() || codepoints[start_pos] != U'し' || codepoints[start_pos + 1] != U'か' ||
      codepoints[start_pos + 2] != U'ね') {
    return;
  }

  std::string surface = extractSubstring(codepoints, start_pos, start_pos + 1);
  auto suru_candidate =
      makeCandidate(surface, start_pos, start_pos + 1, core::PartOfSpeech::Verb, candidate::kNounVerbSplitBonus, true,
                    CandidateOrigin::VerbHiragana, core::ExtendedPOS::VerbRenyokei, "hiragana_suru_inability");
  suru_candidate.lemma = "する";
  suru_candidate.conj_type = dictionary::ConjugationType::Suru;
  candidates.push_back(std::move(suru_candidate));
}

void appendEruObligationCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                   std::vector<UnknownCandidate>& candidates) {
  // In the fixed obligation construction 〜ざるをえない, え is the lexical
  // verb 得る in renyokei, not the potential auxiliary. The following
  // negative or polite-negative inflection identifies this reading.
  const bool negative_follows =
      start_pos + 1 < codepoints.size() &&
      (vh::naiNegativeFollowsAt(codepoints, start_pos + 1) || codepoints[start_pos + 1] == U'ず');
  const bool polite_negative_follows =
      start_pos + 2 < codepoints.size() && codepoints[start_pos + 1] == U'ま' && codepoints[start_pos + 2] == U'せ';
  if (start_pos < 3 || codepoints[start_pos] != U'え' || codepoints[start_pos - 1] != U'を' ||
      codepoints[start_pos - 2] != U'る' || codepoints[start_pos - 3] != U'ざ' ||
      (!negative_follows && !polite_negative_follows)) {
    return;
  }

  std::string surface = extractSubstring(codepoints, start_pos, start_pos + 1);
  auto eru_candidate = makeCandidate(
      surface, start_pos, start_pos + 1, core::PartOfSpeech::Verb,
      candidate::kVerifiedTailCompoundVerbBonus + candidate::kVerifiedV1Bonus + candidate::verb_cost::kStrongBonus,
      true, CandidateOrigin::VerbHiragana, core::ExtendedPOS::VerbRenyokei, "hiragana_eru_obligation");
  eru_candidate.lemma = "える";
  eru_candidate.conj_type = dictionary::ConjugationType::Ichidan;
  candidates.push_back(std::move(eru_candidate));
}

void appendKuruMizenkeiNaiCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                     std::vector<UnknownCandidate>& candidates) {
  // The conditional stem くれ is distinct from benefactive くれる when it
  // is followed immediately by ば after a clear te-form (読んでくれ+ば).
  // The benefactive conditional is くれれ+ば, so this context uniquely marks
  // the directional auxiliary 来る.
  if (start_pos + 2 < codepoints.size() && codepoints[start_pos] == U'く' && codepoints[start_pos + 1] == U'れ' &&
      codepoints[start_pos + 2] == U'ば' && isClearTeFormBeforeSubsidiary(codepoints, start_pos, false)) {
    appendContextualSubsidiaryCandidate(codepoints, start_pos, start_pos + 2, "くる", dictionary::ConjugationType::Kuru,
                                        core::ExtendedPOS::AuxAspectKuru, "hiragana_kuru_conditional",
                                        candidate::verb_cost::kStrongBonus, candidates);
    return;
  }
  if (codepoints[start_pos] != U'こ') {
    return;
  }
  const bool negative_follows = vh::naiNegativeFollowsAt(codepoints, start_pos + 1);
  const bool passive_follows = start_pos + 2 < codepoints.size() && codepoints[start_pos + 1] == U'ら' &&
                               codepoints[start_pos + 2] == U'れ' &&
                               isClearTeFormBeforeSubsidiary(codepoints, start_pos, false);
  const bool volitional_follows =
      start_pos + 2 < codepoints.size() && codepoints[start_pos + 1] == U'よ' && codepoints[start_pos + 2] == U'う';
  if (!negative_follows && !passive_follows && !volitional_follows) {
    return;
  }
  constexpr float kCost = candidate::verb_cost::kStandardBonus;
  const bool is_kko_negative = start_pos > 0 && codepoints[start_pos - 1] == U'っ';
  const bool subsidiary = isClearTeFormBeforeSubsidiary(codepoints, start_pos, false) || is_kko_negative;
  if (volitional_follows) {
    if (!subsidiary) {
      return;
    }
    appendContextualSubsidiaryCandidate(codepoints, start_pos, start_pos + 2, "くる", dictionary::ConjugationType::Kuru,
                                        core::ExtendedPOS::AuxAspectKuru, "hiragana_kuru_volitional", kCost,
                                        candidates);
    return;
  }
  const std::string surface = extractSubstring(codepoints, start_pos, start_pos + 1);
  SUZUME_DEBUG_VERBOSE_BLOCK {
    SUZUME_DEBUG_STREAM << "[VERB_CAND] " << surface << " hiragana_kuru_mizenkei lemma=くる cost=" << kCost
                        << " subsidiary=" << subsidiary << "\n";
  }
  if (subsidiary) {
    auto aux_cand =
        makeCandidate(surface, start_pos, start_pos + 1, core::PartOfSpeech::Auxiliary, kCost, true,
                      CandidateOrigin::VerbHiragana, core::ExtendedPOS::AuxAspectKuru, "hiragana_kuru_mizenkei_nai");
    aux_cand.lemma = "くる";
    aux_cand.conj_type = dictionary::ConjugationType::Kuru;
    candidates.push_back(std::move(aux_cand));
    return;
  }
  // A passive/potential continuation is only admitted after a te-form, where
  // it is the directional subsidiary construction; standalone こられる remains
  // available as a lexical verb candidate.
  if (passive_follows) {
    return;
  }
  candidates.push_back(makeVerbCandidate(surface, start_pos, start_pos + 1, kCost, "くる",
                                         dictionary::ConjugationType::Kuru, true, CandidateOrigin::VerbHiragana,
                                         candidate::kHighOriginConfidence, "hiragana_kuru_mizenkei_nai",
                                         core::ExtendedPOS::VerbMizenkei));
}

// The directional subsidiary いく shares its いけ/いけれ spelling with the
// independent potential verb いける. After a clear te-form, however, negative
// conditional, and volitional continuations identify the subsidiary paradigm
// (読んでいけない, 読んでいければ, 読んでいこう). Keep the short forms
// contextual so standalone lexical uses retain their verb analysis.
void appendIkuAuxiliaryCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                  std::vector<UnknownCandidate>& candidates) {
  if (start_pos + 1 >= codepoints.size() || codepoints[start_pos] != core::hiragana::kI ||
      (codepoints[start_pos + 1] != U'け' && codepoints[start_pos + 1] != U'こ') ||
      !isClearTeFormBeforeSubsidiary(codepoints, start_pos, false)) {
    return;
  }

  if (start_pos + 3 < codepoints.size() && codepoints[start_pos + 2] == U'れ' && codepoints[start_pos + 3] == U'ば') {
    appendContextualSubsidiaryCandidate(codepoints, start_pos, start_pos + 3, "いける",
                                        dictionary::ConjugationType::GodanKa, core::ExtendedPOS::AuxAspectIku,
                                        "hiragana_iku_auxiliary", candidate::verb_cost::kStrongBonus, candidates);
    return;
  }

  if (start_pos + 2 < codepoints.size() && codepoints[start_pos + 1] == U'こ' && codepoints[start_pos + 2] == U'う') {
    appendContextualSubsidiaryCandidate(codepoints, start_pos, start_pos + 2, "いく",
                                        dictionary::ConjugationType::GodanKa, core::ExtendedPOS::AuxAspectIku,
                                        "hiragana_iku_auxiliary", candidate::verb_cost::kStrongBonus, candidates);
    return;
  }

  if (vh::naiNegativeFollowsAt(codepoints, start_pos + 2)) {
    appendContextualSubsidiaryCandidate(codepoints, start_pos, start_pos + 2, "いける",
                                        dictionary::ConjugationType::GodanKa, core::ExtendedPOS::AuxAspectIku,
                                        "hiragana_iku_auxiliary", candidate::verb_cost::kStrongBonus, candidates);
  }
}

// 授受の補助動詞 やる has the same irrealis and renyokei stems as the
// independent verb. A clear te-form boundary makes the benefactive reading
// productive before negative and desiderative auxiliaries.
void appendYaruBenefactiveCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                     std::vector<UnknownCandidate>& candidates) {
  if (start_pos + 1 >= codepoints.size() || codepoints[start_pos] != U'や' ||
      !isClearTeFormBeforeSubsidiary(codepoints, start_pos, false)) {
    return;
  }
  if (codepoints[start_pos + 1] == U'ら' && vh::naiNegativeFollowsAt(codepoints, start_pos + 2)) {
    appendContextualSubsidiaryCandidate(codepoints, start_pos, start_pos + 2, "やる",
                                        dictionary::ConjugationType::GodanRa, core::ExtendedPOS::AuxBenefactive,
                                        "hiragana_yaru_benefactive", candidate::verb_cost::kStrongBonus, candidates);
  }
  if (codepoints[start_pos + 1] != U'り') {
    return;
  }
  appendContextualSubsidiaryCandidate(codepoints, start_pos, start_pos + 2, "やる",
                                      dictionary::ConjugationType::GodanRa, core::ExtendedPOS::AuxBenefactive,
                                      "hiragana_yaru_benefactive", candidate::verb_cost::kStrongBonus, candidates);
}

// 試行の補助動詞 みる is a closed-class te-form attachment. Generate its
// one-stage conjugation forms only after a clear て/で boundary, rather than
// registering the highly ambiguous single-kana stem み unconditionally.
// The stem form is emitted only when a dictionary-backed auxiliary or
// conjunctive particle follows (み+ます/た/て/ない/たい/られ...). The remaining
// finite Ichidan forms share the same contextual gate.
void appendMiruAuxiliaryCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                   const dictionary::DictionaryManager* dict_manager,
                                   std::vector<UnknownCandidate>& candidates) {
  if (start_pos >= codepoints.size() || codepoints[start_pos] != U'み' ||
      !isClearTeFormBeforeSubsidiary(codepoints, start_pos, true)) {
    return;
  }

  appendContextualIchidanSubsidiaryForms(codepoints, start_pos, start_pos + 1, "みる", "hiragana_miru_auxiliary",
                                         dict_manager, candidates);
}

// 見せる is a closed subsidiary verb after a te-form (読んでみせる). Its
// two-kana stem is unambiguous only in that context, so generate the Ichidan
// paradigm there instead of registering a broad hiragana dictionary entry.
void appendMiseruAuxiliaryCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                     const dictionary::DictionaryManager* dict_manager,
                                     std::vector<UnknownCandidate>& candidates) {
  if (start_pos + 1 >= codepoints.size() || codepoints[start_pos] != U'み' || codepoints[start_pos + 1] != U'せ' ||
      !isClearTeFormBeforeSubsidiary(codepoints, start_pos, true)) {
    return;
  }
  appendContextualIchidanSubsidiaryForms(codepoints, start_pos, start_pos + 2, "みせる", "hiragana_miseru_auxiliary",
                                         dict_manager, candidates);
}

// The benefactive auxiliary あげる has the ichidan stem あげ before the
// potential/passive auxiliary (〜てあげられる). Emit it only after a clear
// te-form and only when the following token is grammatical, preserving the
// ordinary lexical verb reading elsewhere.
void appendAgeruBenefactiveCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                      const dictionary::DictionaryManager* dict_manager,
                                      std::vector<UnknownCandidate>& candidates) {
  if (start_pos + 2 >= codepoints.size() || codepoints[start_pos] != U'あ' || codepoints[start_pos + 1] != U'げ' ||
      codepoints[start_pos + 2] != U'ら' || !isClearTeFormBeforeSubsidiary(codepoints, start_pos, true)) {
    return;
  }
  if (!grammaticalStemFollowerStartsAt(codepoints, start_pos + 2, dict_manager)) {
    return;
  }
  appendContextualSubsidiaryCandidate(codepoints, start_pos, start_pos + 2, "あげる",
                                      dictionary::ConjugationType::Ichidan, core::ExtendedPOS::AuxBenefactive,
                                      "hiragana_ageru_benefactive", bigram_cost::kMinor, candidates,
                                      core::PartOfSpeech::Verb);
}

// 準備の補助動詞 おく is homographic with the lexical verb and its short
// conjugation forms are common word fragments. Emit the closed auxiliary
// paradigm only after a clear te-form boundary instead of registering it
// globally in L1.
void appendOkuAuxiliaryCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                  std::vector<UnknownCandidate>& candidates) {
  if (start_pos + 1 >= codepoints.size() || codepoints[start_pos] != U'お' ||
      !isClearTeFormBeforeSubsidiary(codepoints, start_pos, false)) {
    return;
  }

  // 五段カ行: 未然おか/おこ, 連用おき, 音便おい, 終止おく, 仮定・命令おけ.
  const char32_t ending = codepoints[start_pos + 1];
  if (ending != U'か' && ending != U'き' && ending != U'い' && ending != U'く' && ending != U'け' && ending != U'こ') {
    return;
  }
  // Only the irrealis stem can take the negative auxiliary. Score this
  // context-gated inflection locally so an unrelated おく contraction (どい)
  // cannot acquire the same preference across a particle boundary.
  float candidate_cost = bigram_cost::kMinor;
  if (ending == U'か' && vh::naiNegativeFollowsAt(codepoints, start_pos + 2)) {
    candidate_cost += bigram_cost::kDoubleVeryStrongBonus;
  }
  appendContextualSubsidiaryCandidate(codepoints, start_pos, start_pos + 2, "おく",
                                      dictionary::ConjugationType::GodanKa, core::ExtendedPOS::AuxAspectOku,
                                      "hiragana_oku_auxiliary", candidate_cost, candidates);
}

// 1-char ichidan renyokei before て/た (ねて → ね + て). Requires the base form
// (stem + る) in the dictionary. Contextual subsidiary verbs are generated
// separately with their auxiliary ExtendedPOS.
void appendIchidanRenyokei1CharCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                          const dictionary::DictionaryManager* dict_manager,
                                          std::vector<UnknownCandidate>& candidates) {
  // Pattern: e-row hiragana followed by て or た
  // IMPORTANT: Only generate if the base form (stem + る) is a known verb in dictionary
  // to avoid false positives like めて → め + て (め is not a verb)
  if (start_pos < codepoints.size()) {
    char32_t first_char = codepoints[start_pos];
    // Check for e-row hiragana (ichidan renyokei ending)
    // e-row: ね(ねる), め(める), け(ける), etc.
    if (grammar::isERowCodepoint(first_char)) {
      // Check if followed by te/ta particle.
      if (start_pos + 1 < codepoints.size()) {
        char32_t next_char = codepoints[start_pos + 1];
        bool is_valid_follow = (next_char == U'て' || next_char == U'た');
        if (is_valid_follow) {
          // Construct base form (stem + る)
          std::string stem_surface = extractSubstring(codepoints, start_pos, start_pos + 1);
          std::string base_form = stem_surface + "る";

          // Require dict check to prevent false positives (め+て, け+て).
          if (vh::isVerbInDictionary(dict_manager, base_form)) {
            // Strong negative cost to beat particle split
            // Particle path can be as low as -0.2, so we need lower
            constexpr float kCost = candidate::verb_cost::kStandardBonus;
            SUZUME_DEBUG_VERBOSE_BLOCK {
              SUZUME_DEBUG_STREAM << "[VERB_CAND] " << stem_surface
                                  << " hiragana_ichidan_renyokei_1char lemma=" << base_form << " cost=" << kCost
                                  << "\n";
            }
            candidates.push_back(makeVerbCandidate(
                stem_surface, start_pos, start_pos + 1, kCost, base_form, dictionary::ConjugationType::Ichidan, true,
                CandidateOrigin::VerbHiragana, 0.8F, "hiragana_ichidan_renyokei_1char",
                core::ExtendedPOS::VerbRenyokei));  // Explicit VerbRenyokei for て/た connection
          }
        }
      }
    }
  }
}

void appendHiraganaDerivedCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t hiragana_end,
                                     const std::vector<normalize::CharType>& char_types,
                                     const grammar::Inflection& inflection,
                                     const dictionary::DictionaryManager* dict_manager,
                                     std::vector<UnknownCandidate>& candidates) {
  // Generate Godan mizenkei stem candidates for hiragana passive patterns
  // E.g., いわれる → いわ (mizenkei of いう) + れる (passive AUX)
  appendPassiveMizenkeiCandidates(codepoints, start_pos, hiragana_end, inflection, dict_manager, candidates);

  // Generate Ichidan verb stem candidates for hiragana られる pattern
  // E.g., いられる → い (renyokei of いる) + られる (potential/passive AUX)
  appendIchidanRareruCandidates(codepoints, start_pos, hiragana_end, inflection, dict_manager, candidates);

  // Generate Godan mizenkei stem candidates for contracted negative ん pattern
  // E.g., くだらん → くだら (mizenkei of くだる) + ん (contracted negative)
  appendMizenkeiNCandidates(codepoints, start_pos, hiragana_end, dict_manager, candidates);

  // Generate Godan mizenkei stem candidates for negative auxiliary ない pattern
  // E.g., わからない → わから (mizenkei of わかる) + ない (negative auxiliary)
  appendMizenkeiNaiCandidates(codepoints, start_pos, hiragana_end, inflection, dict_manager, candidates);

  // Generate Godan mizenkei stem candidates before なきゃ/なければ contraction
  // E.g., やらなきゃ → やら (mizenkei of やる) + なきゃ (contraction of なければ)
  appendMizenkeiNakyaCandidates(codepoints, start_pos, hiragana_end, inflection, dict_manager, candidates);

  // Generate Godan-ra ん音便 stem candidates for colloquial ん+ない pattern
  // E.g., たまんない → たまん (ん音便 of たまる) + ない (negative auxiliary)
  appendNOnbinNaiCandidates(codepoints, start_pos, hiragana_end, inflection, dict_manager, candidates);

  // Generate Godan onbin stem candidates for contraction auxiliary patterns
  // E.g., やっとく → やっ (onbin of やる) + とく (ておく contraction), 読んでる → 読ん + でる
  appendOnbinContractionCandidates(codepoints, start_pos, hiragana_end, inflection, dict_manager, candidates);

  // Generate 1-char ichidan renyokei stem candidates
  // E.g., ねて → ね (renyokei of ねる) + て (particle)
  appendIchidanRenyokei1CharCandidates(codepoints, start_pos, dict_manager, candidates);

  // Generate 2+ char ichidan renyokei stem candidates
  // E.g., つけて → つけ (renyokei of つける) + て (particle)
  //       たべて → たべ (renyokei of たべる) + て (particle)
  //       あけて → あけ (renyokei of あける) + て (particle)
  //       すぎて → すぎ (renyokei of すぎる) + て (particle)
  // MeCab splits: つけて → つけ(動詞,一段,連用形) + て(助詞,接続助詞)
  // Pattern: 2+ char sequence ending with e-row or i-row hiragana followed by て or た
  // Note: Ichidan verbs have both e-row stems (食べる) and i-row stems (感じる, 過ぎる)
  // Uses inflection analysis confidence to validate (dictionary lookup as bonus)
  //
  // A run that starts with で right after a hatsuonbin ん is the voiced te-form
  // particle of the preceding verb (読ん+で, 飲ん+で), so its でき is
  // で(particle)+き(くる), never the renyokei of できる. Only the ます-family
  // licenser is suppressed for this shape (see is_followed_by_masu below) so
  // 読んできました stays 読ん+で+き+まし+た. The plain て/た path is deliberately
  // left intact — 取り組んできた already resolves to …+で+き+た on its own, and
  // blocking the reading there would instead flip it to でき+た.
  const bool leading_de_after_hatsuonbin =
      codepoints[start_pos] == U'で' && start_pos > 0 && codepoints[start_pos - 1] == U'ん';
  for (size_t end_pos = start_pos + 2; end_pos < hiragana_end; ++end_pos) {
    // Check if position end_pos-1 is e-row or i-row hiragana (ichidan renyokei ending)
    // E-row: 食べる, 見える → 食べ, 見え
    // I-row: 感じる, 過ぎる → 感じ, すぎ
    char32_t stem_end_char = codepoints[end_pos - 1];
    if (!grammar::isERowCodepoint(stem_end_char) && !grammar::isIRowCodepoint(stem_end_char)) {
      continue;
    }

    // Exclude て and で which are more commonly particles
    if (stem_end_char == U'て' || stem_end_char == U'で') {
      continue;
    }

    // Check if followed by te/ta particle, polite ます auxiliary, or conditional れば
    if (end_pos >= codepoints.size()) {
      continue;
    }
    char32_t next_char = codepoints[end_pos];
    bool is_followed_by_te_ta = (next_char == U'て' || next_char == U'た');
    bool is_followed_by_renyokei_conj = next_char == U'な' && end_pos + 2 < codepoints.size() &&
                                        codepoints[end_pos + 1] == U'が' && codepoints[end_pos + 2] == U'ら';
    if (!is_followed_by_renyokei_conj) {
      is_followed_by_renyokei_conj =
          next_char == U'つ' && end_pos + 1 < codepoints.size() && codepoints[end_pos + 1] == U'つ';
    }
    bool is_followed_by_reba = false;
    // Check for a polite ます-family auxiliary (ます / まし / ませ). All three
    // attach only to a verb renyokei, so they license the renyokei reading of a
    // renyokei/aux homograph (つかい→使う, かい→買う before ました/ません). The
    // ん+で te-form shape is excluded so でき before まし is not mis-read as the
    // renyokei of できる (読んできました → 読ん+で+き+まし+た).
    bool is_followed_by_masu = vh::masuAuxFollowsAt(codepoints, end_pos) && !leading_de_after_hatsuonbin;
    // Check for conditional れば pattern (e.g., できれば → でき + れ + ば)
    // This case is handled separately below for kateikei stem generation
    if (next_char == U'れ' && end_pos + 1 < codepoints.size() && codepoints[end_pos + 1] == U'ば') {
      is_followed_by_reba = true;
    }
    // Check for negative ない pattern (e.g., できない → でき + ない)
    bool is_followed_by_nai = false;
    if (next_char == U'な' && end_pos + 1 < codepoints.size() && codepoints[end_pos + 1] == U'い') {
      is_followed_by_nai = true;
    }
    if (!is_followed_by_te_ta && !is_followed_by_masu && !is_followed_by_renyokei_conj && !is_followed_by_reba &&
        !is_followed_by_nai) {
      continue;
    }

    // Construct stem and base form. Small-kana starts (っぱいし, ゃい, …) are
    // already rejected by the function-entry guard.
    std::string stem_surface = extractSubstring(codepoints, start_pos, end_pos);
    std::string base_form = stem_surface + "る";

    // Use inflection analysis to validate - check if stem is recognized as ichidan
    const auto& stem_analysis = inflection.analyze(stem_surface);
    bool found_ichidan = false;
    float ichidan_confidence = 0.0F;
    for (const auto& cand : stem_analysis) {
      if (cand.verb_type == grammar::VerbType::Ichidan && cand.base_form == base_form) {
        found_ichidan = true;
        ichidan_confidence = cand.confidence;
        break;
      }
    }

    // Skip if not recognized as ichidan stem by inflection analysis
    // Threshold 0.3 catches most valid cases while filtering noise
    if (!found_ichidan || ichidan_confidence < 0.3F) {
      continue;
    }

    // Default to the ichidan interpretation (stem + る). For the +ます follower a
    // godan renyokei reading is equally licensed (泳ぎ→泳ぐ, 踊り→踊る), so prefer it
    // when at least as confident: emit the systematic verb base instead of a
    // fabricated 泳ぎる/踊りる. て/た/ない/れば stay ichidan-only — there a bare
    // i-row stem is genuinely ichidan (godan would need onbin or an a-row mizenkei).
    std::string chosen_base = base_form;
    dictionary::ConjugationType chosen_conj = dictionary::ConjugationType::Ichidan;
    float chosen_confidence = ichidan_confidence;
    if (is_followed_by_masu || is_followed_by_renyokei_conj) {
      grammar::VerbType godan_type = grammar::verbTypeFromIRowCodepoint(stem_end_char);
      if (godan_type != grammar::VerbType::Unknown) {
        std::string godan_base = extractSubstring(codepoints, start_pos, end_pos - 1) +
                                 std::string(grammar::godanBaseSuffixFromIRow(stem_end_char));
        for (const auto& cand : stem_analysis) {
          if (cand.verb_type == godan_type && cand.base_form == godan_base && cand.confidence >= chosen_confidence) {
            chosen_base = godan_base;
            chosen_conj = grammar::verbTypeToConjType(godan_type);
            chosen_confidence = cand.confidence;
            break;
          }
        }
      }
    }

    // Check if base form is in dictionary (gives confidence boost)
    bool is_dict_verb = vh::isVerbInDictionary(dict_manager, chosen_base);

    // Skip causative+passive auxiliary chain patterns
    // E.g., "せられ" should be split as せ(causative) + られ(passive), not single verb
    // Preserve the causative, passive, and tense morpheme boundaries.
    if (utf8::endsWith(stem_surface, "せられ")) {
      continue;
    }

    // Skip stems ending in なけ - this is the negative auxiliary ない kateikei (なけれ),
    // not an ichidan verb なける. Prevents a false single-verb reading for
    // mizenkei + なければ: やらなければ must split as やら + なけれ(ない) + ば,
    // never become a fabricated ichidan やらなける.
    if (utf8::endsWith(stem_surface, "なけ")) {
      continue;
    }

    // Skip stems ending in し where the prefix is a dictionary noun (サ変 pattern)
    // E.g., しっぱいし → しっぱい(dict NOUN) + し(する連用), not しっぱいしる
    // This prevents false ichidan candidates from サ変 noun + する patterns
    if (!is_dict_verb && utf8::endsWith(stem_surface, "し") && stem_surface.size() > 3) {  // More than just し
      std::string prefix = stem_surface.substr(0, stem_surface.size() - 3);
      if (vh::hasNonVerbDictionaryEntry(dict_manager, prefix)) {
        continue;
      }
      // Pure hiragana stems with sokuon ending in し are almost always
      // false サ変 patterns (noun+する where noun contains っ)
      if (stem_surface.find("っ") != std::string::npos) {
        continue;
      }
    }

    // Skip て+subsidiary verb patterns that should be split
    // E.g., "してくれ" should be し + て + くれ, not single verb
    //       "してもら" should be し + て + もら, not single verb
    // These patterns contain て-form (して) followed by subsidiary verb stem
    if (stem_surface.find("てくれ") != std::string::npos || stem_surface.find("てもら") != std::string::npos ||
        stem_surface.find("てあげ") != std::string::npos) {
      continue;
    }

    // Skip te-form + subsidiary みる spans: an internal て/で followed by み is
    // always [verb te-form] + みる (やってみ = やっ + て + み, われてみ =
    // われ + て + み), never a single ichidan verb やってみる. This also
    // suppresses the kateikei variant below (やってみれ from やってみれば).
    // @see fabricated closed-class absorption guards (verb_candidates_helpers.h)
    if (!is_dict_verb && vh::embedsTeFormMiruAuxiliary(codepoints, start_pos, end_pos)) {
      continue;
    }

    // Skip a fabricated verb that spans an auxiliary prefix + auxiliary tail:
    // でござい → で(AuxCopulaDa) + ござい(AuxGozaru). MeCab always keeps a
    // closed-class auxiliary chain split, so an open-class verb whose leading
    // codepoint is itself an AUX and whose remainder after that codepoint is
    // exactly an AUX is re-merging what must stay apart. Both halves must be
    // dictionary auxiliaries: the remainder condition alone would wrongly skip
    // real verbs like しまう (し is a particle, not an AUX → しまい stays), and
    // the 2-codepoint floor on the remainder protects genuine short stems かい
    // (い = いる 連用形) and でき (き = くる 連用形).
    if (!is_dict_verb && dict_manager != nullptr && end_pos - start_pos >= 3) {
      std::string aux_first = extractSubstring(codepoints, start_pos, start_pos + 1);
      std::string aux_remainder = extractSubstring(codepoints, start_pos + 1, end_pos);
      if (dict_manager->lookupExact(aux_first, core::PartOfSpeech::Auxiliary) != nullptr &&
          vh::hasDictionaryEntry(dict_manager, aux_remainder, core::PartOfSpeech::Auxiliary)) {
        continue;
      }
    }

    // Strong negative cost to beat NOUN + て(VERB from てる) split
    // Dictionary-verified verbs get stronger bonus
    // Non-dictionary verbs get moderate positive cost to avoid spurious candidates
    // competing with dictionary compound particles like について
    // But not too high to break valid patterns like してほしい
    float cost = is_dict_verb ? -0.8F : 0.5F;
    // A godan-wa renyokei starting か…い immediately after a pronoun (誰かい, なにかい)
    // is spurious: the か is the particle か and い is いる's renyokei
    // (誰か + い + ます). Discourage it so the particle reading wins.
    if (codepoints[start_pos] == U'か' && pronounEndsAt(dict_manager, codepoints, start_pos)) {
      cost += bigram_cost::kStrong;
    }
    SUZUME_DEBUG_VERBOSE_BLOCK {
      SUZUME_DEBUG_STREAM << "[VERB_CAND] " << stem_surface << " hiragana_renyokei lemma=" << chosen_base
                          << " conf=" << chosen_confidence << (is_dict_verb ? " [dict]" : "") << " cost=" << cost
                          << "\n";
    }
    const std::string following = extractSubstring(codepoints, end_pos, std::min(end_pos + 2, codepoints.size()));
    const bool is_negative_continuation = utf8::startsWith(following, "ない") || utf8::startsWith(following, "なか");
    // A stem after a clear te/de boundary belongs to a subsidiary-verb
    // construction.  Leave that category to its dedicated candidate so an
    // otherwise valid Ichidan reconstruction cannot turn 〜てやらない into a
    // lexical predicate.  Outside that boundary, the negative confirms that
    // the ambiguous Ichidan stem is mizenkei (さけ+ない, かけ+ない).
    const bool is_lexical_negative_continuation =
        is_negative_continuation && !isClearTeFormBeforeSubsidiary(codepoints, start_pos, true);
    const core::CandidateOrigin origin = is_lexical_negative_continuation
                                             ? CandidateOrigin::VerbHiraganaNegativeRenyokei
                                         : is_followed_by_te_ta ? CandidateOrigin::VerbHiraganaInflectedRenyokei
                                                                : CandidateOrigin::VerbHiragana;
    // Ichidan stems share their surface in renyokei and mizenkei. A following
    // negative auxiliary determines the latter, which must receive the normal
    // VerbMizenkei → AuxNegativeNai connection instead of competing as a
    // continuative verb (さけ+ない, かけ+ない).
    const core::ExtendedPOS extended_pos =
        is_lexical_negative_continuation ? core::ExtendedPOS::VerbMizenkei : core::ExtendedPOS::VerbRenyokei;
    candidates.push_back(makeVerbCandidate(stem_surface, start_pos, end_pos, cost, chosen_base, chosen_conj, true,
                                           origin, chosen_confidence, "hiragana_renyokei", extended_pos));

    // Also generate kateikei stem if followed by れば
    // E.g., できれば → できれ (kateikei of できる) + ば
    // MeCab splits: できれば → できれ(動詞,仮定形) + ば(接続助詞)
    // Skip suru-verb negative patterns: しなけれ should be し + なけれ, not single verb
    // Pattern: し + な (negative stem prefix)
    // stem_surface = しなけ → base_form = しなける (false ichidan)
    bool is_suru_negative_pattern = (stem_surface.size() >= 6 &&  // しな = 6 bytes
                                     stem_surface.substr(0, 3) == "し" && stem_surface.substr(3, 3) == "な");
    if (is_followed_by_reba && !is_suru_negative_pattern) {
      std::string kateikei_surface = stem_surface + "れ";  // 連用形 + れ = 仮定形
      size_t kateikei_end = end_pos + 1;                   // renyokei + れ
      constexpr float kKateikeiCost = candidate::verb_cost::kStrongBonus;
      SUZUME_DEBUG_VERBOSE_BLOCK {
        SUZUME_DEBUG_STREAM << "[VERB_CAND] " << kateikei_surface << " hiragana_ichidan_kateikei lemma=" << base_form
                            << " conf=" << ichidan_confidence << " cost=" << kKateikeiCost << "\n";
      }
      candidates.push_back(makeVerbCandidate(kateikei_surface, start_pos, kateikei_end, kKateikeiCost, base_form,
                                             dictionary::ConjugationType::Ichidan, true, CandidateOrigin::VerbHiragana,
                                             ichidan_confidence, "hiragana_ichidan_kateikei",
                                             core::ExtendedPOS::VerbKateikei));
    }
  }

  // Generate Godan sokuonbin (っ) candidates for hiragana verbs
  // E.g., しまった → しまっ (onbin of しまう) + た (auxiliary)
  //       なくなった → なくなっ (onbin of なくなる) + た (auxiliary)
  // This separates the verb's onbin stem from the tense auxiliary.
  {
    // Find the complete hiragana run from start_pos.
    const size_t remaining_chars = char_types.size() - start_pos;
    const size_t hira_extent_end =
        vh::findCharRegionEnd(char_types, start_pos, remaining_chars, normalize::CharType::Hiragana);
    size_t hira_len = hira_extent_end - start_pos;

    // Need at least 3 chars: stem(1+) + っ + た/て
    if (hira_len >= 3) {
      char32_t second_last = codepoints[hira_extent_end - 2];
      char32_t last_char = codepoints[hira_extent_end - 1];
      bool is_sokuonbin_te_ta = (second_last == U'っ' && (last_char == U'た' || last_char == U'て'));
      if (is_sokuonbin_te_ta) {
        // Generate candidate for stem + っ (without the た/て)
        size_t onbin_end = hira_extent_end - 1;  // Position after っ
        std::string onbin_surface = extractSubstring(codepoints, start_pos, onbin_end);
        std::string stem = extractSubstring(codepoints, start_pos, onbin_end - 1);

        const auto& sokuonbin_types = vh::getGodanTypesByOnbin("っ");

        auto sokuonbin_match = vh::firstGodanOnbinDictBase(dict_manager, stem, "っ");
        if (sokuonbin_match.matched &&
            (grammar::isSuruBaseForm(sokuonbin_match.base_form) ||
             !hasMatchingGodanInflection(inflection, sokuonbin_match.base_form, sokuonbin_match.verb_type))) {
          sokuonbin_match.matched = false;
        }
        bool found_dict_match = sokuonbin_match.matched;
        if (found_dict_match) {
          constexpr float kHiraganaSokuonbinCost = candidate::verb_cost::kStandardBonus;
          SUZUME_DEBUG_VERBOSE_BLOCK {
            SUZUME_DEBUG_STREAM << "[VERB_CAND] " << onbin_surface
                                << " hiragana_sokuonbin lemma=" << sokuonbin_match.base_form
                                << " type=" << grammar::verbTypeToString(sokuonbin_match.verb_type)
                                << " cost=" << kHiraganaSokuonbinCost << "\n";
          }
          auto sokuonbin_cand = makeVerbCandidate(
              onbin_surface, start_pos, onbin_end, kHiraganaSokuonbinCost, sokuonbin_match.base_form,
              grammar::verbTypeToConjType(sokuonbin_match.verb_type), true, CandidateOrigin::VerbHiragana, 0.9F,
              "hiragana_sokuonbin", core::ExtendedPOS::VerbOnbinkei);
          sokuonbin_cand.lemma_verified = true;
          candidates.push_back(std::move(sokuonbin_cand));
        }
        // Phase 2: Inflection analysis fallback for short hiragana stems (e.g., やっ)
        // Only for stems of 1-2 characters (e.g., や, やる → やっ)
        if (!found_dict_match && stem.size() <= 6) {  // 2 chars * 3 bytes max
          std::string full_surface = extractSubstring(codepoints, start_pos, hira_extent_end);
          const auto& infl_results = inflection.analyze(full_surface);
          for (const auto& result : infl_results) {
            // Short pure-hiragana stems receive a single-stem confidence
            // deduction, so retain the same threshold as the earlier tense
            // fallback (e.g., あらっ + た from あらう).
            if (result.confidence >= candidate::verb_cost::kShortHiraganaSokuonbinMinConfidence) {
              for (const auto& [verb_type, base_suffix] : sokuonbin_types) {
                std::string potential_base = stem + std::string(base_suffix);
                if (result.base_form == potential_base && result.verb_type == verb_type) {
                  // Without a dictionary attestation, a pure-hiragana
                  // sokuonbin is ambiguous among several godan rows. Follow
                  // the tokenizer's ordinary hiragana preference for the
                  // productive wa-row (あらっ + た → あらう).
                  grammar::VerbType selected_type = verb_type;
                  std::string_view selected_suffix = base_suffix;
                  for (const auto& [fallback_type, fallback_suffix] : sokuonbin_types) {
                    if (fallback_type == grammar::VerbType::GodanWa) {
                      selected_type = fallback_type;
                      selected_suffix = fallback_suffix;
                      break;
                    }
                  }
                  std::string selected_base = stem + std::string(selected_suffix);
                  constexpr float kHiraganaSokuonbinCost = candidate::verb_cost::kModerateBonus;
                  SUZUME_DEBUG_VERBOSE_BLOCK {
                    SUZUME_DEBUG_STREAM << "[VERB_CAND] " << onbin_surface
                                        << " hiragana_sokuonbin_infl lemma=" << selected_base
                                        << " type=" << grammar::verbTypeToString(selected_type)
                                        << " cost=" << kHiraganaSokuonbinCost << "\n";
                  }
                  candidates.push_back(makeVerbCandidate(onbin_surface, start_pos, onbin_end, kHiraganaSokuonbinCost,
                                                         selected_base, grammar::verbTypeToConjType(selected_type),
                                                         true, CandidateOrigin::VerbHiragana, 0.8F,
                                                         "hiragana_sokuonbin_infl", core::ExtendedPOS::VerbOnbinkei));
                  found_dict_match = true;
                  break;
                }
              }
              if (found_dict_match)
                break;
            }
          }
        }
      }
    }
  }

  // Generate Godan hatsuonbin (ん) candidates for hiragana verbs
  // E.g., こんだ → こん (onbin of こむ) + だ (auxiliary)
  //       こんで → こん (onbin of こむ) + で (particle)
  //       よんだ → よん (onbin of よむ) + だ (auxiliary)
  // This separates the onbin stem of godan-ma/ba/na verbs from the auxiliary.
  {
    // Find the complete hiragana run from start_pos.
    const size_t remaining_chars = char_types.size() - start_pos;
    const size_t hira_extent_end =
        vh::findCharRegionEnd(char_types, start_pos, remaining_chars, normalize::CharType::Hiragana);
    size_t hira_len = hira_extent_end - start_pos;

    // Need at least 3 chars: stem(1+) + ん + だ/で
    if (hira_len >= 3) {
      char32_t second_last = codepoints[hira_extent_end - 2];
      char32_t last_char = codepoints[hira_extent_end - 1];
      bool is_hatsuonbin_de_da = (second_last == U'ん' && (last_char == U'だ' || last_char == U'で'));
      if (is_hatsuonbin_de_da) {
        // Generate candidate for stem + ん (without the だ/で)
        size_t onbin_end = hira_extent_end - 1;  // Position after ん
        std::string onbin_surface = extractSubstring(codepoints, start_pos, onbin_end);
        std::string stem = extractSubstring(codepoints, start_pos, onbin_end - 1);

        auto hatsuonbin_match = vh::firstGodanOnbinDictBase(dict_manager, stem, "ん");
        if (hatsuonbin_match.matched) {
          constexpr float kHiraganaHatsuonbinCost = candidate::verb_cost::kStandardBonus;
          SUZUME_DEBUG_VERBOSE_BLOCK {
            SUZUME_DEBUG_STREAM << "[VERB_CAND] " << onbin_surface
                                << " hiragana_hatsuonbin lemma=" << hatsuonbin_match.base_form
                                << " type=" << grammar::verbTypeToString(hatsuonbin_match.verb_type)
                                << " cost=" << kHiraganaHatsuonbinCost << "\n";
          }
          auto hatsuonbin_cand = makeVerbCandidate(
              onbin_surface, start_pos, onbin_end, kHiraganaHatsuonbinCost, hatsuonbin_match.base_form,
              grammar::verbTypeToConjType(hatsuonbin_match.verb_type), true, CandidateOrigin::VerbHiragana, 0.9F,
              "hiragana_hatsuonbin", core::ExtendedPOS::VerbOnbinkei);
          hatsuonbin_cand.lemma_verified = true;
          candidates.push_back(std::move(hatsuonbin_cand));
        }
      }
    }
  }
}

}  // namespace suzume::analysis::hiragana_verb_detail
