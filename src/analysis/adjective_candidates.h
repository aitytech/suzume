/**
 * @file adjective_candidates.h
 * @brief Adjective-based unknown word candidate generation
 */

#ifndef SUZUME_ANALYSIS_ADJECTIVE_CANDIDATES_H_
#define SUZUME_ANALYSIS_ADJECTIVE_CANDIDATES_H_

#include <vector>

#include "core/types.h"
#include "dictionary/dictionary.h"
#include "grammar/inflection.h"
#include "normalize/char_type.h"

namespace suzume::analysis {

struct UnknownCandidate;
struct UnknownOptions;

/**
 * @brief Generate i-adjective candidates (kanji + conjugation endings)
 *
 * Detects patterns like 寒い, 美しい, 面白かった where kanji stem
 * is followed by hiragana conjugation endings.
 *
 * @param codepoints Text as codepoints
 * @param start_pos Start position (character index)
 * @param char_types Character types for each position
 * @param inflection Inflection analyzer for conjugation detection
 * @param dict_manager Dictionary manager for base form validation (optional)
 * @return Vector of candidates
 */
std::vector<UnknownCandidate> generateAdjectiveCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                                          const std::vector<normalize::CharType>& char_types,
                                                          const grammar::Inflection& inflection,
                                                          const dictionary::DictionaryManager* dict_manager = nullptr);

/**
 * @brief Generate na-adjective candidates (〜的 patterns)
 *
 * Detects kanji compounds ending with 的 (teki) which form
 * na-adjectives like 理性的, 論理的, 感情的.
 *
 * @param codepoints Text as codepoints
 * @param start_pos Start position (character index)
 * @param char_types Character types for each position
 * @param options Unknown word generation options
 * @return Vector of candidates
 */
std::vector<UnknownCandidate> generateNaAdjectiveCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                                            const std::vector<normalize::CharType>& char_types,
                                                            const UnknownOptions& options);

/**
 * @brief Generate hiragana i-adjective candidates (pure hiragana like まずい)
 *
 * Detects pure hiragana i-adjectives and their conjugated forms
 * like まずい, おいしい, まずかった.
 *
 * @param codepoints Text as codepoints
 * @param start_pos Start position (character index)
 * @param char_types Character types for each position
 * @param inflection Inflection analyzer for conjugation detection
 * @param dict_manager Dictionary, used to reject particle + verb sequences
 *        misread as adjectives (にかかった → に + かかる, not にかい)
 * @return Vector of candidates
 */
std::vector<UnknownCandidate> generateHiraganaAdjectiveCandidates(const std::vector<char32_t>& codepoints,
                                                                  size_t start_pos,
                                                                  const std::vector<normalize::CharType>& char_types,
                                                                  const grammar::Inflection& inflection,
                                                                  const dictionary::DictionaryManager* dict_manager);

/**
 * @brief Generate katakana i-adjective candidates (e.g., エモい, キモい, ウザい)
 *
 * Detects patterns like Katakana + い/かった/くない etc.
 * where the katakana stem is followed by i-adjective conjugation endings.
 * This handles slang/internet adjectives that use katakana stems.
 *
 * @param codepoints Text as codepoints
 * @param start_pos Start position (character index)
 * @param char_types Character types for each position
 * @param inflection Inflection analyzer for conjugation detection
 * @return Vector of candidates
 */
std::vector<UnknownCandidate> generateKatakanaAdjectiveCandidates(const std::vector<char32_t>& codepoints,
                                                                  size_t start_pos,
                                                                  const std::vector<normalize::CharType>& char_types,
                                                                  const grammar::Inflection& inflection);

/**
 * @brief Generate i-adjective STEM candidates (e.g., 難し, 美し, 楽し)
 *
 * Detects i-adjective stems when followed by auxiliary patterns like そう, すぎる.
 * MeCab splits these as: 難しそう → 難し(ADJ) + そう(SUFFIX)
 * This function generates the stem (難し) as an ADJ candidate so the lattice
 * can prefer ADJ+AUX split over ADJ一体.
 *
 * Patterns detected:
 * - 〜しそう (難しそう → 難し + そう)
 * - 〜しすぎる (難しすぎる → 難し + すぎる)
 *
 * @param codepoints Text as codepoints
 * @param start_pos Start position (character index)
 * @param char_types Character types for each position
 * @param inflection Inflection analyzer for stem validation
 * @param dict_manager Dictionary manager for verb lookup (to filter verb renyokei)
 * @return Vector of candidates (adjective stems only)
 */
std::vector<UnknownCandidate> generateAdjectiveStemCandidates(
    const std::vector<char32_t>& codepoints, size_t start_pos, const std::vector<normalize::CharType>& char_types,
    const grammar::Inflection& inflection, const dictionary::DictionaryManager* dict_manager = nullptr);

/**
 * @brief Append i-adjective 未然形 conjectural candidates (stem + かろ + う)
 *
 * The presumptive form 高かろう / うれしかろう / よかろう is not produced by
 * inflection analysis, and the surface Xかろ is homographic with a verb
 * volitional stem (分かろう ← 分かる). A candidate is therefore emitted only when
 * the reconstructed base (stem + い) is a decisive i-adjective — a dictionary
 * adjective, or one the inflection analyzer recognizes with adjective-level
 * confidence — and a う follows. Shared by the kanji and pure-hiragana adjective
 * generators.
 *
 * @param scan_start First index where the trailing かろ may begin (kanji_end for
 *        a kanji stem, start_pos for a pure-hiragana stem); the stem before かろ
 *        must be non-empty.
 * @param scan_end   One past the last index to scan (hiragana region end).
 */
void appendIAdjKaroCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t scan_start,
                              size_t scan_end, const grammar::Inflection& inflection,
                              const dictionary::DictionaryManager* dict_manager,
                              std::vector<UnknownCandidate>& candidates);

}  // namespace suzume::analysis

#endif  // SUZUME_ANALYSIS_ADJECTIVE_CANDIDATES_H_
