/**
 * @file suffix_candidates.h
 * @brief Suffix-based unknown word candidate generation
 */

#ifndef SUZUME_ANALYSIS_SUFFIX_CANDIDATES_H_
#define SUZUME_ANALYSIS_SUFFIX_CANDIDATES_H_

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.h"
#include "normalize/char_type.h"

namespace suzume::dictionary {
class DictionaryManager;
}

namespace suzume::grammar {
class Inflection;
}

namespace suzume::analysis {

struct UnknownCandidate;
struct UnknownOptions;

/**
 * @brief Suffix entry for kanji compounds
 */
struct SuffixEntry {
  std::string_view suffix;
  core::PartOfSpeech pos;
  // Derivational suffixes create a lexical compound noun (新制度, 安全性).
  // Relational suffixes such as 末 retain their compositional boundary.
  bool forms_derived_compound;
};

/**
 * @brief Get list of kanji compound suffixes
 */
const std::array<SuffixEntry, 22>& getSuffixEntries();

/**
 * @brief Get list of na-adjective forming suffixes (的, etc.)
 */
const std::array<std::string_view, 1>& getNaAdjSuffixes();

/**
 * @brief Generate candidates with suffix separation
 *
 * Detects kanji compounds that end with common suffixes (化, 性, 者, etc.)
 * and generates both the full compound and the stem as candidates.
 *
 * @param codepoints Text as codepoints
 * @param start_pos Start position (character index)
 * @param char_types Character types for each position
 * @param options Unknown word generation options
 * @return Vector of candidates
 */
std::vector<UnknownCandidate> generateWithSuffix(const std::vector<char32_t>& codepoints, size_t start_pos,
                                                 const std::vector<normalize::CharType>& char_types,
                                                 const UnknownOptions& options);

/**
 * @brief Generate nominalized noun candidates
 *
 * Detects nominalized verb stems (連用形転成名詞) like:
 *   - 手助け (from 手助ける)
 *   - 片付け (from 片付ける)
 *   - 引き上げ (from 引き上げる)
 *
 * @param codepoints Text as codepoints
 * @param start_pos Start position (character index)
 * @param char_types Character types for each position
 * @param dict_manager Dictionary manager for deverbal compound verification (may be null)
 * @return Vector of candidates
 */
std::vector<UnknownCandidate> generateNominalizedNounCandidates(
    const std::vector<char32_t>& codepoints, size_t start_pos, const std::vector<normalize::CharType>& char_types,
    const dictionary::DictionaryManager* dict_manager = nullptr);

/**
 * @brief Generate kanji + hiragana compound noun candidates
 *
 * Detects compound nouns with kanji prefix and hiragana suffix:
 *   - 玉ねぎ (tamanegi - onion)
 *   - 水たまり (mizutamari - puddle)
 *   - 雨だれ (amadare - raindrop)
 *
 * Distinguished from verb conjugations by requiring longer hiragana
 * portions that don't match typical conjugation patterns.
 *
 * @param codepoints Text as codepoints
 * @param start_pos Start position (character index)
 * @param char_types Character types for each position
 * @return Vector of candidates
 */
std::vector<UnknownCandidate> generateKanjiHiraganaCompoundCandidates(
    const std::vector<char32_t>& codepoints, size_t start_pos, const std::vector<normalize::CharType>& char_types,
    const dictionary::DictionaryManager* dict_manager = nullptr);

/**
 * @brief Generate productive suffix candidates for hiragana sequences
 *
 * Detects productive suffix patterns attached to verb stems:
 *   - V連用形 + がち (tendency): ありがち、なりがち
 *   - V連用形 + っぽい (resemblance): 忘れっぽい、怒りっぽい
 *
 * These patterns allow recognition without explicit dictionary entries.
 *
 * @param codepoints Text as codepoints
 * @param start_pos Start position (character index)
 * @param char_types Character types for each position
 * @return Vector of candidates
 */
std::vector<UnknownCandidate> generateProductiveSuffixCandidates(const std::vector<char32_t>& codepoints,
                                                                 size_t start_pos,
                                                                 const std::vector<normalize::CharType>& char_types);

/**
 * @brief Generate productive suffix-verb candidates from nominal bases.
 *
 * Detects the Godan-ka suffix verb めく and its inflections:
 *   - 春めく、謎めいた、色めいて
 */
std::vector<UnknownCandidate> generateProductiveSuffixVerbCandidates(
    const std::vector<char32_t>& codepoints, size_t start_pos, const std::vector<normalize::CharType>& char_types);

/**
 * @brief Generate counter candidates for numeral + つ patterns
 *
 * Detects closed-class counter patterns:
 *   - 一つ (hitotsu), 二つ (futatsu), ..., 九つ (kokonotsu)
 *
 * This is a closed class of 9 patterns, recognized as grammatical pattern
 * rather than dictionary entries.
 *
 * @param codepoints Text as codepoints
 * @param start_pos Start position (character index)
 * @param char_types Character types for each position
 * @return Vector of candidates
 */
std::vector<UnknownCandidate> generateCounterCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                                        const std::vector<normalize::CharType>& char_types,
                                                        const dictionary::DictionaryManager* dict_manager = nullptr);

/**
 * @brief Generate prefix + single kanji compound candidates
 *
 * Detects temporal/prefix compounds:
 *   - 今日 (kyou - today), 今週 (konshuu - this week)
 *   - 本日 (honjitsu - today formal)
 *
 * The generated compound competes with split analysis.
 * Interrogatives (何, 誰, etc.) act as anchors to prevent over-concatenation.
 *
 * @param codepoints Text as codepoints
 * @param start_pos Start position (character index)
 * @param char_types Character types for each position
 * @param inflection Inflection analyzer, used to suppress the compound when the
 *        second kanji heads a verb continuing into the following hiragana
 *        (今食べてる → 今|食べ|てる, not 今食|べてる)
 * @return Vector of candidates
 */
std::vector<UnknownCandidate> generatePrefixCompoundCandidates(const std::vector<char32_t>& codepoints,
                                                               size_t start_pos,
                                                               const std::vector<normalize::CharType>& char_types,
                                                               const grammar::Inflection& inflection);

/**
 * @brief Generate a temporal-noun boundary split candidate
 *
 * Detects an adverbial temporal noun (現在, 昨日) heading a longer kanji run and
 * discounts the split point so it beats the unknown-word kanji-run merge:
 *   - 現在担当者 (genzai tantousha) -> 現在 | 担当者
 *   - 昨日会議 (kinou kaigi) -> 昨日 | 会議
 *
 * @param codepoints Text as codepoints
 * @param start_pos Start position (character index)
 * @param char_types Character types for each position
 * @return Vector of candidates
 */
std::vector<UnknownCandidate> generateTemporalNounBoundaryCandidates(
    const std::vector<char32_t>& codepoints, size_t start_pos, const std::vector<normalize::CharType>& char_types);

/**
 * @brief Check if a codepoint is a prefix-like kanji
 *
 * Returns true for kanji that commonly form temporal/formal compounds:
 * 今, 本, 来, 先, 昨, 翌, 毎, 全, 各, 両, 諸
 *
 * @param cp Unicode codepoint to check
 * @return true if prefix-like kanji
 */
bool isPrefixLikeKanji(char32_t cp);

/**
 * @brief Check if a codepoint is an interrogative kanji
 *
 * Returns true for kanji that are interrogative words:
 * 何, 誰, 幾
 * These should not form verb compounds (e.g., 何してる should split as 何|し|てる)
 *
 * @param cp Unicode codepoint to check
 * @return true if interrogative kanji
 */
bool isInterrogativeKanji(char32_t cp);

}  // namespace suzume::analysis

#endif  // SUZUME_ANALYSIS_SUFFIX_CANDIDATES_H_
