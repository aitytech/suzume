#include <cmath>

#include "analysis/bigram_table.h"
#include "analysis/category_cost.h"
#include "analysis/scorer.h"
#include "analysis/scorer_constants.h"
#include "analysis/verb_candidates_helpers.h"
#include "core/debug.h"
#include "core/kana_constants.h"
#include "core/types.h"
#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "grammar/honorific_verbs.h"
#include "normalize/utf8.h"

#ifdef SUZUME_DEBUG_INFO
using suzume::core::CandidateOrigin;
#endif
namespace cost = suzume::analysis::bigram_cost;
namespace sc = suzume::analysis::scorer;

// Surface-based adjustments use cost:: namespace directly from bigram_cost.
// See bigram_table.h and scorer_constants.h for constant values.

namespace suzume::analysis {

namespace {

/// Length-scaled bonus helper: base - per_char * (char_len - min_len).
float lengthScaledBonus(float base, size_t char_len, size_t min_len, float per_char) {
  return base - static_cast<float>(char_len > min_len ? char_len - min_len : 0) * per_char;
}

/// Dictionary bonuses for i-adjectives (hiragana, kanji+い, kanji+okurigana).
float computeAdjectiveDictBonus(const core::LatticeEdge& edge) {
  float bonus{};

  // Bonus for hiragana i-adjectives from dictionary
  // Prevents misanalysis as verb+たい (e.g., つめたい → つめ+たい)
  // or as adverb+verb+aux (e.g., はなはだしい → はなはだ+し+い)
  // Longer adjectives get stronger bonus to beat split paths
  // Exclude AdjStem (語幹) as it's not a complete i-adjective
  // Exclude conditional forms ending in ければ (should split: よければ → よけれ + ば)
  if (edge.fromDictionary() && edge.pos == core::PartOfSpeech::Adjective &&
      edge.extended_pos != core::ExtendedPOS::AdjStem && grammar::isPureHiragana(edge.surface) &&
      !utf8::endsWith(edge.surface, "ければ") &&
      // Exclude ない/なく/なかっ - has auxiliary counterpart, context-dependent
      // Exclude そう - has auxiliary counterpart (様態), context-dependent
      edge.surface != "ない" && edge.surface != "なく" && edge.surface != "なかっ" && edge.surface != "そう") {
    size_t char_len = suzume::normalize::utf8Length(edge.surface);
    // Base bonus -2.5, plus 0.5 per character beyond 3
    bonus += lengthScaledBonus(sc::kBonusHiraganaAdjBase, char_len, 3, sc::kBonusHiraganaAdjPerChar);
  }

  // Bonus for kanji+い i-adjectives from dictionary
  // Prevents misanalysis as godan-wa verb (e.g., 暑い → 暑い(VERB wa-row renyokei))
  // Kanji i-adjectives are common (暑い, 寒い, 熱い, 高い, 安い, etc.)
  // The godan-wa verb candidate often beats the adjective due to connection bonuses
  // Surface pattern: 1 kanji + い (2 chars total)
  if (edge.fromDictionary() && edge.pos == core::PartOfSpeech::Adjective &&
      edge.extended_pos != core::ExtendedPOS::AdjStem &&
      edge.surface.size() == core::kTwoJapaneseCharBytes &&  // 2 chars (1 kanji + い) = 6 bytes
      utf8::endsWith(edge.surface, "い") && grammar::isAllKanji(edge.surface.substr(0, 3))) {  // First char is kanji
    bonus += cost::kModerateBonus;  // -0.5 to beat godan-wa verb candidate
  }

  // Bonus for kanji+okurigana i-adjectives from dictionary (情けない, etc.)
  // These compete with verb renyokei + ない split paths that get strong
  // VERB_連用→AUX_否定 connection bonus (-0.8).
  // Pattern: kanji-containing, 4+ chars, ending in い, from dictionary
  if (edge.fromDictionary() && edge.pos == core::PartOfSpeech::Adjective &&
      edge.extended_pos != core::ExtendedPOS::AdjStem && grammar::containsKanji(edge.surface) &&
      edge.surface.size() >= 4 * core::kJapaneseCharBytes && utf8::endsWith(edge.surface, "い")) {
    size_t char_len = suzume::normalize::utf8Length(edge.surface);
    bonus += lengthScaledBonus(sc::kBonusKanjiOkuriganaAdjBase, char_len, 4, sc::kBonusKanjiOkuriganaAdjPerChar);
  }

  return bonus;
}

/// Penalties for spurious non-dictionary verb renyokei/stem candidates.
float computeSpuriousVerbPenalty(const core::LatticeEdge& edge) {
  float penalty{};

  // Penalty for spurious kanji+hiragana verb renyokei not in dictionary
  // These are often false positives like 学生み (学生みる doesn't exist)
  // Prevents misanalysis like 学生みたい → 学生み+たい
  // Only apply to surfaces with 2+ kanji (e.g., 学生み) to avoid penalizing
  // legitimate verb renyokei like 行き, 読み, 書き
  if (!edge.fromDictionary() && edge.pos == core::PartOfSpeech::Verb &&
      edge.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      edge.surface.length() >= core::kThreeJapaneseCharBytes) {  // ≥3 chars (at least 2 kanji + 1 hiragana)
    // Count kanji characters
    size_t kanji_count = 0;
    for (char32_t cp : suzume::normalize::utf8::decode(edge.surface)) {
      if (suzume::normalize::isKanjiCodepoint(cp)) {
        ++kanji_count;
      }
    }
    if (kanji_count >= 2) {
      penalty += sc::kPenaltySpuriousVerbRenyokei;
    }
  }

  // Penalty for pure-hiragana hatsuonbin (撥音便) verb forms not in dictionary
  // E.g., "おさん" as 撥音便 of "おさむ" is rare and likely mis-segmentation
  // Valid hatsuonbin usually has kanji stem (読ん, 飲ん, 呼ん)
  // This helps prevent が+おさん misanalysis (should be がお+さん)
  // Exception: short hiragana verbs (2-4 chars like もらっ, あげっ) get reduced penalty
  // as they are more likely to be legitimate verbs written in hiragana
  // Exception: an onbin edge whose base lemma was dictionary-verified during
  // candidate generation (LemmaVerified) is a genuine verb form and is exempt.
  if (!edge.lemmaVerified() && edge.pos == core::PartOfSpeech::Verb &&
      edge.extended_pos == core::ExtendedPOS::VerbOnbinkei && grammar::isPureHiragana(edge.surface) &&
      edge.surface.size() >= core::kTwoJapaneseCharBytes) {  // 2+ chars (avoid single-char like ん)
    // Reduced penalty for short forms (2-4 chars = 6-12 bytes)
    // to allow common hiragana verbs like もらっ, あげっ to compete
    penalty += (edge.surface.size() <= core::kFourJapaneseCharBytes) ? sc::kPenaltyHatsuonbinShort
                                                                     : sc::kPenaltyHatsuonbinLong;
  }

  // An unverified kanji 音便 candidate is not reliable enough to absorb a
  // nominal prefix. Genuine forms retain their dictionary-verified lemma and
  // therefore remain exempt (読んだ, 閉まった).
  if (!edge.lemmaVerified() && edge.pos == core::PartOfSpeech::Verb &&
      edge.extended_pos == core::ExtendedPOS::VerbOnbinkei && grammar::containsKanji(edge.surface)) {
    penalty += sc::kPenaltySpuriousKanjiOnbin;
  }

  // Penalty for pure-hiragana verb forms containing "さん" pattern
  // E.g., "おさんで" as te-form of "おさむ" is likely name+さん+で misanalysis
  // E.g., "さんで" as te-form of "さむ" is likely さん+で misanalysis
  // Patterns: xさん, xさんで, さんで where x is short hiragana (likely name)
  // This complements the hatsuonbin penalty above for other verb forms
  if (!edge.fromDictionary() && edge.pos == core::PartOfSpeech::Verb && grammar::isPureHiragana(edge.surface) &&
      (utf8::contains(edge.surface, "さん"))) {
    size_t san_pos = edge.surface.find("さん");
    if (san_pos != std::string::npos) {
      // Penalize if:
      // 1. さん appears after 0-2 hiragana chars (likely name+さん or just さん)
      // 2. The verb form is short enough to be a misanalysis
      if (san_pos <= core::kTwoJapaneseCharBytes &&
          edge.surface.size() <= core::kFiveJapaneseCharBytes) {  // 0-2 chars before, up to 5 total
        penalty += sc::kPenaltySanPatternVerb;
      }
    }
  }

  // Penalty for pure-hiragana ichidan verb renyokei starting with に
  // E.g., "につけ" as renyokei of "につける" is spurious
  // Should be に|つけ (particle + verb), not につけ (verb)
  // Valid verbs like "につける" don't exist; this is a mis-analysis
  if (!edge.fromDictionary() && edge.pos == core::PartOfSpeech::Verb &&
      edge.extended_pos == core::ExtendedPOS::VerbRenyokei && grammar::isPureHiragana(edge.surface) &&
      utf8::startsWith(edge.surface, "に") && edge.surface.size() >= core::kTwoJapaneseCharBytes &&
      edge.surface.size() <= core::kFourJapaneseCharBytes) {  // 2-4 chars
    penalty += sc::kPenaltyNiPrefixVerb;
  }

  // Penalty for very long pure-hiragana verb candidates not in dictionary
  // E.g., "ございませんでし" as verb renyokei is spurious
  // Should be ござい|ませ|ん|でし (aux chain), not ございませんでし (verb)
  // Valid long verbs typically have kanji stems
  if (!edge.fromDictionary() && edge.pos == core::PartOfSpeech::Verb && grammar::isPureHiragana(edge.surface) &&
      edge.surface.size() >= core::kSixJapaneseCharBytes) {  // 6+ hiragana chars (6*3=18 bytes)
    penalty += sc::kPenaltyVeryLongHiraganaVerb;
  }

  // Penalty for 5-char pure-hiragana verb renyokei not in dictionary
  // E.g., "つるつるし" as godan-sa renyokei — should be つるつる(ADV) + し(する)
  // Only renyokei: base forms like "づけられる" (from づける) are legitimate
  if (!edge.fromDictionary() && edge.pos == core::PartOfSpeech::Verb &&
      edge.extended_pos == core::ExtendedPOS::VerbRenyokei && grammar::isPureHiragana(edge.surface) &&
      edge.surface.size() >= core::kFiveJapaneseCharBytes) {  // 5+ hiragana chars (5*3=15 bytes)
    penalty += sc::kPenaltyVeryLongHiraganaVerb;
  }

  // Penalty for kanji+hiragana verb renyokei ending in いし pattern
  // E.g., "願いし" as renyokei of "願いす" is spurious
  // Should be 願い + し (願う renyokei + する renyokei)
  // The いし ending suggests the analyzer incorrectly merged a renyokei い
  // with the following し (suru renyokei)
  // Valid pattern: 漢字 + い (renyokei) vs invalid: 漢字 + いし (fake verb base 漢字いす)
  if (!edge.fromDictionary() && edge.pos == core::PartOfSpeech::Verb &&
      edge.extended_pos == core::ExtendedPOS::VerbRenyokei && grammar::containsKanji(edge.surface) &&
      utf8::endsWith(edge.surface, "いし") && edge.surface.size() >= 9) {  // At least 1 kanji + いし (3 + 6 bytes)
    penalty += sc::kPenaltyIshiVerbRenyokei;
  }

  return penalty;
}

/// Penalties for spurious verb candidates identified by their inflected endings (そう/てき/まし/てい/te/ta).
float computeVerbEndingPenalty(const core::LatticeEdge& edge) {
  float penalty{};

  // Penalty for pure-hiragana verb candidates ending with そう
  // E.g., "なさそう" should be な + さ + そう, not なさそう (verb)
  // The そう ending is typically from そう (様態 auxiliary), not a verb stem
  // Valid verbs ending in そう are rare and usually have kanji stems
  if (!edge.fromDictionary() && edge.pos == core::PartOfSpeech::Verb && grammar::isPureHiragana(edge.surface) &&
      utf8::endsWith(edge.surface, "そう") &&
      edge.surface.size() >= core::kThreeJapaneseCharBytes) {  // 3+ chars (at least xそう)
    penalty += cost::kRare;
  }

  // Penalty for pure-hiragana verb candidates ending with てき
  // E.g., "なってき" should be なっ + て + き (来る), not なってき (verb)
  // The てき ending is almost always て (particle) + き/こ (来る auxiliary)
  // Exception: できる is valid but is in dictionary
  if (!edge.fromDictionary() && edge.pos == core::PartOfSpeech::Verb && grammar::isPureHiragana(edge.surface) &&
      utf8::endsWith(edge.surface, "てき") && edge.surface.size() >= 9) {  // 3+ chars (at least xてき)
    penalty += cost::kVeryRare;
  }

  // Penalty for pure-hiragana verb candidates ending with まし
  // E.g., "しまし" should be し + まし (masu renyokei), not しまし (verb)
  // The まし ending is almost always ます (polite aux) renyokei form
  if (!edge.fromDictionary() && edge.pos == core::PartOfSpeech::Verb && grammar::isPureHiragana(edge.surface) &&
      utf8::endsWith(edge.surface, "まし") && edge.surface.size() >= 9) {  // 3+ chars (at least xまし)
    penalty += cost::kVeryRare;
  }

  // Penalty for pure-hiragana verb candidates ending with てい
  // E.g., "させてい" should be させ + て + い (progressive), not させてい (verb)
  // The てい ending is almost always て (particle) + い (いる renyokei)
  if (!edge.fromDictionary() && edge.pos == core::PartOfSpeech::Verb && grammar::isPureHiragana(edge.surface) &&
      utf8::endsWith(edge.surface, "てい") && edge.surface.size() >= 9) {  // 3+ chars (at least xてい)
    penalty += cost::kVeryRare;
  }

  // Penalty for pure-hiragana verb te-form candidates not in dictionary
  // E.g., "もらって" should be もらっ + て, not もらって (verb te-form)
  // E.g., "ねて" should be ね + て, not ねて (verb te-form)
  // MeCab splits pure-hiragana verb te-forms into verb + て particle
  // Exception: keep short forms (2 chars like して, きて) as they're common L1 entries
  if (!edge.fromDictionary() && edge.pos == core::PartOfSpeech::Verb &&
      edge.extended_pos == core::ExtendedPOS::VerbTeForm && grammar::isPureHiragana(edge.surface) &&
      edge.surface.size() >= 9) {  // 3+ chars (9 bytes) - allows して, きて
    penalty += cost::kVeryRare;
  }

  // Penalty for kanji+hiragana verb te-form candidates (e.g., 押して, 泳いで)
  // MeCab splits these as 押し + て, 泳い + で
  // Single kanji + て/で pattern is most common: 押して, 待って, 書いて, etc.
  // This penalty encourages verb_stem + て particle split
  // Apply to both dict and non-dict candidates as some come from auto-generation
  if (edge.pos == core::PartOfSpeech::Verb && edge.extended_pos == core::ExtendedPOS::VerbTeForm &&
      grammar::containsKanji(edge.surface) &&
      (utf8::endsWith(edge.surface, "て") || utf8::endsWith(edge.surface, "で")) &&
      edge.surface.size() <= core::kFourJapaneseCharBytes) {  // Short te-forms (1-2 kanji + て/で)
    penalty += cost::kSevere;                                 // Very strong penalty to overcome negative costs
  }

  // Penalty for kanji+hiragana verb ta-form candidates (e.g., 書いた, 泳いだ)
  // MeCab splits these as 書い + た, 泳い + だ
  // Single kanji + いた/いだ pattern is most common (godan i-onbin + ta/da)
  // This penalty encourages verb_onbin + た/だ auxiliary split
  if (!edge.fromDictionary() && edge.pos == core::PartOfSpeech::Verb &&
      edge.extended_pos == core::ExtendedPOS::VerbTaForm && grammar::containsKanji(edge.surface) &&
      (utf8::endsWith(edge.surface, "いた") || utf8::endsWith(edge.surface, "いだ")) &&
      edge.surface.size() <= 12) {  // Short ta-forms (1-2 kanji + いた/いだ)
    penalty += cost::kSevere;       // Strong penalty to prefer onbin + auxiliary split
  }

  return penalty;
}

/// Dictionary bonuses for long nouns, hiragana suffixes, and short hiragana verbs.
float computeNounSuffixVerbDictBonus(const core::LatticeEdge& edge) {
  float bonus{};

  // Bonus for hiragana+kanji mixed nouns from dictionary (e.g., なし崩し, みじん切り, お茶)
  // These are idiomatic expressions that should not be split
  // E.g., なし崩し should not be split as な+し+崩し (AUX+PARTICLE+NOUN)
  // Requires 3+ chars with both hiragana and kanji
  if (edge.fromDictionary() && edge.pos == core::PartOfSpeech::Noun) {
    size_t char_len = suzume::normalize::utf8Length(edge.surface);
    if (char_len >= 3 && grammar::isMixedHiraganaKanji(edge.surface)) {
      if (char_len >= 4) {
        // Length-scaled bonus for long mixed nouns (お兄ちゃん, お父さん, なし崩し)
        bonus += lengthScaledBonus(sc::kBonusLongMixedNounBase, char_len, 4, -sc::kBonusLongMixedNounPerChar);
      } else {
        bonus += sc::kBonusMixedNoun;
      }
    }
  }

  // Bonus for long all-kanji nouns from dictionary (4+ chars)
  // Split path gets dict+dict connection bonus (-0.5) and split_candidates
  // both-in-dict bonus (-0.2), making it -0.7 cheaper than 1-token path.
  // Length-scaled bonus ensures registered compounds beat split paths.
  if (edge.fromDictionary() && edge.pos == core::PartOfSpeech::Noun && grammar::isAllKanji(edge.surface)) {
    size_t char_len = suzume::normalize::utf8Length(edge.surface);
    if (char_len >= 4) {
      bonus += lengthScaledBonus(sc::kBonusLongKanjiNounBase, char_len, 4, -sc::kBonusLongKanjiNounPerChar);
    }
  }

  // Bonus for multi-char hiragana suffixes from dictionary (e.g., まみれ, だらけ, ごと)
  // These are L1 closed-class morphemes that should beat false verb candidates
  // E.g., 血まみれ should be 血+まみれ(SUFFIX), not 血まみ(VERB)+れ(AUX)
  if (edge.fromDictionary() && edge.pos == core::PartOfSpeech::Suffix && grammar::isPureHiragana(edge.surface) &&
      edge.surface.size() >= core::kThreeJapaneseCharBytes) {  // 3+ chars (9+ bytes)
    bonus += sc::kBonusLongSuffix;
  }

  // Bonus for short hiragana verbs from dictionary (e.g., なる, ある, いる, する, かかる)
  // These compete with L1 function word entries (DET, AUX) which have lower category costs.
  // Dictionary registration indicates standalone verb usage should take precedence.
  if (edge.fromDictionary() && edge.pos == core::PartOfSpeech::Verb && grammar::isPureHiragana(edge.surface) &&
      edge.surface.length() <= core::kThreeJapaneseCharBytes) {  // ≤3 chars
    bonus += sc::kBonusShortHiraganaVerb;
  }

  return bonus;
}

/// Dictionary bonuses for みたい auxiliary and prefixed entries (negation prefix, honorific 御).
float computeMitaiAndPrefixDictBonus(const core::LatticeEdge& edge) {
  float bonus{};

  // Bonus for みたい (conjecture auxiliary) from dictionary
  // Works together with bigram bonuses and spurious verb penalty
  if (edge.fromDictionary() && edge.extended_pos == core::ExtendedPOS::AuxConjectureMitai) {
    bonus += sc::kBonusMitaiDict;
  }

  // Bonus for dictionary entries starting with negation prefixes (非/不/無/未)
  // E.g., 不可能, 非常, 無理, 無限, 無鉄砲 - these are single lexical items
  // Without this bonus, PREFIX+NOUN split path wins due to strong connection bonus (-2)
  // Dictionary entries should take precedence over compositional analysis
  // Scales with length so longer entries (無理やり) beat shorter ones (無理)
  if (edge.fromDictionary() &&
      (edge.pos == core::PartOfSpeech::Adjective || edge.pos == core::PartOfSpeech::Noun ||
       edge.pos == core::PartOfSpeech::Adverb) &&
      edge.surface.size() >= 6 &&  // At least 2 chars (prefix + something)
      sc::startsWithNegationPrefix(edge.surface)) {
    size_t char_len = suzume::normalize::utf8Length(edge.surface);
    // Base -3.0 for 2-char entries, -0.5 per additional char
    bonus += lengthScaledBonus(sc::kBonusNegationPrefixBase, char_len, 2, sc::kBonusNegationPrefixPerChar);
  }

  // Bonus for dictionary NOUN entries starting with honorific prefix kanji 御
  // E.g., 御者, 御所, 御曹司 are lexicalized words where 御 is part of the noun,
  // not a separable prefix. Without this bonus, the strong PREFIX→NOUN bigram
  // bonus (-1.3) makes 御(PREFIX) + X split path cheaper than the dict entry.
  if (edge.fromDictionary() && edge.pos == core::PartOfSpeech::Noun && edge.surface.size() >= 6 &&
      edge.surface.compare(0, 3, "御") == 0) {
    size_t char_len = suzume::normalize::utf8Length(edge.surface);
    bonus += lengthScaledBonus(sc::kBonusHonorificGoNounBase, char_len, 2, sc::kBonusHonorificGoNounPerChar);
  }

  return bonus;
}

/// Dictionary bonuses for fixed expressions: interjections, conjunctions, compound particles.
float computeFixedExpressionDictBonus(const core::LatticeEdge& edge) {
  float bonus{};

  // Bonus for hiragana interjections/greetings from dictionary
  // Prevents misanalysis like さようなら → さ+よう+なら (volitional pattern)
  // or ありがとう → あり+が+とう (verb + particle + noun pattern)
  // These are fixed expressions that should remain as single tokens
  // Longer interjections get stronger bonus to beat common split patterns
  // Note: applies to both Interjection (L1/L2) and Other (legacy)
  if (edge.fromDictionary() &&
      (edge.pos == core::PartOfSpeech::Interjection || edge.pos == core::PartOfSpeech::Other) &&
      grammar::isPureHiragana(edge.surface)) {
    size_t char_len = suzume::normalize::utf8Length(edge.surface);
    // Stronger bonus for longer interjections (common greetings are 4-5 chars)
    float interjection_bonus = (char_len <= 2)   ? sc::kBonusHiraganaInterjectionShort
                               : (char_len <= 3) ? sc::kBonusHiraganaInterjectionMid
                                                 : lengthScaledBonus(sc::kBonusHiraganaInterjectionBase, char_len, 3,
                                                                     sc::kBonusHiraganaInterjectionPerChar);
    bonus += interjection_bonus;
  }

  // Bonus for non-hiragana interjections from dictionary (お疲れ様, etc.)
  // Mixed script interjections also need bonus to beat split paths
  // E.g., お疲れ様 should not split as お+疲れ+様
  if (edge.fromDictionary() && edge.pos == core::PartOfSpeech::Interjection && !grammar::isPureHiragana(edge.surface)) {
    size_t char_len = suzume::normalize::utf8Length(edge.surface);
    // Moderate bonus for mixed interjections
    bonus +=
        lengthScaledBonus(sc::kBonusNonHiraganaInterjectionBase, char_len, 3, sc::kBonusNonHiraganaInterjectionPerChar);
  }

  // Bonus for hiragana conjunctions from dictionary (たとえば, それから, etc.)
  // Prevents misanalysis like たとえば → たとえ+ば (adverb + particle)
  // These are fixed expressions that should remain as single tokens
  // Needs to beat adverb bonus path, so use stronger bonus
  // Exclude でも - it has ambiguous interpretation (conjunction vs 副助詞)
  // and context-dependent splitting (彼女でもない → 彼女+で+も+ない)
  if (edge.fromDictionary() && edge.pos == core::PartOfSpeech::Conjunction && grammar::isPureHiragana(edge.surface) &&
      edge.surface != "でも") {
    size_t char_len = suzume::normalize::utf8Length(edge.surface);
    // Stronger bonus for conjunctions to beat adverb+particle splits
    // Adverb 3-char gets -3.0, plus particle gets bonus, so we need > -3.5
    float conjunction_bonus = (char_len <= 3) ? sc::kBonusHiraganaConjunctionShort
                                              : lengthScaledBonus(sc::kBonusHiraganaConjunctionBase, char_len, 3,
                                                                  sc::kBonusHiraganaConjunctionPerChar);
    bonus += conjunction_bonus;
  }

  // Bonus for compound particles from dictionary (について, によって, として, etc.)
  // These are multi-character particles that should not be split into verb+て patterns
  // Helps compound particles compete with high-bonus splits like し+て (-1 connection bonus)
  // Also applies to kanji-containing particles (において, に関して, に際して, に対して)
  if (edge.fromDictionary() && edge.pos == core::PartOfSpeech::Particle) {
    size_t char_len = suzume::normalize::utf8Length(edge.surface);
    // Compound particles are 3+ chars (について, によって, として, において, etc.)
    // Give strong bonus to beat verb+て split paths
    if (char_len >= 3) {
      bonus += sc::kBonusCompoundParticle;
    }
  }

  return bonus;
}

/// Dictionary bonuses for kanji determiners and long hiragana nouns.
float computeDeterminerNounDictBonus(const core::LatticeEdge& edge) {
  float bonus{};

  // Bonus for dictionary determiners/adnominals containing kanji (小さな, 大きな, etc.)
  // These compete with ADJ_語幹 + suffix split paths which get connection bonuses.
  // E.g., 小さな(DET, cost=0.4) vs 小(ADJ_語幹, -0.68) + さ(SUFFIX, 0) + な(AUX)
  // Only apply to kanji-containing entries to avoid boosting pure hiragana determiners
  // like といった which should remain as particles
  if (edge.fromDictionary() && edge.pos == core::PartOfSpeech::Determiner && grammar::containsKanji(edge.surface)) {
    size_t char_len = suzume::normalize::utf8Length(edge.surface);
    if (char_len >= 3) {
      bonus += lengthScaledBonus(sc::kBonusKanjiDeterminerBase, char_len, 3, sc::kBonusKanjiDeterminerPerChar);
    }
  }

  // Bonus for longer hiragana nouns from dictionary (ふともも, ひとつ, etc.)
  // These compete with adverb+noun split paths that get adverb bonus + connection bonus.
  // E.g., ふともも(NOUN, 0.5) vs ふと(ADV, -0.5) + もも(NOUN, 0.5, conn=-0.5) = -0.5
  // Without bonus, the split path wins even though the longer dict match is better.
  if (edge.fromDictionary() && edge.pos == core::PartOfSpeech::Noun && grammar::isPureHiragana(edge.surface)) {
    size_t char_len = suzume::normalize::utf8Length(edge.surface);
    if (char_len >= 4) {
      bonus += sc::kBonusLongHiraganaNoun;
    }
  }

  return bonus;
}

/// Dictionary bonuses for adverbs (pure hiragana and non-hiragana).
float computeAdverbDictBonus(const core::LatticeEdge& edge) {
  float bonus{};

  // Bonus for hiragana adverbs from dictionary
  // Prevents misanalysis as verb+ん (e.g., たくさん → たくさ+ん)
  // and compound splits (e.g., どうして → どう+し+て)
  // Longer adverbs get stronger bonus to beat split paths
  // Short adverbs (2 chars) get weaker bonus to avoid false matches in patterns
  // like かもしれない (should not be か+もし+れない)
  if (edge.fromDictionary() && edge.pos == core::PartOfSpeech::Adverb && grammar::isPureHiragana(edge.surface)) {
    size_t char_len = suzume::normalize::utf8Length(edge.surface);
    // Short adverbs (2 chars) get weaker bonus
    // Longer adverbs get stronger bonus (0.5 per character beyond 2)
    float adverb_bonus =
        (char_len <= 2) ? sc::kBonusHiraganaAdverbShort
                        : lengthScaledBonus(sc::kBonusHiraganaAdverbBase, char_len, 2, sc::kBonusHiraganaAdverbPerChar);
    bonus += adverb_bonus;
  }

  // Bonus for non-hiragana adverbs from dictionary (初めて, 大して, etc.)
  // These contain kanji so the pure-hiragana adverb bonus above doesn't apply.
  // They compete with verb renyokei + て split paths which get connection bonuses.
  // E.g., 初めて(ADV, cost=0.5) vs 初め(VERB_連用, -0.13) + て(PART, conn=-0.5)
  if (edge.fromDictionary() && edge.pos == core::PartOfSpeech::Adverb && !grammar::isPureHiragana(edge.surface)) {
    size_t char_len = suzume::normalize::utf8Length(edge.surface);
    if (char_len >= 3) {
      bonus += lengthScaledBonus(sc::kBonusNonHiraganaAdverbBase, char_len, 3, sc::kBonusNonHiraganaAdverbPerChar);
    }
  }

  return bonus;
}

}  // namespace

float Scorer::wordCost(const core::LatticeEdge& edge) const {
  // v0.8: Base cost from ExtendedPOS category
  float category_cost = getCategoryCost(edge.extended_pos);

  // HasCustomCost distinguishes a tuned 0.0 from "unset"; unflagged edges use non-zero edge.cost, else category.
  const bool use_edge_cost = edge.hasCustomCost() || edge.cost != 0.0F;
  float cost = use_edge_cost ? edge.cost : category_cost;

  // User dictionary bonus (still needed for user customization)
  if (edge.fromUserDict()) {
    cost += options_.user_dict_bonus;
  }

  // Dictionary bonuses for i-adjectives (hiragana, kanji+い, kanji+okurigana).
  cost += computeAdjectiveDictBonus(edge);

  // Dictionary bonuses for adverbs (pure hiragana and non-hiragana).
  cost += computeAdverbDictBonus(edge);

  // Dictionary bonuses for kanji determiners and long hiragana nouns.
  cost += computeDeterminerNounDictBonus(edge);

  // Dictionary bonuses for fixed expressions: interjections, conjunctions, compound particles.
  cost += computeFixedExpressionDictBonus(edge);

  // Dictionary bonuses for みたい auxiliary and prefixed entries (negation prefix, honorific 御).
  cost += computeMitaiAndPrefixDictBonus(edge);

  // Dictionary bonuses for long nouns, hiragana suffixes, and short hiragana verbs.
  cost += computeNounSuffixVerbDictBonus(edge);

  // Penalties for spurious non-dictionary verb renyokei/stem candidates.
  cost += computeSpuriousVerbPenalty(edge);

  // Penalties for spurious verb candidates identified by their inflected endings (そう/てき/まし/てい/te/ta).
  cost += computeVerbEndingPenalty(edge);

  // Bonus for compound adjectives from dictionary (e.g., 男らしい, 女らしい)
  // These compete with noun+らしい split which has -1.5 connection bonus.
  // Dictionary registration indicates compound adjective should take precedence.
  // Pattern: kanji stem + hiragana suffix forming an i-adjective
  if (edge.fromDictionary() && edge.pos == core::PartOfSpeech::Adjective &&
      edge.surface.length() >= core::kFourJapaneseCharBytes) {  // ≥4 chars (kanji + ひらがな suffix)
    // Check if surface contains kanji — compound adjective from dictionary
    // Covers both base form (い) and inflected forms (く, かっ, けれ, etc.)
    if (grammar::containsKanji(edge.surface)) {
      // Longer compounds need stronger bonus to beat noun+adj split paths
      // Must overcome NOUN→dict_ADJ surface bonus (-0.5) on the split path
      size_t char_len = suzume::normalize::utf8Length(edge.surface);
      cost += lengthScaledBonus(sc::kBonusCompoundAdjBase, char_len, 4, sc::kBonusCompoundAdjPerChar);
    }
  }

  // Penalty for kanji compound NOUN ending with 中 (chuu/juu suffix)
  // E.g., "一日中" should be split as 一日|中 (noun + suffix)
  // Registered compounds like "世界中" will also split (accepted difference from MeCab)
  // This helps Suffix 中 candidates win over NOUN compounds
  if (!edge.fromDictionary() && edge.pos == core::PartOfSpeech::Noun && utf8::endsWith(edge.surface, "中") &&
      grammar::isAllKanji(edge.surface) && edge.surface.size() >= 6) {  // 2+ kanji (at least N中)
    cost += sc::kPenaltyKanjiChuuCompound;
  }

  // Debug output - show which cost was used and candidate origin (verbose level)
  SUZUME_DEBUG_VERBOSE_BLOCK {
    // Base source type
    const char* source = edge.fromDictionary() ? "dict" : edge.isUnknown() ? "unk" : "infl";
    const char* cost_from = use_edge_cost ? "edge" : "category";

    SUZUME_DEBUG_STREAM << "[WORD] \"" << edge.surface << "\" (" << source;
#ifdef SUZUME_DEBUG_INFO
    // Show detailed origin if available (e.g., "dict:adj_i", "unk:verb_kanji")
    if (edge.origin != CandidateOrigin::Unknown) {
      SUZUME_DEBUG_STREAM << ":" << core::originToString(edge.origin);
    }
    if (!edge.origin_detail.empty()) {
      SUZUME_DEBUG_STREAM << "/" << edge.origin_detail;
    }
#endif
    SUZUME_DEBUG_STREAM << ") cost=" << cost << " (from " << cost_from << ")";
    SUZUME_DEBUG_STREAM << " [cat=" << category_cost;
    if (use_edge_cost) {
      SUZUME_DEBUG_STREAM << " edge=" << edge.cost;
    }
    // Show epos with source indicator
    SUZUME_DEBUG_STREAM << " epos=" << core::extendedPosToString(edge.extended_pos);
    // Check if epos matches default for this POS (indicates derived vs explicit)
    core::ExtendedPOS default_epos = core::posToExtendedPos(edge.pos);
    if (edge.extended_pos == core::ExtendedPOS::Unknown) {
      SUZUME_DEBUG_STREAM << "(!UNKNOWN)";  // Warning: missing mapping
    } else if (edge.extended_pos != default_epos) {
#ifdef SUZUME_DEBUG_INFO
      // Show where the ExtendedPOS was set (e.g., "binary_dict", "l1_dict", "candidate_gen")
      if (!edge.epos_source.empty()) {
        SUZUME_DEBUG_STREAM << "(from:" << edge.epos_source << ")";
      } else {
        SUZUME_DEBUG_STREAM << "(explicit)";
      }
#else
      SUZUME_DEBUG_STREAM << "(explicit)";  // Explicitly set, different from default
#endif
    }
    SUZUME_DEBUG_STREAM << "]";
    // Highlight non-dictionary candidates (potential spurious entries)
    if (!edge.fromDictionary()) {
      SUZUME_DEBUG_STREAM << " [non-dict]";
    }
    SUZUME_DEBUG_STREAM << "\n";
  }
  return cost;
}

}  // namespace suzume::analysis
