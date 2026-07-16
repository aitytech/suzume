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

void appendContextualSubsidiaryCandidate(const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos,
                                         std::string_view lemma, dictionary::ConjugationType conj_type,
                                         core::ExtendedPOS extended_pos, const char* pattern,
                                         std::vector<UnknownCandidate>& candidates) {
  // The candidate is useful only with its ParticleConj connection bonus. A
  // positive standalone cost prevents a raw character boundary from overriding
  // a lexical reading when the preceding て/で is not a conjunctive particle.
  constexpr float kContextualCost = bigram_cost::kMinor;
  const std::string surface = extractSubstring(codepoints, start_pos, end_pos);
  auto candidate = makeCandidate(surface, start_pos, end_pos, core::PartOfSpeech::Auxiliary, kContextualCost, true,
                                 CandidateOrigin::VerbHiragana, extended_pos, pattern);
  candidate.lemma = lemma;
  candidate.conj_type = conj_type;
  candidates.push_back(std::move(candidate));
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
      bool is_valid_verb = vh::isVerbInDictionary(dict_manager, base_form);
      // Capture dictionary attestation before the inflection fallback below may
      // set is_valid_verb on a non-dictionary base.
      const bool lemma_dict_verified = is_valid_verb;

      // For sokuonbin tense patterns (っ+た/て), also use inflection analysis:
      // it validates common verbs like かう, やる that may not be in the dictionary.
      // Hatsuonbin (ん) is deliberately EXCLUDED: its godan type is rule-ambiguous
      // (む/ぶ/ぬ are table-identical, all endorsed at equal confidence), so the
      // fallback would validate a table-first NON-WORD base (あそんで→あそむ). The
      // ん-onbin lemma must come from the dictionary path instead (bounded L2 +
      // the dedicated hatsuonbin generator), never from an inflection guess.
      // Exception: skip short particle-starting stems (となっ should be と+なっ);
      // longer stems like はじまっ (3+ chars) are allowed.
      if (!is_valid_verb && is_tense_pattern && is_sokuonbin && !starts_with_short_particle_stem) {
        // Construct full form: onbin + tense suffix (e.g., かった, やった)
        std::string_view tense_char = (next_char == U'た' || next_char == U'だ') ? "た" : "て";
        std::string full_form = stem + "っ" + std::string(tense_char);
        const auto& analysis = inflection.analyze(full_form);
        for (const auto& cand : analysis) {
          // Lower threshold (0.25) for short stems like かっ, やっ
          // since godan_single_hiragana_stem penalty reduces confidence
          if (cand.verb_type == verb_type && cand.base_form == base_form && cand.confidence >= 0.25F) {
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
      float cost = is_contraction_pattern ? -0.5F : 0.2F;
      SUZUME_DEBUG_VERBOSE_BLOCK {
        SUZUME_DEBUG_STREAM << "[VERB_CAND] " << onbin_surface
                            << (is_tense_pattern ? " hiragana_onbin_tense" : " hiragana_onbin_contraction")
                            << " lemma=" << base_form << " cost=" << cost << "\n";
      }
      const char* pattern = is_sokuonbin ? "hiragana_sokuonbin" : "hiragana_hatsuonbin";
      auto onbin_cand = makeVerbCandidate(onbin_surface, start_pos, onbin_pos + 1, cost, base_form,
                                          grammar::verbTypeToConjType(verb_type), true, CandidateOrigin::VerbHiragana,
                                          0.9F, pattern, core::ExtendedPOS::VerbOnbinkei);
      // Verified only for genuinely rule-only forms: if the onbin surface is
      // itself a dictionary entry, that edge carries the authoritative reading
      // and this candidate must not undercut it (であった あっ, いった いっ).
      const bool onbin_surface_in_dict = dict_manager != nullptr && dict_manager->lookupExact(onbin_surface) != nullptr;
      onbin_cand.lemma_verified = lemma_dict_verified && !onbin_surface_in_dict;
      candidates.push_back(std::move(onbin_cand));
      break;  // Found valid candidate for this position
    }
  }
}

// Irregular 来る (カ変) mizenkei こ before a ない-family negative (こない,
// こなかった, こなくて, こなければ, こなきゃ). The surface こ is far too
// frequent as a word fragment for an unconditional dictionary entry (こと,
// これ, きのこ, ...), but こ immediately followed by the negative auxiliary is
// unambiguously 来る + negation, so the candidate is generated only under that
// gate. The reading is chosen from the preceding context, mirroring MeCab:
//   - after て: directional auxiliary てくる (出て + こ + ない) → Auxiliary / AuxAspectKuru
//   - otherwise: main verb 来る (誰も + こ + ない, こない)        → Verb / VerbMizenkei
// Emitting a single context-appropriate reading avoids relying on an
// AuxAspectKuru→ない bigram, which would also mis-flip other てくる auxiliaries
// (くれ, etc.) sharing that ExtendedPOS.
void appendKuruMizenkeiNaiCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                     std::vector<UnknownCandidate>& candidates) {
  if (codepoints[start_pos] != U'こ' || !vh::naiNegativeFollowsAt(codepoints, start_pos + 1)) {
    return;
  }
  const std::string surface = extractSubstring(codepoints, start_pos, start_pos + 1);
  constexpr float kCost = candidate::verb_cost::kStandardBonus;
  const bool subsidiary = start_pos > 0 && codepoints[start_pos - 1] == U'て';
  SUZUME_DEBUG_VERBOSE_BLOCK {
    SUZUME_DEBUG_STREAM << "[VERB_CAND] " << surface << " hiragana_kuru_mizenkei_nai lemma=くる cost=" << kCost
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
  candidates.push_back(makeVerbCandidate(surface, start_pos, start_pos + 1, kCost, "くる",
                                         dictionary::ConjugationType::Kuru, true, CandidateOrigin::VerbHiragana,
                                         candidate::kHighOriginConfidence, "hiragana_kuru_mizenkei_nai",
                                         core::ExtendedPOS::VerbMizenkei));
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

  if (grammaticalStemFollowerStartsAt(codepoints, start_pos + 1, dict_manager)) {
    appendContextualSubsidiaryCandidate(codepoints, start_pos, start_pos + 1, "みる",
                                        dictionary::ConjugationType::Ichidan, core::ExtendedPOS::AuxAspectMiru,
                                        "hiragana_miru_auxiliary", candidates);
  }

  if (start_pos + 1 >= codepoints.size()) {
    return;
  }
  // 一段活用の終止・仮定・命令・意志形: みる/みれ/みろ/みよ.
  const char32_t ending = codepoints[start_pos + 1];
  if (ending == core::hiragana::kRu || ending == core::hiragana::kRe || ending == U'ろ' ||
      ending == core::hiragana::kYo) {
    appendContextualSubsidiaryCandidate(codepoints, start_pos, start_pos + 2, "みる",
                                        dictionary::ConjugationType::Ichidan, core::ExtendedPOS::AuxAspectMiru,
                                        "hiragana_miru_auxiliary", candidates);
  }
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
  appendContextualSubsidiaryCandidate(codepoints, start_pos, start_pos + 2, "おく",
                                      dictionary::ConjugationType::GodanKa, core::ExtendedPOS::AuxAspectOku,
                                      "hiragana_oku_auxiliary", candidates);
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
    candidates.push_back(makeVerbCandidate(stem_surface, start_pos, end_pos, cost, chosen_base, chosen_conj, true,
                                           CandidateOrigin::VerbHiragana, chosen_confidence, "hiragana_renyokei"));

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
    // Find hiragana extent (all consecutive hiragana from start_pos)
    size_t hira_extent_end = start_pos;
    while (hira_extent_end < char_types.size() && char_types[hira_extent_end] == normalize::CharType::Hiragana) {
      ++hira_extent_end;
    }
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
          // Verified only when the onbin surface is not itself a dictionary
          // entry (that edge would carry the authoritative reading).
          sokuonbin_cand.lemma_verified =
              dict_manager == nullptr || dict_manager->lookupExact(onbin_surface) == nullptr;
          candidates.push_back(std::move(sokuonbin_cand));
        }
        // Phase 2: Inflection analysis fallback for short hiragana stems (e.g., やっ)
        // Only for stems of 1-2 characters (e.g., や, やる → やっ)
        if (!found_dict_match && stem.size() <= 6) {  // 2 chars * 3 bytes max
          std::string full_surface = extractSubstring(codepoints, start_pos, hira_extent_end);
          const auto& infl_results = inflection.analyze(full_surface);
          for (const auto& result : infl_results) {
            if (result.confidence >= 0.5F) {
              for (const auto& [verb_type, base_suffix] : sokuonbin_types) {
                std::string potential_base = stem + std::string(base_suffix);
                if (result.base_form == potential_base && result.verb_type == verb_type) {
                  constexpr float kHiraganaSokuonbinCost = -0.3F;  // Slightly higher than dict match
                  SUZUME_DEBUG_VERBOSE_BLOCK {
                    SUZUME_DEBUG_STREAM << "[VERB_CAND] " << onbin_surface
                                        << " hiragana_sokuonbin_infl lemma=" << potential_base
                                        << " type=" << grammar::verbTypeToString(verb_type)
                                        << " cost=" << kHiraganaSokuonbinCost << "\n";
                  }
                  candidates.push_back(makeVerbCandidate(onbin_surface, start_pos, onbin_end, kHiraganaSokuonbinCost,
                                                         potential_base, grammar::verbTypeToConjType(verb_type), true,
                                                         CandidateOrigin::VerbHiragana, 0.8F, "hiragana_sokuonbin_infl",
                                                         core::ExtendedPOS::VerbOnbinkei));
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
    // Find hiragana extent (all consecutive hiragana from start_pos)
    size_t hira_extent_end = start_pos;
    while (hira_extent_end < char_types.size() && char_types[hira_extent_end] == normalize::CharType::Hiragana) {
      ++hira_extent_end;
    }
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
          // Verified only when the onbin surface is not itself a dictionary
          // entry (that edge would carry the authoritative reading).
          hatsuonbin_cand.lemma_verified =
              dict_manager == nullptr || dict_manager->lookupExact(onbin_surface) == nullptr;
          candidates.push_back(std::move(hatsuonbin_cand));
        }
      }
    }
  }
}

}  // namespace suzume::analysis::hiragana_verb_detail
