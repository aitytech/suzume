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

bool hasInternalPredicateBoundary(const std::vector<char32_t>& codepoints, size_t start_pos, size_t onbin_pos,
                                  const dictionary::DictionaryManager* dict_manager) {
  if (dict_manager == nullptr) {
    return false;
  }
  for (size_t boundary = start_pos + 1; boundary < onbin_pos; ++boundary) {
    const std::string tail = extractSubstring(codepoints, boundary, onbin_pos + 1);
    constexpr PartOfSpeechMask kPredicateMask = partOfSpeechMask(core::PartOfSpeech::Verb) |
                                                partOfSpeechMask(core::PartOfSpeech::Adjective) |
                                                partOfSpeechMask(core::PartOfSpeech::Auxiliary);
    if (hasExactPartOfSpeech(*dict_manager, tail, kPredicateMask)) {
      return true;
    }
  }
  return false;
}

size_t closedOnbinTenseEnd(const std::vector<char32_t>& codepoints, size_t start_pos,
                           const std::vector<normalize::CharType>& char_types, const grammar::Inflection& inflection,
                           const dictionary::DictionaryManager* dict_manager) {
  const bool has_left_predicate_boundary =
      start_pos == 0 || normalize::classifyChar(codepoints[start_pos - 1]) == normalize::CharType::Symbol ||
      normalize::isExtendedParticle(codepoints[start_pos - 1]);
  if (!has_left_predicate_boundary) {
    return 0;
  }

  size_t end_pos = start_pos;
  while (end_pos < char_types.size() && end_pos - start_pos < 12 &&
         char_types[end_pos] == normalize::CharType::Hiragana) {
    ++end_pos;
  }
  for (size_t onbin_pos = start_pos + 1; onbin_pos + 1 < end_pos; ++onbin_pos) {
    const char32_t onbin = codepoints[onbin_pos];
    const char32_t tense = codepoints[onbin_pos + 1];
    // Preserve the established particle-initial の exception: a complete
    // inflected predicate such as のっとって is stronger than its internal
    // のっ/とっ homographs.  Other starts require the internal boundary.
    const bool has_internal_predicate = hasInternalPredicateBoundary(codepoints, start_pos, onbin_pos, dict_manager);
    if (codepoints[start_pos] != U'の' && has_internal_predicate) {
      continue;
    }
    const std::string closed_surface = extractSubstring(codepoints, start_pos, onbin_pos + 2);
    constexpr PartOfSpeechMask kClosedSurfaceMask =
        partOfSpeechMask(core::PartOfSpeech::Particle) | partOfSpeechMask(core::PartOfSpeech::Conjunction);
    if (dict_manager != nullptr && hasExactPartOfSpeech(*dict_manager, closed_surface, kClosedSurfaceMask)) {
      continue;
    }
    if (onbin == U'っ' && (tense == U'た' || tense == U'て')) {
      for (const auto& inflection_candidate : inflection.analyze(closed_surface)) {
        const bool is_sokuonbin_godan = inflection_candidate.verb_type == grammar::VerbType::GodanWa ||
                                        inflection_candidate.verb_type == grammar::VerbType::GodanRa ||
                                        inflection_candidate.verb_type == grammar::VerbType::GodanTa;
        if (is_sokuonbin_godan && inflection_candidate.confidence >= candidate::kParticleVerbBoundaryMinConfidence) {
          return onbin_pos + 2;
        }
      }
    }

    // The ma/ba/na rows are indistinguishable in ん+だ.  This is nevertheless
    // decisive boundary evidence for a long predicate stem; it does not claim
    // that the reconstructed lemma row is lexically verified.  Exclude ん+で,
    // which is also compatible with the classical negative ぬ.
    if (onbin == U'ん' && tense == U'だ' && onbin_pos - start_pos >= 3) {
      return onbin_pos + 2;
    }
  }
  return 0;
}

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

bool hasInternalLexicalParticleBoundary(const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos,
                                        const dictionary::DictionaryManager* dict_manager) {
  if (dict_manager == nullptr) {
    return false;
  }
  for (size_t lexical_start = start_pos; lexical_start + 1 < end_pos; ++lexical_start) {
    // A leading closed particle is already sufficient evidence that the run
    // did not begin at a lexical verb boundary (は+ここ+で).  Accept a complete
    // compound particle as well, while leaving merely particle-homographic
    // prefixes of real verbs untouched.
    if (lexical_start > start_pos) {
      const std::string prefix = extractSubstring(codepoints, start_pos, lexical_start);
      if (dict_manager->lookupExact(prefix, core::PartOfSpeech::Particle) == nullptr) {
        continue;
      }
    }
    for (size_t split = lexical_start + 1; split < end_pos; ++split) {
      const std::string right = extractSubstring(codepoints, split, end_pos);
      if (dict_manager->lookupExact(right, core::PartOfSpeech::Particle) == nullptr) {
        continue;
      }
      const std::string left = extractSubstring(codepoints, lexical_start, split);
      constexpr PartOfSpeechMask kLexicalMask =
          partOfSpeechMask(core::PartOfSpeech::Pronoun) | partOfSpeechMask(core::PartOfSpeech::Noun) |
          partOfSpeechMask(core::PartOfSpeech::Adverb) | partOfSpeechMask(core::PartOfSpeech::Determiner) |
          partOfSpeechMask(core::PartOfSpeech::Conjunction);
      if (hasExactPartOfSpeech(*dict_manager, left, kLexicalMask)) {
        return true;
      }
    }
  }
  return false;
}

void appendHiraganaRenyokeiBeforeAspect(const std::vector<char32_t>& codepoints, size_t start_pos,
                                        const std::vector<normalize::CharType>& char_types,
                                        const dictionary::DictionaryManager* dict_manager,
                                        std::vector<UnknownCandidate>& candidates) {
  if (dict_manager == nullptr) {
    return;
  }
  size_t stem_end = start_pos;
  while (stem_end < char_types.size() && char_types[stem_end] == normalize::CharType::Hiragana) {
    ++stem_end;
  }
  if (stem_end < start_pos + 2 || stem_end >= codepoints.size()) {
    return;
  }
  if (hasInternalLexicalParticleBoundary(codepoints, start_pos, stem_end, dict_manager)) {
    return;
  }

  const std::string following = extractSubstring(codepoints, stem_end, codepoints.size());
  // Kanji-led aspect candidates are generated rather than dictionary-backed;
  // the leading aspect kanji is therefore also accepted as structural evidence.
  bool aspect_follows = codepoints[stem_end] == U'始';
  for (const auto& result : dict_manager->lookup(following, 0)) {
    if (result.entry != nullptr && result.entry->extended_pos == core::ExtendedPOS::AuxAspectHajimeru) {
      aspect_follows = true;
      break;
    }
  }
  if (!aspect_follows) {
    return;
  }

  const char32_t final_cp = codepoints[stem_end - 1];
  const std::string_view suffix = grammar::godanBaseSuffixFromIRow(final_cp);
  std::string lemma;
  if (grammar::isERowCodepoint(final_cp) || final_cp == U'じ') {
    lemma = extractSubstring(codepoints, start_pos, stem_end) + "る";
  } else if (!suffix.empty()) {
    lemma = extractSubstring(codepoints, start_pos, stem_end - 1) + std::string(suffix);
  }
  if (lemma.empty()) {
    return;
  }

  auto candidate = makeVerbCandidate(
      extractSubstring(codepoints, start_pos, stem_end), start_pos, stem_end, candidate::verb_cost::kStrongBonus, lemma,
      dictionary::ConjugationType::None, true, CandidateOrigin::VerbHiragana, candidate::kHighOriginConfidence,
      "hiragana_renyokei_before_aspect", core::ExtendedPOS::VerbRenyokei, "aspect_follower");
  candidate.lemma_verified = true;
  candidates.push_back(std::move(candidate));
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

  const size_t closed_onbin_tense_end =
      closedOnbinTenseEnd(codepoints, start_pos, char_types, inflection, dict_manager);

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
  appendHiraganaRenyokeiBeforeAspect(codepoints, start_pos, char_types, dict_manager, candidates);

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
  bool crossed_particle_guard = normalize::isNeverVerbStemAtStart(first_char);
  if (crossed_particle_guard && closed_onbin_tense_end == 0) {
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

  // A valid bare continuative before the literal Japanese comma may contain a
  // particle-homographic mora in its lexical stem (かがやき).  Validate the
  // complete run first using both the left case-particle context and the
  // inflection analyzer; only then may the scanner cross those internal morae.
  size_t comma_clause_end = 0;
  size_t comma_probe = start_pos;
  while (comma_probe < char_types.size() && comma_probe - start_pos < 12 &&
         char_types[comma_probe] == normalize::CharType::Hiragana) {
    ++comma_probe;
  }
  if (comma_probe > start_pos + 1 &&
      vh::isCommaClauseChainingRenyokei(codepoints, start_pos, comma_probe, dict_manager)) {
    const std::string comma_surface = extractSubstring(codepoints, start_pos, comma_probe);
    const auto& comma_inflections = inflection.analyze(comma_surface);
    const bool has_valid_renyokei =
        std::any_of(comma_inflections.begin(), comma_inflections.end(), [&](const auto& candidate) {
          return candidate.verb_type != grammar::VerbType::Unknown &&
                 candidate.verb_type != grammar::VerbType::IAdjective && candidate.morphemes.empty() &&
                 candidate.suffix.size() == core::kJapaneseCharBytes &&
                 grammar::isIRowCodepoint(codepoints[comma_probe - 1]) &&
                 candidate.confidence >= verb_opts.confidence_ichidan_dict;
        });
    if (has_valid_renyokei) {
      comma_clause_end = comma_probe;
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

      // A complete onbin+tense tail in a predicate slot is stronger evidence
      // than a particle-homograph inside its stem (しゃがんだ, ともった).  Carry
      // the scanner only as far as that closed tail; unrelated hiragana after
      // it still goes through the ordinary boundary checks.
      if (closed_onbin_tense_end != 0 && hiragana_end < closed_onbin_tense_end) {
        crossed_particle_guard =
            crossed_particle_guard || normalize::isNeverVerbStemAfterKanji(curr) || normalize::isExtendedParticle(curr);
        ++hiragana_end;
        continue;
      }
      if (comma_clause_end != 0 && hiragana_end < comma_clause_end) {
        ++hiragana_end;
        continue;
      }

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

        if (curr == U'と' && has_godan_wa_negative) {
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

  const bool has_inflected_candidate = appendInflectedHiraganaVerbCandidates(
      codepoints, start_pos, hiragana_end, first_char, char_types, inflection, dict_manager, verb_opts, candidates);
  if (!has_inflected_candidate && closed_onbin_tense_end == 0) {
    return candidates;
  }

  if (has_inflected_candidate) {
    appendHiraganaDerivedCandidates(codepoints, start_pos, hiragana_end, char_types, inflection, dict_manager,
                                    candidates);
  } else {
    appendOnbinContractionCandidates(codepoints, start_pos, hiragana_end, inflection, dict_manager, candidates);
  }

  // When the ordinary scanner had to cross a particle-homograph, reconstruct
  // the complete onbin stem from the same closed tense form that licensed the
  // crossing.  This avoids the short-stem row heuristic selecting a fragment
  // (よぎう) or rejecting the complete predicate (ともる).  The surface still
  // cannot prove which homophonous Godan row is lexically correct, so do not
  // mark the lemma as dictionary-verified.
  if (crossed_particle_guard && closed_onbin_tense_end != 0) {
    const size_t onbin_pos = closed_onbin_tense_end - 2;
    const char32_t onbin = codepoints[onbin_pos];
    const std::string inflected_surface = extractSubstring(codepoints, start_pos, closed_onbin_tense_end);
    for (const auto& inflection_candidate : inflection.analyze(inflected_surface)) {
      const bool matching_sokuon = onbin == U'っ' && (inflection_candidate.verb_type == grammar::VerbType::GodanWa ||
                                                      inflection_candidate.verb_type == grammar::VerbType::GodanRa ||
                                                      inflection_candidate.verb_type == grammar::VerbType::GodanTa);
      const bool matching_hatsuon = onbin == U'ん' && (inflection_candidate.verb_type == grammar::VerbType::GodanMa ||
                                                       inflection_candidate.verb_type == grammar::VerbType::GodanBa ||
                                                       inflection_candidate.verb_type == grammar::VerbType::GodanNa);
      if (!matching_sokuon && !matching_hatsuon) {
        continue;
      }
      const std::string onbin_surface = extractSubstring(codepoints, start_pos, onbin_pos + 1);
      candidates.push_back(makeVerbCandidate(
          onbin_surface, start_pos, onbin_pos + 1, candidate::verb_cost::kStandardBonus, inflection_candidate.base_form,
          grammar::verbTypeToConjType(inflection_candidate.verb_type), true, CandidateOrigin::VerbHiragana,
          inflection_candidate.confidence, "hiragana_closed_onbin_tense", core::ExtendedPOS::VerbOnbinkei));
      break;
    }
  }

  // A particle-initial kana run normally loses to a particle analysis.  When
  // the full run independently proves a Godan sokuonbin tense form, however,
  // favor its complete stem over a shorter accidental particle-plus-auxiliary
  // path.  This is limited to the stem immediately before た/て, so internal
  // small-tsu contractions retain their ordinary component analysis.
  if (crossed_particle_guard && closed_onbin_tense_end != 0) {
    for (auto& verb_candidate : candidates) {
      const bool ends_before_tense =
          verb_candidate.extended_pos == core::ExtendedPOS::VerbOnbinkei && verb_candidate.end < codepoints.size() &&
          (codepoints[verb_candidate.end] == U'た' || codepoints[verb_candidate.end] == U'て' ||
           codepoints[verb_candidate.end] == U'だ');
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
