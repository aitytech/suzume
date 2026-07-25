/**
 * @file verb_candidates_kanji_classical.cpp
 * @brief Classical ハ行四段 (historical kana) verb candidates
 */

#include <algorithm>
#include <initializer_list>

#include "analysis/candidate_constants.h"
#include "analysis/verb_candidates_kanji_internal.h"
#include "core/debug.h"
#include "grammar/conjugation.h"
#include "tokenizer_utils.h"
#include "unknown.h"
#include "verb_candidates.h"

namespace suzume::analysis::kanji_verb_detail {

namespace {

// Longest closed-class tail probed after a paradigm cell (ざり, ども).
constexpr size_t kClassicalTailProbeChars = 3;

// Longest okurigana run allowed between the kanji stem and the row kana
// (移ろ+ひ, 恥ぢら+ひ).
constexpr size_t kOkuriganaProbeChars = 2;

/**
 * @brief Paradigm cell named by a ha-row tail kana.
 *
 * The classical ハ行四段 row is the historical-kana spelling of the modern
 * ワ行五段 row (思ふ/思う, 移ろふ/移ろう): only the tail kana differs, so the
 * paradigm needs no conjugation table of its own.  The tail names the cell and
 * the base form keeps the historical terminal ふ.  終止形 and 連体形 share one
 * form, and 已然形 occupies the modern conditional slot.
 */
core::ExtendedPOS classicalHaRowCell(char32_t tail) {
  switch (tail) {
    case U'は':
      return core::ExtendedPOS::VerbMizenkei;
    case U'ひ':
      return core::ExtendedPOS::VerbRenyokei;
    case U'ふ':
      return core::ExtendedPOS::VerbShuushikei;
    case U'へ':
      return core::ExtendedPOS::VerbKateikei;
    default:
      return core::ExtendedPOS::Unknown;
  }
}

bool dictionaryTailFollowsAt(const std::vector<char32_t>& codepoints, size_t pos,
                             const dictionary::DictionaryManager* dict_manager, core::PartOfSpeech pos_class,
                             std::initializer_list<core::ExtendedPOS> accepted) {
  if (dict_manager == nullptr || pos >= codepoints.size()) {
    return false;
  }
  const size_t probe_end = std::min(codepoints.size(), pos + kClassicalTailProbeChars);
  for (size_t end = pos + 1; end <= probe_end; ++end) {
    const auto* entry = dict_manager->lookupExact(extractSubstring(codepoints, pos, end), pos_class);
    if (entry == nullptr) {
      continue;
    }
    for (const core::ExtendedPOS candidate_pos : accepted) {
      if (entry->extended_pos == candidate_pos) {
        return true;
      }
    }
  }
  return false;
}

bool clauseEndsAt(const std::vector<char32_t>& codepoints, size_t pos) {
  if (pos >= codepoints.size()) {
    return true;
  }
  const char32_t next = codepoints[pos];
  return next == U'。' || next == U'、' || next == U'！' || next == U'？' || next == U'」';
}

/**
 * @brief Evidence found after a paradigm cell.
 *
 * Every ha-row kana is also a frequent modern particle (topic は, direction へ)
 * or a plain noun ending, so a cell is admitted only where what follows it
 * selects the classical paradigm.  A closed-class tail names the cell outright
 * and is stronger evidence than a bare clause boundary.
 */
struct HaRowLicense {
  bool licensed = false;
  bool closed_class_tail = false;
};

HaRowLicense haRowCellLicense(core::ExtendedPOS cell, const std::vector<char32_t>& codepoints, size_t end_pos,
                              const dictionary::DictionaryManager* dict_manager) {
  HaRowLicense license;
  switch (cell) {
    case core::ExtendedPOS::VerbMizenkei:
      // 未然形 exists only under a classical irrealis auxiliary (思は+ず).
      license.closed_class_tail = dictionaryTailFollowsAt(
          codepoints, end_pos, dict_manager, core::PartOfSpeech::Auxiliary, {core::ExtendedPOS::AuxNegativeNu});
      break;
    case core::ExtendedPOS::VerbRenyokei:
      // 連用形 heads a classical predicate chain or closes a clause.
      license.closed_class_tail =
          dictionaryTailFollowsAt(codepoints, end_pos, dict_manager, core::PartOfSpeech::Auxiliary,
                                  {core::ExtendedPOS::AuxClassicalKeri, core::ExtendedPOS::AuxClassicalPerfect,
                                   core::ExtendedPOS::AuxVolitional, core::ExtendedPOS::AuxDesireTai});
      license.licensed = clauseEndsAt(codepoints, end_pos);
      break;
    case core::ExtendedPOS::VerbShuushikei:
      // 終止形 closes a clause or carries a terminal-attaching auxiliary.
      license.closed_class_tail =
          dictionaryTailFollowsAt(codepoints, end_pos, dict_manager, core::PartOfSpeech::Auxiliary,
                                  {core::ExtendedPOS::AuxClassicalBeshi, core::ExtendedPOS::AuxVolitional,
                                   core::ExtendedPOS::AuxClassicalNari});
      license.licensed = clauseEndsAt(codepoints, end_pos);
      break;
    case core::ExtendedPOS::VerbKateikei:
      // 已然形 exists only before a concessive or conditional conjunction
      // (思へ+ど, 思へ+ば).
      license.closed_class_tail = dictionaryTailFollowsAt(
          codepoints, end_pos, dict_manager, core::PartOfSpeech::Particle, {core::ExtendedPOS::ParticleConj});
      break;
    default:
      return license;
  }
  license.licensed = license.licensed || license.closed_class_tail;
  return license;
}

}  // namespace

void appendClassicalHaRowCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t kanji_end,
                                    size_t hiragana_end, const dictionary::DictionaryManager* dict_manager,
                                    std::vector<UnknownCandidate>& candidates) {
  if (kanji_end == start_pos || kanji_end >= hiragana_end) {
    return;
  }
  const size_t scan_end = std::min(hiragana_end, kanji_end + kOkuriganaProbeChars + 1);
  for (size_t tail_pos = kanji_end; tail_pos < scan_end; ++tail_pos) {
    const core::ExtendedPOS cell = classicalHaRowCell(codepoints[tail_pos]);
    if (cell == core::ExtendedPOS::Unknown) {
      continue;
    }
    // は is both the topic particle and the first mora of the formal noun はず,
    // and both of those follow a word that is already complete (読む+はず,
    // 村の+はずれ). Only a bare kanji stem leaves the paradigm as the sole
    // reading, so the 未然形 cell takes no okurigana in front of it.
    if (cell == core::ExtendedPOS::VerbMizenkei && tail_pos != kanji_end) {
      continue;
    }
    const size_t end_pos = tail_pos + 1;
    const HaRowLicense license = haRowCellLicense(cell, codepoints, end_pos, dict_manager);
    if (!license.licensed) {
      continue;
    }
    const std::string surface = extractSubstring(codepoints, start_pos, end_pos);
    const std::string lemma = extractSubstring(codepoints, start_pos, tail_pos) + "ふ";
    const float cost = license.closed_class_tail ? candidate::verb_cost::kClassicalHaRowLicensedCost
                                                 : candidate::verb_cost::kClassicalHaRowCost;
    auto candidate = makeVerbCandidate(surface, start_pos, end_pos, cost, lemma,
                                       grammar::verbTypeToConjType(grammar::VerbType::GodanWa), true,
                                       CandidateOrigin::VerbKanji, candidate::kNoConfidence, "classical_ha_row", cell);
    candidates.push_back(std::move(candidate));
    SUZUME_DEBUG_LOG_VERBOSE("[VERB_CAND] " << surface << " classical_ha_row lemma=" << lemma << "\n");
  }
}

}  // namespace suzume::analysis::kanji_verb_detail
