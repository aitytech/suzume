/**
 * @file verb_candidates_helpers.h
 * @brief Internal helpers for verb candidate generation
 *
 * This file contains shared helper functions used by verb candidate generators.
 * These helpers are internal to the analysis module and should not be exposed
 * in the public API.
 */

#ifndef SUZUME_ANALYSIS_VERB_CANDIDATES_HELPERS_H_
#define SUZUME_ANALYSIS_VERB_CANDIDATES_HELPERS_H_

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.h"
#include "core/utf8_constants.h"
#include "dictionary/dictionary.h"
#include "grammar/conjugation.h"
#include "grammar/inflection.h"
#include "normalize/char_type.h"
#include "tokenizer_utils.h"
#include "unknown.h"

namespace suzume::analysis::verb_helpers {

// =============================================================================
// Single-kanji Ichidan verbs (単漢字一段動詞)
// =============================================================================

/**
 * @brief Check if character is a known single-kanji ichidan verb
 *
 * Common single-kanji Ichidan verbs:
 * 見(みる), 居(いる), 着(きる), 寝(ねる), 煮(にる), 似(にる)
 * 経(へる), 干(ひる), 射(いる), 得(える/うる), 出(でる), 鋳(いる)
 */
bool isSingleKanjiIchidan(char32_t c);

/**
 * @brief Check if a surface form is exactly one single-kanji Ichidan verb
 *
 * True when the surface consists of exactly one codepoint and that codepoint
 * is a known single-kanji Ichidan verb (see isSingleKanjiIchidan).
 */
bool isSingleKanjiIchidanSurface(std::string_view surface);

// =============================================================================
// Dictionary Lookup Helpers
// =============================================================================

/**
 * @brief Generic dictionary entry lookup by part of speech
 * @param dict_manager Dictionary manager (may be null)
 * @param surface Surface form to lookup
 * @param pos Part of speech to match
 * @return true if an exact-match entry with the specified POS exists
 */
bool hasDictionaryEntry(const dictionary::DictionaryManager* dict_manager, std::string_view surface,
                        core::PartOfSpeech pos);

/**
 * @brief Check if a base form exists in dictionary as a verb
 */
inline bool isVerbInDictionary(const dictionary::DictionaryManager* dict_manager, std::string_view base_form) {
  return hasDictionaryEntry(dict_manager, base_form, core::PartOfSpeech::Verb);
}

/**
 * @brief Check if a base form exists in dictionary as an adjective
 */
inline bool isAdjectiveInDictionary(const dictionary::DictionaryManager* dict_manager, std::string_view base_form) {
  return hasDictionaryEntry(dict_manager, base_form, core::PartOfSpeech::Adjective);
}

/**
 * @brief Check if a surface exists in dictionary as a noun (exact match)
 *
 * Reports a hit only for an entry whose surface equals @p surface, so a shorter
 * dictionary prefix (e.g. a single-kanji noun) does not spuriously match a
 * longer verb candidate.
 */
inline bool isNounInDictionary(const dictionary::DictionaryManager* dict_manager, std::string_view surface) {
  return hasDictionaryEntry(dict_manager, surface, core::PartOfSpeech::Noun);
}

/**
 * @brief Check if a surface exists in dictionary as a noun or adjective (exact match)
 *
 * Reports a hit only for an entry whose surface equals @p surface (see
 * isNounInDictionary for the exact-match rationale).
 */
inline bool isNounOrAdjectiveInDictionary(const dictionary::DictionaryManager* dict_manager, std::string_view surface) {
  return hasDictionaryEntry(dict_manager, surface, core::PartOfSpeech::Noun) ||
         hasDictionaryEntry(dict_manager, surface, core::PartOfSpeech::Adjective);
}

/**
 * @brief Check if a surface has a non-verb entry in dictionary
 */
bool hasNonVerbDictionaryEntry(const dictionary::DictionaryManager* dict_manager, std::string_view surface);

/**
 * @brief Check if a surface has a particle entry in dictionary
 *
 * Used to detect compound particles (について, によって, として, etc.)
 */
bool hasParticleDictionaryEntry(const dictionary::DictionaryManager* dict_manager, std::string_view surface);

/**
 * @brief Check if a span ends in a dictionary particle of the given POS
 *
 * True when the span [start_pos, end_pos) ends in a dictionary-registered
 * particle of @p particle_pos, optionally followed by the negative
 * ない / なかっ / なかった. Detects candidates fabricated by absorbing
 * [word] + particle (+ negative) into a single token: the 副助詞 しか ends in
 * the a-row mora か, which coincides with the godan-ka mizenkei/onbin ending,
 * so a non-word verb conjugation can absorb noun + しか(…ない) (水しかない read
 * as a form of the non-word 水しく). The particle must be 2+ codepoints so the
 * single mora か of a genuine godan-ka mizenkei (行かない) can never match, and
 * a non-empty prefix must remain before the particle.
 */
bool endsWithParticleTailOfPos(const dictionary::DictionaryManager* dict_manager,
                               const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos,
                               core::ExtendedPOS particle_pos);

/**
 * @brief Check if a span ends in a focus particle (副助詞 or 係助詞) tail
 *
 * Convenience wrapper over endsWithParticleTailOfPos covering both focus
 * particle classes: 副助詞 (しか, だけ, ばかり, ...) and 係助詞 (さえ, こそ,
 * すら, ...). Both attach after a noun and may be followed by ない, so a
 * candidate spanning [word] + focus particle (+ negative) is never a single
 * word (お金さえない = お金 + さえ + ない, never a form of the non-word 金さう).
 */
bool endsWithFocusParticleTail(const dictionary::DictionaryManager* dict_manager,
                               const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos);

/**
 * @brief Look up a verb's lemma from the dictionary
 *
 * Returns the lemma of the first verb entry whose surface exactly matches
 * @p surface and whose lemma is non-empty. Falls back to @p fallback when the
 * dictionary is null or no matching verb entry exists.
 */
std::string lookupVerbLemma(const dictionary::DictionaryManager* dict_manager, std::string_view surface,
                            std::string_view fallback);

/**
 * @brief Verify a constructed base form as a real verb
 *
 * Accepts the base form when it is a dictionary verb, or when inflection
 * analysis recognizes it with confidence strictly above @p min_confidence as
 * a Godan verb (@p require_godan true) or an Ichidan verb (@p require_godan
 * false).
 */
bool isVerifiedVerbBase(const dictionary::DictionaryManager* dict_manager, const grammar::Inflection& inflection,
                        std::string_view base_form, float min_confidence, bool require_godan);

// =============================================================================
// Candidate Sorting
// =============================================================================

/**
 * @brief Sort candidates by cost (lowest cost first)
 */
void sortCandidatesByCost(std::vector<UnknownCandidate>& candidates);

// =============================================================================
// Emphatic Pattern Helpers (口語強調パターン)
// =============================================================================

/**
 * @brief Check if character is an emphatic suffix character
 *
 * Emphatic characters: っ, ッ, ー, ぁぃぅぇぉ, ァィゥェォ
 */
bool isEmphaticChar(char32_t c);

/**
 * @brief Get the vowel character (あいうえお) for a hiragana's ending vowel
 *
 * Maps any hiragana to its vowel row character.
 * Returns 0 for characters without vowels (ん, っ) or non-hiragana.
 */
char32_t getHiraganaVowel(char32_t c);

/**
 * @brief A matched emphatic suffix and the input position after it.
 */
struct EmphaticSuffixMatch {
  std::string suffix;
  size_t end = 0;
  size_t standard_char_count = 0;
  size_t repeated_vowel_count = 0;

  [[nodiscard]] bool empty() const { return suffix.empty(); }
};

/**
 * @brief Context-specific treatment of a sokuon before て/た.
 */
enum class SokuonOnsetPolicy {
  Candidate,        // Generated full-form candidate: release っ from って/った.
  DictionaryEntry,  // Dictionary stem: preserve productive onbin (あらっ+て/た).
};

/**
 * @brief Decide whether a sokuon (っ/ッ) begins a following morpheme rather than
 *        attaching to the base candidate as emphatic elongation.
 *
 * A sokuon after a verb/adjective/auxiliary is normally emphatic (行くっ, やばいっ).
 * It must instead be released when it begins the colloquial polite auxiliary
 * っす/っさ/っせ or a Godan quotative っと. Generated full-form candidates also
 * release っ from って/った. Dictionary stems retain productive onbin before
 * て/た (あらっ+て/た), except for a u-row dictionary form that would otherwise
 * absorb a separate って. The shared policy keeps these intentional differences
 * explicit while reusing the scanning and cost logic.
 *
 * @param codepoints Full input codepoints.
 * @param sokuon_pos Index of the sokuon character.
 * @param base_pos   POS of the base candidate (Verb enables the っと quotative check).
 * @param base_final Final codepoint of the base candidate.
 * @param policy     Candidate source policy for って/った handling.
 * @return true if the sokuon should be released (not absorbed as emphatic).
 */
inline bool isSuppressedSokuonOnset(const std::vector<char32_t>& codepoints, size_t sokuon_pos,
                                    core::PartOfSpeech base_pos, char32_t base_final, SokuonOnsetPolicy policy) {
  if (sokuon_pos + 1 >= codepoints.size()) {
    return false;  // Sokuon at end - keep as emphatic
  }
  char32_t next = codepoints[sokuon_pos + 1];
  // Colloquial polite auxiliary っす/っさ/っせ (=です).
  if (next == U'す' || next == U'さ' || next == U'せ') {
    return true;
  }
  const bool u_row_verb = base_pos == core::PartOfSpeech::Verb && normalize::isURowHiragana(base_final);
  if (next == U'と') {
    return u_row_verb;  // Godan quotative っと: 行く+っと, not 行くっ+と.
  }
  if (policy == SokuonOnsetPolicy::DictionaryEntry) {
    // A dictionary stem such as あら must retain the onbin っ in あらっ+て/た.
    return next == core::hiragana::kTe && u_row_verb;
  }
  return next == core::hiragana::kTe || next == core::hiragana::kTa;
}

/**
 * @brief Match standard emphatic marks and repeated final vowels after a candidate.
 */
EmphaticSuffixMatch matchEmphaticSuffix(const std::vector<char32_t>& codepoints, size_t base_end,
                                        core::PartOfSpeech base_pos,
                                        SokuonOnsetPolicy policy = SokuonOnsetPolicy::Candidate);

/**
 * @brief Return the cost adjustment for a matched emphatic suffix.
 */
float emphaticCostAdjustment(const EmphaticSuffixMatch& match);

/**
 * @brief True when a single-verb candidate surface embeds a て/で-form followed
 *        by a subsidiary or aspect verb that would otherwise merge into one verb.
 *
 * The 〜ていく directional aspect ends in く, so a candidate like 食べていく is
 * mis-generated as a lone godan-ka verb and must be split (食べ+て+いく), unlike
 * 食べている where the plain split already wins. The benefactive/request verbs
 * (てもらう/てくれ/てあげ/てほしい) likewise split (助けてもらう → 助け+て+もらう).
 * Continuation 〜ている/ておく is intentionally NOT matched here: it would also
 * catch verbs whose renyokei ends in て (慌て+ている, 捨て+ておく) and strand the
 * stem.
 */
inline bool embedsTeFormAuxiliary(std::string_view surface) {
  static constexpr std::string_view kPatterns[] = {
      "ていく", "ていっ", "ていけ", "ていか",                // 〜ていく directional aspect
      "てもら", "てくれ", "てあげ", "てほしい", "てくださ",  // benefactive / request
  };
  for (const std::string_view pat : kPatterns) {
    if (surface.find(pat) != std::string_view::npos) {
      return true;
    }
  }
  return false;
}

/**
 * @brief True when a candidate span embeds a te-form て/で immediately followed
 *        by み past its first codepoint.
 *
 * An internal て/で inside a verb surface is always a conjugation boundary (the
 * te-form particle or its voiced onbin form), and a following み is the onset of
 * the subsidiary verb みる, so the span is [te-form] + みる, never a single
 * conjugated verb (食べてみれば = 食べ + て + みれ + ば, やってみ = やっ + て +
 * み). No real verb embeds てみ/でみ inside one conjugated form. The codepoint
 * at @p start_pos is exempt: a candidate that merely begins with て/で (てみ
 * itself, で-leading runs) is a different shape and is left untouched.
 */
inline bool embedsTeFormMiruAuxiliary(const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos) {
  if (end_pos > codepoints.size()) {
    return false;
  }
  for (size_t pos = start_pos + 1; pos + 1 < end_pos; ++pos) {
    if ((codepoints[pos] == core::hiragana::kTe || codepoints[pos] == U'で') && codepoints[pos + 1] == U'み') {
      return true;
    }
  }
  return false;
}

/**
 * @brief Extend candidates with emphatic suffix variants
 *
 * For each verb/adjective candidate, checks if input continues with emphatic
 * characters and creates an extended variant.
 */
void addEmphaticVariants(std::vector<UnknownCandidate>& candidates, const std::vector<char32_t>& codepoints);

// =============================================================================
// Pattern Skip Helpers
// =============================================================================

/**
 * @brief Check if surface ends with ます auxiliary patterns
 *
 * Returns true if pattern should be skipped (to allow auxiliary split)
 */
bool shouldSkipMasuAuxPattern(std::string_view surface, grammar::VerbType verb_type);

/**
 * @brief Check if surface ends with そう auxiliary patterns
 */
bool shouldSkipSouPattern(std::string_view surface, grammar::VerbType verb_type);

/**
 * @brief Check if surface contains compound adjective patterns (にくい/やすい/がたい)
 */
bool isCompoundAdjectivePattern(std::string_view surface);

/**
 * @brief Check if surface contains adj renyokei + なる conjugation pattern
 *
 * Matches: くなっ, くなり, くなる, くなれ anywhere in the string.
 * Used to skip/penalize false candidates that absorb adj く-form + なる.
 */
bool containsKuNaruPattern(std::string_view surface);

/**
 * @brief Detect a fully spelled-out reduplicated 〜しい adjective head at @p start_pos
 *
 * 畳語 i-adjectives whose doubled stem is written out instead of using the
 * iteration mark: a repeated two-character unit (XYXY) followed by し and an
 * i-adjective inflection onset (い/く/か/け), e.g. 馬鹿馬鹿しい, バカバカしく,
 * ばかばかしかった. The halves are compared by codepoint, so one rule covers
 * kanji and both kana scripts. The iteration-mark spelling (若々しい) needs no
 * special handling because 々 keeps the stem within the regular 2-kanji path.
 *
 * @param codepoints Full input codepoints
 * @param start_pos Index of the first character of the doubled unit
 * @return true if positions [start_pos, start_pos+5] form the reduplicated head
 */
bool isReduplicatedShiiAdjectiveHead(const std::vector<char32_t>& codepoints, size_t start_pos);

/**
 * @brief Get Godan VerbTypes that use a specific onbin pattern
 *
 * Onbin patterns:
 * - "い" (ikuon) → GodanKa, GodanGa
 * - "っ" (sokuon) → GodanKa (行く irregular), GodanRa, GodanTa, GodanWa
 * - "ん" (hatsuonbin) → GodanNa, GodanBa, GodanMa
 * - "" (none) → GodanSa
 *
 * @param onbin Onbin pattern to match ("い", "っ", "ん", or "")
 * @return Reference to a shared immutable table of (VerbType, base_suffix) pairs
 */
const std::vector<std::pair<grammar::VerbType, std::string_view>>& getGodanTypesByOnbin(std::string_view onbin);

/**
 * @brief Result of matching an onbin stem against the dictionary's godan verbs.
 *
 * @c base_suffix points into the immutable getGodanTypesByOnbin() table and is
 * valid for the program's lifetime. When @c matched is false, @c verb_type is
 * Unknown, @c base_form is empty, and @c base_suffix is empty.
 */
struct GodanOnbinDictMatch {
  grammar::VerbType verb_type = grammar::VerbType::Unknown;
  std::string base_form;         // stem + base_suffix
  std::string_view base_suffix;  // the matched suffix from the table
  bool matched = false;
};

/**
 * @brief First (verb_type, stem+base_suffix) pair for @p onbin whose base form
 *        is a dictionary verb, in getGodanTypesByOnbin() table order.
 *
 * Reproduces the phase-1 "check every godan candidate, keep the first dictionary
 * hit" scan shared by the onbin candidate generators.
 *
 * @param dict_manager Dictionary manager (may be null → no match)
 * @param stem         Verb stem to which each table suffix is appended
 * @param onbin        Onbin pattern ("い", "っ", "ん", or "")
 * @return The first dictionary-verified match, or an unmatched result
 */
GodanOnbinDictMatch firstGodanOnbinDictBase(const dictionary::DictionaryManager* dict_manager, std::string_view stem,
                                            std::string_view onbin);

/**
 * @brief Check if surface contains passive/potential auxiliary patterns
 */
bool shouldSkipPassiveAuxPattern(std::string_view surface, grammar::VerbType verb_type);

/**
 * @brief Check whether the codepoint after passive れ continues an auxiliary chain
 *
 * Matches the passive/potential continuation set after れ (or られ):
 * る/た/て immediately, な only when followed by い (れない, れなかった), and
 * ま (れます, れました). With @p strict_masu the ま branch additionally
 * requires a following す or せ (れます/れません), excluding bare ま.
 *
 * @param codepoints Full input codepoints
 * @param pos_after_re Index of the codepoint immediately after れ
 * @param strict_masu Require す/せ after ま
 */
bool isPassiveAuxContinuation(const std::vector<char32_t>& codepoints, size_t pos_after_re, bool strict_masu);

/**
 * @brief Check if surface contains causative auxiliary patterns
 */
bool shouldSkipCausativeAuxPattern(std::string_view surface, grammar::VerbType verb_type);

/**
 * @brief Check if surface matches suru-verb auxiliary patterns
 *
 * Detects サ変名詞 + する-auxiliary chains (勉強して, 対応される, 実行させた)
 * via connection-based inflection analysis: the hiragana tail after the kanji
 * run must analyze as a conjugation of する with an auxiliary chain attached.
 * Returns true if the pattern should be skipped (to allow the noun + する-aux
 * split to win).
 */
bool shouldSkipSuruVerbAuxPattern(std::string_view surface, size_t kanji_count, const grammar::Inflection& inflection);

// =============================================================================
// Auxiliary Pattern Penalty Checks (for verb candidate cost adjustment)
// =============================================================================

/**
 * @brief Check if surface contains te-form + auxiliary verb patterns
 * Uses kTeFormAuxPenaltyPatterns from scorer_constants.h
 */
bool containsTeFormAuxPattern(std::string_view surface);

/**
 * @brief Check if surface contains causative auxiliary patterns (contains-based)
 * Uses kCausativeAuxPenaltyPatterns from scorer_constants.h
 * Unlike shouldSkipCausativeAuxPattern, this uses contains() not endsWith()
 */
bool containsCausativeAuxPattern(std::string_view surface);

// =============================================================================
// Inflection Analysis Helpers
// =============================================================================

/**
 * @brief Get the best ichidan confidence from inflection analysis results
 *
 * Scans all inflection candidates for Ichidan verb type and returns
 * the maximum confidence above the given threshold.
 *
 * @param candidates Inflection analysis results
 * @param min_threshold Minimum confidence to consider (default 0.4)
 * @return Best ichidan confidence, or 0.0 if none found
 */
inline float getIchidanConfidence(const std::vector<grammar::InflectionCandidate>& candidates,
                                  float min_threshold = 0.4F) {
  float best = 0.0F;
  for (const auto& cand : candidates) {
    if (cand.verb_type == grammar::VerbType::Ichidan && cand.confidence >= min_threshold) {
      best = std::max(best, cand.confidence);
    }
  }
  return best;
}

/**
 * @brief Check whether a polite-auxiliary (ます family) follows at @p pos.
 *
 * ます / まし / ませ attach only to a verb renyokei, never to a bare noun, so
 * this licenses the verb reading of a noun/renyokei homograph (感じます).
 * Matches ま followed by す (ます), し (ました), or せ (ません).
 *
 * @param codepoints Full input codepoints
 * @param pos Index expected to hold the leading ま
 */
inline bool masuAuxFollowsAt(const std::vector<char32_t>& codepoints, size_t pos) {
  if (pos + 1 >= codepoints.size() || codepoints[pos] != U'ま') {
    return false;
  }
  const char32_t next = codepoints[pos + 1];
  return next == U'す' || next == U'し' || next == U'せ';
}

/**
 * @brief Check whether an ichidan causative auxiliary (させ family) follows at @p pos.
 *
 * The causative させる attaches only to a verb mizenkei, never to a bare noun,
 * so — like the ます family — it licenses the verb reading of a noun/renyokei
 * homograph (感じさせる → 感じ(VERB) + させる, not 感じ(NOUN) + さ + せる).
 * Matches さ followed by せ (させる/させた/させ...).
 *
 * @param codepoints Full input codepoints
 * @param pos Index expected to hold the leading さ
 */
inline bool causativeSaseFollowsAt(const std::vector<char32_t>& codepoints, size_t pos) {
  return pos + 1 < codepoints.size() && codepoints[pos] == U'さ' && codepoints[pos + 1] == U'せ';
}

/**
 * @brief Check whether a character can start a する-auxiliary after renyokei し.
 *
 * Covers the continuations of する in renyokei position:
 * ちゃ (contracted しちゃう), て/た (して/した), な (しない), ま (します),
 * よ (しよう), ろ (imperative しろ), そ (しそう), と/か/つ (しとく/しかける/しつつ).
 * Used to tell a renyokei し + する-auxiliary chain apart from a nominalized
 * noun or a false godan-sa stem that would absorb the し.
 */
inline bool isSuruAuxiliaryStarter(char32_t next_char) {
  return next_char == U'ち' || next_char == U'て' || next_char == U'た' || next_char == U'な' || next_char == U'ま' ||
         next_char == U'よ' || next_char == U'ろ' || next_char == U'そ' || next_char == U'と' || next_char == U'か' ||
         next_char == U'つ';
}

/**
 * @brief Check whether a ない-family negative begins at @p pos.
 *
 * Matches the negative auxiliary ない and its conjugated/contracted onsets:
 * ない, なかっ(た), なく(て), なけれ(ば), なきゃ. A bare な followed by
 * anything else (なる, なさい, ...) does not match, so callers can use this as
 * an unambiguous "negation follows" gate after a verb mizenkei.
 *
 * @param codepoints Full input codepoints
 * @param pos Index expected to hold the leading な
 */
inline bool naiNegativeFollowsAt(const std::vector<char32_t>& codepoints, size_t pos) {
  if (pos + 1 >= codepoints.size() || codepoints[pos] != U'な') {
    return false;
  }
  const char32_t second = codepoints[pos + 1];
  if (second == U'い' || second == U'く') {
    return true;  // ない / なく(て)
  }
  if (pos + 2 >= codepoints.size()) {
    return false;
  }
  const char32_t third = codepoints[pos + 2];
  return (second == U'か' && third == U'っ') ||  // なかっ(た)
         (second == U'け' && third == U'れ') ||  // なけれ(ば)
         (second == U'き' && third == U'ゃ');    // なきゃ
}

/**
 * @brief Check whether the いただく paradigm begins at @p pos.
 *
 * The receptive humble auxiliary いただく conjugates as いただ + ka-row kana
 * or the onbin い: いただか(ない), いただき, いただく, いただけ(ば/ます),
 * いただこ(う), いただい(た/て). A candidate that ends by absorbing this
 * leading い steals the auxiliary's onset (ご覧いただき → 覧い+ただき,
 * お使いいただく → 使+いい+ただく), so generators use this gate to keep the
 * い with いただく.
 *
 * @param codepoints Full input codepoints
 * @param pos Index expected to hold the leading い
 */
inline bool itadakuParadigmStartsAt(const std::vector<char32_t>& codepoints, size_t pos) {
  if (pos + 3 >= codepoints.size() || codepoints[pos] != U'い' || codepoints[pos + 1] != U'た' ||
      codepoints[pos + 2] != U'だ') {
    return false;
  }
  const char32_t inflected = codepoints[pos + 3];
  return inflected == U'か' || inflected == U'き' || inflected == U'く' || inflected == U'け' || inflected == U'こ' ||
         inflected == U'い';
}

/**
 * @brief Best inflection candidate per verb class (Ichidan / Suru / Godan)
 *
 * Members left unmatched keep confidence 0.0 and are otherwise
 * default-constructed.
 */
struct VerbClassBests {
  grammar::InflectionCandidate ichidan;
  grammar::InflectionCandidate suru;
  grammar::InflectionCandidate godan;
};

/**
 * @brief Scan inflection candidates for the best Ichidan, Suru, and Godan entries
 *
 * Candidates matched via のだ/んだ stripping (has_explanatory_suffix) are
 * ignored. Ties keep the earlier candidate (strict > comparison).
 */
VerbClassBests bestByVerbClass(const std::vector<grammar::InflectionCandidate>& candidates);

// =============================================================================
// Verb Type / Stem Analysis Helpers
// =============================================================================

/**
 * @brief Get the terminal-form (終止形) okurigana suffix for a verb type
 *
 * Returns the dictionary-form ending: Ichidan yields "る", Godan types yield
 * their base vowel (GodanKa -> "く", GodanSa -> "す", ...). Returns an empty
 * string for verb types without a Godan terminal ending (Suru, Kuru,
 * IAdjective, Unknown).
 */
std::string baseFormSuffix(grammar::VerbType verb_type);

/**
 * @brief Check whether a stem is a valid i-row Ichidan verb stem
 *
 * A valid i-row Ichidan stem ends in an i-row hiragana, has at least two
 * characters, and is not the single-kanji + い pattern (人い -> 人 + いる),
 * which is almost always NOUN + いる rather than an Ichidan verb.
 */
bool isValidIRowIchidanStem(std::string_view stem);

/**
 * @brief Check if an inflection suffix contains auxiliary verb patterns
 *
 * Looks for た/て/で/だ/ない/れ, which indicate a complete inflected form (as
 * opposed to a bare renyokei ending like し/み that is nominal, not verbal,
 * evidence). ます is intentionally excluded for MeCab-compatible splits
 * (申し上げます -> 申し上げ + ます). Shared by the compound-verb join path and
 * the sokuonbin-prefix stem probe so both treat conjugation evidence alike.
 */
inline bool hasAuxiliarySuffix(std::string_view suffix) {
  if (suffix.empty()) {
    return false;
  }
  return utf8::containsAny(suffix, {"た", "て", "で", "だ", "ない", "れ"});
}

// =============================================================================
// Character Region Detection
// =============================================================================

// Delegate to shared implementation in tokenizer_utils.h
using ::suzume::analysis::findCharRegionEnd;

}  // namespace suzume::analysis::verb_helpers

#endif  // SUZUME_ANALYSIS_VERB_CANDIDATES_HELPERS_H_
