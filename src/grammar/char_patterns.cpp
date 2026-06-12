/**
 * @file char_patterns.cpp
 * @brief Character pattern utilities for Japanese verb/adjective analysis
 */

#include "char_patterns.h"

#include <unordered_map>
#include <unordered_set>

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

// Onbin, Mizenkei, Renyokei endings are now defined in kana_constants.h
// Use kana:: namespace versions via the aliases in char_patterns.h

// Full i-row hiragana including い for u-verb stems
// Includes voiced/semi-voiced variants for ichidan/godan disambiguation.
const char* kIRowEndings[] = {"い", "き", "ぎ", "し", "じ", "ち", "ぢ", "に", "ひ", "び", "ぴ", "み", "り"};
const size_t kIRowCount = 13;

// E-row hiragana for Ichidan renyokei
const char* kERowEndings[] = {"べ", "め", "せ", "け", "げ", "て", "ね", "れ", "え", "で", "ぜ", "へ", "ぺ"};
const size_t kERowCount = 13;

bool endsWithIRow(std::string_view stem) {
  return endsWithChar(stem, kIRowEndings, kIRowCount);
}

bool endsWithERow(std::string_view stem) {
  return endsWithChar(stem, kERowEndings, kERowCount);
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
  // Static set for O(1) lookup - initialized once
  static const std::unordered_set<std::string_view> kSmallKana = {// Hiragana small kana (拗音・促音)
                                                                  "ょ", "ゃ", "ゅ", "ぁ", "ぃ", "ぅ", "ぇ", "ぉ", "っ",
                                                                  // Katakana small kana
                                                                  "ョ", "ャ", "ュ", "ァ", "ィ", "ゥ", "ェ", "ォ", "ッ"};
  return kSmallKana.count(ch) > 0;
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

bool endsWithARow(std::string_view stem) {
  return endsWithChar(stem, kARowEndings, kARowCount);
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

// Lookup GodanRow by a_row codepoint (cached for efficiency)
const std::pair<VerbType, const Conjugation::GodanRow*>* lookupByARow(char32_t a_row_cp) {
  // Build lookup table on first access
  static const auto kARowLookup = []() {
    std::unordered_map<char32_t, std::pair<VerbType, const Conjugation::GodanRow*>> lookup;
    for (const auto& [type, row] : Conjugation::getGodanRows()) {
      lookup[row.a_row] = {type, &row};
    }
    return lookup;
  }();

  auto it = kARowLookup.find(a_row_cp);
  return it != kARowLookup.end() ? &it->second : nullptr;
}

// Cache for base suffix strings (避免毎回生成)
const std::string& getBaseSuffixCached(char32_t base_vowel) {
  static const auto kBaseSuffixCache = []() {
    std::unordered_map<char32_t, std::string> cache;
    for (const auto& [type, row] : Conjugation::getGodanRows()) {
      cache[row.a_row] = encodeUtf8(row.base_vowel);
    }
    return cache;
  }();
  static const std::string kEmpty;

  // Note: We look up by a_row, not base_vowel
  // This function is called with a_row codepoint
  auto it = kBaseSuffixCache.find(base_vowel);
  return it != kBaseSuffixCache.end() ? it->second : kEmpty;
}
// Cache for A-row suffix strings from U-row
const std::string& getARowSuffixFromURowCached(char32_t u_row_cp) {
  static const auto kCache = []() {
    std::unordered_map<char32_t, std::string> cache;
    for (const auto& [type, row] : Conjugation::getGodanRows()) {
      cache[row.base_vowel] = encodeUtf8(row.a_row);
    }
    return cache;
  }();
  static const std::string kEmpty;
  auto it = kCache.find(u_row_cp);
  return it != kCache.end() ? it->second : kEmpty;
}

// Cache for I-row suffix strings from U-row
const std::string& getIRowSuffixFromURowCached(char32_t u_row_cp) {
  static const auto kCache = []() {
    std::unordered_map<char32_t, std::string> cache;
    for (const auto& [type, row] : Conjugation::getGodanRows()) {
      cache[row.base_vowel] = encodeUtf8(row.i_row);
    }
    return cache;
  }();
  static const std::string kEmpty;
  auto it = kCache.find(u_row_cp);
  return it != kCache.end() ? it->second : kEmpty;
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

// Lookup GodanRow by i_row codepoint (cached for efficiency)
const std::pair<VerbType, const Conjugation::GodanRow*>* lookupByIRow(char32_t i_row_cp) {
  // Build lookup table on first access
  static const auto kIRowLookup = []() {
    std::unordered_map<char32_t, std::pair<VerbType, const Conjugation::GodanRow*>> lookup;
    for (const auto& [type, row] : Conjugation::getGodanRows()) {
      lookup[row.i_row] = {type, &row};
    }
    return lookup;
  }();

  auto it = kIRowLookup.find(i_row_cp);
  return it != kIRowLookup.end() ? &it->second : nullptr;
}

// Cache for base suffix strings from I-row
const std::string& getBaseSuffixFromIRowCached(char32_t i_row_cp) {
  static const auto kBaseSuffixCache = []() {
    std::unordered_map<char32_t, std::string> cache;
    for (const auto& [type, row] : Conjugation::getGodanRows()) {
      cache[row.i_row] = encodeUtf8(row.base_vowel);
    }
    return cache;
  }();
  static const std::string kEmpty;

  auto it = kBaseSuffixCache.find(i_row_cp);
  return it != kBaseSuffixCache.end() ? it->second : kEmpty;
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
