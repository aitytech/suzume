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

#include <string>
#include <string_view>
#include <vector>

#include "core/types.h"
#include "dictionary/dictionary.h"
#include "grammar/conjugation.h"
#include "grammar/inflection.h"
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

/** Return true for a one-kanji stem that takes the polite auxiliary directly. */
bool isSingleKanjiPoliteStem(char32_t c);

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

// Detect a grammatical chain boundary inside a larger fabricated verb candidate:
// either productive て/で between verified verbs (なっ+て+なら), or classical
// negative ず before a verified continuation (あら+ず+し).  The full candidate
// is discarded only when both lexical sides are dictionary-backed.
bool hasInternalVerbChainBoundary(const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos,
                                  const grammar::Inflection& inflection,
                                  const dictionary::DictionaryManager* dict_manager);

/**
 * @brief Check if a base form exists in dictionary as a verb
 */
bool isVerbInDictionary(const dictionary::DictionaryManager* dict_manager, std::string_view base_form);

/**
 * @brief Check if a base form exists in dictionary as an adjective
 */
bool isAdjectiveInDictionary(const dictionary::DictionaryManager* dict_manager, std::string_view base_form);

/**
 * @brief Check if a surface exists in dictionary as a noun (exact match)
 *
 * Reports a hit only for an entry whose surface equals @p surface, so a shorter
 * dictionary prefix (e.g. a single-kanji noun) does not spuriously match a
 * longer verb candidate.
 */
bool isNounInDictionary(const dictionary::DictionaryManager* dict_manager, std::string_view surface);

/**
 * @brief Check if a surface exists in dictionary as a noun or adjective (exact match)
 *
 * Reports a hit only for an entry whose surface equals @p surface (see
 * isNounInDictionary for the exact-match rationale).
 */
bool isNounOrAdjectiveInDictionary(const dictionary::DictionaryManager* dict_manager, std::string_view surface);

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

// Exact one-token case-particle lookup.  Conjunctive particles such as ば are
// valid syllables inside inflected lexical stems and must not trigger the
// noun+case-particle+する guard.
bool hasCaseParticleDictionaryEntry(const dictionary::DictionaryManager* dict_manager, std::string_view surface);

// A bare continuative can chain clauses before the literal Japanese comma
// when a non-quotative case particle or quantified focus phrase licenses a
// predicate on its left.
bool isCommaClauseChainingRenyokei(const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos,
                                   const dictionary::DictionaryManager* dict_manager);

// True when start_pos is strictly inside a dictionary particle. Candidate
// generators use this to avoid manufacturing a verb from the tail of a
// compound particle (から + やり直す, not か + らやり直す).
bool startsInsideDictionaryParticle(const std::vector<char32_t>& codepoints, size_t start_pos,
                                    const dictionary::DictionaryManager* dict_manager);

// Detect a multi-mora particle beginning exactly at @p start_pos. Such a
// closed-class prefix cannot be the first half of a productive compound verb.
bool startsWithMultiMoraDictionaryParticle(const std::vector<char32_t>& codepoints, size_t start_pos,
                                           const dictionary::DictionaryManager* dict_manager);

// True when start_pos is strictly inside a contiguous kanji run immediately
// followed by し. Such an internal position cannot begin a separate lexical
// candidate: the complete run is a productive verbal noun (提出+し) or the
// kanji portion of a lexical verb stem (見直し).
bool startsInsideKanjiRunBeforeShi(const std::vector<char32_t>& codepoints, size_t start_pos);

// =============================================================================
// Fabricated closed-class absorption guards
// =============================================================================
// A recurring defect this family defends against: a verb/adjective candidate
// generator builds a NON-dictionary conjugation whose surface swallows an
// adjacent closed-class morpheme, because that morpheme's kana coincide with an
// inflectional ending. The 副助詞 しか / 係助詞 さえ・すら end in an a-row か/え
// that matches a godan mizenkei; a て/で-form + 補助動詞 みる has an internal
// てみ/でみ that matches an ichidan stem. Unchecked, these fabricated tokens
// (水しく for 水しか, 金さう for 金さえ, やってみる for やっ+て+み) outscore the
// correct split.
//
// The guards reject such fabrications and fall into three shapes by where the
// closed-class element sits relative to the fabricated verb:
//   - Tail  (T): the run ends in [word] + particle (+ negative). Helpers:
//                endsWithParticleTailOfPos, endsWithFocusParticleTail (副助詞 ‖
//                係助詞), and hiragana_verb_detail::endsWithParticleAfterVerb
//                (verb-prefix + 副助詞). Plus an inline 副助詞 head check in the
//                kanji adjective path.
//   - Embed (E): an internal て/で + 補助動詞 must split the run. Helpers:
//                embedsTeFormMiruAuxiliary (て/で + みる), embedsTeFormAuxiliary
//                (ていく / benefactive-request). Plus inline てくれ/てもら/てあげ
//                and で + auxiliary-chain checks in the onbin paths — see the
//                per-site comments there for why each set differs from the
//                helper's pattern list (ている/ておく are deliberately absent).
//   - Head  (H): a leading 副助詞 opens the hiragana portion of an adjective —
//                inline in the kanji adjective path.
//
// Shared invariant: a real verb/adjective that genuinely embeds these kana
// (押さえる, 起こす) is protected by its own dictionary base form, so every guard
// is gated on the candidate NOT being a dictionary word (`in_dict` / an
// exact-surface lookup) before it fires. Helper call sites and the T3 definition
// carry an @see back-reference to this note.
// =============================================================================

/**
 * @brief Check if a span ends in a dictionary particle of the given POS
 *
 * True when the span [start_pos, end_pos) ends in a dictionary-registered
 * particle of @p particle_pos, optionally followed by the negative
 * ない / なかっ / なかった or the copula inflection だ / だっ. Detects
 * candidates fabricated by absorbing [word] + particle (+ auxiliary) into a
 * single token: the 副助詞 しか ends in
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
 * すら, ...). Both attach after a noun and may be followed by an auxiliary,
 * so a candidate spanning [word] + focus particle (+ auxiliary) is never a
 * single word (お金さえない = お金 + さえ + ない, never a form of the non-word
 * 金さう).
 */
bool endsWithFocusParticleTail(const dictionary::DictionaryManager* dict_manager,
                               const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos);

// True when a fabricated verb candidate starts with an exact auxiliary entry
// and absorbs that auxiliary's negative inflection (過ぎない → 過ぎ + ない).
// The check is POS-based: lexical verbs with the same surface are unaffected.
bool hasAuxiliaryNegativeBoundary(const dictionary::DictionaryManager* dict_manager,
                                  const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos);

// True when a dictionary formal noun starts at @p pos. This lets candidate
// generation preserve the boundary after a predicate's inflecting auxiliary.
bool formalNounFollowsAt(const dictionary::DictionaryManager* dict_manager, const std::vector<char32_t>& codepoints,
                         size_t pos);

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
void sortCandidatesByCost(std::vector<UnknownCandidate>& candidates, size_t first_index = 0);

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
 * stem. The completed-state construction 〜てある is different: it is always a
 * te-form followed by the existential subsidiary, including after such stems.
 */
bool embedsTeFormAuxiliary(std::string_view surface);

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
bool embedsTeFormMiruAuxiliary(const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos);

/**
 * @brief True when a dictionary auxiliary stands directly on an onbin kana
 *        inside the span.
 *
 * The te-form guards above look for a て/で that the contraction and the past
 * auxiliary simply do not leave behind: 書い+とけ+ば and 書い+た+って both put a
 * complete auxiliary straight onto the onbin stem, and the run then reads as one
 * fabricated verb (書いとける, 書いたる). The onbin kana in front of the auxiliary
 * is the boundary evidence the surface still carries.
 */
bool embedsAuxiliaryOnOnbinStem(const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos,
                                const dictionary::DictionaryManager* dict_manager);

/**
 * @brief True when a dictionary auxiliary that selects a predicate starts at
 *        @p pos.
 *
 * Such an auxiliary attaches to an inflected verb form, so the span in front of
 * it is verbal and the only open question is where the verb begins (花散り+ぬ,
 * 見送り+けむ). The copula is deliberately excluded: it follows a deverbal noun
 * just as readily (足取り+だっ+た), so it carries no such evidence.
 */
bool predicateAuxiliaryFollowsAt(const dictionary::DictionaryManager* dict_manager,
                                 const std::vector<char32_t>& codepoints, size_t pos);

/**
 * @brief Extend candidates with emphatic suffix variants
 *
 * For each verb/adjective candidate, checks if input continues with emphatic
 * characters and creates an extended variant.
 */
void addEmphaticVariants(std::vector<UnknownCandidate>& candidates, const std::vector<char32_t>& codepoints,
                         size_t first_index = 0);

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
grammar::GodanOnbinRange getGodanTypesByOnbin(std::string_view onbin);

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
 * る/た/て immediately, the closed ない-family paradigm via
 * naiNegativeFollowsAt(), and ま (れます, れました). With @p strict_masu the ま branch additionally
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

// A passive followed by a causative is always an auxiliary chain (書かれさせる),
// unlike an ordinary lexical compound that merely ends in せる.
bool containsPassiveCausativeAuxPattern(std::string_view surface);

// =============================================================================
// Inflection Analysis Helpers
// =============================================================================

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
bool masuAuxFollowsAt(const std::vector<char32_t>& codepoints, size_t pos);

// Returns the length of a complete finite ます inflection beginning at pos,
// or zero when the following characters do not form one.  The caller decides
// whether the form is at a clause boundary.
size_t finiteMasuFormLengthAt(const std::vector<char32_t>& codepoints, size_t pos);

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
bool causativeSaseFollowsAt(const std::vector<char32_t>& codepoints, size_t pos);

/**
 * @brief Check whether a character can start a する-auxiliary after renyokei し.
 *
 * Covers the continuations of する in renyokei position:
 * ちゃ (contracted しちゃう), て/た (して/した), な (しない), ま (します),
 * よ (しよう), ろ (imperative しろ), そ (しそう), と/か/つ (しとく/しかける/しつつ).
 * Used to tell a renyokei し + する-auxiliary chain apart from a nominalized
 * noun or a false godan-sa stem that would absorb the し.
 */
bool isSuruAuxiliaryStarter(char32_t next_char);

/**
 * @brief Check whether a ない-family negative begins at @p pos.
 *
 * Matches the negative auxiliary ない and its conjugated/contracted onsets:
 * ない, なかっ(た), なく(て), なけれ(ば), なけりゃ, なきゃ. A bare な followed by
 * anything else (なる, なさい, ...) does not match, so callers can use this as
 * an unambiguous "negation follows" gate after a verb mizenkei.
 *
 * @param codepoints Full input codepoints
 * @param pos Index expected to hold the leading な
 */
size_t naiNegativeFormLengthAt(const std::vector<char32_t>& codepoints, size_t pos);

/**
 * @brief Check whether a ない-family negative begins at @p pos.
 *
 * Boolean facade over naiNegativeFormLengthAt() for callers that only need
 * boundary evidence rather than the exact closed-paradigm span.
 */
bool naiNegativeFollowsAt(const std::vector<char32_t>& codepoints, size_t pos);

/**
 * @brief Check whether the conditional negative auxiliary なけれ begins at @p pos.
 *
 * This is the irrealis-stem continuation used by 〜なければ.  Keeping it
 * separate from the broader ない-family gate lets candidate generators apply
 * the conditional's stronger boundary evidence without changing ordinary or
 * contracted negative forms.
 *
 * @param codepoints Full input codepoints
 * @param pos Index expected to hold the leading な
 */
bool naiConditionalFollowsAt(const std::vector<char32_t>& codepoints, size_t pos);

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
bool itadakuParadigmStartsAt(const std::vector<char32_t>& codepoints, size_t pos);

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

// =============================================================================
// Character Region Detection
// =============================================================================

// Delegate to shared implementation in tokenizer_utils.h
using ::suzume::analysis::findCharRegionEnd;

}  // namespace suzume::analysis::verb_helpers

#endif  // SUZUME_ANALYSIS_VERB_CANDIDATES_HELPERS_H_
