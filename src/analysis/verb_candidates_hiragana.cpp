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

bool startsWithParticleThenVerifiedVerb(const std::vector<char32_t>& codepoints, size_t start_pos, size_t hiragana_end,
                                        const std::vector<normalize::CharType>& char_types,
                                        const grammar::Inflection& inflection,
                                        const dictionary::DictionaryManager* dict_manager,
                                        bool allow_single_char_particle_after_kanji) {
  if (dict_manager == nullptr || hiragana_end <= start_pos + 1) {
    return false;
  }
  size_t probe_end = hiragana_end;
  while (probe_end < char_types.size() && probe_end - start_pos < 12 &&
         char_types[probe_end] == normalize::CharType::Hiragana) {
    ++probe_end;
  }
  constexpr size_t kMaxParticleChars = 4;
  size_t max_particle_end = std::min(probe_end, start_pos + kMaxParticleChars);
  const std::string full_surface = extractSubstring(codepoints, start_pos, probe_end);
  const bool full_surface_is_dictionary_verb =
      dict_manager->lookupExact(full_surface, core::PartOfSpeech::Verb) != nullptr;
  for (size_t particle_end = start_pos + 1; particle_end <= max_particle_end; ++particle_end) {
    std::string particle_surface = extractSubstring(codepoints, start_pos, particle_end);
    const auto* particle_entry = dict_manager->lookupExact(particle_surface, core::PartOfSpeech::Particle);
    if (particle_entry == nullptr) {
      continue;
    }
    // A sentence-final particle cannot introduce a dependent auxiliary or a
    // following verb inflection. Treating its homographic mora as a boundary
    // would suppress productive open-class verbs such as さける and かける.
    if (particle_entry->extended_pos == core::ExtendedPOS::ParticleFinal) {
      continue;
    }
    SUZUME_DEBUG_LOG_VERBOSE("[VERB_PARTICLE] \"" << particle_surface << "\" at pos=" << start_pos << "\n");
    size_t verb_start = particle_end;
    for (size_t verb_end = probe_end; verb_end > verb_start + 1; --verb_end) {
      std::string verb_surface = extractSubstring(codepoints, verb_start, verb_end);
      // An exact open-class verb after a closed particle is stronger boundary
      // evidence than a generated particle-prefixed verb, even immediately
      // after kanji (結果+と+ひきかえる). Preserve an independently attested
      // whole verb such as できる before considering this split.
      if (allow_single_char_particle_after_kanji && !full_surface_is_dictionary_verb &&
          dict_manager->lookupExact(verb_surface, core::PartOfSpeech::Verb) != nullptr) {
        return true;
      }
      if (dict_manager->lookupExact(verb_surface, core::PartOfSpeech::Auxiliary) != nullptr) {
        SUZUME_DEBUG_LOG_VERBOSE("[VERB_SKIP] \"" << particle_surface << "+" << verb_surface
                                                  << " particle_then_auxiliary\n");
        return true;
      }
      for (const auto& candidate : inflection.analyze(verb_surface)) {
        bool is_verified_verb =
            vh::isVerbInDictionary(dict_manager, candidate.base_form) ||
            vh::hasDictionaryEntry(dict_manager, candidate.base_form, core::PartOfSpeech::Auxiliary);
        // A connective て/で unambiguously ends the preceding predicate. When
        // its remainder inflects to a dictionary verb, do not fabricate a
        // larger hiragana verb across that boundary (嬉しく|て|なら|ない).
        // Other particle boundaries retain the confidence gate because their
        // surface forms can also begin lexical verbs.
        bool is_connective = particle_surface == "て" || particle_surface == "で";
        if (is_verified_verb &&
            (is_connective || candidate.confidence >= candidate::kParticleVerbBoundaryMinConfidence)) {
          if (allow_single_char_particle_after_kanji && particle_end == start_pos + 1) {
            continue;
          }
          return true;
        }
      }
    }
  }
  return false;
}

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
      if (normalize::isNeverVerbStemAfterKanji(curr)) {
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

  // A closed-class particle followed by a dictionary-verified verb inflection
  // is a grammatical boundary, never the stem of an unknown hiragana verb.
  // This preserves て+さえ+いれ+ば and analogous binding-particle sequences.
  const bool follows_kanji = start_pos > 0 && normalize::isKanjiCodepoint(codepoints[start_pos - 1]);
  if (startsWithParticleThenVerifiedVerb(codepoints, start_pos, hiragana_end, char_types, inflection, dict_manager,
                                         follows_kanji)) {
    SUZUME_DEBUG_LOG_VERBOSE("[VERB_SKIP] pos=" << start_pos << " particle_then_verified_verb\n");
    return candidates;
  }

  // Try different lengths, starting from longest
  for (size_t end_pos = hiragana_end; end_pos > start_pos + 1; --end_pos) {
    std::string surface = extractSubstring(codepoints, start_pos, end_pos);

    if (surface.empty()) {
      continue;
    }

    // Check if this looks like a conjugated verb
    // First try the best match, but also check all candidates for dictionary verbs
    const auto& all_candidates = inflection.analyze(surface);
    grammar::InflectionCandidate best;
    bool is_dictionary_verb = false;

    // Look through all candidates to find ones whose base form is in the dictionary
    // Collect all matches and select the best one based on:
    // 1. Higher confidence
    // 2. GodanWa > GodanRa/GodanTa when tied (う verbs are much more common for hiragana)
    // This helps with cases like しまった where しまう (GodanWa) should beat しまる (GodanRa)
    if (dict_manager != nullptr) {
      std::vector<grammar::InflectionCandidate> dict_matches;

      for (const auto& cand : all_candidates) {
        if (cand.verb_type == grammar::VerbType::IAdjective || cand.base_form.empty()) {
          continue;
        }
        if (vh::isVerbInDictionary(dict_manager, cand.base_form)) {
          // Found a dictionary verb - collect this candidate
          dict_matches.push_back(cand);
        }
      }

      // Select the best dictionary match
      if (!dict_matches.empty()) {
        is_dictionary_verb = true;
        best = dict_matches[0];

        for (size_t i = 1; i < dict_matches.size(); ++i) {
          const auto& cand = dict_matches[i];
          // Higher confidence wins
          if (cand.confidence > best.confidence + 0.01F) {
            best = cand;
          } else if (std::abs(cand.confidence - best.confidence) <= 0.01F) {
            // When confidence is tied (within 0.01), prefer GodanWa over GodanRa/GodanTa
            // Rationale: For pure hiragana stems, う verbs (しまう, あらう, かう) are
            // much more common than る/つ verbs with the same stem pattern.
            // GodanRa: rare for pure hiragana (most are kanji: 走る, 帰る)
            // GodanTa: rare (持つ, 勝つ, etc. - usually with kanji)
            // GodanWa: very common in hiragana (しまう, あらう, まよう, etc.)
            if (cand.verb_type == grammar::VerbType::GodanWa &&
                (best.verb_type == grammar::VerbType::GodanRa || best.verb_type == grammar::VerbType::GodanTa)) {
              best = cand;
            }
          }
        }
      }
    }

    // If no dictionary match, select best candidate with GodanWa preference
    // When confidence is tied, GodanWa should beat GodanRa/GodanTa because
    // う verbs (あらう, かう, まよう) are much more common than る/つ verbs
    // for pure hiragana stems
    if (!is_dictionary_verb && !all_candidates.empty()) {
      bool found_verb_candidate = false;
      for (const auto& cand : all_candidates) {
        if (cand.verb_type == grammar::VerbType::IAdjective) {
          continue;
        }
        if (!found_verb_candidate) {
          best = cand;
          found_verb_candidate = true;
          continue;
        }
        // Higher confidence wins
        if (cand.confidence > best.confidence + 0.01F) {
          best = cand;
        } else if (std::abs(cand.confidence - best.confidence) <= 0.01F) {
          // When confidence is tied (within 0.01), prefer GodanWa over GodanRa/GodanTa
          if (cand.verb_type == grammar::VerbType::GodanWa &&
              (best.verb_type == grammar::VerbType::GodanRa || best.verb_type == grammar::VerbType::GodanTa)) {
            best = cand;
          }
        }
      }
    }

    // A terminal hiragana run ending in く can be an unattested Godan-ka
    // dictionary form. Some stems are otherwise analyzed only as i-adjective
    // fragments, even though 〜く is their finite verb ending. Dictionary
    // adverbs and adjective forms remain protected by the non-verb gate below.
    if (!is_dictionary_verb && end_pos == hiragana_end && end_pos - start_pos >= 3 &&
        codepoints[end_pos - 1] == U'く' &&
        (best.verb_type == grammar::VerbType::IAdjective ||
         best.confidence < candidate::verb_cost::kTerminalHiraganaGodanKaConfidence)) {
      best.base_form = surface;
      best.stem = surface.substr(0, surface.size() - core::kJapaneseCharBytes);
      best.suffix.clear();
      best.verb_type = grammar::VerbType::GodanKa;
      best.confidence = candidate::verb_cost::kTerminalHiraganaGodanKaConfidence;
      best.morphemes.clear();
    }

    // A fabricated verb must not cross a productive te-form boundary between
    // two dictionary-verified verb forms (なっ+て+なら, やっ+て+みる).
    // Genuine lexical verbs are exempt because their full surface is verified.
    if (!is_dictionary_verb &&
        vh::hasInternalVerbChainBoundary(codepoints, start_pos, end_pos, inflection, dict_manager)) {
      SUZUME_DEBUG_LOG_VERBOSE("[VERB_SKIP] \"" << surface << "\" internal_connective_verb_boundary\n");
      continue;
    }

    const size_t pre_filter_len = end_pos - start_pos;
    const bool looks_like_short_godan_base = pre_filter_len == 2 && grammar::isGodanVerbType(best.verb_type) &&
                                             best.base_form == surface &&
                                             best.confidence >= verb_opts.confidence_short_godan_base;

    // Filter out 2-char hiragana that don't end with a recognized verb form.
    // Short Godan base forms are licensed by inflection structure rather than
    // by the surface-only form detector, which deliberately treats ambiguous
    // u-row endings conservatively.
    // Also allow れ (Ichidan renyokei/meireikei like くれ from くれる).
    // This prevents false positives like まじ, ため from being recognized as verbs
    if (pre_filter_len == 2 && surface.size() >= core::kJapaneseCharBytes) {
      // Use string_view directly into surface to avoid dangling reference
      // (surface.substr() returns a temporary std::string)
      std::string_view last_char(surface.data() + surface.size() - core::kJapaneseCharBytes, core::kJapaneseCharBytes);
      const core::ExtendedPOS detected_form = core::detectVerbForm(surface);
      if (detected_form != core::ExtendedPOS::VerbShuushikei && detected_form != core::ExtendedPOS::VerbTeForm &&
          detected_form != core::ExtendedPOS::VerbTaForm && last_char != "れ" && !looks_like_short_godan_base) {
        continue;  // Skip 2-char hiragana not ending with valid verb suffix
      }
    }

    // Filter out i-adjective conjugation suffixes (standalone, not verb candidates)
    // See scorer_constants.h for documentation on these patterns.
    if (surface == scorer::kIAdjPastKatta || surface == scorer::kIAdjPastKattara || surface == scorer::kIAdjTeKute ||
        surface == scorer::kIAdjNegKunai || surface == scorer::kIAdjCondKereba || surface == scorer::kIAdjStemKa ||
        surface == scorer::kIAdjNegStemKuna || surface == scorer::kIAdjCondStemKere) {
      continue;  // Skip i-adjective conjugation patterns
    }

    // Note: Common adverbs/onomatopoeia (ぴったり, はっきり, etc.) are filtered
    // by the dictionary lookup below - they are registered as Adverb in L1 dictionary.

    // Filter out words that exist in dictionary as non-verb entries
    // e.g., あなた (pronoun), わたし (pronoun) should not be verb candidates
    if (vh::hasNonVerbDictionaryEntry(dict_manager, surface)) {
      continue;  // Skip - dictionary has non-verb entry for this surface
    }

    // Filter out volitional-shaped surfaces (お-row kana + う) of dictionary-
    // attested verbs. A conjugated verb surface can end in う only as its
    // dictionary form (しまう, まよう, おもう), in which case it equals the
    // analyzed base form. When the surface differs from the base form,
    // [お-row]+う is the volitional shape (未然形 お-row + auxiliary う):
    // なろう = なろ + う. Emitting it merged (auto-tagged 連用形 by
    // detectVerbForm's fallback) pre-empts the 未然形 + AuxVolitional split
    // path, so suppress the merged candidate. Restricted to dict-attested
    // bases: for unknown verbs no 未然形 edge exists, so the merged reading
    // stays (and spurious o-row stem edges inside longer words are avoided).
    if (pre_filter_len >= 2 && codepoints[end_pos - 1] == U'う' && grammar::isORowCodepoint(codepoints[end_pos - 2]) &&
        best.base_form != surface) {
      bool base_is_dict_aux = vh::hasDictionaryEntry(dict_manager, best.base_form, core::PartOfSpeech::Auxiliary);
      if (is_dictionary_verb || base_is_dict_aux) {
        // Dictionary verbs already expose their 未然形 as a dict edge (なろ).
        // Aux-registered subsidiary verbs (しまう) list only hand-picked
        // forms, so generate the 未然形 stem here to complete the split path
        // (しまおう → しまお + う).
        const auto* godan_row = grammar::Conjugation::getGodanRow(best.verb_type);
        if (!is_dictionary_verb && godan_row != nullptr && godan_row->o_row == codepoints[end_pos - 2] &&
            pre_filter_len >= 3) {
          std::string stem_surface = extractSubstring(codepoints, start_pos, end_pos - 1);
          SUZUME_DEBUG_LOG_VERBOSE("[VERB_CAND] " << stem_surface
                                                  << " hiragana_volitional_mizenkei lemma=" << best.base_form << "\n");
          candidates.push_back(makeVerbCandidate(
              stem_surface, start_pos, end_pos - 1, candidate::verb_cost::kWeakPenalty, best.base_form,
              grammar::verbTypeToConjType(best.verb_type), true, CandidateOrigin::VerbHiragana, best.confidence,
              "hiragana_volitional_mizenkei", core::ExtendedPOS::VerbMizenkei));
        }
        SUZUME_DEBUG_LOG_VERBOSE("[VERB_SKIP] \"" << surface << "\" skip volitional_shape (base=" << best.base_form
                                                  << ")\n");
        continue;  // Let 未然形 + う (AuxVolitional) split win
      }
    }

    // A pure-hiragana verb candidate must not absorb the closed appearance
    // auxiliary and following copula. In なさそうだ, a fabricated verb such as
    // さそうだ otherwise wins over the grammatical さ + そう + だ path.
    if (utf8::endsWith(surface, "そうだ")) {
      continue;
    }

    // Filter out verb stems that would form compound particles with て/で
    // e.g., によっ + て = によって (particle), とし + て = として (particle)
    // These compound particles exist as dictionary entries and should not be
    // split into spurious verb + て patterns
    {
      std::string_view last_char = utf8::lastChar(surface);
      if (utf8::equalsAny(last_char, {"っ", "し", "つ", "い"})) {
        std::string te_form = std::string(surface) + "て";
        std::string de_form = std::string(surface) + "で";
        if (vh::hasParticleDictionaryEntry(dict_manager, te_form) ||
            vh::hasParticleDictionaryEntry(dict_manager, de_form)) {
          continue;  // Skip - would split a compound particle
        }
      }
    }

    // Filter out te-form compound verb patterns that should be split
    // e.g., なっております → なっ+て+おり+ます, してます → し+て+ます
    //       してください → し+て+ください, してほしい → し+て+ほしい
    //       してくれます → し+て+くれ+ます
    // These contain て+auxiliary patterns that should be analyzed separately
    // Only skip for longer forms (5+ chars) to avoid blocking short verbs
    if (end_pos - start_pos >= 5) {
      // Check for ており/ていま/てい/てお/てくださ/てほしい/てくれ/てもら patterns
      // (te-form + auxiliary verb patterns)
      if (surface.find("ており") != std::string::npos || surface.find("ていま") != std::string::npos ||
          surface.find("ている") != std::string::npos || surface.find("ていた") != std::string::npos ||
          surface.find("てくださ") != std::string::npos || surface.find("てほしい") != std::string::npos ||
          surface.find("てくれ") != std::string::npos || surface.find("てもら") != std::string::npos ||
          surface.find("てお") != std::string::npos) {
        SUZUME_DEBUG_LOG_VERBOSE("[VERB_SKIP] \"" << surface << "\" skip te_compound_pattern\n");
        continue;  // Skip - let te-form split win
      }
    }
    // Filter ていく/ていっ/ていけ (te + iku directional aspect) at 4+ chars
    // E.g., していく → し+て+いく (not a single verb)
    //       していった → し+て+いっ+た, していって → し+て+いっ+て
    if (end_pos - start_pos >= 4) {
      if (surface.find("ていく") != std::string::npos || surface.find("ていっ") != std::string::npos ||
          surface.find("ていけ") != std::string::npos || surface.find("ていか") != std::string::npos) {
        SUZUME_DEBUG_LOG_VERBOSE("[VERB_SKIP] \"" << surface << "\" skip te_iku_pattern\n");
        continue;  // Skip - let te + iku split win
      }
    }

    // Check for 3-4 char hiragana verb ending with た/だ (past form) BEFORE threshold check
    // e.g., つかれた (疲れた), ねむった (眠った), おきた (起きた)
    // These need lower threshold because ichidan_pure_hiragana_stem penalty reduces confidence
    size_t pre_check_len = end_pos - start_pos;
    bool looks_like_past_form = false;
    bool looks_like_te_form = false;
    if ((pre_check_len == 3 || pre_check_len == 4) && surface.size() >= core::kJapaneseCharBytes) {
      std::string_view last_char = utf8::lastChar(surface);
      if (grammar::isPastMarkerTaDaSurface(last_char)) {
        looks_like_past_form = true;
      } else if (grammar::isTeDeSurface(last_char)) {
        // Te-form verbs (あらって, しまって, かって) need lower threshold too
        looks_like_te_form = true;
      }
    }

    // Check for ichidan dictionary form (e-row stem + る)
    // e.g., たべる (食べる), しらべる (調べる), つかれる (疲れる)
    // These need lower threshold because ichidan_pure_hiragana_stem penalty reduces confidence
    // Note: Check pattern structure directly, not verb_type, because when multiple
    // candidates have the same confidence, the godan candidate may be returned first
    // Exception: Exclude てる pattern (て + る) which is the ている contraction
    // e.g., してる should be する+ている, not しる (ichidan)
    bool looks_like_ichidan_dict_form = false;
    if (pre_check_len >= 3 && surface.size() >= core::kTwoJapaneseCharBytes) {
      std::string_view last_char = utf8::lastChar(surface);
      if (last_char == "る" && end_pos >= 2) {
        // Check if second-to-last char is e-row or i-row hiragana (ichidan stem ending)
        // E-row: 食べる, 見える, 調べる
        // I-row: 感じる, 信じる (kanji + i-row + る pattern)
        char32_t stem_end = codepoints[end_pos - 2];
        if (grammar::isERowCodepoint(stem_end) || grammar::isIRowCodepoint(stem_end)) {
          // Exclude てる pattern (ている contraction) - this should be suru/godan + ている
          // not ichidan dictionary form
          bool is_te_iru_contraction = (stem_end == U'て' || stem_end == U'で');
          // Exclude particle + いる pattern (にいる, でいる, etc.)
          // These should be split as particle + いる (existence verb), not a single verb
          // Valid hiragana verbs starting with particle chars: にる (煮る), にげる (逃げる)
          // But にいる, であるいる, etc. are not valid verbs
          bool is_particle_iru = false;
          if (pre_check_len == 3 && stem_end == U'い' && normalize::isCommonParticle(first_char)) {
            // 3-char pattern: particle + いる
            is_particle_iru = true;
          }
          if (!is_te_iru_contraction && !is_particle_iru) {
            // Find ichidan candidate to use for verb type and base form
            // For dictionary forms (e-row stem + る), prefer longer valid stems
            // Valid: つかれる (e-row ending), Invalid: つかれるる (るる pattern)
            grammar::InflectionCandidate best_ichidan;
            bool found_ichidan = false;
            for (const auto& cand : all_candidates) {
              if (cand.verb_type == grammar::VerbType::Ichidan &&
                  cand.confidence >= verb_opts.confidence_ichidan_dict) {
                // Skip invalid るる pattern (e.g., つかれるる)
                if (cand.base_form.size() >= 2 * core::kJapaneseCharBytes) {
                  std::string_view ending(cand.base_form.data() + cand.base_form.size() - 2 * core::kJapaneseCharBytes,
                                          2 * core::kJapaneseCharBytes);
                  if (ending == "るる") {
                    continue;  // Skip invalid pattern
                  }
                }
                if (!found_ichidan) {
                  best_ichidan = cand;
                  found_ichidan = true;
                } else if (cand.base_form.size() > best_ichidan.base_form.size()) {
                  // Prefer longer base form (e.g., つかれる > つかる)
                  best_ichidan = cand;
                }
              }
            }
            if (found_ichidan) {
              looks_like_ichidan_dict_form = true;
              // Use ichidan candidate as best if pattern matches
              if (best.verb_type != grammar::VerbType::Ichidan) {
                best = best_ichidan;
              } else if (best_ichidan.base_form.size() > best.base_form.size()) {
                // Even if already Ichidan, prefer longer base form
                best = best_ichidan;
              }
            }
          }
        }
      }
    }

    // Only accept verb types (not IAdjective) with sufficient confidence
    // Lower threshold for dictionary-verified verbs, past/te forms, and ichidan dict forms
    // Ichidan dict forms get very low threshold (0.28) because pure hiragana stems
    // with 3+ chars get multiple penalties (stem_long + ichidan_pure_hiragana_stem)
    // When both is_dictionary_verb AND (past/te form) apply, use the lower threshold
    // This handles cases like つかんで (掴んで) where confidence is ~0.3
    float conf_threshold;
    if (is_dictionary_verb && (looks_like_past_form || looks_like_te_form)) {
      // Dictionary verb in past/te form: use lower of the two thresholds
      conf_threshold = std::min(verb_opts.confidence_dict_verb, verb_opts.confidence_past_te);
    } else if (is_dictionary_verb) {
      conf_threshold = verb_opts.confidence_dict_verb;
    } else if (looks_like_past_form || looks_like_te_form) {
      conf_threshold = verb_opts.confidence_past_te;
    } else if (looks_like_ichidan_dict_form) {
      conf_threshold = verb_opts.confidence_ichidan_dict;
    } else if (looks_like_short_godan_base) {
      conf_threshold = verb_opts.confidence_short_godan_base;
    } else {
      conf_threshold = verb_opts.confidence_standard;
    }
    if (best.confidence > conf_threshold && best.verb_type != grammar::VerbType::IAdjective) {
      // Skip long particle-starting verb candidates when remainder is a valid verb form
      // e.g., "になっております" should be "に" + "なっております", not a single verb
      //       "はならぬ" should be "は" + "なら" + "ぬ", not a godan-ra negative
      // This prevents false verbs like "になる" + conjugation from being recognized
      // Apply to 4+ char forms; remainder check ensures genuine verbs are preserved
      size_t len_check = end_pos - start_pos;
      if (len_check >= 4 && normalize::isCommonParticle(first_char)) {
        // Extract remainder (surface without first character)
        std::string remainder = surface.substr(core::kJapaneseCharBytes);
        const auto& remainder_cands = inflection.analyze(remainder);
        // Use a relaxed confidence threshold (0.3) for 4-char surfaces — for
        // remainders with 1-char stems like なる→なら+ぬ the score is hit by
        // godan_ra_single_hiragana (-0.3) so confidence sits around 0.37 even
        // for genuinely valid forms. The 5+ char path keeps the stricter 0.5.
        float min_conf = (len_check >= 5) ? candidate::kParticlePrefixedVerbRemainderMinConfidenceLong
                                          : candidate::kParticlePrefixedVerbRemainderMinConfidenceShort;
        for (const auto& rem_cand : remainder_cands) {
          if (rem_cand.verb_type != grammar::VerbType::IAdjective && rem_cand.verb_type != grammar::VerbType::Unknown &&
              rem_cand.confidence >= min_conf) {
            // Remainder looks like a valid verb form - skip this candidate
            // to let particle + verb split win
            SUZUME_DEBUG_LOG_VERBOSE("[VERB_SKIP] \"" << surface << "\" skip particle_start_verb (particle=U+"
                                                      << std::hex << static_cast<uint32_t>(first_char) << std::dec
                                                      << ", remainder=" << remainder << ", conf=" << rem_cand.confidence
                                                      << ")\n");
            goto next_length;  // Continue to next end_pos
          }
        }
      }

      // Skip 3-char particle + いる/ある patterns (にいる, にある, でいる, といる, etc.)
      // These should be particle + existence verb, not a single hiragana verb
      // Valid 3-char verbs: にる(煮る), にげる(逃げる) have different patterns
      // Include extended particles: で, と, も (in addition to common particles)
      if (len_check == 3 && normalize::isExtendedParticle(first_char)) {
        std::string remainder = surface.substr(core::kJapaneseCharBytes);
        if (remainder == "いる" || remainder == "ある") {
          SUZUME_DEBUG_LOG_VERBOSE("[VERB_SKIP] \"" << surface << "\" skip particle_iru_aru (particle=U+" << std::hex
                                                    << static_cast<uint32_t>(first_char) << std::dec << ")\n");
          goto next_length;
        }
      }

      // Lower cost for higher confidence matches
      float base_cost =
          candidate::confidenceScaledCost(verb_opts.base_cost_high, best.confidence, verb_opts.confidence_cost_scale);

      // Give significant bonus for dictionary-verified hiragana verbs
      // This helps them beat the particle+adj+particle split path
      // Only apply to longer forms (5+ chars) to avoid boosting short forms like
      // "あった" (ある) which can interfere with copula recognition (であった)
      // Exception: Conditional forms (ending with ば) are unambiguous and should
      // get the bonus even if short (e.g., あれば = ある conditional)
      size_t candidate_len = end_pos - start_pos;
      bool is_conditional = utf8::endsWith(surface, "ば");
      // Check for っとく pattern (ておく contraction: やっとく, 見っとく)
      // This is a common colloquial pattern that should get bonus treatment
      bool is_teoku_contraction = utf8::endsWith(surface, "っとく");
      // Check for short te/de-form (e.g., ねて, でて, みて)
      // These are 2-char hiragana verbs that need a bonus to beat particle splits
      bool is_short_te_form = false;
      if (candidate_len == 2 && best.confidence >= verb_opts.confidence_high) {
        // Check last char in bytes (UTF-8)
        // て = E3 81 A6, で = E3 81 A7
        if (surface.size() >= 3) {
          char c1 = surface[surface.size() - 3];
          char c2 = surface[surface.size() - 2];
          char c3 = surface[surface.size() - 1];
          if (c1 == '\xE3' && c2 == '\x81' && (c3 == '\xA6' || c3 == '\xA7')) {
            is_short_te_form = true;
          }
        }
      }

      // Check for 3-4 char hiragana verb ending with た/だ (past form)
      // e.g., つかれた (疲れた), ねむった (眠った), おきた (起きた)
      // These medium-length verbs need a bonus to beat particle splits like つ+か+れた
      // Note: Lower confidence threshold (0.25) because ichidan_pure_hiragana_stem penalty
      // reduces confidence significantly for pure hiragana verbs
      // Skip if stem (without た/だ) is a known auxiliary (e.g., そうだ → そう is AUX)
      bool is_medium_past_form = false;
      if ((candidate_len == 3 || candidate_len == 4) && best.confidence >= verb_opts.confidence_past_te) {
        if (grammar::isPastMarkerTaDaSurface(utf8::lastChar(surface))) {
          // Extract stem (surface without last た/だ)
          std::string_view stem(surface.data(), surface.size() - core::kJapaneseCharBytes);
          // Skip if stem is a known auxiliary (e.g., そう+だ should not be verb candidate)
          if (!vh::hasDictionaryEntry(dict_manager, stem, core::PartOfSpeech::Auxiliary)) {
            is_medium_past_form = true;
          }
        }
      }

      if (is_dictionary_verb && (candidate_len >= 5 || is_conditional || is_teoku_contraction)) {
        base_cost = candidate::confidenceScaledCost(verb_opts.base_cost_verified, best.confidence,
                                                    verb_opts.confidence_cost_scale_medium);
      } else if (is_short_te_form) {
        // Short te-form with high confidence: give strong bonus to beat particle splits
        // e.g., ねて (conf=0.79) should beat ね(PARTICLE) + て(PARTICLE)
        // Particle path total cost can be as low as 0.002 due to dictionary bonuses,
        // so we need negative cost to compete. After adding POS prior (0.2 for verb),
        // the total needs to be below 0.002, so base needs to be below -0.2.
        //
        // When the first char is a common particle (で, に, etc.), these particles
        // have very low cost (e.g., で: -0.4), making particle+て path even cheaper
        // (total around -0.5). Need extra strong bonus for these cases.
        // EXCEPTION: If the 1-char stem is a known verb (e.g., でる, ねる in dictionary),
        // we want to prefer split path (で+て, ね+て), so use weaker bonus
        bool starts_with_common_particle =
            (first_char == U'で' || first_char == U'に' || first_char == U'が' || first_char == U'を' ||
             first_char == U'は' || first_char == U'の' || first_char == U'へ');
        // Check if 1-char stem + る is a known verb (e.g., でる, ねる)
        std::string one_char_stem = extractSubstring(codepoints, start_pos, start_pos + 1);
        std::string potential_verb = one_char_stem + "る";
        bool has_1char_verb_in_dict = vh::isVerbInDictionary(dict_manager, potential_verb);
        if (has_1char_verb_in_dict) {
          // Prefer split path (で+て) over combined (でて) when verb is in dictionary
          // Use moderate cost that can be beaten by 1-char renyokei candidate
          base_cost = candidate::confidenceScaledCost(verb_opts.base_cost_low, best.confidence,
                                                      verb_opts.confidence_cost_scale_small);
        } else if (starts_with_common_particle) {
          // Extra strong bonus: need to beat particle paths around -0.5
          base_cost = candidate::confidenceScaledCost(verb_opts.bonus_long_verified, best.confidence,
                                                      verb_opts.confidence_cost_scale_small);
        } else {
          base_cost = candidate::confidenceScaledCost(verb_opts.bonus_long_dict, best.confidence,
                                                      verb_opts.confidence_cost_scale_small);
        }
      } else if (is_medium_past_form) {
        // Medium-length past form verbs (3-4 chars ending with た/だ)
        // e.g., つかれた (conf=0.43) should beat つ+か+れた split
        // Give bonus to compete with particle splits
        base_cost = candidate::confidenceScaledCost(verb_opts.confidence_cost_scale_medium, best.confidence,
                                                    verb_opts.confidence_cost_scale_medium);
      } else if (looks_like_ichidan_dict_form) {
        // Ichidan dictionary form (e-row stem + る)
        // e.g., たべる (conf=0.39), しらべる, つかれる
        // These are highly likely to be real verbs, give modest bonus
        // Starting with particle-like chars (た, etc.) needs stronger bonus
        bool starts_with_aux_like_char = (first_char == U'た' || first_char == U'で' || first_char == U'に');
        if (starts_with_aux_like_char) {
          // Extra bonus: need to beat た(AUX) + べる(AUX) split
          base_cost = candidate::confidenceScaledCost(verb_opts.base_cost_verified, best.confidence,
                                                      verb_opts.confidence_cost_scale_medium);
        } else {
          base_cost = candidate::confidenceScaledCost(verb_opts.base_cost_low, best.confidence,
                                                      verb_opts.confidence_cost_scale_medium);
        }
      } else if (candidate_len >= 7 && best.confidence >= verb_opts.confidence_very_high) {
        // For long hiragana verb forms (7+ chars) with high confidence,
        // give a bonus even without dictionary verification.
        // This helps forms like かけられなくなった (9 chars) beat the
        // particle+verb split path (か + けられなくなった).
        // The length requirement (7+ chars) helps avoid false positives.
        //
        // When the verb starts with a character that's commonly mistaken for
        // a particle (か, は, が, etc.), give an extra strong bonus because
        // the particle split path is very likely to compete.
        bool starts_with_particle_char = (first_char == U'か' || first_char == U'は' || first_char == U'が' ||
                                          first_char == U'を' || first_char == U'に' || first_char == U'で' ||
                                          first_char == U'と' || first_char == U'も' || first_char == U'へ');
        if (starts_with_particle_char) {
          // Extra strong bonus for forms starting with particle-like char
          // e.g., かけられなくなった should strongly beat か + けられなくなった
          base_cost = candidate::confidenceScaledCost(verb_opts.base_cost_long_verified, best.confidence,
                                                      verb_opts.confidence_cost_scale_small);
        } else {
          base_cost = candidate::confidenceScaledCost(verb_opts.confidence_cost_scale_medium, best.confidence,
                                                      verb_opts.confidence_cost_scale_medium);
        }
      }

      // Penalty for unverified bare godan renyokei candidates
      // A godan renyokei with no auxiliary chain (suffix is a single i-row
      // char, e.g., もたち → もたつ, もだち → もだつ) whose base form is not
      // in the dictionary is rarely a genuine verb usage unless followed by
      // a renyokei-connecting auxiliary. Without this penalty, noun+suffix
      // splits like こども+たち lose to spurious verb readings (こど+もたち).
      // Exemptions:
      // - Dictionary-verified verbs keep their cost (e.g., わたし from わたす)
      // - Next char starting a renyokei continuation keeps the candidate
      //   viable for verb+aux splits: ま(ます), そ(そう), な(ながら/なさい),
      //   た(たい/たがる), や(やすい), に(にくい/purpose に), つ(つつ)
      if (!is_dictionary_verb && best.morphemes.empty() && best.suffix.size() == core::kJapaneseCharBytes &&
          grammar::isIRowCodepoint(codepoints[end_pos - 1])) {
        char32_t next_after = (end_pos < codepoints.size()) ? codepoints[end_pos] : 0;
        bool licenses_renyokei =
            (next_after == U'ま' || next_after == U'そ' || next_after == U'な' || next_after == U'た' ||
             next_after == U'や' || next_after == U'に' || next_after == U'つ');
        if (!licenses_renyokei) {
          base_cost += scorer::kPenaltyUnverifiedVerbLemma;
          SUZUME_DEBUG_LOG_VERBOSE("[VERB_PENALTY] \"" << surface << "\" unverified_bare_renyokei +"
                                                       << scorer::kPenaltyUnverifiedVerbLemma << "\n");
        }
      }

      // Penalty for hiragana verb candidates containing auxiliary chains
      // Same as kanji verb penalties - て+auxiliary, causative, etc.
      // E.g., きなくなる should not win over でき+なく+なる
      if (vh::containsTeFormAuxPattern(surface)) {
        base_cost += bigram_cost::kStrong;
      }
      if (vh::containsCausativeAuxPattern(surface)) {
        base_cost += bigram_cost::kStrong;
      }
      // Penalty for negative auxiliary chains (なくなる = become unable to)
      if (utf8::contains(surface, "なくなる") || utf8::contains(surface, "なくなっ") ||
          utf8::contains(surface, "なくなり")) {
        base_cost += bigram_cost::kRare;
      }
      // Penalty for verb candidates absorbing auxiliary まい (negative volitional)
      // まい attaches to 終止形 as an independent AUX token: なるまい = なる + まい
      // Same rule as the kanji verb path (出来まい = 出来 + まい)
      if (utf8::endsWith(surface, "まい") && surface.size() > 2 * core::kJapaneseCharBytes) {
        base_cost += bigram_cost::kStrong;
      }

      // Set lemma from inflection analysis for pure hiragana verbs
      // This is essential for P4 (ひらがな動詞活用展開) to work without dictionary
      // The lemmatizer can't derive lemma accurately for unknown verbs
      const core::ExtendedPOS explicit_form =
          looks_like_short_godan_base ? core::ExtendedPOS::VerbShuushikei : core::ExtendedPOS::Unknown;
      candidates.push_back(makeVerbCandidate(
          surface, start_pos, end_pos, base_cost, best.base_form, grammar::verbTypeToConjType(best.verb_type), false,
          CandidateOrigin::VerbHiragana, best.confidence, grammar::verbTypeToString(best.verb_type).data(),
          explicit_form, looks_like_short_godan_base ? "short_godan_base" : nullptr));
    }
  next_length:;  // Label for goto from particle-starting verb skip
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
