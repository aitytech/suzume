/**
 * @file char_patterns.cpp
 * @brief Character pattern utilities for Japanese verb/adjective analysis
 */

#include "char_patterns.h"

#include <array>

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

bool endsWithIRow(std::string_view stem) {
  const char32_t codepoint = utf8::decodeLastChar(stem);
  return codepoint != 0 && kana::isIRowCodepoint(codepoint);
}

bool endsWithERow(std::string_view stem) {
  const char32_t codepoint = utf8::decodeLastChar(stem);
  return codepoint != 0 && kana::isERowCodepoint(codepoint);
}

bool endsWithOnbin(std::string_view stem) {
  const char32_t codepoint = utf8::decodeLastChar(stem);
  return codepoint != 0 && kana::isOnbinCodepoint(codepoint);
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

// A-row (あ段) endings for Godan mizenkei detection.
// This is a DELIBERATE subset of the full phonological a-row recognized by
// kana::isARowCodepoint, which also carries だ/ざ/は/ぱ/や — none of which are
// Godan mizenkei endings.
// In particular だ (copula) and は must NOT match here, so this cannot be
// replaced by the kana::isARowCodepoint predicate the way endsWithORow uses
// isORowCodepoint. The curated list is the source of truth for this grammar.
const char* kARowEndings[] = {"あ", "か", "が", "さ", "た", "な", "ば", "ま", "ら", "わ"};
const size_t kARowCount = 10;

bool endsWithARow(std::string_view stem) {
  return endsWithChar(stem, kARowEndings, kARowCount);
}

// O-row (お段) ending: the mizenkei a Godan verb takes before volitional う.
// Shares the kana::isORowCodepoint source of truth.
bool endsWithORow(std::string_view stem) {
  char32_t cp = utf8::decodeLastChar(stem);
  return cp != 0 && kana::isORowCodepoint(cp);
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

enum class GodanColumn : uint8_t { Base, A, I, E };

using EncodedGodanRow = std::array<std::string, 4>;

const std::array<EncodedGodanRow, Conjugation::kGodanRowCount>& encodedGodanRows() {
  static const std::array<EncodedGodanRow, Conjugation::kGodanRowCount> kEncodedRows = []() {
    std::array<EncodedGodanRow, Conjugation::kGodanRowCount> rows;
    size_t index = 0;
    for (const auto& [type, row] : Conjugation::getGodanRows()) {
      (void)type;
      rows[index++] = {encodeUtf8(row.base_vowel), encodeUtf8(row.a_row), encodeUtf8(row.i_row), encodeUtf8(row.e_row)};
    }
    return rows;
  }();
  return kEncodedRows;
}

char32_t codepointAt(const Conjugation::GodanRow& row, GodanColumn column) {
  switch (column) {
    case GodanColumn::Base:
      return row.base_vowel;
    case GodanColumn::A:
      return row.a_row;
    case GodanColumn::I:
      return row.i_row;
    case GodanColumn::E:
      return row.e_row;
  }
  return 0;
}

size_t columnIndex(GodanColumn column) {
  return static_cast<size_t>(column);
}

std::string_view lookupGodanSuffix(char32_t key, GodanColumn key_column, GodanColumn result_column) {
  const auto& rows = Conjugation::getGodanRows();
  const auto& encoded_rows = encodedGodanRows();
  for (size_t index = 0; index < rows.size(); ++index) {
    if (codepointAt(rows[index].second, key_column) == key) {
      return encoded_rows[index][columnIndex(result_column)];
    }
  }
  return {};
}

VerbType lookupGodanType(char32_t key, GodanColumn key_column) {
  for (const auto& [type, row] : Conjugation::getGodanRows()) {
    if (codepointAt(row, key_column) == key) {
      return type;
    }
  }
  return VerbType::Unknown;
}

}  // namespace

std::string_view godanARowSuffixFromURow(char32_t u_row_cp) {
  return lookupGodanSuffix(u_row_cp, GodanColumn::Base, GodanColumn::A);
}

std::string_view godanIRowSuffixFromURow(char32_t u_row_cp) {
  return lookupGodanSuffix(u_row_cp, GodanColumn::Base, GodanColumn::I);
}

std::string_view godanBaseSuffixFromARow(char32_t a_row_cp) {
  return lookupGodanSuffix(a_row_cp, GodanColumn::A, GodanColumn::Base);
}

VerbType verbTypeFromARowCodepoint(char32_t a_row_cp) {
  return lookupGodanType(a_row_cp, GodanColumn::A);
}

std::string_view godanBaseSuffixFromIRow(char32_t i_row_cp) {
  return lookupGodanSuffix(i_row_cp, GodanColumn::I, GodanColumn::Base);
}

std::string_view godanBaseSuffixFromERow(char32_t e_row_cp) {
  return lookupGodanSuffix(e_row_cp, GodanColumn::E, GodanColumn::Base);
}

VerbType verbTypeFromIRowCodepoint(char32_t i_row_cp) {
  return lookupGodanType(i_row_cp, GodanColumn::I);
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
