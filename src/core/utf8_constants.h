#ifndef SUZUME_CORE_UTF8_CONSTANTS_H_
#define SUZUME_CORE_UTF8_CONSTANTS_H_

#include <cstddef>
#include <initializer_list>

namespace suzume::core {

// =============================================================================
// UTF-8 Byte Length Constants
// =============================================================================
// Japanese characters (hiragana, katakana, kanji) are encoded as 3 bytes in UTF-8.
// These constants make byte-level string operations self-documenting.
//
// UTF-8 encoding ranges:
// - U+3040-309F (Hiragana): 3 bytes each
// - U+30A0-30FF (Katakana): 3 bytes each
// - U+4E00-9FFF (CJK Unified Ideographs): 3 bytes each
// =============================================================================

/// Number of bytes for a single Japanese character in UTF-8
/// Applies to hiragana, katakana, and kanji
constexpr size_t kJapaneseCharBytes = 3;

// =============================================================================
// Common Multi-Character Lengths
// =============================================================================

/// Length of two Japanese characters in bytes (e.g., "そう", "ない", "たい")
constexpr size_t kTwoJapaneseCharBytes = kJapaneseCharBytes * 2;  // 6

/// Length of three Japanese characters in bytes
constexpr size_t kThreeJapaneseCharBytes = kJapaneseCharBytes * 3;  // 9

/// Length of four Japanese characters in bytes
constexpr size_t kFourJapaneseCharBytes = kJapaneseCharBytes * 4;  // 12

/// Length of five Japanese characters in bytes (e.g., "させられる", "させられた")
constexpr size_t kFiveJapaneseCharBytes = kJapaneseCharBytes * 5;  // 15

/// Length of six Japanese characters in bytes
constexpr size_t kSixJapaneseCharBytes = kJapaneseCharBytes * 6;  // 18

/// Number of UTF-8 bytes required to encode a single Unicode codepoint.
/// Returns 1/2/3/4; codepoints above U+10FFFF are treated as 4 bytes.
[[nodiscard]] inline constexpr size_t utf8ByteLength(char32_t codepoint) noexcept {
  if (codepoint < 0x80)
    return 1;
  if (codepoint < 0x800)
    return 2;
  if (codepoint < 0x10000)
    return 3;
  return 4;
}

// =============================================================================
// Hiragana Codepoint Constants for Auxiliary Patterns
// =============================================================================
// Common hiragana codepoints used in verb/auxiliary pattern detection.
// Using named constants improves readability and maintainability.

namespace hiragana {

// Polite auxiliary ます
constexpr char32_t kMa = U'ま';  // ま (0x307E)
constexpr char32_t kSu = U'す';  // す (0x3059)

// Negative auxiliary ない and its conjugations (なく、なかっ、なけれ)
constexpr char32_t kNa = U'な';  // な (0x306A)
constexpr char32_t kI = U'い';   // い (0x3044)
constexpr char32_t kKu = U'く';  // く (0x304F) - for なく (adverbial)
constexpr char32_t kKa = U'か';  // か (0x304B) - for なかっ (ta-connection)
constexpr char32_t kKe = U'け';  // け (0x3051) - for なけれ (conditional)
constexpr char32_t kKi = U'き';  // き (0x304D) - for なきゃ (colloquial conditional contraction)

// Passive/potential られる
constexpr char32_t kRa = U'ら';  // ら (0x3089)
constexpr char32_t kRe = U'れ';  // れ (0x308C)
constexpr char32_t kRu = U'る';  // る (0x308B)

// Te/ta forms
constexpr char32_t kTe = U'て';   // て (0x3066)
constexpr char32_t kTa = U'た';   // た (0x305F)
constexpr char32_t kTo = U'と';   // と (0x3068)
constexpr char32_t kChi = U'ち';  // ち (0x3061)

// Sokuon (促音)
constexpr char32_t kSmallTsu = U'っ';  // っ (0x3063)

// Common particles
constexpr char32_t kO = U'お';  // お (0x304A) - prefix marker

// Volitional auxiliary よう/う
constexpr char32_t kYo = U'よ';  // よ (0x3088)
constexpr char32_t kU = U'う';   // う (0x3046)
constexpr char32_t kN = U'ん';   // ん (0x3093)

// Causative auxiliary させる
constexpr char32_t kSa = U'さ';  // さ (0x3055)
constexpr char32_t kSe = U'せ';  // せ (0x305B)

// Classical negative auxiliary ず/ざる/ざれ (attaches to mizenkei)
constexpr char32_t kZu = U'ず';  // ず (0x305A) - 終止形 / ずに
constexpr char32_t kZa = U'ざ';  // ざ (0x3056) - ざる (連体) / ざれ (已然)

}  // namespace hiragana

}  // namespace suzume::core

// =============================================================================
// UTF-8 String Utility Functions
// =============================================================================
// Zero-overhead inline functions for common Japanese string operations.
// These replace verbose patterns like:
//   surface.substr(surface.size() - kTwoJapaneseCharBytes) == "そう"
// With more readable:
//   utf8::endsWith(surface, "そう")

#include <string_view>

namespace utf8 {

using suzume::core::kJapaneseCharBytes;
using suzume::core::kThreeJapaneseCharBytes;
using suzume::core::kTwoJapaneseCharBytes;

/// Check if string ends with the given suffix
/// @param s The string to check
/// @param suffix The suffix to look for
/// @return true if s ends with suffix
[[nodiscard]] inline constexpr bool endsWith(std::string_view s, std::string_view suffix) noexcept {
  return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

/// Check if string starts with the given prefix
/// @param s The string to check
/// @param prefix The prefix to look for
/// @return true if s starts with prefix
[[nodiscard]] inline constexpr bool startsWith(std::string_view s, std::string_view prefix) noexcept {
  return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

/// Check if string contains the given substring
/// @param s The string to check
/// @param substr The substring to look for
/// @return true if s contains substr
[[nodiscard]] inline constexpr bool contains(std::string_view s, std::string_view substr) noexcept {
  return s.find(substr) != std::string_view::npos;
}

namespace detail {

/// The one membership scan behind the containsAny/equalsAny/…Any families.
/// Both the braced-list and the named-table overloads route here: a braced
/// list cannot deduce a range parameter, so the pair of entry points is
/// required, but the test itself is stated once.
template <typename Values, typename Match>
[[nodiscard]] bool anyOf(const Values& values, Match match) noexcept {
  for (const std::string_view value : values) {
    if (match(value)) {
      return true;
    }
  }
  return false;
}

constexpr auto kContains = [](std::string_view s, std::string_view pattern) noexcept {
  return s.find(pattern) != std::string_view::npos;
};
constexpr auto kEquals = [](std::string_view s, std::string_view value) noexcept { return s == value; };
constexpr auto kEndsWith = [](std::string_view s, std::string_view suffix) noexcept {
  return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
};
constexpr auto kStartsWith = [](std::string_view s, std::string_view prefix) noexcept {
  return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
};

}  // namespace detail

/// Declare a membership family over a braced list and over a named fixed-size
/// table, so a table never has to carry a separate element count.
#define SUZUME_UTF8_ANY_FAMILY(name, match)                                                                     \
  [[nodiscard]] inline bool name(std::string_view s, std::initializer_list<std::string_view> values) noexcept { \
    return detail::anyOf(values, [s](std::string_view value) { return match(s, value); });                      \
  }                                                                                                             \
  template <typename Entry, size_t Size>                                                                        \
  [[nodiscard]] bool name(std::string_view s, const Entry(&values)[Size]) noexcept {                            \
    return detail::anyOf(values, [s](std::string_view value) { return match(s, value); });                      \
  }

/// True if s contains any of the given patterns.
SUZUME_UTF8_ANY_FAMILY(containsAny, detail::kContains)
/// True if s equals any of the given values.
SUZUME_UTF8_ANY_FAMILY(equalsAny, detail::kEquals)
/// True if s ends with any of the given suffixes.
SUZUME_UTF8_ANY_FAMILY(endsWithAny, detail::kEndsWith)
/// True if s starts with any of the given prefixes.
SUZUME_UTF8_ANY_FAMILY(startsWithAny, detail::kStartsWith)

#undef SUZUME_UTF8_ANY_FAMILY

/// Get the last N bytes of a string as a string_view
/// @param s The source string
/// @param n Number of bytes to get
/// @return The last N bytes, or empty if s.size() < n
[[nodiscard]] inline constexpr std::string_view lastNBytes(std::string_view s, size_t n) noexcept {
  return s.size() >= n ? s.substr(s.size() - n) : std::string_view{};
}

/// Get the first N bytes of a string as a string_view
/// @param s The source string
/// @param n Number of bytes to get
/// @return The first N bytes, or entire string if s.size() < n
[[nodiscard]] inline constexpr std::string_view firstNBytes(std::string_view s, size_t n) noexcept {
  return s.substr(0, n);
}

/// Get string without the last N bytes
/// @param s The source string
/// @param n Number of bytes to drop from end
/// @return String without last N bytes, or empty if s.size() < n
[[nodiscard]] inline constexpr std::string_view dropLast(std::string_view s, size_t n) noexcept {
  return s.size() >= n ? s.substr(0, s.size() - n) : std::string_view{};
}

/// Get string without the first N bytes
/// @param s The source string
/// @param n Number of bytes to drop from start
/// @return String without first N bytes, or empty if s.size() < n
[[nodiscard]] inline constexpr std::string_view dropFirst(std::string_view s, size_t n) noexcept {
  return s.size() >= n ? s.substr(n) : std::string_view{};
}

// Convenience aliases for common Japanese character operations
// These use byte counts, not character counts

/// Get the last Japanese character (3 bytes)
[[nodiscard]] inline constexpr std::string_view lastChar(std::string_view s) noexcept {
  return lastNBytes(s, kJapaneseCharBytes);
}

/// Get the last 2 Japanese characters (6 bytes)
[[nodiscard]] inline constexpr std::string_view last2Chars(std::string_view s) noexcept {
  return lastNBytes(s, kTwoJapaneseCharBytes);
}

/// Get the last 3 Japanese characters (9 bytes)
[[nodiscard]] inline constexpr std::string_view last3Chars(std::string_view s) noexcept {
  return lastNBytes(s, kThreeJapaneseCharBytes);
}

/// Drop the last Japanese character (3 bytes)
[[nodiscard]] inline constexpr std::string_view dropLastChar(std::string_view s) noexcept {
  return dropLast(s, kJapaneseCharBytes);
}

/// Drop the last 2 Japanese characters (6 bytes)
[[nodiscard]] inline constexpr std::string_view dropLast2Chars(std::string_view s) noexcept {
  return dropLast(s, kTwoJapaneseCharBytes);
}

// =============================================================================
// UTF-8 Decoding Utilities for Japanese Characters
// =============================================================================
// These functions decode 3-byte UTF-8 sequences (Japanese characters).
// They replace the common pattern:
//   const unsigned char* ptr = reinterpret_cast<const unsigned char*>(s.data() + pos);
//   if ((ptr[0] & 0xF0) != 0xE0) { return false; }
//   char32_t cp = ((ptr[0] & 0x0F) << 12) | ((ptr[1] & 0x3F) << 6) | (ptr[2] & 0x3F);

/// Check if byte at position starts a 3-byte UTF-8 sequence
/// @param s The string to check
/// @param pos Byte position
/// @return true if position starts a 3-byte sequence (Japanese character)
[[nodiscard]] inline bool is3ByteUtf8At(std::string_view s, size_t pos) noexcept {
  if (pos + kJapaneseCharBytes > s.size())
    return false;
  auto byte = static_cast<unsigned char>(s[pos]);
  return (byte & 0xF0) == 0xE0;
}

/// Decode 3-byte UTF-8 at position (assumes valid 3-byte sequence)
/// @param s The string to decode from
/// @param pos Byte position (must be valid 3-byte start)
/// @return Unicode codepoint
[[nodiscard]] inline char32_t decode3ByteUtf8At(std::string_view s, size_t pos) noexcept {
  const auto* ptr = reinterpret_cast<const unsigned char*>(s.data() + pos);
  return static_cast<char32_t>(((ptr[0] & 0x0F) << 12) | ((ptr[1] & 0x3F) << 6) | (ptr[2] & 0x3F));
}

/// Decode last Japanese character as codepoint
/// @param s The string (must have at least 3 bytes)
/// @return Unicode codepoint, or 0 if invalid
[[nodiscard]] inline char32_t decodeLastChar(std::string_view s) noexcept {
  if (s.size() < kJapaneseCharBytes)
    return 0;
  size_t pos = s.size() - kJapaneseCharBytes;
  if (!is3ByteUtf8At(s, pos))
    return 0;
  return decode3ByteUtf8At(s, pos);
}

/// Decode first Japanese character as codepoint
/// @param s The string (must have at least 3 bytes)
/// @return Unicode codepoint, or 0 if invalid
[[nodiscard]] inline char32_t decodeFirstChar(std::string_view s) noexcept {
  if (!is3ByteUtf8At(s, 0))
    return 0;
  return decode3ByteUtf8At(s, 0);
}

}  // namespace utf8

#endif  // SUZUME_CORE_UTF8_CONSTANTS_H_
