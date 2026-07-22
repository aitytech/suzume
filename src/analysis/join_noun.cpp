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
#include "scorer_constants.h"
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
    {U'不', candidate::kProductivePrefixJoinBonus, true},  // 不安, 不要, 不便
    {U'未', candidate::kProductivePrefixJoinBonus, true},  // 未経験, 未確認
    {U'非', candidate::kProductivePrefixJoinBonus, true},  // 非常, 非公開
    {U'無', candidate::kProductivePrefixJoinBonus, true},  // 無理, 無料

    // Degree/quantity prefixes
    {U'超', candidate::kIntensifierPrefixJoinBonus, true},  // 超人, 超高速
    {U'再', candidate::kProductivePrefixJoinBonus, true},   // 再開, 再確認
    {U'準', candidate::kProductivePrefixJoinBonus, true},   // 準備, 準決勝
    {U'副', candidate::kProductivePrefixJoinBonus, true},   // 副社長, 副作用
    {U'総', candidate::kProductivePrefixJoinBonus, true},   // 総合, 総数
    {U'各', candidate::kProductivePrefixJoinBonus, true},   // 各地, 各種
    {U'両', candidate::kProductivePrefixJoinBonus, true},   // 両方, 両手
    {U'最', candidate::kProductivePrefixJoinBonus, true},   // 最高, 最新
    {U'全', candidate::kProductivePrefixJoinBonus, true},   // 全部, 全員
    {U'半', candidate::kProductivePrefixJoinBonus, true},   // 半分, 半額
};

constexpr size_t kNumPrefixes = sizeof(kProductivePrefixes) / sizeof(kProductivePrefixes[0]);

// Maximum noun length for prefix joining
constexpr size_t kMaxNounLenForPrefix = 6;

// A productive prefix compound can be a na-adjective only when its base has
// an adjective entry. A following copula alone is not sufficient evidence:
// ordinary nouns such as 最中 also occur before だ.
bool hasNaAdjectiveContinuation(const std::vector<char32_t>& codepoints, size_t pos) {
  if (pos >= codepoints.size()) {
    return false;
  }
  if (codepoints[pos] == U'だ' || codepoints[pos] == U'で' || codepoints[pos] == U'な') {
    return true;
  }
  return pos + 1 < codepoints.size() && codepoints[pos] == U'そ' && codepoints[pos + 1] == U'う';
}

// Cost bonus imported from candidate_constants.h:
// candidate::kVerifiedNounBonus

bool isHiraganaHonorificPrefix(char32_t codepoint) {
  return codepoint == U'お' || codepoint == U'ご';
}

bool isCaseParticleCodepoint(char32_t codepoint) {
  switch (codepoint) {
    case U'に':
    case U'で':
    case U'と':
    case U'を':
    case U'が':
    case U'は':
    case U'へ':
    case U'も':
    case U'か':
    case U'や':
      return true;
    default:
      return false;
  }
}

void addHonorificSamaNounJoinCandidate(core::Lattice& lattice, std::string_view text,
                                       const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                       size_t start_pos, const std::vector<normalize::CharType>& char_types,
                                       const Scorer& scorer) {
  if (start_pos + 2 >= codepoints.size() || !isHiraganaHonorificPrefix(codepoints[start_pos]) ||
      char_types[start_pos + 1] != CharType::Kanji) {
    return;
  }

  size_t end_pos = start_pos + 1;
  while (end_pos < codepoints.size() && char_types[end_pos] == CharType::Kanji) {
    ++end_pos;
  }

  // A productive honorific noun needs a lexical kanji base before the closed
  // honorific suffix. This preserves the ordinary prefix analysis for お様.
  if (end_pos - start_pos < 3 || codepoints[end_pos - 1] != U'様') {
    return;
  }

  const size_t start_byte = byteOffsetAt(byte_offsets, start_pos);
  const size_t end_byte = byteOffsetAt(byte_offsets, end_pos);
  std::string surface(text.substr(start_byte, end_byte - start_byte));
  const float cost = scorer.posPrior(core::PartOfSpeech::Noun) + candidate::kHonorificSamaNounBonus;
  lattice.addEdge(surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(end_pos), core::PartOfSpeech::Noun,
                  cost, core::LatticeEdge::kIsUnknown);
}

void addStandaloneHonorificPrefixInterjectionCandidate(core::Lattice& lattice, std::string_view text,
                                                       const std::vector<char32_t>& codepoints,
                                                       const ByteOffsets& byte_offsets, size_t start_pos,
                                                       const Scorer& scorer) {
  if (start_pos + 1 != codepoints.size() ||
      !grammar::isHonorificPrefix(extractSubstring(codepoints, start_pos, start_pos + 1))) {
    return;
  }

  const size_t start_byte = byteOffsetAt(byte_offsets, start_pos);
  const size_t end_byte = byteOffsetAt(byte_offsets, start_pos + 1);
  std::string surface(text.substr(start_byte, end_byte - start_byte));
  const float cost =
      scorer.posPrior(core::PartOfSpeech::Interjection) + candidate::kStandaloneHonorificPrefixInterjectionBonus;
  lattice.addEdge(surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(start_pos + 1),
                  core::PartOfSpeech::Interjection, cost, core::LatticeEdge::kIsUnknown);
}

}  // namespace

void addPrefixNounJoinCandidates(core::Lattice& lattice, std::string_view text, const std::vector<char32_t>& codepoints,
                                 const ByteOffsets& byte_offsets, size_t start_pos,
                                 const std::vector<normalize::CharType>& char_types,
                                 const dictionary::DictionaryManager& dict_manager, const Scorer& scorer) {
  if (start_pos >= codepoints.size()) {
    return;
  }

  addHonorificSamaNounJoinCandidate(lattice, text, codepoints, byte_offsets, start_pos, char_types, scorer);
  addStandaloneHonorificPrefixInterjectionCandidate(lattice, text, codepoints, byte_offsets, start_pos, scorer);

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
  size_t noun_start_byte = byteOffsetAt(byte_offsets, noun_start);
  auto noun_results = dict_manager.lookup(text, noun_start_byte);
  bool noun_in_dict = false;
  bool adjective_in_dict = false;
  size_t dict_noun_end = noun_end;

  for (const auto& result : noun_results) {
    if (result.entry == nullptr) {
      continue;
    }
    if (result.entry->extended_pos == core::ExtendedPOS::AdjNaAdj) {
      if (result.length >= noun_end - noun_start) {
        adjective_in_dict = true;
      }
      if (result.length > dict_noun_end - noun_start) {
        dict_noun_end = noun_start + result.length;
      }
    }
    if (result.entry->pos == core::PartOfSpeech::Noun) {
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
      if (char_types[noun_end] == CharType::Hiragana && !hasNaAdjectiveContinuation(codepoints, noun_end)) {
        return;
      }
    }
  }

  // Check if the combined form is already in dictionary.
  size_t start_byte = byteOffsetAt(byte_offsets, start_pos);
  size_t end_byte = byteOffsetAt(byte_offsets, noun_end);
  std::string surface(text.substr(start_byte, end_byte - start_byte));

  if (dict_manager.lookupExact(surface) != nullptr) {
    return;
  }

  // Generate joined candidate
  float base_cost = scorer.posPrior(core::PartOfSpeech::Noun);
  float final_cost = base_cost + matched_prefix->bonus;

  // Apply length penalty to prevent over-concatenation
  // Prefix + noun should be 2-3 chars total for most verified cases
  // (e.g., 全員=2, 再開=2, 不安=2)
  // Longer unverified combinations should be split
  size_t total_len = noun_end - start_pos;
  const bool has_copular_na_adjective_continuation =
      noun_end < codepoints.size() &&
      (codepoints[noun_end] == U'だ' || codepoints[noun_end] == U'で' || codepoints[noun_end] == U'な');
  const bool has_predicative_adjective_evidence =
      adjective_in_dict || normalize::isNumeralCodepoint(codepoints[noun_start]);
  const bool is_predicative_negation_compound = scorer::startsWithNegationPrefix(surface) &&
                                                has_copular_na_adjective_continuation &&
                                                has_predicative_adjective_evidence;
  if (total_len >= 4 && !noun_in_dict) {
    // Strong penalty for unverified 4+ char combinations
    // Must overcome: prefix_bonus(-0.4) + optimal_length_bonus(-0.5) = -0.9
    // Target: make final cost higher than split path (~1.0)
    // Penalty: +2.0 base, +0.5 per extra char
    final_cost += candidate::kUnverifiedPrefixJoinLongBasePenalty +
                  candidate::kUnverifiedPrefixJoinLongPerCharPenalty * static_cast<float>(total_len - 4);
  } else if (total_len == 3 && !noun_in_dict && !is_predicative_negation_compound) {
    // Penalty for 3-char unverified so the join cannot beat the plain
    // 2-char kanji_seq noun split (e.g. 全部食 vs 全部|食 from 全部食べちゃった).
    // A negation-prefix compound before a copula or adjectival continuation is
    // a complete predicative unit (不十分だ, 不確かではない), even when its
    // open-class base has no dictionary entry.
    final_cost += candidate::kUnverifiedPrefixJoin3charPenalty;
  }

  if (noun_in_dict) {
    final_cost += scorer.joinOpts().verified_noun_bonus;
  }

  uint8_t flags = core::LatticeEdge::kIsUnknown;

  lattice.addEdge(surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(noun_end), core::PartOfSpeech::Noun,
                  final_cost, flags, "");

  // Capability compounds ending in 可能 are nominal expressions (再利用可能、
  // 使用可能). Their following な is the attributive copula, not evidence that
  // the whole productive prefix compound should be reclassified as an
  // adjective.
  bool is_nominal_capability_compound = utf8::endsWith(surface, "可能");
  // A negation-prefix compound in a na-adjective continuation is productive
  // even where its open-class base is absent from the compact dictionary
  // (不十分だ, 不確かではない).  Keep the nominal path too: the lattice can
  // still select it in non-adjectival contexts.
  if (!is_nominal_capability_compound && (adjective_in_dict || is_predicative_negation_compound)) {
    float adjective_cost = scorer.posPrior(core::PartOfSpeech::Adjective) + matched_prefix->bonus;
    if (is_predicative_negation_compound) {
      adjective_cost += candidate::kPredicativeNegationPrefixAdjectiveBonus;
    }
    lattice.addEdge(surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(noun_end),
                    core::PartOfSpeech::Adjective, adjective_cost, flags, surface, dictionary::ConjugationType::None,
                    core::CandidateOrigin::PrefixCompound, candidate::kNoOriginConfidence, "prefix_na_adjective",
                    core::ExtendedPOS::AdjNaAdj);
  }
}

void addPronounPluralJoinCandidates(core::Lattice& lattice, std::string_view text,
                                    const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                    size_t start_pos, const dictionary::DictionaryManager& dict_manager,
                                    const Scorer& scorer) {
  if (start_pos >= codepoints.size()) {
    return;
  }

  const size_t start_byte = byteOffsetAt(byte_offsets, start_pos);
  for (const auto& result : dict_manager.lookup(text, start_byte)) {
    if (result.entry == nullptr || result.entry->pos != core::PartOfSpeech::Pronoun) {
      continue;
    }
    const size_t suffix_pos = start_pos + result.length;
    if (suffix_pos >= codepoints.size() || codepoints[suffix_pos] != U'ら') {
      continue;
    }

    const size_t end_pos = suffix_pos + 1;
    const size_t end_byte = byteOffsetAt(byte_offsets, end_pos);
    std::string surface(text.substr(start_byte, end_byte - start_byte));
    const float cost = scorer.posPrior(core::PartOfSpeech::Pronoun) + candidate::kVerifiedNounBonus;
    lattice.addEdge(surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(end_pos),
                    core::PartOfSpeech::Pronoun, cost, core::LatticeEdge::kFromDictionary, surface);
  }
}

void addVerbSuffixNounJoinCandidates(core::Lattice& lattice, std::string_view text,
                                     const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                     size_t start_pos, const std::vector<normalize::CharType>& char_types,
                                     const dictionary::DictionaryManager& dict_manager, const Scorer& scorer,
                                     [[maybe_unused]] const grammar::Inflection& inflection) {
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
    // A complete adjective (including adjectival ない) before 方 is an
    // attributive predicate, not a pure-hiragana verb continuative
    // (ない+方法, not ない方+法).
    const std::string stem = extractSubstring(codepoints, start_pos, start_pos + 2);
    if (dict_manager.lookupExact(stem, core::PartOfSpeech::Adjective) != nullptr) {
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
    size_t start_byte = byteOffsetAt(byte_offsets, start_pos);
    size_t end_byte = byteOffsetAt(byte_offsets, end_pos);
    std::string surface(text.substr(start_byte, end_byte - start_byte));
    float base_cost = scorer.posPrior(core::PartOfSpeech::Noun);
    float final_cost = base_cost + candidate::kVerbSuffixNounJoinBonus;
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
  if (hiragana_end > kanji_end && (codepoints[hiragana_end - 1] == U'た' || codepoints[hiragana_end - 1] == U'だ')) {
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

  // A case particle cannot be the final mora of a verb continuative.  The
  // scan admits up to two hiragana, so this must cover both one- and two-mora
  // spans: otherwise 高みを目 and 越えを目 are fabricated as deverbal nouns.
  // Valid forms such as 食べ物, 割れ目, and 読み方 end in the continuative,
  // never in a case particle.
  if (hiragana_end > kanji_end && isCaseParticleCodepoint(codepoints[hiragana_end - 1])) {
    return;
  }

  // The two-mora case particles から・より・まで can otherwise look like a
  // kanji verb stem ending in ら/り.  They are grammatical boundaries, so
  // never use them as the renyokei portion of a deverbal 手/場 noun.
  const std::string hiragana_portion = extractSubstring(codepoints, kanji_end, hiragana_end);
  if (utf8::equalsAny(hiragana_portion, {"から", "より", "まで"})) {
    return;
  }
  if (hiragana_end > kanji_end && !grammar::isIRowCodepoint(codepoints[hiragana_end - 1]) &&
      !grammar::isERowCodepoint(codepoints[hiragana_end - 1])) {
    return;
  }

  // Check for suffix kanji: 物, 方, 所, 手, 場
  if (hiragana_end >= codepoints.size()) {
    return;
  }

  char32_t suffix_char = codepoints[hiragana_end];
  bool is_mono_suffix = (suffix_char == U'物');
  bool is_kata_suffix = (suffix_char == U'方');
  bool is_tokoro_suffix = (suffix_char == U'所');
  bool is_me_suffix = (suffix_char == U'目');  // 割れ目, 切れ目, 裂け目
  bool is_te_suffix = (suffix_char == U'手');  // 読み手, 書き手, 受け手
  bool is_ba_suffix = (suffix_char == U'場');  // 売り場, 買い場

  if (!is_mono_suffix && !is_kata_suffix && !is_tokoro_suffix && !is_me_suffix && !is_te_suffix && !is_ba_suffix) {
    return;
  }

  // One-mora -い can be a godan-wa continuative (言い方), but an attested
  // i-adjective such as 古い/高い must retain its attributive boundary before
  // every ordinary suffix-homograph noun (古い物, 高い所, 高い方). Longer -い
  // adjective tails were rejected above; consult the dictionary here for the
  // ambiguous one-mora case.
  if (hiragana_end == kanji_end + 1 && codepoints[hiragana_end - 1] == U'い') {
    const std::string potential_adjective = extractSubstring(codepoints, start_pos, hiragana_end);
    if (verb_helpers::isAdjectiveInDictionary(&dict_manager, potential_adjective)) {
      return;
    }
  }

  // We need at least some hiragana between kanji and suffix (verb renyokei ending)
  // Exception: single kanji + suffix is allowed for some patterns
  if (hiragana_end == kanji_end && kanji_end - start_pos < 2) {
    return;  // Too short without hiragana
  }

  // 手 and 場 form productive deverbal nouns only after a dictionary-backed
  // continuative verb form. Verify the reconstructed terminal form before
  // adding the joined search unit, so a coincidental kanji+hira sequence (for
  // example an adjective stem) cannot be absorbed merely because it is
  // followed by either suffix.
  if (is_te_suffix || is_ba_suffix) {
    if (hiragana_end == kanji_end) {
      return;
    }
    const std::string renyokei = extractSubstring(codepoints, start_pos, hiragana_end);
    const bool has_verb_reading = dict_manager.lookupExact(renyokei, core::PartOfSpeech::Verb) != nullptr;
    const bool is_closed_modifier = dict_manager.lookupExact(renyokei, core::PartOfSpeech::Determiner) != nullptr ||
                                    dict_manager.lookupExact(renyokei, core::PartOfSpeech::Adjective) != nullptr;
    if (is_closed_modifier && !has_verb_reading) {
      return;
    }
    const char32_t final_kana = codepoints[hiragana_end - 1];
    const std::string_view godan_ending = grammar::godanBaseSuffixFromIRow(final_kana);
    if (!godan_ending.empty()) {
      const std::string base_form =
          renyokei.substr(0, renyokei.size() - core::kJapaneseCharBytes) + std::string(godan_ending);
      if (dict_manager.lookupExact(base_form, core::PartOfSpeech::Verb) == nullptr) {
        return;
      }
    } else {
      // A kanji+e-row surface can be either an Ichidan continuative form
      // (受け→受ける) or an i-adjective conditional fragment (高け→高い).
      // The latter cannot form a deverbal 手/場 compound, so reject it when
      // the reconstructed i-adjective is attested; otherwise retain the
      // productive Ichidan reading.
      const std::string adjective_base = renyokei.substr(0, renyokei.size() - core::kJapaneseCharBytes) + "い";
      if (verb_helpers::isAdjectiveInDictionary(&dict_manager, adjective_base)) {
        return;
      }
    }
  }

  // Build the compound noun surface
  size_t end_pos = hiragana_end + 1;  // Include suffix
  size_t start_byte = byteOffsetAt(byte_offsets, start_pos);
  size_t end_byte = byteOffsetAt(byte_offsets, end_pos);

  std::string surface(text.substr(start_byte, end_byte - start_byte));

  // Calculate cost with bonus for compound noun pattern
  float base_cost = scorer.posPrior(core::PartOfSpeech::Noun);
  float final_cost = base_cost + candidate::kVerbSuffixNounJoinBonus;

  uint8_t flags = core::LatticeEdge::kFromDictionary;

  lattice.addEdge(surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(end_pos), core::PartOfSpeech::Noun,
                  final_cost, flags, surface);  // lemma = surface for compound nouns
}

}  // namespace suzume::analysis
