/**
 * @file adjective_candidates_kanji_compound.cpp
 * @brief Compound i-adjective candidates for kanji stems
 */

#include <algorithm>
#include <string>
#include <utility>

#include "adjective_candidates_internal.h"
#include "analysis/candidate_constants.h"
#include "core/debug.h"
#include "grammar/inflection.h"
#include "normalize/char_type.h"
#include "tokenizer_utils.h"
#include "unknown.h"
#include "verb_candidates_helpers.h"

namespace suzume::analysis {

namespace {

// A duration/formal-noun kanji may begin a compound adjective only when its
// tail is independently an i-adjective, never merely a Godan continuative.
bool hasValidDurationCompoundTail(const std::vector<char32_t>& codepoints, size_t start_pos, size_t kanji_end,
                                  char32_t first_hira, const grammar::Inflection& inflection,
                                  const dictionary::DictionaryManager* dict_manager) {
  const std::string tail_adj = extractSubstring(codepoints, start_pos + 1, kanji_end) + "い";
  const bool tail_is_dict_adj = verb_helpers::isAdjectiveInDictionary(dict_manager, tail_adj);
  bool tail_is_i_adj = tail_is_dict_adj;
  float tail_adj_confidence = candidate::kNoOriginConfidence;
  if (!tail_is_i_adj && !(first_hira == U'い' && adj_detail::isVerbOnbinContextAfterI(codepoints, kanji_end + 1)) &&
      !verb_helpers::isNounInDictionary(dict_manager, tail_adj) &&
      !verb_helpers::hasDictionaryEntry(dict_manager, tail_adj, core::PartOfSpeech::Verb)) {
    for (const auto& tail_res : inflection.analyze(tail_adj)) {
      if (tail_res.verb_type == grammar::VerbType::IAdjective &&
          tail_res.confidence >= candidate::kCompoundAdjConfMin) {
        tail_is_i_adj = true;
        tail_adj_confidence = std::max(tail_adj_confidence, tail_res.confidence);
      }
    }
  }
  if (tail_is_i_adj && !tail_is_dict_adj) {
    const std::string tail_surface = extractSubstring(codepoints, start_pos + 1, kanji_end + 1);
    for (const auto& tail_res : inflection.analyze(tail_surface)) {
      if (grammar::isGodanVerbType(tail_res.verb_type) && tail_res.base_form == tail_surface &&
          tail_res.confidence > tail_adj_confidence) {
        return false;
      }
    }
  }
  return tail_is_i_adj;
}

}  // namespace

void adj_detail::appendKanjiCompoundIAdjCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                                   size_t kanji_end, size_t hiragana_end,
                                                   const grammar::Inflection& inflection,
                                                   const dictionary::DictionaryManager* dict_manager,
                                                   std::vector<UnknownCandidate>& candidates) {
  // Compound adjective: set has_suffix on existing 2-kanji stem ADJ candidates
  // to skip exceeds_dict_length penalty in tokenizer (薄暗い, 物悲しく, etc.)
  // Guards prevent false positives on suru-verb patterns (遅刻しそう, 確認して):
  //  1. First hiragana must be valid i-adj inflection char (い,く,け,か,し)
  //  2. Hiragana portion must be short (≤5 chars)
  if (kanji_end == start_pos + 2 && kanji_end < codepoints.size()) {
    char32_t first_hira = codepoints[kanji_end];
    // A period/duration formal-noun suffix (間/分/秒/中) must not head an
    // i-adjective compound stem: "3分間続いた" would split as 3分 + 間続い(fake
    // ADJ) instead of 3分間 + 続い(verb), and "長い間続いた" likewise severs 間.
    // Allow only when the second kanji itself forms a genuine i-adjective
    // (間近い → 近い, 分厚い → 厚い), otherwise the compound is masking a verb
    // renyokei (間続い ← 続く). Common tail adjectives are open-class and
    // rule-derived, so a dictionary hit alone is too narrow: accept the tail
    // by rule when it is not a dictionary noun/verb form itself (勢い, 洗い,
    // 違い are nominalizations, not adjectives), inflection recognizes
    // kanji+い as an i-adjective, and the compound's い is not a verb-onbin
    // surface (間続いた, 分置いて).
    char32_t head_char = codepoints[start_pos];
    if (normalize::isDurationSuffixKanji(head_char) &&
        !hasValidDurationCompoundTail(codepoints, start_pos, kanji_end, first_hira, inflection, dict_manager)) {
      SUZUME_DEBUG_LOG_VERBOSE("[ADJ_SKIP] duration-suffix head \"" << head_char << "\" not an i-adj compound\n");
      goto skip_compound_adj;
    }
    {
      // For し: must be followed by い/く/け/か (しい-adj conjugation),
      // NOT そ/な/て/た (suru verb + auxiliary)
      bool valid_adj_start = (first_hira == U'い' || first_hira == U'く' || first_hira == U'け' || first_hira == U'か');
      if (first_hira == U'し' && kanji_end + 1 < codepoints.size()) {
        char32_t second_hira = codepoints[kanji_end + 1];
        valid_adj_start =
            (second_hira == U'い' || second_hira == U'く' || second_hira == U'け' || second_hira == U'か');
      }
      if (valid_adj_start) {
        constexpr size_t kMaxHiraganaLen = 5;
        // Mark existing candidates with has_suffix if they fit the compound pattern
        for (auto& cand : candidates) {
          size_t hira_len = cand.end - kanji_end;
          if (hira_len <= kMaxHiraganaLen) {
            cand.has_suffix = true;
          }
        }
        // Generate new compound candidate if main loop didn't produce one.
        // The 2-kanji penalty drops inflection confidence below the main loop's
        // 0.5 threshold for compound adjectives like 薄暗い, 物悲しく.
        // Use tighter hiragana limits for い/く/か/け (max 2) to prevent
        if (candidates.empty()) {
          size_t hira_limit = (first_hira == U'し') ? kMaxHiraganaLen : 2;
          size_t max_end = std::min(hiragana_end, kanji_end + hira_limit);
          for (size_t end_pos = max_end; end_pos > kanji_end; --end_pos) {
            std::string surface = extractSubstring(codepoints, start_pos, end_pos);
            if (surface.empty())
              continue;
            // The 副助詞 しか is not an adjective conjugation: a genuine しい-
            // adjective past keeps っ right after しか (美味しかっ + た), while
            // noun + しか(…ない) never has the っ. Skip surfaces whose hiragana
            // portion opens with an adverbial particle not followed by っ.
            // @see fabricated closed-class absorption guards (verb_candidates_helpers.h)
            if (end_pos >= kanji_end + 2 && dict_manager != nullptr) {
              std::string leading_hira = extractSubstring(codepoints, kanji_end, kanji_end + 2);
              const dictionary::DictionaryEntry* particle_entry = dict_manager->lookupExact(leading_hira);
              if (particle_entry != nullptr && particle_entry->extended_pos == core::ExtendedPOS::ParticleAdverbial &&
                  (end_pos == kanji_end + 2 || codepoints[kanji_end + 2] != U'っ')) {
                SUZUME_DEBUG_LOG_VERBOSE("[ADJ_SKIP] \"" << surface << "\" hiragana head is adverbial particle\n");
                continue;
              }
            }
            const auto& all_cands = inflection.analyze(surface);
            for (const auto& ic : all_cands) {
              if (ic.confidence >= candidate::kCompoundAdjConfMin && ic.verb_type == grammar::VerbType::IAdjective) {
                float cost = candidate::confidenceScaledCost(candidate::kCompoundAdjBaseCost, ic.confidence,
                                                             candidate::kKanjiAdjConfScale);
                SUZUME_DEBUG_LOG_VERBOSE("[ADJ_COMPOUND] \"" << surface << "\" cost=" << cost
                                                             << " conf=" << ic.confidence << "\n");
                auto adj_cand = makeIAdjCandidate(surface, start_pos, end_pos, ic.base_form, cost,
                                                  CandidateOrigin::AdjectiveI, ic.confidence, "i_adjective_compound");
                adj_cand.has_suffix = true;
                candidates.push_back(std::move(adj_cand));
                goto compound_adj_done;
              }
            }
          }
        compound_adj_done:;
        }
      }
    }
  skip_compound_adj:;
  }
}

}  // namespace suzume::analysis
