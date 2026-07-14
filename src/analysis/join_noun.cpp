/**
 * @file join_noun.cpp
 * @brief Prefix+noun and verb-renyokei+suffix join candidate generation
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

// Productive prefixes for prefix+noun joining
struct ProductivePrefix {
  char32_t codepoint;
  float bonus;
  bool needs_kanji;
};

const ProductivePrefix kProductivePrefixes[] = {
    // Note: Honorific prefixes お, ご, 御 are NOT included here.
    // They should be tokenized separately as PREFIX + NOUN.
    // E.g., お水 → お(PREFIX) + 水(NOUN), not お水(NOUN)

    // Negation prefixes
    {U'不', -0.4F, true},  // 不安, 不要, 不便
    {U'未', -0.4F, true},  // 未経験, 未確認
    {U'非', -0.4F, true},  // 非常, 非公開
    {U'無', -0.4F, true},  // 無理, 無料

    // Degree/quantity prefixes
    {U'超', -0.3F, true},  // 超人, 超高速
    {U'再', -0.4F, true},  // 再開, 再確認
    {U'準', -0.4F, true},  // 準備, 準決勝
    {U'副', -0.4F, true},  // 副社長, 副作用
    {U'総', -0.4F, true},  // 総合, 総数
    {U'各', -0.4F, true},  // 各地, 各種
    {U'両', -0.4F, true},  // 両方, 両手
    {U'最', -0.4F, true},  // 最高, 最新
    {U'全', -0.4F, true},  // 全部, 全員
    {U'半', -0.4F, true},  // 半分, 半額
};

constexpr size_t kNumPrefixes = sizeof(kProductivePrefixes) / sizeof(kProductivePrefixes[0]);

// Maximum noun length for prefix joining
constexpr size_t kMaxNounLenForPrefix = 6;

// Cost bonus imported from candidate_constants.h:
// candidate::kVerifiedNounBonus

}  // namespace

void addPrefixNounJoinCandidates(core::Lattice& lattice, std::string_view text, const std::vector<char32_t>& codepoints,
                                 size_t start_pos, const std::vector<normalize::CharType>& char_types,
                                 const dictionary::DictionaryManager& dict_manager, const Scorer& scorer) {
  if (start_pos >= codepoints.size()) {
    return;
  }

  // Check if current character is a productive prefix
  char32_t current_char = codepoints[start_pos];
  const ProductivePrefix* matched_prefix = nullptr;

  for (size_t idx = 0; idx < kNumPrefixes; ++idx) {
    if (kProductivePrefixes[idx].codepoint == current_char) {
      matched_prefix = &kProductivePrefixes[idx];
      break;
    }
  }

  if (matched_prefix == nullptr) {
    return;
  }

  // Check if there's a noun part following
  size_t noun_start = start_pos + 1;
  if (noun_start >= codepoints.size()) {
    return;
  }

  // For most prefixes, the noun part should start with kanji
  if (matched_prefix->needs_kanji) {
    if (char_types[noun_start] != CharType::Kanji) {
      return;
    }
  } else {
    if (char_types[noun_start] != CharType::Kanji && char_types[noun_start] != CharType::Katakana) {
      return;
    }
  }

  // Find the end of the noun part
  CharType noun_type = char_types[noun_start];
  size_t noun_end = findCharRegionEnd(char_types, noun_start, kMaxNounLenForPrefix, noun_type);

  if (noun_end <= noun_start) {
    return;
  }

  // Check dictionary for compound nouns
  size_t noun_start_byte = charPosToBytePos(codepoints, noun_start);
  auto noun_results = dict_manager.lookup(text, noun_start_byte);
  bool noun_in_dict = false;
  size_t dict_noun_end = noun_end;

  for (const auto& result : noun_results) {
    if (result.entry != nullptr && result.entry->pos == core::PartOfSpeech::Noun) {
      if (result.length > dict_noun_end - noun_start) {
        dict_noun_end = noun_start + result.length;
        noun_in_dict = true;
      } else if (result.length == noun_end - noun_start) {
        noun_in_dict = true;
      }
    }
  }

  if (dict_noun_end > noun_end) {
    noun_end = dict_noun_end;
  } else {
    // Skip single-kanji noun when followed by hiragana (likely verb pattern)
    if (noun_end - noun_start == 1 && noun_end < codepoints.size()) {
      if (char_types[noun_end] == CharType::Hiragana) {
        return;
      }
    }
  }

  // Check if the combined form is already in dictionary
  size_t start_byte = charPosToBytePos(codepoints, start_pos);
  auto combined_results = dict_manager.lookup(text, start_byte);

  for (const auto& result : combined_results) {
    if (result.entry != nullptr && result.length == noun_end - start_pos) {
      return;  // Already in dictionary
    }
  }

  // Generate joined candidate
  size_t end_byte = charPosToBytePos(codepoints, noun_end);
  std::string surface(text.substr(start_byte, end_byte - start_byte));

  float base_cost = scorer.posPrior(core::PartOfSpeech::Noun);
  float final_cost = base_cost + matched_prefix->bonus;

  // Apply length penalty to prevent over-concatenation
  // Prefix + noun should be 2-3 chars total for most verified cases
  // (e.g., 全員=2, 再開=2, 不安=2)
  // Longer unverified combinations should be split
  size_t total_len = noun_end - start_pos;
  if (total_len >= 4 && !noun_in_dict) {
    // Strong penalty for unverified 4+ char combinations
    // Must overcome: prefix_bonus(-0.4) + optimal_length_bonus(-0.5) = -0.9
    // Target: make final cost higher than split path (~1.0)
    // Penalty: +2.0 base, +0.5 per extra char
    final_cost += 2.0F + 0.5F * static_cast<float>(total_len - 4);
  } else if (total_len == 3 && !noun_in_dict) {
    // Moderate penalty for 3-char unverified
    final_cost += 0.8F;
  }

  if (noun_in_dict) {
    final_cost += scorer.joinOpts().verified_noun_bonus;
  }

  uint8_t flags = core::LatticeEdge::kIsUnknown;

  lattice.addEdge(surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(noun_end), core::PartOfSpeech::Noun,
                  final_cost, flags, "");
}

void addVerbSuffixNounJoinCandidates(core::Lattice& lattice, std::string_view text,
                                     const std::vector<char32_t>& codepoints, size_t start_pos,
                                     const std::vector<normalize::CharType>& char_types,
                                     [[maybe_unused]] const dictionary::DictionaryManager& dict_manager,
                                     const Scorer& scorer) {
  if (start_pos >= codepoints.size()) {
    return;
  }

  // Hiragana-only stem + 方 (やり方, あり方) — V連用形 written entirely in hiragana.
  // Only emit when we're at a word boundary (start of input or preceded by
  // non-kanji), so we don't emit しい方 (from 美しい方) or similar adjective tails.
  if (char_types[start_pos] == CharType::Hiragana) {
    if (start_pos > 0 && char_types[start_pos - 1] == CharType::Kanji) {
      return;
    }
    if (start_pos + 2 >= codepoints.size() || codepoints[start_pos + 2] != U'方') {
      return;
    }
    if (char_types[start_pos + 1] != CharType::Hiragana) {
      return;
    }
    // Allow godan-ra renyokei (り: やる→やり, ある→あり) and godan-wa renyokei
    // (い: 言う→いい). 言う often has its stem-only renyokei written as いい
    // even when the kanji 言 is omitted.
    char32_t c1 = codepoints[start_pos + 1];
    if (c1 != U'り' && c1 != U'い') {
      return;
    }
    size_t end_pos = start_pos + 3;
    size_t start_byte = charPosToBytePos(codepoints, start_pos);
    size_t end_byte = charPosToBytePos(codepoints, end_pos);
    std::string surface(text.substr(start_byte, end_byte - start_byte));
    float base_cost = scorer.posPrior(core::PartOfSpeech::Noun);
    constexpr float kCompoundNounBonus = -1.0F;
    float final_cost = base_cost + kCompoundNounBonus;
    uint8_t flags = core::LatticeEdge::kFromDictionary;
    lattice.addEdge(surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(end_pos), core::PartOfSpeech::Noun,
                    final_cost, flags, surface);
    return;
  }

  // Must start with kanji (verb stem)
  if (char_types[start_pos] != CharType::Kanji) {
    return;
  }

  // Look for patterns: Kanji + Hiragana + Suffix(物/方/所)
  // Examples: 食べ物, 飲み物, 読み方, 居場所
  size_t pos = start_pos;

  // Find kanji portion (verb stem)
  size_t kanji_end = pos;
  while (kanji_end < codepoints.size() && char_types[kanji_end] == CharType::Kanji) {
    ++kanji_end;
  }

  if (kanji_end == pos) {
    return;  // No kanji found
  }

  // Look for optional hiragana (verb renyokei suffix like べ, み, き)
  size_t hiragana_end = kanji_end;
  while (hiragana_end < codepoints.size() && char_types[hiragana_end] == CharType::Hiragana) {
    // Only allow 1-2 hiragana characters for renyokei
    if (hiragana_end - kanji_end >= 2) {
      break;
    }
    ++hiragana_end;
  }

  // Reject if hiragana ends with な (na-adjective 連体形, not verb renyokei)
  // e.g., 効率的な方 should NOT become a compound noun (it's 効率+的+な+方)
  if (hiragana_end > kanji_end && codepoints[hiragana_end - 1] == U'な') {
    return;
  }

  // Reject if hiragana ends with た (past form, not verb renyokei)
  // e.g., 書いた方 should NOT become a compound noun (it's 書い+た+方)
  // Correct patterns: 歩き方, 食べ方 (V連用形+方)
  if (hiragana_end > kanji_end && codepoints[hiragana_end - 1] == U'た') {
    return;
  }

  // Reject if hiragana ends with の (genitive particle, not verb renyokei)
  // e.g., 今後の方針 should NOT become 今後の方 + 針 (it's 今後+の+方針)
  // の is a case particle, not a verb renyokei ending
  if (hiragana_end > kanji_end && codepoints[hiragana_end - 1] == U'の') {
    return;
  }

  // Reject if hiragana ends with い AND hiragana run is 2+ chars (i-adjective).
  // e.g., 美しい方 (kanji+しい) is an adjective + noun, not a compound.
  // But 言い方 (kanji+い, single hiragana い) is godan-wa V連用形 + 方 — valid.
  if (hiragana_end > kanji_end + 1 && codepoints[hiragana_end - 1] == U'い') {
    return;
  }

  // Reject if hiragana ends with る (verb rentaikei/dictionary form, not renyokei)
  // e.g., 見渡せる所 should NOT become a compound noun (it's 見渡せる + 所)
  // Valid patterns: 食べ物, 居場所 (verb renyokei + suffix)
  if (hiragana_end > kanji_end && codepoints[hiragana_end - 1] == U'る') {
    return;
  }

  // Reject if hiragana ends with に (case particle, not verb renyokei)
  // e.g., 静かに目 should NOT become a compound noun (it's 静か+に+目)
  // Godan-na renyokei (死に) is only 1 hiragana, already blocked by particle check
  if (hiragana_end > kanji_end && codepoints[hiragana_end - 1] == U'に') {
    return;
  }

  // Reject if hiragana is a single case particle (not verb renyokei)
  // e.g., 東京都渋谷区に所在 should NOT become 東京都渋谷区に所 + ... (it's ...+に+所在+...)
  // Case particles: に, で, と, を, が, は, へ, も, か, や
  // These cannot be verb renyokei endings
  if (hiragana_end - kanji_end == 1) {
    char32_t hira_char = codepoints[kanji_end];
    if (hira_char == U'に' || hira_char == U'で' || hira_char == U'と' || hira_char == U'を' || hira_char == U'が' ||
        hira_char == U'は' || hira_char == U'へ' || hira_char == U'も' || hira_char == U'か' || hira_char == U'や') {
      return;
    }
  }

  // Check for suffix kanji: 物, 方, 所
  if (hiragana_end >= codepoints.size()) {
    return;
  }

  char32_t suffix_char = codepoints[hiragana_end];
  bool is_mono_suffix = (suffix_char == U'物');
  bool is_kata_suffix = (suffix_char == U'方');
  bool is_tokoro_suffix = (suffix_char == U'所');
  bool is_me_suffix = (suffix_char == U'目');  // 割れ目, 切れ目, 裂け目

  if (!is_mono_suffix && !is_kata_suffix && !is_tokoro_suffix && !is_me_suffix) {
    return;
  }

  // We need at least some hiragana between kanji and suffix (verb renyokei ending)
  // Exception: single kanji + suffix is allowed for some patterns
  if (hiragana_end == kanji_end && kanji_end - start_pos < 2) {
    return;  // Too short without hiragana
  }

  // Build the compound noun surface
  size_t end_pos = hiragana_end + 1;  // Include suffix
  size_t start_byte = charPosToBytePos(codepoints, start_pos);
  size_t end_byte = charPosToBytePos(codepoints, end_pos);

  std::string surface(text.substr(start_byte, end_byte - start_byte));

  // Calculate cost with bonus for compound noun pattern
  float base_cost = scorer.posPrior(core::PartOfSpeech::Noun);
  constexpr float kCompoundNounBonus = -1.0F;  // Moderate bonus
  float final_cost = base_cost + kCompoundNounBonus;

  uint8_t flags = core::LatticeEdge::kFromDictionary;

  lattice.addEdge(surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(end_pos), core::PartOfSpeech::Noun,
                  final_cost, flags, surface);  // lemma = surface for compound nouns
}

}  // namespace suzume::analysis
