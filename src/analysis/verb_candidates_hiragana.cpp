/**
 * @file verb_candidates_hiragana.cpp
 * @brief Hiragana-based verb candidate generation (generateHiraganaVerbCandidates)
 *
 * Handles verb candidate generation for pure hiragana patterns.
 * Split from verb_candidates.cpp for maintainability.
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

namespace suzume::analysis {

namespace vh = verb_helpers;
using namespace hiragana_verb_detail;

namespace {

bool hasSokuonbinTenseEvidence(const std::vector<char32_t>& codepoints, size_t start_pos,
                               const std::vector<normalize::CharType>& char_types,
                               const grammar::Inflection& inflection) {
  size_t end_pos = start_pos;
  while (end_pos < char_types.size() && end_pos - start_pos < 12 &&
         char_types[end_pos] == normalize::CharType::Hiragana) {
    ++end_pos;
  }
  for (size_t onbin_pos = start_pos + 1; onbin_pos + 1 < end_pos; ++onbin_pos) {
    if (codepoints[onbin_pos] != U'っ' || (codepoints[onbin_pos + 1] != U'た' && codepoints[onbin_pos + 1] != U'て')) {
      continue;
    }
    const std::string inflected_surface = extractSubstring(codepoints, start_pos, onbin_pos + 2);
    for (const auto& inflection_candidate : inflection.analyze(inflected_surface)) {
      const bool is_sokuonbin_godan = inflection_candidate.verb_type == grammar::VerbType::GodanWa ||
                                      inflection_candidate.verb_type == grammar::VerbType::GodanRa ||
                                      inflection_candidate.verb_type == grammar::VerbType::GodanTa;
      if (is_sokuonbin_godan && inflection_candidate.confidence >= candidate::kParticleVerbBoundaryMinConfidence) {
        return true;
      }
    }
  }
  return false;
}

// A long pure-hiragana sequence ending in わない carries explicit Godan-wa
// irrealis evidence. This lets the sequence scanner retain particle-shaped
// internal morae only when the full inflection proves they belong to a verb.
bool hasLongGodanWaNegativeEvidence(const std::vector<char32_t>& codepoints, size_t start_pos, size_t current_pos,
                                    const std::vector<normalize::CharType>& char_types) {
  for (size_t negative_pos = current_pos + 1; negative_pos + 2 < codepoints.size() && negative_pos - start_pos < 12;
       ++negative_pos) {
    if (char_types[negative_pos] != normalize::CharType::Hiragana) {
      break;
    }
    if (negative_pos >= start_pos + 3 && codepoints[negative_pos] == U'わ' && codepoints[negative_pos + 1] == U'な' &&
        codepoints[negative_pos + 2] == U'い') {
      return true;
    }
  }
  return false;
}

}  // namespace

std::vector<UnknownCandidate> generateHiraganaVerbCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                                             const std::vector<normalize::CharType>& char_types,
                                                             const grammar::Inflection& inflection,
                                                             const dictionary::DictionaryManager* dict_manager,
                                                             const VerbCandidateOptions& verb_opts) {
  std::vector<UnknownCandidate> candidates;

  if (start_pos >= char_types.size() || char_types[start_pos] != normalize::CharType::Hiragana) {
    return candidates;
  }

  if (vh::startsInsideDictionaryParticle(codepoints, start_pos, dict_manager)) {
    SUZUME_DEBUG_LOG_VERBOSE("[VERB_SKIP] pos=" << start_pos << " inside_dictionary_particle\n");
    return candidates;
  }

  // In the causative-passive chain V未然+させ+られ+た, られ is the
  // closed-class passive auxiliary. Do not fabricate an independent
  // hiragana verb られる/られた across that already-proven auxiliary
  // boundary; the dictionary auxiliary candidate owns this position.
  if (start_pos >= 2 && codepoints[start_pos] == U'ら' && codepoints[start_pos - 1] == U'せ' &&
      codepoints[start_pos - 2] == U'さ') {
    return candidates;
  }

  // Do not start a fabricated hiragana Ichidan verb in the final mora of a
  // kanji-written Ichidan stem immediately before a voice auxiliary:
  // 確かめ+られ, 確かめ+させ, 見せ+させ. The preceding kanji-tail candidate
  // owns that lexical stem and the closed-class auxiliary owns the suffix.
  const bool voice_auxiliary_follows =
      (start_pos + 2 < codepoints.size() && codepoints[start_pos + 1] == U'さ' && codepoints[start_pos + 2] == U'せ') ||
      (start_pos + 2 < codepoints.size() && codepoints[start_pos + 1] == U'ら' && codepoints[start_pos + 2] == U'れ');
  if (voice_auxiliary_follows &&
      (grammar::isERowCodepoint(codepoints[start_pos]) || grammar::isIRowCodepoint(codepoints[start_pos]))) {
    size_t kana_stem_start = start_pos;
    while (kana_stem_start > 0 && char_types[kana_stem_start - 1] == normalize::CharType::Hiragana) {
      --kana_stem_start;
    }
    if (kana_stem_start > 0 && char_types[kana_stem_start - 1] == normalize::CharType::Kanji) {
      return candidates;
    }
  }

  // Context-gated irregular 来る mizenkei: こ + ない-family negative
  appendKkoNegativeConjectureCandidates(codepoints, start_pos, candidates);
  appendSuruInabilityCandidates(codepoints, start_pos, candidates);
  appendEruObligationCandidates(codepoints, start_pos, candidates);
  appendKuruMizenkeiNaiCandidates(codepoints, start_pos, candidates);

  // Context-gated directional いく inflections after a clear te-form.
  appendIkuAuxiliaryCandidates(codepoints, start_pos, candidates);

  // Context-gated benefactive やる irrealis before negation.
  appendYaruBenefactiveCandidates(codepoints, start_pos, candidates);

  // Context-gated 試行補助動詞 みる after a clear te-form boundary.
  appendMiruAuxiliaryCandidates(codepoints, start_pos, dict_manager, candidates);
  appendMiseruAuxiliaryCandidates(codepoints, start_pos, dict_manager, candidates);
  appendAgeruBenefactiveCandidates(codepoints, start_pos, dict_manager, candidates);
  // Context-gated 準備補助動詞 おく after a clear te-form boundary.
  appendOkuAuxiliaryCandidates(codepoints, start_pos, candidates);

  // Skip if starting with a small kana (拗音・促音: ゃ/ゅ/ょ/っ/ぁ…). No Japanese
  // word starts with a small kana — it always continues the preceding digraph, so
  // any candidate here would cut through it. E.g., おっしゃい must not spawn a
  // fragment verb ゃい (fabricated godan-wa ゃう).
  char32_t first_char = codepoints[start_pos];
  if (kana::isSmallKanaCodepoint(first_char)) {
    SUZUME_DEBUG_LOG_VERBOSE("[VERB_SKIP] pos=" << start_pos << " small_kana_start (impossible word start)\n");
    return candidates;
  }

  // Skip if starting character is a particle that is NEVER a verb stem
  // Note: Characters that CAN be verb stems are NOT skipped:
  //   - な→なる/なくす, て→できる, や→やる, か→かける/かえる
  // The initial の is normally a particle, but a following っ+た/て sequence
  // is independent Godan inflectional evidence.  Admit that structurally
  // verified path so kana-written verbs are not cut through their onbin stem.
  const bool is_particle_initial_sokuonbin =
      first_char == U'の' && hasSokuonbinTenseEvidence(codepoints, start_pos, char_types, inflection);
  if (normalize::isNeverVerbStemAtStart(first_char) && !is_particle_initial_sokuonbin) {
    SUZUME_DEBUG_LOG_VERBOSE("[VERB_BLACKLIST] pos=" << start_pos << " char=U+" << std::hex
                                                     << static_cast<uint32_t>(first_char) << std::dec
                                                     << " blocked (isNeverVerbStemAtStart)\n");
    return candidates;
  }

  // Skip if starting with demonstrative pronouns (これ, それ, あれ, どれ, etc.)
  // These are commonly mistaken for verbs (これる, それる, etc.)
  // Exception: あれば is the conditional form of ある (verb), not pronoun + particle
  if (start_pos + 1 < codepoints.size()) {
    char32_t second_char = codepoints[start_pos + 1];
    if (normalize::isDemonstrativeStart(first_char, second_char)) {
      // Check if followed by conditional ば - if so, it might be verb conditional form
      // E.g., あれば = ある (verb) + ば, not あれ (pronoun) + ば
      bool is_conditional_form = (start_pos + 2 < codepoints.size() && codepoints[start_pos + 2] == U'ば');
      if (!is_conditional_form) {
        SUZUME_DEBUG_LOG_VERBOSE("[VERB_SKIP] pos=" << start_pos
                                                    << " demonstrative_pronoun (これ/それ/あれ/どれ pattern)\n");
        return candidates;
      }
    }

    // Skip if starting with 「ない」(auxiliary verb/i-adjective for negation)
    // These should be recognized as AUX by dictionary, not as hiragana verbs.
    // E.g., 「ないんだ」→「ない」+「んだ」, not a single verb「ないむ」
    if (first_char == U'な' && second_char == U'い') {
      SUZUME_DEBUG_LOG_VERBOSE("[VERB_SKIP] pos=" << start_pos << " nai_pattern (ない is auxiliary/adjective)\n");
      return candidates;
    }

    // Skip if starting with 「く」+ な行 (くな, くに, くぬ, くね, くの)
    // These are i-adjective ku-form + なる/ない patterns, not verbs
    // E.g., 「たくなる」→「たく」+「なる」, not a verb「くなる」
    //       「くなってきた」→「く」+「なっ」+「て」+...
    // Note: くる (来る) is a valid verb but has kanji and is handled by dictionary
    if (first_char == U'く') {
      // く + な行 hiragana = i-adjective pattern
      if (second_char == U'な' || second_char == U'に' || second_char == U'ぬ' || second_char == U'ね' ||
          second_char == U'の') {
        SUZUME_DEBUG_LOG_VERBOSE("[VERB_SKIP] pos=" << start_pos
                                                    << " ku_naru_pattern (i-adjective ku-form + なる/ない)\n");
        return candidates;
      }
    }

    // Skip if starting with 「であり」(copula de + aru renyokei)
    // The copula で and the verb stem あり are separate grammatical units.
    // E.g., 「であります」→「で」+「あり」+「ます」, not a single verb「でありる」
    if (first_char == U'で' && second_char == U'あ') {
      char32_t third_char = (start_pos + 2 < codepoints.size()) ? codepoints[start_pos + 2] : 0;
      if (third_char == U'り' || third_char == U'れ' || third_char == U'る' || third_char == U'ろ') {
        SUZUME_DEBUG_LOG_VERBOSE("[VERB_SKIP] pos=" << start_pos << " deari_pattern (copula de + aru conjugation)\n");
        return candidates;
      }
    }
  }

  // Find hiragana sequence, breaking at particle boundaries
  // Note: Be careful not to break at characters that are part of verb conjugations:
  //   - か can be part of なかった (negative past) or かった (i-adj past)
  //   - で can be part of んで (te-form for godan) or できる (potential verb)
  //   - も can be part of ても (even if) or もらう (receiving verb)
  size_t hiragana_end = start_pos;
  while (hiragana_end < char_types.size() && hiragana_end - start_pos < 12 &&  // Max 12 hiragana for verb + endings
         char_types[hiragana_end] == normalize::CharType::Hiragana) {
    // Don't include particles that appear after the first hiragana character.
    // E.g., for "りにする", stop at "り" to not include "にする".
    if (hiragana_end > start_pos) {
      char32_t curr = codepoints[hiragana_end];

      // Check for particle-like characters (common particles + も, や)
      const bool has_godan_wa_negative =
          hasLongGodanWaNegativeEvidence(codepoints, start_pos, hiragana_end, char_types);
      if (normalize::isNeverVerbStemAfterKanji(curr) && !(curr == U'の' && has_godan_wa_negative)) {
        SUZUME_DEBUG_LOG_TRACE("[HIRA_SEQ] pos=" << hiragana_end << " char=U+" << std::hex
                                                 << static_cast<uint32_t>(curr) << std::dec
                                                 << " action=break (isNeverVerbStemAfterKanji)\n");
        break;  // These are always particles in this context
      }

      // For か, で, も, と: check if they're part of verb conjugation patterns
      // Don't break if they appear in known conjugation contexts
      if (curr == U'か' || curr == U'で' || curr == U'も' || curr == U'と') {
        // Check the preceding character for conjugation patterns
        char32_t prev = codepoints[hiragana_end - 1];

        // か: OK if preceded by な (なかった = negative past)
        //    Also OK if followed by れ (かれ = ichidan stem like つかれる, ふざける)
        //    Also OK if followed by んで/んだ (onbin te/ta-form: つかんで, 歩かんで)
        //    Also OK if followed by A-row + ん (mizenkei + contracted negative: わからん)
        if (curr == U'か') {
          if (prev == U'な') {
            ++hiragana_end;
            continue;
          }
          // Check if followed by れ (ichidan stem pattern)
          if (hiragana_end + 1 < codepoints.size() && codepoints[hiragana_end + 1] == U'れ') {
            ++hiragana_end;
            continue;
          }
          // Check if followed by んで/んだ (GodanMa/Na/Ba onbin te/ta-form)
          // e.g., つかんで (掴んで), 歩かんで (歩かない colloquial negative te-form)
          if (hiragana_end + 2 < codepoints.size() && codepoints[hiragana_end + 1] == U'ん' &&
              (codepoints[hiragana_end + 2] == U'で' || codepoints[hiragana_end + 2] == U'だ')) {
            ++hiragana_end;
            continue;
          }
          // Check if followed by A-row + ん (mizenkei + contracted negative)
          // e.g., わからん = わから (mizenkei of わかる) + ん
          if (hiragana_end + 2 < codepoints.size() && grammar::isARowCodepoint(codepoints[hiragana_end + 1]) &&
              codepoints[hiragana_end + 2] == U'ん') {
            ++hiragana_end;
            continue;
          }
          // Check if followed by ら + な (godan-ra mizenkei + negative auxiliary)
          // e.g., わからない = わから (mizenkei of わかる) + ない
          if (hiragana_end + 2 < codepoints.size() && codepoints[hiragana_end + 1] == U'ら' &&
              codepoints[hiragana_end + 2] == U'な') {
            ++hiragana_end;
            continue;
          }
          // Check if followed by ない (godan-ka mizenkei + negative auxiliary)
          // e.g., いかない = いか (mizenkei of いく) + ない
          if (hiragana_end + 1 < codepoints.size() && codepoints[hiragana_end + 1] == U'な') {
            ++hiragana_end;
            continue;
          }
          // Check if followed by んない (godan-ra ん音便 + negative auxiliary)
          // e.g., わかんない = わか + ん (ら→ん音便) + ない
          if (hiragana_end + 3 < codepoints.size() && codepoints[hiragana_end + 1] == U'ん' &&
              codepoints[hiragana_end + 2] == U'な' && codepoints[hiragana_end + 3] == U'い') {
            ++hiragana_end;
            continue;
          }
          // Check if followed by godan-ra conjugation endings (り、る、れ、ろ、っ)
          // e.g., わかり (renyokei), わかる (shuushikei), わかれ (kateikei/meireikei)
          if (hiragana_end + 1 < codepoints.size()) {
            char32_t next = codepoints[hiragana_end + 1];
            if (next == U'り' || next == U'る' || next == U'れ' || next == U'ろ' || next == U'っ') {
              ++hiragana_end;
              continue;
            }
          }
          // Check if followed by せ/さ/ず (causative/transitive/classical negative)
          // e.g., つかせる, つかさどる, いかず
          if (hiragana_end + 1 < codepoints.size()) {
            char32_t next = codepoints[hiragana_end + 1];
            if (next == U'せ' || next == U'さ' || next == U'ず') {
              ++hiragana_end;
              continue;
            }
          }
          // Check if followed by い + ま (godan-wa renyokei before ます:
          // つかい→使う, むかい→向かう). Guarded by the following ま so bare
          // か + いる sequences (誰か+いる) still break here; the pronoun-か
          // cases are additionally discouraged by the renyokei cost gate.
          if (hiragana_end + 2 < codepoints.size() && codepoints[hiragana_end + 1] == U'い' &&
              codepoints[hiragana_end + 2] == U'ま') {
            ++hiragana_end;
            continue;
          }
          // Check if followed by う (godan-wa shuushikei/rentaikei base form:
          // つかう→使う, むかう→向かう, すう is not か-initial). Scoring rejects
          // the impossible mizenkei+volitational reading (つか+う) separately.
          if (hiragana_end + 1 < codepoints.size() && codepoints[hiragana_end + 1] == U'う') {
            ++hiragana_end;
            continue;
          }
          // Check if followed by わ + {な,れ,せ,ず} (godan-wa mizenkei:
          // つかわない, つかわれる, つかわせる, つかわず). か+わ+ん is already
          // covered by the A-row + ん rule above.
          if (hiragana_end + 2 < codepoints.size() && codepoints[hiragana_end + 1] == U'わ' &&
              (codepoints[hiragana_end + 2] == U'な' || codepoints[hiragana_end + 2] == U'れ' ||
               codepoints[hiragana_end + 2] == U'せ' || codepoints[hiragana_end + 2] == U'ず')) {
            ++hiragana_end;
            continue;
          }
        }

        // で: OK if preceded by ん (んで = te-form) or き (できる)
        if (curr == U'で' && (prev == U'ん' || prev == U'き')) {
          ++hiragana_end;
          continue;
        }

        // も: OK if preceded by て (ても = even if)
        if (curr == U'も' && prev == U'て') {
          ++hiragana_end;
          continue;
        }

        // と: OK if preceded by っ (っとく = ておく contraction)
        // やっとく = やって + おく where ておく → とく
        if (curr == U'と' && prev == U'っ') {
          ++hiragana_end;
          continue;
        }

        // A medial と can be part of an all-hiragana Godan-wa stem rather
        // than the quotative particle when the remaining contiguous kana
        // explicitly end in the wa-row irrealis plus ない.  The final わない
        // is a unique Godan-wa signal, unlike the more ambiguous らない, and
        // requiring at least three preceding morae keeps short particle
        // sequences out of this verb path.
        if (curr == U'と') {
          if (has_godan_wa_negative) {
            ++hiragana_end;
            continue;
          }
        }

        // Otherwise, treat as particle
        SUZUME_DEBUG_LOG_TRACE("[HIRA_SEQ] pos=" << hiragana_end << " char=U+" << std::hex
                                                 << static_cast<uint32_t>(curr) << std::dec
                                                 << " action=break (unrecognized_particle_context)\n");
        break;
      }
    }
    ++hiragana_end;
  }

  // Log final hiragana sequence bounds
  SUZUME_DEBUG_LOG_TRACE("[HIRA_SEQ] final: start=" << start_pos << " end=" << hiragana_end
                                                    << " len=" << (hiragana_end - start_pos) << "\n");

  appendSuruSubsidiaryCandidates(codepoints, start_pos, dict_manager, candidates);

  // Need at least 2 hiragana for a verb
  if (hiragana_end <= start_pos + 1) {
    SUZUME_DEBUG_LOG_VERBOSE("[VERB_SKIP] pos=" << start_pos << " too_short (need >=2 hiragana, got "
                                                << (hiragana_end - start_pos) << ")\n");
    return candidates;
  }

  if (!appendInflectedHiraganaVerbCandidates(codepoints, start_pos, hiragana_end, first_char, char_types, inflection,
                                             dict_manager, verb_opts, candidates)) {
    return candidates;
  }

  appendHiraganaDerivedCandidates(codepoints, start_pos, hiragana_end, char_types, inflection, dict_manager,
                                  candidates);

  // A particle-initial kana run normally loses to a particle analysis.  When
  // the full run independently proves a Godan sokuonbin tense form, however,
  // favor its complete stem over a shorter accidental particle-plus-auxiliary
  // path.  This is limited to the stem immediately before た/て, so internal
  // small-tsu contractions retain their ordinary component analysis.
  if (is_particle_initial_sokuonbin) {
    for (auto& verb_candidate : candidates) {
      const bool ends_before_tense =
          verb_candidate.extended_pos == core::ExtendedPOS::VerbOnbinkei && verb_candidate.end < codepoints.size() &&
          (codepoints[verb_candidate.end] == U'た' || codepoints[verb_candidate.end] == U'て');
      if (ends_before_tense) {
        verb_candidate.cost += bigram_cost::kDoubleVeryStrongBonus + bigram_cost::kExtraStrongBonus;
      }
    }
  }

  // Add emphatic variants (いくっ, するっ, etc.)
  vh::addEmphaticVariants(candidates, codepoints);

  // Sort by cost
  vh::sortCandidatesByCost(candidates);

  return candidates;
}

}  // namespace suzume::analysis
