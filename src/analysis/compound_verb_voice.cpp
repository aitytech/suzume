/**
 * @file compound_verb_voice.cpp
 * @brief Voice auxiliary tails that remain separate from lexical compound verbs
 */

#include "join_compound_verb_internal.h"

namespace suzume::analysis::compound_verb_detail {

bool addPassiveContinuativeTailCandidates(core::Lattice& lattice, const std::vector<char32_t>& codepoints,
                                          size_t start_pos, size_t kanji_end,
                                          const dictionary::DictionaryManager& dict_manager) {
  // A voice auxiliary followed by the continuative subsidiary remains a
  // grammatical tail rather than a lexical V1+V2 compound. Keep the tail as
  // one search unit so 使わ+れ続ける, 見+られ続ける, and 聞かさ+れ続ける do
  // not collapse into a fabricated whole verb. The gate distinguishes this
  // from lexical 〜れ続ける verbs such as 汚れ続ける: only a preceding
  // mizenkei (a-row) or the ichidan passive られ licenses it.
  for (size_t passive_pos = kanji_end; passive_pos + 3 < codepoints.size(); ++passive_pos) {
    if (codepoints[passive_pos] != U'れ' || codepoints[passive_pos + 1] != U'続' ||
        codepoints[passive_pos + 2] != U'け') {
      continue;
    }
    const char32_t tail_ending = codepoints[passive_pos + 3];
    if (tail_ending != U'る' && tail_ending != U'た' && tail_ending != U'て') {
      continue;
    }

    size_t tail_start = passive_pos;
    const bool follows_godan_mizenkei =
        passive_pos > kanji_end && grammar::isARowCodepoint(codepoints[passive_pos - 1]);
    const bool follows_ichidan_passive = passive_pos == kanji_end + 1 && codepoints[kanji_end] == U'ら';
    // Causative-passive chains retain their voice boundaries, but the
    // passive-continuative tail itself remains one search unit: サ変/一段
    // + させ + られ続ける. The exact three-mora sequence is grammatical
    // evidence; it does not admit an arbitrary られ+続ける join.
    const bool follows_causative_passive = passive_pos >= kanji_end + 3 && codepoints[passive_pos - 3] == U'さ' &&
                                           codepoints[passive_pos - 2] == U'せ' && codepoints[passive_pos - 1] == U'ら';
    if (!follows_godan_mizenkei && !follows_ichidan_passive && !follows_causative_passive) {
      continue;
    }
    if (follows_ichidan_passive || follows_causative_passive) {
      tail_start = follows_causative_passive ? passive_pos - 1 : kanji_end;
    }

    // A Godan causative mizenkei (聞かさ from 聞く) can itself precede
    // passive-continuative れ続ける. Derive that stem only when the underlying
    // pre-causative verb is dictionary-confirmed, avoiding a free-form
    // kanji+hira guess while preserving the productive voice chain.
    if (follows_godan_mizenkei && codepoints[passive_pos - 1] == U'さ' && passive_pos >= start_pos + 2) {
      const char32_t underlying_a_row = codepoints[passive_pos - 2];
      const std::string_view underlying_suffix = grammar::godanBaseSuffixFromARow(underlying_a_row);
      if (!underlying_suffix.empty()) {
        const std::string underlying_base =
            extractSubstring(codepoints, start_pos, passive_pos - 2) + std::string(underlying_suffix);
        if (dict_manager.lookupExact(underlying_base, core::PartOfSpeech::Verb) != nullptr) {
          const std::string causative_stem = extractSubstring(codepoints, start_pos, passive_pos);
          const std::string causative_lemma = extractSubstring(codepoints, start_pos, passive_pos - 1) + "す";
          lattice.addEdge(causative_stem, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(passive_pos),
                          core::PartOfSpeech::Verb, candidate::verb_cost::kStandardBonus, 0, causative_lemma,
                          dictionary::ConjugationType::GodanSa, core::CandidateOrigin::VerbCompound,
                          candidate::kNoOriginConfidence, "causative_mizenkei_before_passive_continuative",
                          core::ExtendedPOS::VerbMizenkei, "causative_mizenkei_before_passive_continuative");
        }
      }
    }

    // The terminal form remains a single auxiliary-like search unit. In past
    // and te forms, expose the renyokei and let the regular た/て auxiliary
    // candidate supply the inflectional boundary.
    const size_t tail_end = passive_pos + (tail_ending == U'る' ? 4 : 3);
    const std::string tail_surface = extractSubstring(codepoints, tail_start, tail_end);
    const std::string tail_lemma = tail_ending == U'る' ? tail_surface : tail_surface + "る";
    lattice.addEdge(tail_surface, static_cast<uint32_t>(tail_start), static_cast<uint32_t>(tail_end),
                    core::PartOfSpeech::Verb, candidate::kVerifiedTailCompoundVerbBonus, 0, tail_lemma,
                    dictionary::ConjugationType::Ichidan, core::CandidateOrigin::VerbCompound,
                    candidate::kNoOriginConfidence, "passive_continuative_tail", core::ExtendedPOS::AuxPassive,
                    "passive_continuative_tail");
    return true;
  }

  return false;
}

}  // namespace suzume::analysis::compound_verb_detail
