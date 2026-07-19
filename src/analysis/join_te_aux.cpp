/**
 * @file join_te_aux.cpp
 * @brief Te-form auxiliary and taru-adjective join candidate generation
 */

#include "bigram_table.h"
#include "candidate_constants.h"
#include "core/debug.h"
#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "grammar/inflection.h"
#include "join_candidates.h"
#include "normalize/char_type.h"
#include "normalize/exceptions.h"
#include "normalize/utf8.h"
#include "tokenizer_utils.h"
#include "verb_candidates_helpers.h"

namespace suzume::analysis {

namespace {

using CharType = normalize::CharType;

// Te-form auxiliary verb patterns
struct TeFormAuxiliary {
  const char* stem;
  const char* base_form;
  bool is_benefactive;  // Benefactive verbs should not form negative compounds
};

const TeFormAuxiliary kTeFormAuxiliaries[] = {
    {"い", "いく", false},      // 〜ていく
    {"く", "くる", false},      // 〜てくる
    {"み", "みる", false},      // 〜てみる
    {"お", "おく", false},      // 〜ておく
    {"しま", "しまう", false},  // 〜てしまう
    {"ちゃ", "しまう", false},  // 〜ちゃう (colloquial)
    {"じゃ", "しまう", false},  // 〜じゃう (colloquial)
    {"もら", "もらう", true},   // 〜てもらう (benefactive)
    {"くれ", "くれる", true},   // 〜てくれる (benefactive)
    {"あげ", "あげる", true},   // 〜てあげる (benefactive)
    {"や", "やる", true},       // 〜てやる (benefactive)
};

// Cost bonus imported from candidate_constants.h:
// candidate::kTeFormAuxBonus

}  // namespace

void addTeFormAuxiliaryCandidates(core::Lattice& lattice, std::string_view text,
                                  const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                  size_t start_pos, const std::vector<normalize::CharType>& char_types,
                                  const Scorer& scorer, const grammar::Inflection& inflection) {
  if (start_pos >= codepoints.size()) {
    return;
  }

  // Look for て or で at this position
  char32_t current = codepoints[start_pos];
  if (current != U'て' && current != U'で') {
    return;
  }

  // Skip if preceded by verb renyokei - MeCab splits verb+て+auxiliary
  // E.g., してみる → し + て + みる, 助けてもらう → 助け + て + もらう
  // This prevents generating combined "てもらう" candidate when it should be split
  // Check for: kanji (verb stem), い-row (ichidan renyokei), え-row (ichidan renyokei),
  // っ (sokuonbin)
  if (start_pos > 0) {
    // Kanji: verb stems (見て → 見 + て, 食て → 食 + て, etc.)
    if (char_types[start_pos - 1] == CharType::Kanji) {
      return;
    }
    char32_t prev = codepoints[start_pos - 1];
    // Sokuonbin (っ): godan verbs (買って, 行って, 持って)
    if (prev == U'っ') {
      return;
    }
    // Hatsuonbin (ん): godan-ma/ba/na verbs (読んで, 飛んで, 死んで)
    if (prev == U'ん') {
      return;
    }
    // い-row: suru renyokei (し), godan renyokei (き, ぎ, etc.), kami-ichidan (起き, etc.)
    // え-row: shimo-ichidan renyokei (食べ, 助け, 見え, etc.)
    if (grammar::isIRowCodepoint(prev) || grammar::isERowCodepoint(prev)) {
      return;
    }
  }

  // Check if there's hiragana following
  size_t aux_start = start_pos + 1;
  if (aux_start >= codepoints.size() || char_types[aux_start] != CharType::Hiragana) {
    return;
  }

  // Get byte positions
  size_t te_byte = byteOffsetAt(byte_offsets, start_pos);
  size_t aux_start_byte = byteOffsetAt(byte_offsets, aux_start);

  // Find the extent of hiragana following て/で
  size_t hiragana_end = findCharRegionEnd(char_types, aux_start, 10, CharType::Hiragana);

  // Try each auxiliary pattern
  for (const auto& aux : kTeFormAuxiliaries) {
    std::string_view stem(aux.stem);

    std::string_view text_after_te = text.substr(aux_start_byte);
    if (text_after_te.size() < stem.size()) {
      continue;
    }

    if (text_after_te.substr(0, stem.size()) != stem) {
      continue;
    }

    const size_t stem_char_len = normalize::utf8Length(stem);

    // Try different lengths after the stem
    for (size_t aux_end = aux_start + stem_char_len; aux_end <= hiragana_end && aux_end <= aux_start + 8; ++aux_end) {
      size_t aux_end_byte = byteOffsetAt(byte_offsets, aux_end);
      std::string aux_surface(text.substr(aux_start_byte, aux_end_byte - aux_start_byte));

      auto best = inflection.getBest(aux_surface);
      if (best.confidence > 0.4F && best.base_form == aux.base_form) {
        // Skip negative forms of benefactive verbs
        // E.g., てあげない should be split as て + あげない, not combined
        // This allows proper analysis of patterns like 教えてあげない
        if (aux.is_benefactive) {
          // Check if the surface ends with negative patterns
          bool is_negative = utf8::endsWith(aux_surface, "ない") || utf8::endsWith(aux_surface, "なく") ||
                             utf8::endsWith(aux_surface, "なかった") || utf8::endsWith(aux_surface, "なくて");
          if (is_negative) {
            continue;  // Don't create compound for benefactive negative
          }
        }

        size_t combo_end_byte = aux_end_byte;
        std::string combo_surface(text.substr(te_byte, combo_end_byte - te_byte));

        float final_cost = scorer.posPrior(core::PartOfSpeech::Verb) + scorer.joinOpts().te_form_aux_bonus;

        uint8_t flags = core::LatticeEdge::kIsUnknown;

        lattice.addEdge(combo_surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(aux_end),
                        core::PartOfSpeech::Verb, final_cost, flags, aux.base_form);

        break;
      }
    }
  }
}

void addTaruAdjectiveJoinCandidates(core::Lattice& lattice, std::string_view text,
                                    const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                    size_t start_pos, const std::vector<normalize::CharType>& char_types,
                                    const Scorer& scorer) {
  if (start_pos >= codepoints.size()) {
    return;
  }

  // Must start with kanji
  if (char_types[start_pos] != CharType::Kanji) {
    return;
  }

  // Look for X然と pattern where X is 1+ kanji
  // Need at least 3 characters: X + 然 + と
  if (start_pos + 2 >= codepoints.size()) {
    return;
  }

  // Find the kanji portion (including 然)
  size_t kanji_end = start_pos + 1;
  while (kanji_end < codepoints.size() && char_types[kanji_end] == CharType::Kanji) {
    ++kanji_end;
  }

  // Need at least 2 kanji, and the last one must be 然
  if (kanji_end - start_pos < 2) {
    return;
  }

  // Check if the last kanji is 然
  char32_t last_kanji = codepoints[kanji_end - 1];
  if (last_kanji != U'然') {
    return;
  }

  // Next character must be と (hiragana)
  if (kanji_end >= codepoints.size() || codepoints[kanji_end] != U'と') {
    return;
  }

  // Build the surface: X然と
  size_t start_byte = byteOffsetAt(byte_offsets, start_pos);
  size_t end_pos = kanji_end + 1;  // Include と
  size_t end_byte = byteOffsetAt(byte_offsets, end_pos);

  std::string surface(text.substr(start_byte, end_byte - start_byte));

  // X然 without と is the lemma
  size_t zen_end_byte = byteOffsetAt(byte_offsets, kanji_end);
  std::string lemma(text.substr(start_byte, zen_end_byte - start_byte));

  // Calculate cost with bonus for this pattern
  float base_cost = scorer.posPrior(core::PartOfSpeech::Adverb);
  constexpr float kTaruAdverbBonus = -1.5F;  // Strong bonus to beat Noun + Particle
  float final_cost = base_cost + kTaruAdverbBonus;

  uint8_t flags = core::LatticeEdge::kFromDictionary;

  lattice.addEdge(surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(end_pos), core::PartOfSpeech::Adverb,
                  final_cost, flags, lemma);
}

}  // namespace suzume::analysis
