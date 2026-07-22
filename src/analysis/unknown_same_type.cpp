/**
 * @file unknown_same_type.cpp
 * @brief Same-type sequence candidate generation for unknown words
 *
 * Split from unknown.cpp: houses UnknownWordGenerator::generateBySameType and the
 * particle-boundary helpers used exclusively by it.
 */

#include <algorithm>
#include <cstdint>

#include "adjective_candidates.h"
#include "analysis/scorer_constants.h"
#include "analysis/unknown.h"
#include "candidate_constants.h"
#include "core/kana_constants.h"
#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "normalize/char_type.h"
#include "normalize/exceptions.h"
#include "normalize/utf8.h"
#include "suffix_candidates.h"
#include "tokenizer_utils.h"
#include "verb_candidates.h"

namespace suzume::analysis {

namespace {

// Particle that can immediately PRECEDE a content noun (私は…, 本を…, 犬が…). Used as
// the left bracket of a post-particle noun promotion.
bool isLeftBoundaryParticle(char32_t code_point) {
  switch (code_point) {
    case U'は':
    case U'が':
    case U'を':
    case U'に':
    case U'で':
    case U'へ':
    case U'と':
    case U'も':
    case U'の':
      return true;
    default:
      return false;
  }
}

// Particle that can immediately FOLLOW a content noun (…を, …が, …は). Used as the
// right bracket. の and end-of-input are excluded: の frequently follows a verb
// nominalization (食べるの) and would over-promote.
bool isRightBoundaryParticle(char32_t code_point) {
  switch (code_point) {
    case U'を':
    case U'が':
    case U'は':
    case U'も':
    case U'に':
    case U'で':
    case U'へ':
    case U'と':
      return true;
    default:
      return false;
  }
}

// Hiragana that reads as a particle when it appears WORD-INTERNALLY during a
// bracketed-noun scan. A native noun may span at most one of these (こども, ともだち);
// a second one marks a genuine particle chain and stops the scan. を remains a
// hard stop; が/の may occur inside native words (つながり, かけがえ) and are
// admitted only under the same one-internal-particle cap.
bool isInternalParticleChar(char32_t code_point) {
  switch (code_point) {
    case U'は':
    case U'に':
    case U'へ':
    case U'で':
    case U'と':
    case U'も':
    case U'か':
    case U'が':
    case U'の':
      return true;
    default:
      return false;
  }
}

// A generated hiragana noun cannot consist solely of two or more closed
// particles. The dynamic program preserves a complete multi-mora particle as
// one grammatical unit while rejecting accidental noun rescue paths such as
// へ+と. Lexical dictionary readings remain separate candidates.
bool decomposesIntoMultipleParticles(const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos,
                                     const dictionary::DictionaryManager* dict_manager) {
  if (dict_manager == nullptr || end_pos <= start_pos + 1) {
    return false;
  }
  std::vector<int> part_count(end_pos - start_pos + 1, -1);
  part_count[0] = 0;
  for (size_t relative_start = 0; relative_start < end_pos - start_pos; ++relative_start) {
    if (part_count[relative_start] < 0) {
      continue;
    }
    for (size_t relative_end = relative_start + 1; relative_end <= end_pos - start_pos; ++relative_end) {
      const std::string part = extractSubstring(codepoints, start_pos + relative_start, start_pos + relative_end);
      if (dict_manager->lookupExact(part, core::PartOfSpeech::Particle) != nullptr) {
        part_count[relative_end] = std::max(part_count[relative_end], part_count[relative_start] + 1);
      }
    }
  }
  return part_count.back() >= 2;
}

bool isFollowedByNominalParticle(const std::vector<char32_t>& codepoints, size_t end_pos,
                                 const dictionary::DictionaryManager* dict_manager) {
  if (dict_manager == nullptr || end_pos >= codepoints.size()) {
    return false;
  }
  const size_t maximum_end = std::min(codepoints.size(), end_pos + 3);
  for (size_t particle_end = end_pos + 1; particle_end <= maximum_end; ++particle_end) {
    const auto* entry =
        dict_manager->lookupExact(extractSubstring(codepoints, end_pos, particle_end), core::PartOfSpeech::Particle);
    if (entry != nullptr && (entry->extended_pos == core::ExtendedPOS::ParticleCase ||
                             entry->extended_pos == core::ExtendedPOS::ParticleTopic ||
                             entry->extended_pos == core::ExtendedPOS::ParticleAdverbial ||
                             entry->extended_pos == core::ExtendedPOS::ParticleBinding ||
                             entry->extended_pos == core::ExtendedPOS::ParticleNo)) {
      return true;
    }
  }
  return false;
}

// Phonologically impossible hiragana word starts: small kana (拗音・促音), the
// moraic nasal ん, and the case particles を/が which never begin a native word.
bool isImpossibleHiraganaStart(char32_t code_point) {
  // Small kana (拗音・促音) share the single kana:: source of truth; ん and the case
  // particles を/が never begin a native hiragana word. Callers gate on hiragana,
  // so the katakana half of isSmallKanaCodepoint is never reached here.
  return kana::isSmallKanaCodepoint(code_point) || code_point == U'ん' || code_point == U'を' || code_point == U'が';
}

// A one-kanji formal noun followed by an attributive na-adjective is a word
// boundary (この時妙なもの, その事不思議な結末). The whole kanji run determines
// this boundary so that shorter fabricated prefixes such as 時不 do not evade it.
bool hasFormalNounNaAdjectiveBoundary(const std::vector<char32_t>& codepoints, size_t start_pos, size_t kanji_end,
                                      normalize::CharType start_type) {
  if (start_type != normalize::CharType::Kanji || kanji_end <= start_pos + 1 || kanji_end >= codepoints.size() ||
      codepoints[kanji_end] != U'な') {
    return false;
  }
  if (kanji_end + 1 < codepoints.size() && codepoints[kanji_end + 1] == U'ら') {
    return false;
  }

  std::string first_char;
  normalize::encodeUtf8(codepoints[start_pos], first_char);
  return normalize::isFormalNounSurface(first_char);
}

// A hiragana nominalized continuative ending in -み can precede the
// independent adjective continuative なく (よどみなく, たゆみなく).  Emit the
// productive nominal boundary instead of letting an unknown-verb candidate
// absorb the suffix.  The -み condition excludes ordinary i-adjective
// continuatives such as かたくなく, which remain on the adjective path.
bool hasHiraganaNominalNakuEnding(const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos) {
  constexpr size_t kNakuLength = 2;
  constexpr size_t kMinimumNominalLength = 3;
  if (end_pos - start_pos < kMinimumNominalLength + kNakuLength || codepoints[end_pos - 2] != U'な' ||
      codepoints[end_pos - 1] != U'く') {
    return false;
  }
  return codepoints[end_pos - kNakuLength - 1] == U'み';
}

}  // namespace

std::vector<UnknownCandidate> UnknownWordGenerator::generateBySameType(
    std::string_view text, const std::vector<char32_t>& codepoints, size_t start_pos,
    const std::vector<normalize::CharType>& char_types) const {
  std::vector<UnknownCandidate> candidates;

  if (start_pos >= char_types.size()) {
    return candidates;
  }

  normalize::CharType start_type = char_types[start_pos];

  // Track if sequence starts with a particle character
  // These sequences may be valid nouns (はし, はな, etc.) despite starting with particles
  bool started_with_particle = false;

  // For hiragana starting with common particle characters (は, に, へ, の),
  // we still generate candidates but with a penalty, as they could be nouns.
  // Examples: はし (橋/箸), はな (花/鼻), にく (肉), へや (部屋), のり (海苔), etc.
  // Note: を, が are excluded - they almost never start nouns
  // Note: よ, わ are excluded - they are sentence-final particles
  if (start_type == normalize::CharType::Hiragana) {
    char32_t first_char = codepoints[start_pos];
    // Case particles を/が are valid standalone dictionary tokens, but they
    // cannot begin a multi-character native unknown word.  Returning here
    // prevents same-type fallbacks such as をよぎった from swallowing the
    // particle and predicate together; dictionary lookup still supplies the
    // one-character particle candidate.
    if (first_char == U'を' || first_char == U'が') {
      return candidates;
    }
    // Only は, に, へ, の can start hiragana nouns
    if (first_char == U'は' || first_char == U'に' || first_char == U'へ' || first_char == U'の') {
      started_with_particle = true;  // Generate but with penalty
    }

    // Skip small kana (拗音・促音) - Japanese words don't start with these
    // ゃゅょぁぃぅぇぉっ are always part of compound sounds (e.g., きょう not ょう)
    if (kana::isSmallKanaCodepoint(first_char)) {
      return candidates;  // Phonologically impossible word start
    }

    // Skip if starting with demonstrative pronouns (これ, それ, あれ, どれ, etc.)
    // These should be recognized by dictionary lookup, not generated as unknown words.
    if (start_pos + 1 < codepoints.size()) {
      char32_t second_char = codepoints[start_pos + 1];
      if (normalize::isDemonstrativeStart(first_char, second_char)) {
        return candidates;
      }
    }
  }

  size_t max_len = getMaxLength(start_type);

  // Position of a single particle character the hiragana scan was allowed to
  // cross (SIZE_MAX = none). Candidates extending past it get a penalty below.
  size_t crossed_particle_pos = SIZE_MAX;

  // Find end of same-type sequence
  size_t end_pos = start_pos + 1;
  while (end_pos < char_types.size() && end_pos - start_pos < max_len) {
    normalize::CharType curr_type = char_types[end_pos];
    char32_t curr_char = codepoints[end_pos];

    // Check if current character matches the sequence type
    bool matches_type = (curr_type == start_type);

    // Special handling for prolonged sound mark (ー) in hiragana sequences
    // Colloquial expressions like すごーい, やばーい, かわいー use ー in hiragana
    // Also handle consecutive prolonged marks: すごーーい, やばーーーい
    if (!matches_type && start_type == normalize::CharType::Hiragana && normalize::isProlongedSoundMark(curr_char)) {
      // Check if followed by hiragana, another ー, or end of text (かわいー)
      if (end_pos + 1 >= char_types.size() || char_types[end_pos + 1] == normalize::CharType::Hiragana ||
          normalize::isProlongedSoundMark(codepoints[end_pos + 1])) {
        matches_type = true;  // Treat ー as part of hiragana sequence
      }
    }

    // Special handling for emoji modifiers (ZWJ, variation selectors, skin tones)
    // These should always be grouped with the preceding emoji
    if (!matches_type && start_type == normalize::CharType::Emoji && normalize::isEmojiModifier(curr_char)) {
      matches_type = true;  // Treat modifiers as part of emoji sequence
    }

    // Special handling for regional indicators (country flags)
    // Two regional indicators together form a flag emoji (e.g., 🇯🇵)
    if (!matches_type && start_type == normalize::CharType::Emoji && normalize::isRegionalIndicator(curr_char)) {
      matches_type = true;  // Treat regional indicators as part of emoji sequence
    }

    // Special handling for ideographic iteration mark (々) in kanji sequences
    // e.g., 人々, 日々, 堂々, 時々 should be grouped as single tokens
    // The iteration mark U+3005 is classified as Symbol, but it should be
    // treated as part of the kanji sequence when following kanji
    if (!matches_type && start_type == normalize::CharType::Kanji && normalize::isIterationMark(curr_char)) {
      matches_type = true;  // Treat 々 as part of kanji sequence
    }

    // Special handling for ヶ/ケ in kanji sequences (place names, counters)
    // e.g., 姉ヶ崎, 市ヶ谷, 霞ヶ関 should be grouped as single tokens
    // ヶ (U+30F6) is classified as Katakana, but in these contexts it functions
    // as a kanji-like character connecting surrounding kanji
    if (!matches_type && start_type == normalize::CharType::Kanji && (curr_char == U'ヶ' || curr_char == U'ケ') &&
        end_pos + 1 < char_types.size() && char_types[end_pos + 1] == normalize::CharType::Kanji) {
      matches_type = true;  // Treat ヶ/ケ as part of kanji sequence
    }

    if (!matches_type) {
      break;
    }

    // For hiragana, break at common particle characters to avoid
    // swallowing particles into unknown words (e.g., don't create "ぎをみじん")
    if (start_type == normalize::CharType::Hiragana) {
      // Always break at を and が (case particles that never start words)
      // This applies even if we started with a particle character
      if (curr_char == U'を' || curr_char == U'が') {
        break;
      }
      // For non-particle starts, particle characters usually mark word
      // boundaries. However, genuine hiragana nouns can contain one such
      // character word-internally (こども, おとな, ひとつ), so allow the scan
      // to cross a single particle character; candidates extending past it
      // receive a penalty in the generation loop below.
      //
      // Crossing is restricted to keep particle chains intact:
      // - の always breaks: genitive の marks a compound boundary in
      //   hiragana noun+noun patterns (みせ+の+まえ, こころ+の+こえ)
      // - A second particle character breaks (likely a real particle chain)
      // - Sequences starting with を/が never cross: those characters never
      //   start words (see hard break above), so such a sequence is already
      //   a particle chain and must not absorb a following particle
      // - At most one character may follow the crossed particle: native
      //   words with a word-internal particle character are short (こども,
      //   おとな, ひとつ); longer tails just absorb a genuine particle
      if (!started_with_particle) {
        if (crossed_particle_pos != SIZE_MAX && end_pos > crossed_particle_pos + 1) {
          break;  // Already extended one char past the crossed particle
        }
        // Genitive の: always a word boundary
        if (curr_char == U'の') {
          break;
        }
        // Common particles は, に, へ + で, と, も, か (word boundaries)
        // Note: Don't include「や」as it's also the stem of「やる」verb
        if (curr_char == U'は' || curr_char == U'に' || curr_char == U'へ' || curr_char == U'で' ||
            curr_char == U'と' || curr_char == U'も' || curr_char == U'か') {
          char32_t seq_first_char = codepoints[start_pos];
          if (crossed_particle_pos != SIZE_MAX || seq_first_char == U'を' || seq_first_char == U'が') {
            break;  // Stop before the particle character
          }
          crossed_particle_pos = end_pos;  // Cross one, penalized per length
        }
      }
    }
    ++end_pos;
  }

  // Generate candidates for different lengths
  const bool has_formal_noun_na_adjective_boundary =
      hasFormalNounNaAdjectiveBoundary(codepoints, start_pos, end_pos, start_type);
  for (size_t len = 1; len <= end_pos - start_pos; ++len) {
    size_t candidate_end = start_pos + len;
    std::string surface = extractSubstring(codepoints, start_pos, candidate_end);

    if (!surface.empty()) {
      // Particle-start hiragana sequences are potential nouns (はし, はな, にく)
      // Use NOUN POS instead of OTHER to avoid exceeds_dict_length penalty
      core::PartOfSpeech pos = started_with_particle ? core::PartOfSpeech::Noun : getPosForType(start_type);
      float cost = getCostForType(start_type, len);

      if (has_formal_noun_na_adjective_boundary && len >= 2) {
        cost += candidate::kFormalNounNaAdjectiveBoundaryPenalty;
      }

      // Penalize kanji sequences ending with honorific/title suffixes (様, 氏)
      // to encourage NOUN + SUFFIX separation (e.g., 客様 → 客 + 様, 田中様 → 田中 + 様)
      // Note: 的 was removed — kanji_seq cost 1.0 with 1-char prefix (目+的 = 1.1)
      // naturally keeps 目的/動的/知的/射的 as 1 token while 論理+的 still splits
      // (2-char prefix gives 論理(1.0)+的(SUFFIX 0.5)-0.8 = 0.7 < 1.0).
      if (start_type == normalize::CharType::Kanji && len >= 2) {
        char32_t last_char = codepoints[candidate_end - 1];
        if (last_char == U'様' || last_char == U'氏') {
          cost += 4.0F;  // Strong penalty to prefer NOUN + SUFFIX path
        }
      }

      // Penalize kanji sequences starting with the prefix kanji 御.
      // 御 is an L1 PREFIX entry and should split off as a productive prefix
      // (御 + 尽力, 御 + 挨拶, 御 + 協力). The +2.0 penalty makes the PREFIX path
      // win over any 2+ char kanji_seq starting with 御. Lexicalized 御-X nouns
      // (御者, 御所, 御曹司) come from the dictionary and get a separate bonus
      // in scorer.cpp to beat the prefix path.
      if (start_type == normalize::CharType::Kanji && len >= 2 && codepoints[start_pos] == U'御') {
        cost += 2.0F;
      }

      // Skip kanji sequences starting with iteration mark (々)
      // 々 always attaches to the preceding kanji (人々, 時々)
      // It can never start a word
      if (start_type == normalize::CharType::Kanji && normalize::isIterationMark(codepoints[start_pos])) {
        continue;
      }

      // Penalize kanji sequences that extend past iteration mark (々), except
      // for a complete double-reduplication X々Y々 (津々浦々, 様々). The latter
      // is a productive four-character compound shape and must retain its
      // whole-word candidate; a lone completed pair followed by another kanji
      // (時々妙) is still a boundary.
      if (start_type == normalize::CharType::Kanji && len >= 3) {
        bool is_double_reduplication = len == 4 && normalize::isIterationMark(codepoints[start_pos + 1]) &&
                                       normalize::isIterationMark(codepoints[start_pos + 3]);
        for (size_t i = start_pos + 1; i < candidate_end - 1; ++i) {
          if (!is_double_reduplication && normalize::isIterationMark(codepoints[i])) {
            // Found 々 in the middle - penalize extending past it
            cost += 5.0F;
            break;
          }
        }
      }

      // Penalize kanji sequences with interrogative kanji (何, 誰, 幾) at NON-initial position
      // e.g., 今何 should be split as 今 + 何, not kept as one compound
      // But 何日, 何人 (interrogative + counter) should stay together
      // Interrogatives are standalone words unless they're at the start (counter pattern)
      if (start_type == normalize::CharType::Kanji && len >= 2) {
        for (size_t i = start_pos + 1; i < candidate_end; ++i) {  // Skip first char
          if (isInterrogativeKanji(codepoints[i])) {
            // Heavy penalty to force split
            cost += 3.0F;
            break;
          }
        }
      }

      // Penalize hiragana candidates that include an internal particle
      // character (see scan loop above). The penalty keeps particle splits
      // preferred when the prefix is a plausible word (ここ+で beats ここで),
      // while letting genuine nouns spanning a particle char (こども, ひとつ)
      // win when the split leaves an implausible fragment (こど+も, ひ+と+つ).
      // - 2-char candidates (single char + particle char) are skipped
      //   entirely: they would just absorb a genuine particle (み+と)
      // - Particle-final candidates (こども) get a minor penalty
      // - Medial crossing (one char after the particle, e.g. ひとつ) is less
      //   plausible and gets a strong penalty; genuine words still win
      //   because their split path needs multiple unknown 1-char fragments
      if (start_type == normalize::CharType::Hiragana && !started_with_particle &&
          candidate_end > crossed_particle_pos) {
        if (len < 3) {
          continue;
        }
        cost += (candidate_end > crossed_particle_pos + 1) ? scorer::scale::kStrong : scorer::scale::kMinor;
      }

      // Penalize hiragana sequences starting with particle characters
      // These could be nouns (はし, はな, にく, にゃんこ) but are less likely than
      // the particle interpretation, unless the particle path has connection penalties
      bool has_suffix = false;
      if (started_with_particle) {
        if (len == 1) {
          continue;  // Single-char particle-start never forms a noun alone
        }
        // Check if this is a reduplicated pattern (same character repeated)
        // Reduplicated hiragana like はは (母), ちち (父) are likely real words
        bool is_reduplicated = (len == 2 && codepoints[start_pos] == codepoints[start_pos + 1]);
        if (is_reduplicated) {
          // Small bonus for reduplicated patterns - they're often real words
          cost -= 0.5F;
        } else if (len == 2) {
          // 2-char: light penalty — bigram penalties on unnatural particle chains
          // provide enough discouragement for false splits (は+し vs はし)
          cost += 0.5F;
        } else if (len == 3) {
          // 3-char: moderate penalty (にある, によれ are likely particle chains)
          cost += 0.8F;
        } else {
          // 4+ char: heavier penalty scaling with length
          // but still generated so words like にゃんこ have a chance
          cost += 1.0F + static_cast<float>(len - 3) * 0.5F;
        }
        // Mark as has_suffix to skip exceeds_dict_length penalty in tokenizer
        has_suffix = true;
      }
      if (started_with_particle && !isFollowedByNominalParticle(codepoints, candidate_end, dict_manager_) &&
          decomposesIntoMultipleParticles(codepoints, start_pos, candidate_end, dict_manager_)) {
        continue;
      }
      auto cand = makeCandidate(surface, start_pos, candidate_end, pos, cost, has_suffix, CandidateOrigin::SameType);
#ifdef SUZUME_DEBUG_INFO
      cand.confidence = started_with_particle ? 0.7F : 1.0F;
      switch (start_type) {
        case normalize::CharType::Kanji:
          cand.pattern = "kanji_seq";
          break;
        case normalize::CharType::Katakana:
          cand.pattern = "kata_seq";
          break;
        case normalize::CharType::Hiragana:
          cand.pattern = started_with_particle ? "hira_noun_seq" : "hira_seq";
          break;
        case normalize::CharType::Alphabet:
          cand.pattern = "alpha_seq";
          break;
        case normalize::CharType::Digit:
          cand.pattern = "digit_seq";
          break;
        default:
          cand.pattern = "other_seq";
          break;
      }
#endif
      candidates.push_back(cand);

      // Emit a standalone SUFFIX candidate for plural-honorific 方 when it sits
      // at the tail of a kanji_seq (i.e., preceded by another kanji). Enables
      // splits like 皆様(NOUN) + 方(SUFFIX) for 皆様方. Restricting to "prev is
      // kanji" avoids false splits like その方(NOUN), 北の方(NOUN) where 方 is
      // a standalone noun, not a plural-honorific suffix.
      if (start_type == normalize::CharType::Kanji && len == 1 && codepoints[start_pos] == U'方' && start_pos > 0 &&
          char_types[start_pos - 1] == normalize::CharType::Kanji) {
        auto suffix_cand = makeCandidate(surface, start_pos, candidate_end, core::PartOfSpeech::Suffix, 0.5F,
                                         /*has_suffix=*/true, CandidateOrigin::SameType);
#ifdef SUZUME_DEBUG_INFO
        suffix_cand.confidence = 1.0F;
        suffix_cand.pattern = "tail_suffix_方";
#endif
        candidates.push_back(suffix_cand);
      }
    }
  }

  // Productive hiragana nominal + adjective-continuative boundary.  This is
  // deliberately independent of particle bracketing: literary adverbials
  // commonly occur at the beginning of a clause (よどみなく話す).
  if (start_type == normalize::CharType::Hiragana && hasHiraganaNominalNakuEnding(codepoints, start_pos, end_pos)) {
    const size_t nominal_end = end_pos - 2;
    const size_t nominal_len = nominal_end - start_pos;
    std::string surface = extractSubstring(codepoints, start_pos, nominal_end);
    auto noun_cand =
        makeCandidate(surface, start_pos, nominal_end, core::PartOfSpeech::Noun,
                      getCostForType(start_type, nominal_len) + candidate::kHiraganaNominalNakuCandidateBonus,
                      /*has_suffix=*/true, CandidateOrigin::SameType);
#ifdef SUZUME_DEBUG_INFO
    noun_cand.pattern = "hiragana_nominal_naku";
#endif
    candidates.push_back(noun_cand);
  }

  // Bracketed hiragana noun promotion. A short hiragana run genuinely bracketed by
  // particles (私は|たばこ|を, 彼は|ともだち|と) reads as a content noun, but the
  // same-type scan above truncates at the first internal particle character and a
  // particle-initial run (にんじん) is only ever emitted as a penalized particle-noun,
  // so the correct whole-run candidate never reaches the lattice. This dedicated
  // scan is independent of that truncation and emits an ADDITIVE Noun candidate; the
  // Other/particle candidates remain and any real dictionary/verb/adverb reading of
  // the span still outranks the ~1.8 Noun, so it wins only when nothing better spans
  // the bracket and never shatters the run. Left bracket: a boundary particle
  // preceded by a non-hiragana content word (私は…, 彼は…). Right bracket: a boundary
  // particle. の is excluded on both sides (genitive marks a compound boundary).
  // Left bracket: a boundary particle after a non-hiragana content word (私は…), or a
  // clause boundary — sentence start / a preceding symbol (punctuation). の is not a
  // left boundary here (genitive marks a compound boundary).
  bool left_particle_bracket = start_pos >= 2 && isLeftBoundaryParticle(codepoints[start_pos - 1]) &&
                               char_types[start_pos - 2] != normalize::CharType::Hiragana;
  bool left_determiner_bracket = false;
  if (dict_manager_ != nullptr) {
    const size_t lookback = std::min(start_pos, static_cast<size_t>(4));
    for (size_t length = 1; length <= lookback; ++length) {
      const std::string preceding = extractSubstring(codepoints, start_pos - length, start_pos);
      const auto* entry = dict_manager_->lookupExact(preceding, core::PartOfSpeech::Determiner);
      if (entry != nullptr) {
        left_determiner_bracket = true;
        break;
      }
    }
  }
  bool left_clause_bracket =
      (start_pos == 0) || (start_pos >= 1 && char_types[start_pos - 1] == normalize::CharType::Symbol);
  if (start_type == normalize::CharType::Hiragana &&
      (left_particle_bracket || left_determiner_bracket || left_clause_bracket) &&
      !isImpossibleHiraganaStart(codepoints[start_pos])) {
    bool particle_initial =
        (codepoints[start_pos] == U'は' || codepoints[start_pos] == U'に' || codepoints[start_pos] == U'へ');
    size_t max_internal = particle_initial ? 0 : 2;
    size_t internal_particles = 0;
    // A multi-char L1 particle (ながら, まで, から, だけ, …) beginning at a position is a
    // real right boundary: terminate the run there rather than swallowing its head into
    // the noun (…およぎ|ながら, never およぎな|がら where ながら's が is mistaken for a bracket).
    auto multi_char_particle_at = [&](size_t pos) -> bool {
      if (dict_manager_ == nullptr || pos >= codepoints.size()) {
        return false;
      }
      size_t win_end = pos + 4 < codepoints.size() ? pos + 4 : codepoints.size();
      std::string window = extractSubstring(codepoints, pos, win_end);
      for (const auto& res : dict_manager_->lookup(window, 0)) {
        if (res.entry != nullptr && res.entry->pos == core::PartOfSpeech::Particle && res.length >= 2) {
          return true;
        }
      }
      return false;
    };
    auto lies_inside_formal_noun_negative_predicate = [&](size_t pos) -> bool {
      if (dict_manager_ == nullptr) {
        return false;
      }
      for (size_t predicate_start = start_pos + 1; predicate_start < pos; ++predicate_start) {
        const std::string prefix = extractSubstring(codepoints, start_pos, predicate_start);
        const auto* noun = dict_manager_->lookupExact(prefix, core::PartOfSpeech::Noun);
        if (noun == nullptr || noun->extended_pos != core::ExtendedPOS::NounFormal) {
          continue;
        }
        const auto predicates = generateHiraganaVerbCandidates(text, codepoints, predicate_start, char_types);
        for (const auto& predicate : predicates) {
          if (predicate.extended_pos != core::ExtendedPOS::VerbMizenkei || predicate.end <= pos ||
              predicate.end >= codepoints.size()) {
            continue;
          }
          const size_t probe_end = std::min(codepoints.size(), predicate.end + static_cast<size_t>(2));
          const std::string following = extractSubstring(codepoints, predicate.end, probe_end);
          for (const auto& match : dict_manager_->lookup(following, 0)) {
            if (match.entry != nullptr && (match.entry->extended_pos == core::ExtendedPOS::AuxNegativeNu ||
                                           match.entry->extended_pos == core::ExtendedPOS::AuxNegativeNai)) {
              return true;
            }
          }
        }
        const size_t auxiliary_limit = std::min(codepoints.size(), pos + static_cast<size_t>(5));
        for (size_t auxiliary_start = pos + 1; auxiliary_start < auxiliary_limit; ++auxiliary_start) {
          const std::string window = extractSubstring(codepoints, auxiliary_start, auxiliary_limit);
          for (const auto& match : dict_manager_->lookup(window, 0)) {
            if (match.entry == nullptr || (match.entry->extended_pos != core::ExtendedPOS::AuxNegativeNu &&
                                           match.entry->extended_pos != core::ExtendedPOS::AuxNegativeNai)) {
              continue;
            }
            const std::string full_predicate =
                extractSubstring(codepoints, predicate_start, auxiliary_start + match.length);
            for (const auto& analysis : inflection_.analyze(full_predicate)) {
              if (analysis.verb_type != grammar::VerbType::Unknown &&
                  analysis.verb_type != grammar::VerbType::IAdjective && !analysis.morphemes.empty() &&
                  analysis.confidence >= candidate::verb_cost::kConstructedVerbMinConfidence) {
                return true;
              }
            }
          }
        }
      }
      return false;
    };
    bool crossed_verified_predicate = false;
    size_t scan = start_pos + 1;
    while (scan < codepoints.size() && scan - start_pos < 4 && char_types[scan] == normalize::CharType::Hiragana) {
      char32_t curr = codepoints[scan];
      if (curr == U'を') {
        break;  // accusative を does not sit inside a native hiragana noun
      }
      // Once a substantive three-mora run has formed, a case/topic particle
      // starts the right bracket even when the next word is also hiragana
      // (あたり|は|すっかり). Shorter offsets remain eligible as genuine
      // word-internal homographs.
      if (scan - start_pos >= 3 && isRightBoundaryParticle(curr)) {
        break;
      }
      // A multi-character particle immediately after the first mora can be
      // part of a native hiragana noun (こども).  Require a substantive
      // preceding run before treating it as an internal word boundary.
      if (scan - start_pos >= 2 && multi_char_particle_at(scan)) {
        if (lies_inside_formal_noun_negative_predicate(scan)) {
          crossed_verified_predicate = true;
        } else {
          break;  // stop before a multi-char particle boundary
        }
      }
      if (isInternalParticleChar(curr)) {
        // A particle char followed by a fresh (non-hiragana) word is a trailing case
        // particle (…およぎ|に|行く): stop before it so the right-bracket test sees it.
        // At the run's end it is word-final (こども), so keep it, capped by max_internal.
        bool word_follows = scan + 1 < codepoints.size() && char_types[scan + 1] != normalize::CharType::Hiragana;
        if (word_follows || internal_particles >= max_internal) {
          break;
        }
        ++internal_particles;
      }
      ++scan;
    }
    size_t len = scan - start_pos;
    // Right bracket: a single boundary particle, a multi-char particle start, or a
    // clause boundary (sentence end / symbol).
    const bool right_genitive_after_internal_particle =
        scan < codepoints.size() && codepoints[scan] == U'の' && internal_particles > 0;
    bool right_particle = (scan < codepoints.size() && isRightBoundaryParticle(codepoints[scan])) ||
                          multi_char_particle_at(scan) || right_genitive_after_internal_particle;
    bool right_clause =
        (scan == codepoints.size()) || (scan < codepoints.size() && char_types[scan] == normalize::CharType::Symbol);
    // Whole-run candidate is safe at length 2 only when both sides are real particles
    // (私は|はし|を). A run leaning on any clause boundary needs length >= 3, so short
    // isolated hiragana — usually adverbs/particles (もう, すぐ, ため) — are not promoted.
    bool fully_particle_bracketed = left_particle_bracket && right_particle;
    size_t min_len = fully_particle_bracketed ? 2 : 3;
    const std::string promoted_surface = extractSubstring(codepoints, start_pos, scan);
    const auto& promoted_inflections = inflection_.analyze(promoted_surface);
    const bool has_inflected_predicate_reading =
        right_clause &&
        std::any_of(promoted_inflections.begin(), promoted_inflections.end(),
                    [](const grammar::InflectionCandidate& inflection_candidate) {
                      return !inflection_candidate.suffix.empty() &&
                             inflection_candidate.confidence >= candidate::verb_cost::kConstructedVerbMinConfidence;
                    });
    if (len >= min_len && (right_particle || right_clause) && !crossed_verified_predicate &&
        !has_inflected_predicate_reading &&
        !hasAuxiliaryParticleDecomposition(codepoints, start_pos, scan, dict_manager_)) {
      float noun_cost = getCostForType(start_type, len) + candidate::kPostParticleNounPenalty;
      const bool selected_nominal = right_particle && (left_determiner_bracket || left_clause_bracket ||
                                                       (start_pos > 0 && codepoints[start_pos - 1] == U'の'));
      // This is an unknown-noun rescue path.  Keep the homographic noun
      // candidate when an exact lexical reading exists, but do not give it
      // the rescue bonus that would erase the dictionary POS (きれい, しかれ,
      // かしら).  Grammatical right context can then select either reading.
      const auto* exact_dictionary_reading =
          dict_manager_ != nullptr ? dict_manager_->lookupExact(promoted_surface) : nullptr;
      const bool has_exact_noun =
          dict_manager_ != nullptr && dict_manager_->lookupExact(promoted_surface, core::PartOfSpeech::Noun) != nullptr;
      constexpr PartOfSpeechMask kPredicateMask = partOfSpeechMask(core::PartOfSpeech::Verb) |
                                                  partOfSpeechMask(core::PartOfSpeech::Adjective) |
                                                  partOfSpeechMask(core::PartOfSpeech::Auxiliary);
      const bool has_competing_exact_predicate =
          dict_manager_ != nullptr && hasExactPartOfSpeech(*dict_manager_, promoted_surface, kPredicateMask);
      const bool quoted_final_particle = exact_dictionary_reading != nullptr &&
                                         exact_dictionary_reading->extended_pos == core::ExtendedPOS::ParticleFinal &&
                                         scan < codepoints.size() &&
                                         grammar::isSingleHiragana(extractSubstring(codepoints, scan, scan + 1), U'と');
      const bool exact_reading_owns_context =
          exact_dictionary_reading != nullptr &&
          (exact_dictionary_reading->pos != core::PartOfSpeech::Particle || quoted_final_particle) &&
          !(has_exact_noun && has_competing_exact_predicate);
      if (selected_nominal && !exact_reading_owns_context) {
        noun_cost += scorer::kBonusDoubleVeryStrong;
      }
      if (right_genitive_after_internal_particle) {
        noun_cost += scorer::scale::kStrongBonus;
      }
      auto noun_cand = makeCandidate(promoted_surface, start_pos, scan, core::PartOfSpeech::Noun, noun_cost,
                                     /*has_suffix=*/true, CandidateOrigin::SameType);
#ifdef SUZUME_DEBUG_INFO
      noun_cand.pattern = "bracketed_hira_noun";
#endif
      candidates.push_back(noun_cand);
    }
  }

  return candidates;
}

}  // namespace suzume::analysis
