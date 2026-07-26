#include <cmath>

#include "analysis/bigram_table.h"
#include "analysis/category_cost.h"
#include "analysis/scorer.h"
#include "analysis/scorer_connection_rules.h"
#include "analysis/scorer_connection_rules_internal.h"
#include "analysis/scorer_constants.h"
#include "analysis/verb_candidates_helpers.h"
#include "core/debug.h"
#include "core/kana_constants.h"
#include "core/types.h"
#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "grammar/honorific_verbs.h"
#include "normalize/char_type.h"
#include "normalize/utf8.h"

namespace cost = suzume::analysis::bigram_cost;
namespace sc = suzume::analysis::scorer;

// Surface-based adjustments use cost:: namespace directly from bigram_cost.
// See bigram_table.h and scorer_constants.h for constant values.

namespace suzume::analysis::connection_rules {

// Surface-based connection rules, extracted from connectionCost for readability.
// Each helper accumulates the `surface_bonus +=` contributions of a thematically
// related group of rules and returns their sum. Helpers are self-contained: they
// recompute any needed locals from prev/next and never read caller state. Because
// every contribution is additive, the order among these helpers does not affect the
// total; call sites are kept at their original positions for readability.

// Short pure-hiragana verb false-split penalties after particles/OTHER, plus
// determiner→noun and case-particle→final-particle guards.
float computeParticleDeterminerBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float bonus{};  // value-init to 0

  // An object marker followed by a continuative verb strongly licenses a
  // predicate (本を買いに行く, 本を読み始める). This left-context evidence
  // offsets the general renyokei-before-case-particle nominalization bias while
  // leaving standalone nominal forms such as 読みを/香りを untouched.
  if (prev.extended_pos == core::ExtendedPOS::ParticleCase && grammar::isAccusativeParticleWoSurface(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::VerbRenyokei && next.fromDictionary()) {
    bonus += cost::kExtraStrongBonus;
  }

  // Penalty for single-char case particle → very short inflected
  // pure-hiragana verb pattern. Two-kana base forms are deliberately exempt:
  // object + きく/やく/けす is a basic feature-derived verb boundary.
  // The risky false splits here are short stems such as が+おさ.
  // Single-char particles: が, を, に, へ, と, で, から, etc.
  // Only penalize very short verbs (2 chars or less) to avoid affecting なくし, etc.
  // Exception: "い" (いる renyokei) has specific bonus rule below for PART_格→い pattern
  // Generated ichidan stems are emitted only after a following inflection has
  // validated the reconstruction. They are therefore not the short
  // unconstrained stems this guard targets (混雑を+さけ+ない/て/た), even
  // though their surfaces are two morae.
  const bool is_validated_ichidan_inflection = (next.origin == core::CandidateOrigin::VerbHiraganaNegativeRenyokei &&
                                                next.extended_pos == core::ExtendedPOS::VerbMizenkei) ||
                                               (next.origin == core::CandidateOrigin::VerbHiraganaInflectedRenyokei &&
                                                next.extended_pos == core::ExtendedPOS::VerbRenyokei);
  if (prev.extended_pos == core::ExtendedPOS::ParticleCase &&
      prev.surface.size() <= core::kJapaneseCharBytes &&  // Single hiragana char (3 bytes in UTF-8)
      next.pos == core::PartOfSpeech::Verb && !next.fromDictionary() && grammar::isPureHiragana(next.surface) &&
      next.surface.size() <= 6 &&  // 2 chars or less (6 bytes in UTF-8)
      next.extended_pos != core::ExtendedPOS::VerbShuushikei &&
      next.surface != "い" &&  // Exclude い - has specific rule
      !is_validated_ichidan_inflection) {
    bonus += cost::kAlmostNever;
  }

  // Penalty for は (topic) → short pure-hiragana verb pattern
  // E.g., は+し in はしがかかる should be はし (noun), not は+し (topic + する連用形)
  // Only applies to は — other topic particles (も, こそ) naturally precede し
  // (何もしない, 誰もしない are common patterns)
  // Exception: い (renyokei of いる) - valid in ずにはいられない pattern
  // Exception: し (renyokei of する) - valid in emphatic negation ありはしない pattern
  if (prev.extended_pos == core::ExtendedPOS::ParticleTopic && prev.surface == "は" &&
      next.pos == core::PartOfSpeech::Verb && grammar::isPureHiragana(next.surface) &&
      next.surface.size() <= 3 &&                       // 1 char only (3 bytes in UTF-8)
      next.surface != "い" &&                           // い+られ is valid (いる potential)
      !grammar::isSuruRenyokeiSurface(next.surface)) {  // し+ない is valid (emphatic negation)
    bonus += cost::kVeryRare;
  }

  // Penalty for pure-hiragana OTHER → single-char VerbRenyokei
  // E.g., ふんど+し should be ふんどし (one word), not noun+する連用形
  // Pure hiragana unknown sequences split before し/き/etc. are usually wrong
  // Does not apply when prev is a known particle/aux (those have specific EPOS)
  if (prev.pos == core::PartOfSpeech::Other && grammar::isPureHiragana(prev.surface) &&
      prev.surface.size() >= 6 &&                                                          // 2+ hiragana chars
      next.extended_pos == core::ExtendedPOS::VerbRenyokei && next.surface.size() <= 3) {  // Single char (し, き, etc.)
    bonus += cost::kUncommon;
  }

  // An unknown hiragana fragment cannot directly introduce an onbin verb.
  // Such a path is an over-segmentation of one inflected word (よろこんで),
  // whereas ordinary adverbial modifiers have their own lexical categories.
  if (prev.pos == core::PartOfSpeech::Other && grammar::isPureHiragana(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::VerbOnbinkei) {
    bonus += cost::kStrong;
  }

  // Penalty for NOUN → single-hiragana OTHER
  // A single hiragana character classified as OTHER after a kanji NOUN is almost
  // always a misparse: the hiragana should be part of a verb (先+生きのこる) or
  // okurigana (読み+残す), not a standalone unknown token
  // E.g., 先生+き(OTHER) should lose to 先+生きのこる
  // Needs a very high penalty to overcome prefix compound bonus advantages
  if (prev.pos == core::PartOfSpeech::Noun && grammar::containsKanji(prev.surface) &&
      next.pos == core::PartOfSpeech::Other && next.surface.size() == 3 &&  // Single char = 3 bytes UTF-8
      grammar::isPureHiragana(next.surface)) {
    bonus += cost::kAlmostNever;
  }

  // Penalty for Noun → かかる(Determiner) with no intervening particle
  // The classical determiner 斯かる always needs a preceding particle, topic
  // marker, or clause boundary (は+かかる, として+かかる, かかる事態 at clause
  // start); it never directly follows a bare noun. Direct noun adjacency
  // (3週間かかる, 5分かかる) is the intransitive verb 掛かる/罹る taking a
  // duration/quantity noun without a particle. The ParticleCase→Determiner
  // bigram penalty above only covers noun+particle+かかる (壁にかかる); this
  // covers noun+かかる directly (3週間かかる).
  if (prev.pos == core::PartOfSpeech::Noun && grammar::isDurationPredicateKakaru(next.surface) &&
      next.extended_pos == core::ExtendedPOS::Determiner) {
    bonus += cost::kAlmostNever;
  }

  // A duration closing suffix and a degree particle both introduce a
  // predicate in a duration expression (三時間かかる、三時間ほどかかる).
  // The homographic determiner cannot fill that predicate slot.
  if ((prev.extended_pos == core::ExtendedPOS::Suffix && prev.surface == "間") ||
      prev.extended_pos == core::ExtendedPOS::ParticleAdverbial) {
    if (grammar::isDurationPredicateKakaru(next.surface) && next.extended_pos == core::ExtendedPOS::Determiner) {
      bonus += cost::kAlmostNever;
    }
  }

  // The quotative determiner cannot introduce the comparative particle. When
  // the particle is also emitted as an unknown fallback, keep the productive
  // quotation sequence と+いう+より available.
  if (prev.extended_pos == core::ExtendedPOS::DeterminerQuotative && next.surface == "より" &&
      next.pos == core::PartOfSpeech::Other) {
    bonus += cost::kAlmostNever;
  }

  // Penalty for DET → non-dict single-kanji NOUN
  // The DET→NOUN bigram bonus (-2.5) is too strong for unknown single-kanji tokens,
  // causing splits like こんな+伸+びる instead of こんな+伸びる
  // Valid DET+NOUN patterns (こんな+事, あんな+人) use dict nouns or multi-char nouns
  if (prev.pos == core::PartOfSpeech::Determiner && next.pos == core::PartOfSpeech::Noun && !next.fromDictionary() &&
      grammar::containsKanji(next.surface) && suzume::normalize::utf8Length(next.surface) == 1) {
    bonus += cost::kStrong;
  }

  // Penalty for DET → non-dict kanji+hiragana NOUN (nominalized verb pattern)
  // The DET→NOUN bonus (-2.5) makes heuristic candidates like "先生き" (NOUN)
  // too attractive, preventing correct splits like 先+生きのこる
  // Valid DET+NOUN uses dict nouns or pure-kanji nouns; nominalized forms
  // (kanji + 1 trailing hiragana, e.g., 先生き, 出来事み) are rare after DET
  if (prev.pos == core::PartOfSpeech::Determiner && next.pos == core::PartOfSpeech::Noun && !next.fromDictionary()) {
    size_t char_len = suzume::normalize::utf8Length(next.surface);
    if (char_len >= 3 && grammar::containsKanji(next.surface) && !grammar::isAllKanji(next.surface)) {
      // Check whether the final two characters are kanji + hiragana.
      const std::string_view before_last = utf8::dropLastChar(next.surface);
      if (kana::isHiraganaCodepoint(utf8::decodeLastChar(next.surface)) && !before_last.empty() &&
          normalize::isKanjiCodepoint(utf8::decodeLastChar(before_last))) {
        bonus += cost::kAlmostNever;
      }
    }
  }

  return bonus;
}

// Prefix/adverb→short-verb, symbol→particle/aux/furigana, and で+も copula rules.
float computePrefixSymbolBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float bonus{};  // value-init to 0

  // Penalty for PREFIX → short pure-hiragana verb pattern
  // E.g., お+い in において should not happen (お is prefix, い is not a verb here)
  // Valid お+verb patterns: お待ち, お願い (longer, often with kanji)
  // A closed-class honorific verb is the only dictionary-confirmed exception
  // to this short-prefix guard (お+はす). Other one- or two-mora hypotheses,
  // including the L1 い stem, remain too ambiguous to license directly.
  const bool is_honorific_prefix_verb = prev.extended_pos == core::ExtendedPOS::Prefix &&
                                        grammar::isHonorificPrefix(prev.surface) && next.fromDictionary() &&
                                        grammar::isHumbleHonorificLemma(next.lemma);
  if (prev.pos == core::PartOfSpeech::Prefix && next.pos == core::PartOfSpeech::Verb && !is_honorific_prefix_verb &&
      grammar::isPureHiragana(next.surface) && next.surface.size() <= 6) {  // 2 chars or less
    bonus += cost::kAlmostNever;
  }

  // Penalty for PREFIX → non-dictionary pure-hiragana verb pattern (3 chars)
  // E.g., お+はよう in おはよう - はよう is not a real verb
  // Valid patterns like お+待ち have kanji, お+召し would be in dictionary
  if (prev.pos == core::PartOfSpeech::Prefix && next.pos == core::PartOfSpeech::Verb && !next.fromDictionary() &&
      grammar::isPureHiragana(next.surface) && next.surface.size() == 9) {  // Exactly 3 chars (9 bytes)
    bonus += cost::kAlmostNever;
  }

  // Penalty for ADV → short pure-hiragana verb renyokei pattern
  // E.g., はなはだ+し should not happen (はなはだしい is an adjective)
  // Valid ADV+verb patterns: ゆっくり+歩く (verb is longer/has kanji)
  // This prevents split like はなはだ+し+い when はなはだしい exists in dict
  // Exception: dictionary verbs like ね(寝る), み(見る), で(出る) are valid
  if (prev.pos == core::PartOfSpeech::Adverb && isSingleHiraganaVerbRenyokei(next) && !next.fromDictionary()) {
    // This rule formerly contributed kVeryRare in two call sites. Preserve
    // that effective magnitude while owning the rule here only.
    bonus += cost::kVeryRare + cost::kVeryRare;
  }

  // Penalty for opening bracket → PARTICLE pattern (furigana in parentheses).
  // E.g., 東京（とうきょう） should not split と+う+きょう. Punctuation
  // and closing brackets are clause boundaries and may legitimately be
  // followed by a particle, so they must not receive this penalty.
  if (prev.pos == core::PartOfSpeech::Symbol && next.pos == core::PartOfSpeech::Particle &&
      normalize::isOpeningBracket(utf8::decodeFirstChar(prev.surface))) {
    bonus += cost::kAlmostNever;
  }

  // Bonus for SYMBOL → long pure-hiragana OTHER (furigana pattern)
  // E.g., 東京（とうきょう） - the hiragana in parentheses is reading/furigana
  // Long hiragana sequences after symbols should stay as single tokens
  if (prev.pos == core::PartOfSpeech::Symbol && next.pos == core::PartOfSpeech::Other &&
      grammar::isPureHiragana(next.surface) && next.surface.size() >= 12) {  // 4+ chars (12 bytes in UTF-8)
    bonus += cost::kVeryStrongBonus;
  }

  // Penalty for SYMBOL → short hiragana → AUX pattern (furigana), gated to an
  // opening bracket only. Emoji, closing brackets, and other symbols are a soft
  // boundary that still license a following copula: 天気😀です, 犬🐕でした,
  // 本(重要)です, 評価◎です must keep です/でした whole rather than splitting で|す.
  if (prev.pos == core::PartOfSpeech::Symbol && next.pos == core::PartOfSpeech::Auxiliary &&
      normalize::isOpeningBracket(utf8::decodeFirstChar(prev.surface))) {
    bonus += cost::kVeryRare;
  }

  // Penalty for AuxCopulaDa(で) + ParticleTopic(も) pattern
  // This prevents 雨+で+も split when 雨+でも (副助詞) is correct
  // But allows 何+で+も split (で=copula連用形, も=係助詞)
  // The difference: 何(Pronoun) vs 雨(Noun) - Pronoun should split
  if (prev.extended_pos == core::ExtendedPOS::AuxCopulaDa && prev.surface == "で" &&
      next.extended_pos == core::ExtendedPOS::ParticleTopic && next.surface == "も") {
    bonus += cost::kVeryRare;
  }

  return bonus;
}

}  // namespace suzume::analysis::connection_rules
