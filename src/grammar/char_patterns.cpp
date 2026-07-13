/**
 * @file char_patterns.cpp
 * @brief Character pattern utilities for Japanese verb/adjective analysis
 */

#include "char_patterns.h"

#include <unordered_map>

#include "core/kana_constants.h"
#include "core/utf8_constants.h"
#include "normalize/utf8.h"

namespace suzume::grammar {

using normalize::encodeUtf8;

namespace {

// =============================================================================
// Character Iteration Templates
// =============================================================================

/**
 * @brief Check if all characters in string match a predicate
 * Iterates through 3-byte UTF-8 sequences (Japanese characters)
 */
template <typename Predicate>
bool allCharsMatch(std::string_view str, Predicate pred) {
  if (str.empty())
    return false;
  size_t pos = 0;
  while (pos < str.size()) {
    if (!utf8::is3ByteUtf8At(str, pos))
      return false;
    char32_t cp = utf8::decode3ByteUtf8At(str, pos);
    if (!pred(cp))
      return false;
    pos += core::kJapaneseCharBytes;
  }
  return true;
}

/**
 * @brief Check if any character in string matches a predicate
 * Handles mixed-byte strings (skips non-3-byte sequences)
 */
template <typename Predicate>
bool anyCharMatches(std::string_view str, Predicate pred) {
  if (str.empty())
    return false;
  size_t pos = 0;
  while (pos + core::kJapaneseCharBytes <= str.size()) {
    if (utf8::is3ByteUtf8At(str, pos)) {
      char32_t cp = utf8::decode3ByteUtf8At(str, pos);
      if (pred(cp))
        return true;
      pos += core::kJapaneseCharBytes;
    } else {
      pos += 1;
    }
  }
  return false;
}

}  // namespace

// Onbin, Mizenkei, Renyokei and i-row/e-row endings are now defined in
// kana_constants.h. Use kana:: namespace versions via the aliases in
// char_patterns.h (kIRow includes い for u-verb stems; kERow is the ichidan
// renyokei set).

bool endsWithIRow(std::string_view stem) {
  return endsWithChar(stem, kIRow, kIRowCount);
}

bool endsWithERow(std::string_view stem) {
  return endsWithChar(stem, kERow, kERowCount);
}

bool endsWithOnbin(std::string_view stem) {
  return endsWithChar(stem, kOnbinEndings, kOnbinCount);
}

bool endsWithRenyokeiMarker(std::string_view stem) {
  return endsWithIRow(stem) || endsWithERow(stem);
}

bool isERowCodepoint(char32_t cp) {
  return kana::isERowCodepoint(cp);
}

bool isIRowCodepoint(char32_t cp) {
  return kana::isIRowCodepoint(cp);
}

bool isARowCodepoint(char32_t cp) {
  return kana::isARowCodepoint(cp);
}

bool isORowCodepoint(char32_t cp) {
  return kana::isORowCodepoint(cp);
}

bool endsWithChar(std::string_view stem, const char* const chars[], size_t count) {
  if (stem.size() < core::kJapaneseCharBytes) {
    return false;
  }
  std::string_view last = utf8::lastChar(stem);
  for (size_t idx = 0; idx < count; ++idx) {
    if (last == chars[idx]) {
      return true;
    }
  }
  return false;
}

bool isAllKanji(std::string_view stem) {
  return allCharsMatch(stem, kana::isKanjiCodepoint);
}

bool endsWithKanji(std::string_view stem) {
  char32_t cp = utf8::decodeLastChar(stem);
  return cp != 0 && kana::isKanjiCodepoint(cp);
}

bool containsKanji(std::string_view stem) {
  return anyCharMatches(stem, kana::isKanjiCodepoint);
}

bool containsKatakana(std::string_view stem) {
  return anyCharMatches(stem, kana::isKatakanaCodepoint);
}

bool isPureHiragana(std::string_view stem) {
  return allCharsMatch(stem, kana::isHiraganaCodepoint);
}

bool isPureKatakana(std::string_view stem) {
  return allCharsMatch(stem, kana::isKatakanaCodepoint);
}

bool isSmallKana(std::string_view ch) {
  char32_t cp = utf8::decodeFirstChar(ch);
  return cp != 0 && kana::isSmallKanaCodepoint(cp);
}

bool startsWithHiragana(std::string_view s) {
  char32_t cp = utf8::decodeFirstChar(s);
  return cp != 0 && kana::isHiraganaCodepoint(cp);
}

// A-row (あ段) endings for verb mizenkei detection
// Includes all mizenkei endings plus あ for completeness
// Note: Slightly broader than kMizenkeiEndings to catch edge cases
const char* kARowEndings[] = {"あ", "か", "が", "さ", "た", "な", "ば", "ま", "ら", "わ"};
const size_t kARowCount = 10;

// O-row (お段) endings: the mizenkei a Godan verb takes before volitional う
const char* kORowEndings[] = {"お", "こ", "ご", "そ", "ぞ", "と", "ど", "の",
                              "ほ", "ぼ", "ぽ", "も", "よ", "ろ", "を"};
const size_t kORowCount = 15;

bool endsWithARow(std::string_view stem) {
  return endsWithChar(stem, kARowEndings, kARowCount);
}

bool endsWithORow(std::string_view stem) {
  return endsWithChar(stem, kORowEndings, kORowCount);
}

char32_t getVowelForChar(char32_t ch) {
  if (kana::isARowCodepoint(ch)) {
    return U'あ';
  }
  if (kana::isIRowCodepoint(ch)) {
    return U'い';
  }
  if (kana::isURowCodepoint(ch)) {
    return U'う';
  }
  if (kana::isERowCodepoint(ch)) {
    return U'え';
  }
  if (kana::isORowCodepoint(ch)) {
    return U'お';
  }

  // Small kana (ゃゅょ) - treat as their base vowel
  if (ch == U'ゃ')
    return U'あ';
  if (ch == U'ゅ')
    return U'う';
  if (ch == U'ょ')
    return U'お';

  // Default to the character itself if not recognized
  return ch;
}

namespace {

// Build (once) and return a codepoint-keyed map derived from
// Conjugation::getGodanRows(). keyFn maps a GodanRow to its char32_t key;
// valFn maps a (VerbType, GodanRow) pair to the stored value. The cache is a
// function-local static, so every distinct (Value, KeyFn, ValueFn)
// instantiation owns its own map built on first use.
template <typename Value, typename KeyFn, typename ValueFn>
const std::unordered_map<char32_t, Value>& godanRowCache(KeyFn keyFn, ValueFn valFn) {
  static const std::unordered_map<char32_t, Value> kCache = [&]() {
    std::unordered_map<char32_t, Value> cache;
    for (const auto& [type, row] : Conjugation::getGodanRows()) {
      cache[keyFn(row)] = valFn(type, row);
    }
    return cache;
  }();
  return kCache;
}

// Pair-valued lookups: return a pointer into the cache, nullptr on miss.
const std::pair<VerbType, const Conjugation::GodanRow*>* lookupByARow(char32_t a_row_cp) {
  const auto& cache = godanRowCache<std::pair<VerbType, const Conjugation::GodanRow*>>(
      [](const auto& row) { return row.a_row; },
      [](VerbType type, const auto& row) { return std::make_pair(type, &row); });
  auto it = cache.find(a_row_cp);
  return it != cache.end() ? &it->second : nullptr;
}

// String-valued caches: return a reference into the cache, empty sentinel on miss.
const std::string& getBaseSuffixCached(char32_t a_row_cp) {
  const auto& cache = godanRowCache<std::string>([](const auto& row) { return row.a_row; },
                                                 [](VerbType, const auto& row) { return encodeUtf8(row.base_vowel); });
  static const std::string kEmpty;
  auto it = cache.find(a_row_cp);
  return it != cache.end() ? it->second : kEmpty;
}

const std::string& getARowSuffixFromURowCached(char32_t u_row_cp) {
  const auto& cache = godanRowCache<std::string>([](const auto& row) { return row.base_vowel; },
                                                 [](VerbType, const auto& row) { return encodeUtf8(row.a_row); });
  static const std::string kEmpty;
  auto it = cache.find(u_row_cp);
  return it != cache.end() ? it->second : kEmpty;
}

const std::string& getIRowSuffixFromURowCached(char32_t u_row_cp) {
  const auto& cache = godanRowCache<std::string>([](const auto& row) { return row.base_vowel; },
                                                 [](VerbType, const auto& row) { return encodeUtf8(row.i_row); });
  static const std::string kEmpty;
  auto it = cache.find(u_row_cp);
  return it != cache.end() ? it->second : kEmpty;
}

}  // namespace

std::string_view godanARowSuffixFromURow(char32_t u_row_cp) {
  return getARowSuffixFromURowCached(u_row_cp);
}

std::string_view godanIRowSuffixFromURow(char32_t u_row_cp) {
  return getIRowSuffixFromURowCached(u_row_cp);
}

std::string_view godanBaseSuffixFromARow(char32_t a_row_cp) {
  // Use cached lookup derived from Conjugation::getGodanRows()
  return getBaseSuffixCached(a_row_cp);
}

VerbType verbTypeFromARowCodepoint(char32_t a_row_cp) {
  // Use cached lookup derived from Conjugation::getGodanRows()
  auto* result = lookupByARow(a_row_cp);
  return result != nullptr ? result->first : VerbType::Unknown;
}

namespace {

// Pair-valued lookup by i_row: return a pointer into the cache, nullptr on miss.
const std::pair<VerbType, const Conjugation::GodanRow*>* lookupByIRow(char32_t i_row_cp) {
  const auto& cache = godanRowCache<std::pair<VerbType, const Conjugation::GodanRow*>>(
      [](const auto& row) { return row.i_row; },
      [](VerbType type, const auto& row) { return std::make_pair(type, &row); });
  auto it = cache.find(i_row_cp);
  return it != cache.end() ? &it->second : nullptr;
}

// String-valued cache keyed by i_row: empty sentinel on miss.
const std::string& getBaseSuffixFromIRowCached(char32_t i_row_cp) {
  const auto& cache = godanRowCache<std::string>([](const auto& row) { return row.i_row; },
                                                 [](VerbType, const auto& row) { return encodeUtf8(row.base_vowel); });
  static const std::string kEmpty;
  auto it = cache.find(i_row_cp);
  return it != cache.end() ? it->second : kEmpty;
}

}  // namespace

std::string_view godanBaseSuffixFromIRow(char32_t i_row_cp) {
  // Use cached lookup derived from Conjugation::getGodanRows()
  return getBaseSuffixFromIRowCached(i_row_cp);
}

VerbType verbTypeFromIRowCodepoint(char32_t i_row_cp) {
  // Use cached lookup derived from Conjugation::getGodanRows()
  auto* result = lookupByIRow(i_row_cp);
  return result != nullptr ? result->first : VerbType::Unknown;
}

bool isMixedHiraganaKanji(std::string_view stem) {
  bool has_hiragana = false;
  bool has_kanji = false;
  size_t pos = 0;
  while (pos + core::kJapaneseCharBytes <= stem.size()) {
    if (utf8::is3ByteUtf8At(stem, pos)) {
      char32_t cp = utf8::decode3ByteUtf8At(stem, pos);
      if (kana::isHiraganaCodepoint(cp)) {
        has_hiragana = true;
      } else if (kana::isKanjiCodepoint(cp)) {
        has_kanji = true;
      }
      if (has_hiragana && has_kanji)
        return true;
      pos += core::kJapaneseCharBytes;
    } else {
      pos += 1;
    }
  }
  return false;
}

}  // namespace suzume::grammar
