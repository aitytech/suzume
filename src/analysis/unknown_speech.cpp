/**
 * @file unknown_speech.cpp
 * @brief Character-speech and onomatopoeia candidate generation for unknown words
 *
 * Split from unknown.cpp: houses UnknownWordGenerator::generateCharacterSpeechCandidates
 * and UnknownWordGenerator::generateOnomatopoeiaCandidates.
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

std::vector<UnknownCandidate> UnknownWordGenerator::generateCharacterSpeechCandidates(
    std::string_view /*text*/, const std::vector<char32_t>& codepoints, size_t start_pos,
    const std::vector<normalize::CharType>& char_types) const {
  std::vector<UnknownCandidate> candidates;

  if (start_pos >= char_types.size()) {
    return candidates;
  }

  normalize::CharType start_type = char_types[start_pos];

  // Only for hiragana or katakana starting positions
  if (start_type != normalize::CharType::Hiragana && start_type != normalize::CharType::Katakana) {
    return candidates;
  }

  // Skip if starting with common particles (these are handled by dictionary)
  if (start_type == normalize::CharType::Hiragana) {
    char32_t first_char = codepoints[start_pos];
    if (normalize::isExtendedParticle(first_char)) {
      return candidates;
    }
    // Skip small kana (ゃゅょぁぃぅぇぉっ) - these don't start words
    if (kana::isSmallKanaCodepoint(first_char)) {
      return candidates;
    }
  }

  // Skip small katakana as well
  if (start_type == normalize::CharType::Katakana) {
    char32_t first_char = codepoints[start_pos];
    if (kana::isSmallKanaCodepoint(first_char)) {
      return candidates;
    }
  }

  // Whitelist approach: only allow char speech starting with kana that can
  // begin valid auxiliary/character-speech patterns
  if (start_type == normalize::CharType::Hiragana) {
    char32_t first_char = codepoints[start_pos];
    // Valid char speech starters: sentence-ending speech patterns (ぞ,じゃ,のう,etc.)
    // and colloquial auxiliaries (ちゃ,じゃ,etc.)
    // Excludes: grammar chars handled by dict (た,さ,ら,く,あ,け,い,す,る)
    //           and kana that never start auxiliaries (ぱ行,ば行 sound-symbolics, etc.)
    bool valid_starter = first_char == U'ぞ' || first_char == U'じ' || first_char == U'の' || first_char == U'な' ||
                         first_char == U'ね' || first_char == U'よ' || first_char == U'わ' || first_char == U'で' ||
                         first_char == U'だ' || first_char == U'ま' || first_char == U'や' || first_char == U'か' ||
                         first_char == U'が' || first_char == U'べ' || first_char == U'ち' || first_char == U'に' ||
                         first_char == U'せ' || first_char == U'ず' || first_char == U'ど' || first_char == U'て' ||
                         first_char == U'も' || first_char == U'み' || first_char == U'ん' || first_char == U'こ' ||
                         first_char == U'そ' || first_char == U'と' || first_char == U'お' || first_char == U'は' ||
                         first_char == U'へ';
    if (!valid_starter) {
      return candidates;
    }
  }

  size_t max_len = options_.max_character_speech_length;
  size_t text_len = char_types.size();

  // Find end of same-type sequence (limited to max_character_speech_length)
  size_t end_pos = start_pos + 1;
  while (end_pos < text_len && end_pos - start_pos < max_len && char_types[end_pos] == start_type) {
    ++end_pos;
  }

  // Check if this could be a sentence-end position
  auto isSentenceEndPosition = [&](size_t pos) -> bool {
    if (pos >= text_len) {
      return true;  // End of text
    }

    char32_t next_char = codepoints[pos];

    // Punctuation marks
    if (next_char == U'。' || next_char == U'！' || next_char == U'？' || next_char == U'、' || next_char == U',' ||
        next_char == U'.' || next_char == U'!' || next_char == U'?' || next_char == U'…' || next_char == U'」' ||
        next_char == U'』' || next_char == U'"' || next_char == U'\n' || next_char == U'\r') {
      return true;
    }

    // Whitespace (space, full-width space, tab)
    if (next_char == U' ' || next_char == U'　' || next_char == U'\t') {
      return true;
    }

    return false;
  };

  // Generate candidates for different lengths
  for (size_t len = 1; len <= end_pos - start_pos; ++len) {
    size_t candidate_end = start_pos + len;

    // Only generate if this position could be sentence-end
    if (!isSentenceEndPosition(candidate_end)) {
      continue;
    }

    std::string surface = extractSubstring(codepoints, start_pos, candidate_end);

    if (!surface.empty()) {
      // Skip patterns ending with そう - these are aspectual auxiliary patterns
      // that should be handled by verb/adjective + そう analysis, not as character speech
      if (utf8::endsWith(surface, scorer::kSuffixSou)) {
        continue;
      }

      // Skip generating AUX for common particle surfaces
      // These should be handled by the particle dictionary entries, not as auxiliaries
      // This prevents だけ from being generated as AUX (which gets VerbOnbinkei → AuxTenseTa bonus)
      static const std::vector<std::string_view> kParticleSurfaces = {
          "だけ", "ばかり", "ほど", "くらい", "ぐらい", "など", "なんて",
          "しか", "まで",   "より", "から",   "かも",   "でも",
      };
      bool is_particle_surface = false;
      for (const auto& p : kParticleSurfaces) {
        if (surface == p) {
          is_particle_surface = true;
          break;
        }
      }
      if (is_particle_surface) {
        continue;  // Skip - let dictionary entry handle it
      }

      // Calculate character count (not byte count)
      size_t char_count = surface.size() / core::kJapaneseCharBytes;

      // For single-character hiragana, only allow valid auxiliary forms
      // This prevents spurious splits like 玉ね+ぎ where ぎ is misanalyzed as た
      if (char_count == 1 && start_type == normalize::CharType::Hiragana) {
        // Valid single-char auxiliaries: た て ぬ む ん い せ れ ず よ ろ
        static const std::string_view kValidSingleCharAux[] = {
            "た", "て", "ぬ", "む", "ん", "い", "せ", "れ", "ず", "よ", "ろ",
        };
        bool is_valid_aux = false;
        for (const auto& valid : kValidSingleCharAux) {
          if (surface == valid) {
            is_valid_aux = true;
            break;
          }
        }
        if (!is_valid_aux) {
          continue;  // Skip invalid single-char auxiliary candidates
        }
      }

      // Apply length-based penalty for character speech
      // Short patterns (1-2 chars) like ぜ, のだ are common
      // Longer patterns like まむぎ (3+ chars) are rare
      float length_penalty = 0.0F;
      if (char_count >= 3) {
        // Penalty increases with length: 3chars=+2.0, 4chars=+4.0, etc.
        length_penalty = static_cast<float>(char_count - 2) * 2.0F;
      }

      // Skip katakana character speech candidates entirely
      // Katakana words are almost always loanword nouns (パン, キロ), not auxiliaries
      // Character speech (擬態語/擬声語) is almost exclusively written in hiragana
      if (start_type == normalize::CharType::Katakana) {
        continue;  // Skip - let same_type kata_seq handle katakana as NOUN
      }

      // Mark as Auxiliary so it connects properly after verbs/adjectives
      float cost = options_.character_speech_cost + length_penalty;
      auto cand = makeCandidate(surface, start_pos, candidate_end, core::PartOfSpeech::Auxiliary, cost, false,
                                CandidateOrigin::CharacterSpeech);
#ifdef SUZUME_DEBUG_INFO
      cand.confidence = 0.5F;
      cand.pattern = (start_type == normalize::CharType::Hiragana) ? "char_speech_hira" : "char_speech_kata";
#endif
      candidates.push_back(cand);
    }
  }

  return candidates;
}

std::vector<UnknownCandidate> UnknownWordGenerator::generateOnomatopoeiaCandidates(
    const std::vector<char32_t>& codepoints, size_t start_pos,
    const std::vector<normalize::CharType>& char_types) const {
  std::vector<UnknownCandidate> candidates;

  // Need at least 3 characters for ABり pattern (4 for ABAB/AA patterns)
  if (start_pos + 2 >= codepoints.size()) {
    return candidates;
  }

  normalize::CharType start_type = char_types[start_pos];

  // Helper to check if a character belongs to the same script group or is a modifier
  auto isSameScriptOrModifier = [&](size_t pos) -> bool {
    if (pos >= char_types.size())
      return false;
    if (pos >= codepoints.size())
      return false;
    // Same char type
    if (char_types[pos] == start_type)
      return true;
    // Prolonged sound mark (ー) can appear in both hiragana and katakana words
    if (normalize::isProlongedSoundMark(codepoints[pos]))
      return true;
    return false;
  };

  // Helper to check if a character is small kana (part of previous mora)
  auto isSmallKanaAt = [&](size_t pos) -> bool {
    if (pos >= codepoints.size())
      return false;
    std::string ch = normalize::encodeRange(codepoints, pos, pos + 1);
    return grammar::isSmallKana(ch);
  };

  // Find the extent of same-script sequence (including ー)
  size_t seq_end = start_pos;
  while (seq_end < codepoints.size() && isSameScriptOrModifier(seq_end)) {
    ++seq_end;
  }

  size_t seq_len = seq_end - start_pos;

  // Try AA pattern: first half equals second half (ニャーニャー, ワンワン)
  // Sequence must have even length and be at least 4 chars
  if (seq_len >= 4 && seq_len % 2 == 0) {
    size_t half_len = seq_len / 2;
    bool is_aa = true;

    // Check if first half equals second half
    for (size_t i = 0; i < half_len; ++i) {
      if (codepoints[start_pos + i] != codepoints[start_pos + half_len + i]) {
        is_aa = false;
        break;
      }
    }

    if (is_aa) {
      // Verify the first char of each half is not small kana
      // (small kana should be part of previous mora, not start a unit)
      if (!isSmallKanaAt(start_pos) && !isSmallKanaAt(start_pos + half_len)) {
        std::string surface = extractSubstring(codepoints, start_pos, seq_end);
        if (!surface.empty()) {
          auto cand = makeCandidate(surface, start_pos, seq_end, core::PartOfSpeech::Adverb, -1.0F, true,
                                    CandidateOrigin::Onomatopoeia);
#ifdef SUZUME_DEBUG_INFO
          cand.confidence = 1.0F;
          cand.pattern = "aa_doubled";
#endif
          candidates.push_back(cand);
          return candidates;  // Found a match, return early
        }
      }
    }
  }

  // Try ABAB pattern for exactly 4 chars (traditional pattern)
  if (seq_len >= 4) {
    // Check if all 4 chars are the expected type
    bool valid = true;
    for (size_t i = 0; i < 4; ++i) {
      if (!isSameScriptOrModifier(start_pos + i)) {
        valid = false;
        break;
      }
    }

    if (valid) {
      char32_t ch0 = codepoints[start_pos];
      char32_t ch1 = codepoints[start_pos + 1];
      char32_t ch2 = codepoints[start_pos + 2];
      char32_t ch3 = codepoints[start_pos + 3];

      if (ch0 == ch2 && ch1 == ch3 && ch0 != ch1 && !isSmallKanaAt(start_pos)) {
        // ABAB pattern detected (e.g., わくわく, きらきら, どきどき)
        // Excludes AAAA pattern (e.g., もももも) where all chars are the same
        std::string surface = extractSubstring(codepoints, start_pos, start_pos + 4);
        if (!surface.empty()) {
          auto cand = makeCandidate(surface, start_pos, start_pos + 4, core::PartOfSpeech::Adverb, 0.1F, true,
                                    CandidateOrigin::Onomatopoeia);
#ifdef SUZUME_DEBUG_INFO
          cand.confidence = 1.0F;
          cand.pattern = "abab_pattern";
#endif
          candidates.push_back(cand);
        }
      }
    }
  }

  // Try ABり / AっBり patterns (e.g., どさり, ばたり, ぐったり,
  // じっくり). The same-script run can continue through quotative と (and the
  // four-character form can precede a hiragana predicate), so recognize the
  // structural prefix rather than requiring り to end the whole run.
  if (seq_len >= 3 && start_type == normalize::CharType::Hiragana) {
    if (codepoints[start_pos + 2] == U'り' && (seq_len == 3 || (seq_len > 3 && codepoints[start_pos + 3] == U'と'))) {
      // Three-character patterns are extended past the run boundary only when
      // followed by the adverbial marker と, which limits prefix false positives.
      // Skip if first char is a common particle (の, は, が, を, に, で, も, と, へ, か)
      // to avoid false matches like のやり, はしり, がわり
      char32_t first = codepoints[start_pos];
      if (!normalize::isParticleCodepoint(first) && first != U'ら') {
        std::string surface = extractSubstring(codepoints, start_pos, start_pos + 3);
        if (!surface.empty()) {
          auto cand = makeCandidate(surface, start_pos, start_pos + 3, core::PartOfSpeech::Adverb, 0.7F, true,
                                    CandidateOrigin::Onomatopoeia);
#ifdef SUZUME_DEBUG_INFO
          cand.confidence = 0.7F;
          cand.pattern = "ab_ri_pattern";
#endif
          candidates.push_back(cand);
        }
      }
    }

    // Four-character patterns like ぐったり and じっくり.
    if (seq_len >= 4 && isSmallKanaAt(start_pos + 1) && codepoints[start_pos + 3] == U'り') {
      std::string surface = extractSubstring(codepoints, start_pos, start_pos + 4);
      if (!surface.empty()) {
        auto cand = makeCandidate(surface, start_pos, start_pos + 4, core::PartOfSpeech::Adverb, 0.2F, true,
                                  CandidateOrigin::Onomatopoeia);
#ifdef SUZUME_DEBUG_INFO
        cand.confidence = 0.8F;
        cand.pattern = "xtu_cv_ri_pattern";
#endif
        candidates.push_back(cand);
      }
    }
  }

  // Try Xっと pattern for onomatopoeia adverbs (はっと, ぐっと, どきっと, ぷるんっと)
  // Pattern: [hiragana]{1,4} + っと where the hiragana sequence is the onomatopoeia stem
  // These are mimetic/onomatopoeia adverbs that precede する/くる conjugations
  // E.g., はっとした, ぐっときた, どきっとした, ぷるんっとした
  if (seq_len >= 3 && start_type == normalize::CharType::Hiragana) {
    // Look for っと at various positions within the sequence
    for (size_t tto_pos = start_pos + 1; tto_pos + 1 < seq_end && tto_pos <= start_pos + 4; ++tto_pos) {
      if (codepoints[tto_pos] == U'っ' &&                               // っ (small tsu)
          tto_pos + 1 < seq_end && codepoints[tto_pos + 1] == U'と') {  // と
        size_t adv_end = tto_pos + 2;
        size_t stem_len = tto_pos - start_pos;  // chars before っ
        // Stem should be 1-4 hiragana chars
        if (stem_len >= 1 && stem_len <= 4) {
          // Skip if stem starts with a particle character (e.g., にもっと = に+もっと)
          char32_t first_cp = codepoints[start_pos];
          if (stem_len >= 2 &&
              (first_cp == U'に' || first_cp == U'は' || first_cp == U'も' || first_cp == U'を' || first_cp == U'が' ||
               first_cp == U'で' || first_cp == U'と' || first_cp == U'か' || first_cp == U'の' || first_cp == U'へ')) {
            break;
          }
          std::string surface = extractSubstring(codepoints, start_pos, adv_end);
          if (!surface.empty()) {
            // Strong bonus for short patterns (はっと, ぐっと = very common)
            // Needs to beat hiragana verb candidates that absorb the っと
            float cost = (stem_len <= 2) ? -1.5F : -0.5F;
            auto cand = makeCandidate(surface, start_pos, adv_end, core::PartOfSpeech::Adverb, cost, true,
                                      CandidateOrigin::Onomatopoeia);
#ifdef SUZUME_DEBUG_INFO
            cand.confidence = 0.9F;
            cand.pattern = "x_tto_pattern";
#endif
            candidates.push_back(cand);
          }
        }
        break;  // Only match the first っと position
      }
    }
  }

  return candidates;
}

}  // namespace suzume::analysis
