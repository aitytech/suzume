/**
 * @file adjective_candidates_kanji_variants.cpp
 * @brief Post-scan variants for kanji i-adjective candidates
 */

#include <algorithm>
#include <array>
#include <utility>

#include "adjective_candidates.h"
#include "adjective_candidates_internal.h"
#include "analysis/candidate_constants.h"
#include "core/utf8_constants.h"
#include "normalize/utf8.h"
#include "unknown.h"
#include "verb_candidates_helpers.h"

namespace suzume::analysis {

using verb_helpers::addEmphaticVariants;
using verb_helpers::isAdjectiveInDictionary;
using verb_helpers::isVerbInDictionary;

void adj_detail::appendKanjiIAdjPostVariants(const std::vector<char32_t>& codepoints, size_t start_pos,
                                             size_t kanji_end, size_t hiragana_end,
                                             const grammar::Inflection& inflection,
                                             const dictionary::DictionaryManager* dict_manager,
                                             std::vector<UnknownCandidate>& candidates) {
  // Add emphatic variants (すごい → すごいっっ, etc.)
  addEmphaticVariants(candidates, codepoints);

  // Preserve inflection and auxiliary/particle boundaries. Rules remain
  // path-local because the kanji path uses stronger negative splitting and a
  // dictionary-backed ければ disambiguation.
  static constexpr std::array<adj_detail::TrimmedAdjVariantRule, 6> kTrimRules = {{
      {"くない", 2, candidate::kAdjKuSplitBonus, core::ExtendedPOS::AdjRenyokei, 0, "i_adjective_ku_nai"},
      {"くなかった", 4, candidate::kAdjKuSplitBonus, core::ExtendedPOS::AdjRenyokei, 0, "i_adjective_ku_nakatta"},
      {"くなかっ", 3, candidate::kAdjKuSplitBonus, core::ExtendedPOS::AdjRenyokei, 0, "i_adjective_ku_nakatt"},
      {"くて", 1, candidate::kAdjKuSplitBonus, core::ExtendedPOS::AdjRenyokei, 1, "i_adjective_ku_te"},
      {"かった", 1, candidate::kAdjKattSplitBonus, core::ExtendedPOS::AdjKatt, 2, "i_adjective_katt"},
      {"ければ", 1, candidate::kAdjKeSplitBonus, core::ExtendedPOS::AdjKeForm, 3, "i_adjective_kere", false, false,
       true},
  }};
  adj_detail::appendTrimmedAdjVariants(candidates, kTrimRules.data(), kTrimRules.size(), dict_manager);

  // The past た is always a separate auxiliary: an i-adjective past never stands
  // as one かった token (難しかっ|た, 良くなかっ|た). Every span ending in かった
  // produced its trimmed かっ variant above, so drop the merged span itself —
  // it only ever wins over the split when a preceding modifier's connection
  // bonus favors the terminal-form EPOS, which is exactly the wrong parse.
  candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                  [](const UnknownCandidate& cand) { return utf8::endsWith(cand.surface, "かった"); }),
                   candidates.end());

  // The conjunctive くて is never an adjective terminal form. Its trimmed
  // continuative candidate is emitted above, so remove the whole-span
  // alternative that would otherwise hide the connective particle.
  candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                  [](const UnknownCandidate& cand) {
                                    return cand.pos == core::PartOfSpeech::Adjective &&
                                           utf8::endsWith(cand.surface, "くて");
                                  }),
                   candidates.end());

  // Add mizenkei (かろ) candidates for the conjectural pattern: stem + かろ + う
  // (高かろう, 美しかろう). Shared with the pure-hiragana generator.
  appendIAdjKaroCandidates(codepoints, start_pos, kanji_end, hiragana_end, inflection, dict_manager, candidates);
  appendIAdjKaraZuCandidates(codepoints, start_pos, kanji_end, hiragana_end, inflection, dict_manager, candidates);

  // Add classical attributive (文語連体形) き candidates: stem + き + 体言
  // I-adjective 連体形 in classical Japanese: 美しい → 美しき(花), 古い → 古き(良き時代)
  // Inflection analysis does not produce this form, and the surface Xき is
  // homographic with godan-ka verb 連用形 (書き ← 書く), so generate only when
  // the lexical signal is decisive: the reconstructed base (stem + い) is a
  // known dictionary adjective. The lemma normalizes to the modern base form.
  if (dict_manager != nullptr) {
    for (size_t ki_pos = kanji_end; ki_pos < hiragana_end; ++ki_pos) {
      if (codepoints[ki_pos] != U'き') {
        continue;
      }
      std::string ki_stem = extractSubstring(codepoints, start_pos, ki_pos);
      std::string ki_lemma = ki_stem + "い";
      if (!isAdjectiveInDictionary(dict_manager, ki_lemma)) {
        continue;
      }
      // If stem + く is a real godan-ka verb, Xき is its 連用形 (行き, 焼き),
      // not the classical adjective form — leave it to the verb paths.
      if (isVerbInDictionary(dict_manager, ki_stem + "く")) {
        continue;
      }
      // If the surface itself is a dictionary entry (好き, 大好き), the
      // dictionary interpretation wins — do not shadow it.
      std::string ki_surface = extractSubstring(codepoints, start_pos, ki_pos + 1);
      if (verb_helpers::hasNonVerbDictionaryEntry(dict_manager, ki_surface) ||
          isVerbInDictionary(dict_manager, ki_surface)) {
        continue;
      }
      UnknownCandidate ki_cand;
      ki_cand.surface = ki_surface;
      ki_cand.start = start_pos;
      ki_cand.end = ki_pos + 1;
      ki_cand.pos = core::PartOfSpeech::Adjective;
      ki_cand.lemma = ki_lemma;
      // Dictionary-verified adjective: make the 連体形 win over fake verb
      // interpretations (godan-ka 美しく etc.), mirroring the ke-form handling.
      ki_cand.cost = candidate::verb_cost::kStrongBonus;
      ki_cand.has_suffix = true;  // Conjugated form (連体形)
      // Attributive form connects like the basic form (ADJ + 体言)
      ki_cand.extended_pos = core::ExtendedPOS::AdjBasic;
#ifdef SUZUME_DEBUG_INFO
      ki_cand.origin = CandidateOrigin::AdjectiveI;
      ki_cand.confidence = 0.8F;
      ki_cand.pattern = "i_adjective_classical_ki";
#endif
      candidates.push_back(std::move(ki_cand));
    }
  }
}

}  // namespace suzume::analysis
