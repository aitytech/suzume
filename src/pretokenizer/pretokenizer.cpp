#include "pretokenizer/pretokenizer.h"

#include <algorithm>
#include <cctype>

#include "normalize/char_type.h"
#include "normalize/utf8.h"

namespace suzume::pretokenizer {

namespace {

// Check if byte is ASCII digit
bool isAsciiDigit(char chr) {
  return chr >= '0' && chr <= '9';
}

// Decide whether a period suffix 間 immediately following a time/duration
// counter (N時/N年) should be absorbed into the atomic counter token.
//
// Absorb by default — N時間/N年間 are lexicalized durations, so keeping them
// atomic prevents 間 from being stranded at a segment boundary. The single
// exception is the interval signal: 間 followed by a lone kanji that is itself
// followed by a non-kanji boundary (間隔で, 間近に). There the trailing kanji
// would be stranded, so 間 heads the following interval word (間隔/間近) rather
// than the counter. When 間 is followed by two or more kanji (時間営業, 年間活動)
// or by a non-kanji (年間の, 年間で), the duration reading is correct. This
// mirrors the cost-model discrimination that split_candidates applies to the
// non-pretokenized N分間 path.
//
// `pos_after_kan` is the byte offset of the character following 間.
bool absorbsPeriodKan(std::string_view text, size_t pos_after_kan) {
  if (pos_after_kan >= text.size()) {
    return true;  // 間 at end of input → duration
  }
  size_t idx = pos_after_kan;
  char32_t next_cp = normalize::decodeUtf8(text, idx);
  if (!normalize::isKanjiCodepoint(next_cp)) {
    return true;  // 間 + hiragana/particle → duration (年間の, 年間で)
  }
  if (normalize::isTemporalRelationSuffixKanji(next_cp)) {
    return true;  // 間 + 後/前 → duration + relational suffix (2時間|後, 5年間|前)
  }
  if (idx >= text.size()) {
    // 間 + lone kanji at end: interval only for an 間X compound (…間隔), else duration
    return !normalize::isIntervalCompoundSecondKanji(next_cp);
  }
  char32_t after_cp = normalize::decodeUtf8(text, idx);
  if (normalize::isKanjiCodepoint(after_cp)) {
    return true;  // 2+ kanji → duration (時間営業, 年間活動)
  }
  // 間 + single kanji + non-kanji: interval only when the kanji forms an 間X compound
  // (間隔で), else the counter takes the duration reading (年間|続けた, 時間|半).
  return !normalize::isIntervalCompoundSecondKanji(next_cp);
}

// Keep a duration counter in the analyzer when the closed-class interval
// suffix follows it. This preserves the noun→suffix boundary in 1時間おき,
// which an atomic time pretoken would otherwise hide.
bool hasIntervalSuffix(std::string_view text, size_t pos) {
  constexpr std::string_view kIntervalSuffix{"おき"};
  return text.substr(pos).compare(0, kIntervalSuffix.size(), kIntervalSuffix) == 0;
}

// Check if byte is ASCII alpha
bool isAsciiAlpha(char chr) {
  return (chr >= 'a' && chr <= 'z') || (chr >= 'A' && chr <= 'Z');
}

// Check if byte is ASCII alphanumeric
bool isAsciiAlnum(char chr) {
  return isAsciiDigit(chr) || isAsciiAlpha(chr);
}

// Check if this is a full-width digit (０-９)
bool isFullwidthDigit(char32_t codepoint) {
  return codepoint >= 0xFF10 && codepoint <= 0xFF19;
}

struct IntegerScan {
  size_t end;
  size_t digit_count;
  int value;

  bool empty() const { return digit_count == 0; }
};

// Scan integer digits without allocating. Value is retained for the first two
// digits, which is sufficient for the bounded time fields below.
IntegerScan scanInteger(std::string_view text, size_t pos) {
  size_t idx = pos;
  size_t digit_count = 0;
  int value = 0;
  while (idx < text.size()) {
    char chr = text[idx];
    int digit = 0;
    if (isAsciiDigit(chr)) {
      digit = chr - '0';
      ++idx;
    } else {
      size_t byte_pos = idx;
      char32_t codepoint = normalize::decodeUtf8(text, byte_pos);
      if (isFullwidthDigit(codepoint)) {
        digit = static_cast<int>(codepoint - 0xFF10);
        idx = byte_pos;
      } else {
        break;
      }
    }
    if (digit_count < 2) {
      value = value * 10 + digit;
    }
    ++digit_count;
  }
  return {idx, digit_count, value};
}

// Month and place counters have five conventional prefix spellings. Treating
// the spelling variation as one orthographic class keeps the numeric counter
// atomic without adding entries for individual surface forms.
bool isMonthPlaceCounterPrefix(char32_t codepoint) {
  return codepoint == U'か' || codepoint == U'ヶ' || codepoint == U'ヵ' || codepoint == U'ケ' || codepoint == U'箇';
}

// Address-number output historically normalizes full-width digits to ASCII.
// Keep the owning form only for that caller.
size_t parseIntegerText(std::string_view text, size_t pos, std::string& digits) {
  digits.clear();
  size_t idx = pos;
  while (idx < text.size()) {
    const char chr = text[idx];
    if (isAsciiDigit(chr)) {
      digits += chr;
      ++idx;
      continue;
    }
    size_t byte_pos = idx;
    const char32_t codepoint = normalize::decodeUtf8(text, byte_pos);
    if (!isFullwidthDigit(codepoint)) {
      break;
    }
    digits += static_cast<char>('0' + (codepoint - 0xFF10));
    idx = byte_pos;
  }
  return idx;
}

// Parse digits at position (including decimals), return end position
size_t scanDigits(std::string_view text, size_t pos) {
  size_t idx = pos;
  bool seen_comma = false;
  size_t digits_since_comma = 0;
  while (idx < text.size()) {
    char chr = text[idx];
    if (isAsciiDigit(chr)) {
      ++idx;
      ++digits_since_comma;
    } else if (chr == '.') {
      // Check if followed by digit (decimal point)
      if (idx + 1 < text.size() && isAsciiDigit(text[idx + 1])) {
        ++idx;
        digits_since_comma = 0;
      } else {
        break;
      }
    } else if (chr == ',') {
      // Thousand separator: require one to three digits before the first comma,
      // and exactly three digits between subsequent commas.
      if ((!seen_comma || digits_since_comma == 3) && digits_since_comma > 0 && digits_since_comma <= 3 &&
          idx + 3 < text.size() && isAsciiDigit(text[idx + 1]) && isAsciiDigit(text[idx + 2]) &&
          isAsciiDigit(text[idx + 3]) && (idx + 4 >= text.size() || !isAsciiDigit(text[idx + 4]))) {
        seen_comma = true;
        digits_since_comma = 0;
        ++idx;
      } else {
        break;
      }
    } else {
      // Check for full-width digits
      size_t byte_pos = idx;
      char32_t codepoint = normalize::decodeUtf8(text, byte_pos);
      if (isFullwidthDigit(codepoint)) {
        idx = byte_pos;
        ++digits_since_comma;
      } else {
        break;
      }
    }
  }
  return idx;
}

// Check if text at pos starts with given string (case-insensitive for ASCII)
bool startsWithCI(std::string_view text, size_t pos, std::string_view prefix) {
  if (pos + prefix.size() > text.size()) {
    return false;
  }
  for (size_t idx = 0; idx < prefix.size(); ++idx) {
    char txt_c = text[pos + idx];
    char pre_c = prefix[idx];
    if (std::tolower(static_cast<unsigned char>(txt_c)) != std::tolower(static_cast<unsigned char>(pre_c))) {
      return false;
    }
  }
  return true;
}

void setTokenFromRange(PreToken& token, std::string_view text, size_t start, size_t end, PreTokenType type,
                       core::PartOfSpeech pos) {
  token.surface = std::string(text.substr(start, end - start));
  token.start = start;
  token.end = end;
  token.type = type;
  token.pos = pos;
}

}  // namespace

bool PreTokenizer::tryMatchUrl(std::string_view text, size_t pos, PreToken& token) const {
  // Check for http:// or https://
  bool is_https = startsWithCI(text, pos, "https://");
  bool is_http = !is_https && startsWithCI(text, pos, "http://");

  if (!is_https && !is_http) {
    return false;
  }

  size_t start = pos;
  size_t idx = pos + (is_https ? 8 : 7);  // Skip protocol

  // Match URL characters until whitespace or end
  while (idx < text.size()) {
    char chr = text[idx];
    // URL-safe characters
    if (isAsciiAlnum(chr) || chr == '-' || chr == '.' || chr == '_' || chr == '~' || chr == ':' || chr == '/' ||
        chr == '?' || chr == '#' || chr == '[' || chr == ']' || chr == '@' || chr == '!' || chr == '$' || chr == '&' ||
        chr == '\'' || chr == '(' || chr == ')' || chr == '*' || chr == '+' || chr == ',' || chr == ';' || chr == '=' ||
        chr == '%') {
      ++idx;
    } else {
      break;
    }
  }

  // Remove trailing punctuation that's likely not part of URL
  while (idx > start &&
         (text[idx - 1] == '.' || text[idx - 1] == ',' || text[idx - 1] == ')' || text[idx - 1] == '\'')) {
    --idx;
  }

  if (idx > start + (is_https ? 8 : 7)) {
    setTokenFromRange(token, text, start, idx, PreTokenType::Url,
                      core::PartOfSpeech::Noun);  // Treat URLs as nouns (not symbols)
    return true;
  }

  return false;
}

bool PreTokenizer::tryMatchDate(std::string_view text, size_t pos, PreToken& token) const {
  // Match patterns: MM月DD日, YYYY年MM月DD日, YYYY年MM月, YYYY年, YYYY年度 (fiscal year)
  if (pos > 0 && isAsciiDigit(text[pos - 1])) {
    return false;
  }

  const IntegerScan year = scanInteger(text, pos);
  size_t idx = year.end;

  if (year.empty()) {
    return false;
  }

  // A month and day without a year is still an atomic calendar date. Check it
  // before requiring 年 so 7月18日 does not become two adjacent date tokens.
  if (year.digit_count <= 2 && year.value >= 1 && year.value <= 12 && idx < text.size()) {
    size_t byte_pos = idx;
    char32_t codepoint = normalize::decodeUtf8(text, byte_pos);
    if (codepoint == U'月') {
      const IntegerScan day = scanInteger(text, byte_pos);
      if (!day.empty() && day.digit_count <= 2 && day.value >= 1 && day.value <= 31 && day.end < text.size()) {
        byte_pos = day.end;
        codepoint = normalize::decodeUtf8(text, byte_pos);
        if (codepoint == U'日') {
          setTokenFromRange(token, text, pos, byte_pos, PreTokenType::Date, core::PartOfSpeech::Noun);
          return true;
        }
      }
    }
  }

  if (year.digit_count > 4) {
    return false;
  }

  // Check for 年
  size_t byte_pos = idx;
  if (byte_pos >= text.size()) {
    return false;
  }

  char32_t codepoint = normalize::decodeUtf8(text, byte_pos);
  if (codepoint != U'年') {
    return false;
  }
  idx = byte_pos;
  size_t year_end = idx;  // Position right after "年", before any month match

  // Try to match month
  const IntegerScan month = scanInteger(text, idx);
  size_t month_end = month.end;

  bool matched_month = false;
  if (!month.empty() && month.digit_count <= 2) {
    byte_pos = month_end;
    if (byte_pos < text.size()) {
      codepoint = normalize::decodeUtf8(text, byte_pos);
      if (codepoint == U'月') {
        idx = byte_pos;
        matched_month = true;

        // Try to match day
        const IntegerScan day = scanInteger(text, idx);
        size_t day_end = day.end;

        if (!day.empty() && day.digit_count <= 2) {
          byte_pos = day_end;
          if (byte_pos < text.size()) {
            codepoint = normalize::decodeUtf8(text, byte_pos);
            if (codepoint == U'日') {
              idx = byte_pos;
            }
          }
        }
      }
    }
  }

  // Fiscal year suffix: N年度 (令和6年度, 2024年度予算). Only applies when no
  // month was matched — 年度 marks a fiscal/administrative year, distinct
  // from a calendar date with month/day. Matching it here as part of the
  // atomic date token avoids leaving 度 stranded at a pretokenizer segment
  // boundary, where it has no context to attach to the preceding 年.
  if (!matched_month) {
    byte_pos = year_end;
    if (byte_pos < text.size()) {
      codepoint = normalize::decodeUtf8(text, byte_pos);
      if (codepoint == U'度') {
        idx = byte_pos;
      } else if (codepoint == U'間' && absorbsPeriodKan(text, byte_pos)) {
        // Duration suffix 間 (期間接尾): N年 + 間 = N年間. Absorbed only when it
        // is not the interval signal 間 + stranded-kanji (see absorbsPeriodKan),
        // so 3年間活動 stays 3年間|活動 while 3年間隔 splits as 3年|間隔.
        idx = byte_pos;
      }
    }
  }

  if (idx > pos) {
    setTokenFromRange(token, text, pos, idx, PreTokenType::Date, core::PartOfSpeech::Noun);
    return true;
  }

  return false;
}

bool PreTokenizer::tryMatchCounter(std::string_view text, size_t pos, PreToken& token) const {
  const IntegerScan number = scanInteger(text, pos);
  if (number.empty()) {
    return false;
  }

  size_t idx = number.end;
  if (idx >= text.size()) {
    return false;
  }
  const char32_t prefix = normalize::decodeUtf8(text, idx);
  if (!isMonthPlaceCounterPrefix(prefix) || idx >= text.size()) {
    return false;
  }

  const char32_t unit = normalize::decodeUtf8(text, idx);
  if (unit != U'月' && unit != U'所') {
    return false;
  }

  setTokenFromRange(token, text, pos, idx, PreTokenType::Counter, core::PartOfSpeech::Noun);
  return true;
}

bool PreTokenizer::tryMatchCurrency(std::string_view text, size_t pos, PreToken& token) const {
  // Match patterns: 数字+[万億兆]?円
  size_t idx = scanDigits(text, pos);

  if (idx == pos) {
    return false;
  }

  size_t byte_pos = idx;
  if (byte_pos >= text.size()) {
    return false;
  }

  char32_t codepoint = normalize::decodeUtf8(text, byte_pos);

  // Optional: 万, 億, 兆
  if (codepoint == U'万' || codepoint == U'億' || codepoint == U'兆') {
    idx = byte_pos;
    if (byte_pos < text.size()) {
      codepoint = normalize::decodeUtf8(text, byte_pos);
    } else {
      return false;
    }
  }

  // Required: 円
  if (codepoint != U'円') {
    return false;
  }
  idx = byte_pos;

  setTokenFromRange(token, text, pos, idx, PreTokenType::Currency, core::PartOfSpeech::Noun);
  return true;
}

bool PreTokenizer::tryMatchStorage(std::string_view text, size_t pos, PreToken& token) const {
  // Match patterns: 数字[KMGT]?B
  size_t idx = scanDigits(text, pos);

  if (idx == pos) {
    return false;
  }

  if (idx >= text.size()) {
    return false;
  }

  // Optional: K, M, G, T prefix
  char prefix = text[idx];
  if (prefix == 'K' || prefix == 'k' || prefix == 'M' || prefix == 'm' || prefix == 'G' || prefix == 'g' ||
      prefix == 'T' || prefix == 't') {
    ++idx;
  }

  // Required: B
  if (idx >= text.size() || (text[idx] != 'B' && text[idx] != 'b')) {
    return false;
  }
  ++idx;

  // Reject when the byte suffix is immediately followed by another ASCII letter
  // (e.g. the 'p' of Mbps/kbps/bps), which marks a network bit-rate unit rather
  // than a storage size; let it fall through to normal tokenization instead.
  if (idx < text.size()) {
    char next = text[idx];
    if ((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z')) {
      return false;
    }
  }

  setTokenFromRange(token, text, pos, idx, PreTokenType::Storage, core::PartOfSpeech::Noun);
  return true;
}

bool PreTokenizer::tryMatchVersion(std::string_view text, size_t pos, PreToken& token) const {
  // Match patterns: v?数字.数字(.数字)*
  size_t idx = pos;

  // Optional 'v' or 'V' prefix
  if (idx < text.size() && (text[idx] == 'v' || text[idx] == 'V')) {
    ++idx;
  }

  // First number (integer-only scan avoids consuming decimal points)
  IntegerScan number = scanInteger(text, idx);
  size_t num_end = number.end;
  if (number.empty()) {
    return false;
  }
  idx = num_end;

  // Must have at least one .number
  if (idx >= text.size() || text[idx] != '.') {
    return false;
  }
  ++idx;

  number = scanInteger(text, idx);
  num_end = number.end;
  if (number.empty()) {
    return false;
  }
  idx = num_end;

  // Additional .number segments
  while (idx < text.size() && text[idx] == '.') {
    size_t next = idx + 1;
    number = scanInteger(text, next);
    num_end = number.end;
    if (number.empty()) {
      break;
    }
    idx = num_end;
  }

  setTokenFromRange(token, text, pos, idx, PreTokenType::Version, core::PartOfSpeech::Noun);
  return true;
}

bool PreTokenizer::tryMatchPercentage(std::string_view text, size_t pos, PreToken& token) const {
  // Match patterns: 数字%
  size_t idx = scanDigits(text, pos);

  if (idx == pos) {
    return false;
  }

  if (idx >= text.size()) {
    return false;
  }

  // Check for % (ASCII) or ％ (full-width)
  char chr = text[idx];
  if (chr == '%') {
    ++idx;
  } else {
    size_t byte_pos = idx;
    char32_t codepoint = normalize::decodeUtf8(text, byte_pos);
    if (codepoint == U'％') {
      idx = byte_pos;
    } else {
      return false;
    }
  }

  setTokenFromRange(token, text, pos, idx, PreTokenType::Percentage, core::PartOfSpeech::Noun);
  return true;
}

bool PreTokenizer::tryMatchAddressNumber(std::string_view text, size_t pos, PreToken& token) const {
  // Match address number patterns: 1-2-3, 1-2-3-4 etc.
  // Pattern: digit(s) + (hyphen + digit(s))+
  std::string num_str;
  size_t idx = parseIntegerText(text, pos, num_str);

  if (num_str.empty()) {
    return false;
  }

  // Must have at least one hyphen-number sequence
  bool has_hyphen = false;
  std::string surface = num_str;

  while (idx < text.size() && text[idx] == '-') {
    size_t hyphen_pos = idx;
    ++idx;

    // Parse the next number
    std::string next_num;
    size_t next_end = parseIntegerText(text, idx, next_num);
    if (next_num.empty()) {
      // No number after hyphen, revert
      idx = hyphen_pos;
      break;
    }

    surface += '-';
    surface += next_num;
    idx = next_end;
    has_hyphen = true;
  }

  if (!has_hyphen) {
    return false;
  }

  token.surface = surface;
  token.start = pos;
  token.end = idx;
  token.type = PreTokenType::Number;
  token.pos = core::PartOfSpeech::Noun;
  return true;
}

bool PreTokenizer::tryMatchEmail(std::string_view text, size_t pos, PreToken& token) const {
  // Match email: local-part@domain
  // Check that we're not starting in the middle of an email-like string
  if (pos > 0) {
    char prev = text[pos - 1];
    if (isAsciiAlnum(prev) || prev == '.' || prev == '-' || prev == '_' || prev == '+' || prev == '@') {
      return false;
    }
  }

  size_t start = pos;
  size_t idx = pos;

  // Parse local-part: alphanumeric, dot, hyphen, underscore, plus
  while (idx < text.size()) {
    char chr = text[idx];
    if (isAsciiAlnum(chr) || chr == '.' || chr == '-' || chr == '_' || chr == '+') {
      ++idx;
    } else {
      break;
    }
  }

  // Local-part must not be empty and must not start/end with dot
  if (idx == start || text[start] == '.' || text[idx - 1] == '.') {
    return false;
  }

  // Must have @
  if (idx >= text.size() || text[idx] != '@') {
    return false;
  }
  ++idx;

  // Parse domain: alphanumeric, dot, hyphen
  size_t domain_start = idx;
  while (idx < text.size()) {
    char chr = text[idx];
    if (isAsciiAlnum(chr) || chr == '.' || chr == '-') {
      ++idx;
    } else {
      break;
    }
  }

  // Domain must not be empty and must contain at least one dot
  if (idx == domain_start) {
    return false;
  }

  std::string_view domain = text.substr(domain_start, idx - domain_start);
  if (domain.find('.') == std::string_view::npos) {
    return false;
  }

  // Domain must not start/end with dot or hyphen
  if (domain[0] == '.' || domain[0] == '-' || domain[domain.size() - 1] == '.' || domain[domain.size() - 1] == '-') {
    return false;
  }

  setTokenFromRange(token, text, start, idx, PreTokenType::Email, core::PartOfSpeech::Noun);
  return true;
}

bool PreTokenizer::tryMatchTime(std::string_view text, size_t pos, PreToken& token) const {
  // Match patterns: HH時, HH時MM分, HH時MM分SS秒
  // Check that we're not starting in the middle of a number
  if (pos > 0) {
    char prev = text[pos - 1];
    if (isAsciiDigit(prev)) {
      return false;
    }
    // Also check for full-width digits before this position
    // This requires looking back at UTF-8 boundary
  }

  const IntegerScan hour = scanInteger(text, pos);
  size_t idx = hour.end;

  if (hour.empty() || hour.digit_count > 2) {
    return false;
  }

  // Validate hour (0-23 or 1-24)
  if (hour.value > 24) {
    return false;
  }

  // Check for 時
  size_t byte_pos = idx;
  if (byte_pos >= text.size()) {
    return false;
  }

  char32_t codepoint = normalize::decodeUtf8(text, byte_pos);
  if (codepoint != U'時') {
    return false;
  }
  idx = byte_pos;

  // A duration starts with 時間 rather than 時. Consume 間 before scanning
  // its optional minute/second fields so 1時間15分 remains one quantity.
  // Leave 間 to the following span when it heads an interval word (5時|間隔).
  size_t duration_pos = idx;
  if (duration_pos < text.size()) {
    char32_t duration_marker = normalize::decodeUtf8(text, duration_pos);
    if (duration_marker == U'間' && absorbsPeriodKan(text, duration_pos)) {
      idx = duration_pos;
    }
  }

  // Try to match minutes
  const IntegerScan minute = scanInteger(text, idx);
  size_t min_end = minute.end;

  if (!minute.empty() && minute.digit_count <= 2) {
    if (minute.value <= 59) {
      byte_pos = min_end;
      if (byte_pos < text.size()) {
        codepoint = normalize::decodeUtf8(text, byte_pos);
        if (codepoint == U'分') {
          idx = byte_pos;

          // Try to match seconds
          const IntegerScan second = scanInteger(text, idx);
          size_t sec_end = second.end;

          if (!second.empty() && second.digit_count <= 2) {
            if (second.value <= 59) {
              byte_pos = sec_end;
              if (byte_pos < text.size()) {
                codepoint = normalize::decodeUtf8(text, byte_pos);
                if (codepoint == U'秒') {
                  idx = byte_pos;
                }
              }
            }
          }
        }
      }
    }
  }

  // Duration suffix 間 (期間接尾): HH時 + 間 = HH時間. Absorbed only when it is
  // not the interval signal 間 + stranded-kanji (see absorbsPeriodKan), so
  // 24時間営業 stays 24時間|営業 while 5時間隔 splits as 5時|間隔.
  size_t kan_pos = idx;
  if (kan_pos < text.size()) {
    char32_t codepoint_kan = normalize::decodeUtf8(text, kan_pos);
    if (codepoint_kan == U'間' && absorbsPeriodKan(text, kan_pos)) {
      idx = kan_pos;
    }
  }

  if (idx > pos) {
    if (hasIntervalSuffix(text, idx)) {
      return false;
    }
    setTokenFromRange(token, text, pos, idx, PreTokenType::Time, core::PartOfSpeech::Noun);
    return true;
  }

  return false;
}

// Check if codepoint is valid for hashtag content
// Allows: Katakana, Kanji, alphanumeric, underscore
// Note: Hiragana is excluded to avoid including particles like を, は, が
//       This means #ありがとう style hashtags won't work, but they are rare
bool isHashtagChar(char32_t codepoint) {
  // ASCII alphanumeric and underscore
  if ((codepoint >= 'a' && codepoint <= 'z') || (codepoint >= 'A' && codepoint <= 'Z') ||
      (codepoint >= '0' && codepoint <= '9') || codepoint == '_') {
    return true;
  }
  // Katakana (U+30A0-U+30FF) - allowed for hashtags
  if (codepoint >= 0x30A0 && codepoint <= 0x30FF) {
    return true;
  }
  // CJK Unified Ideographs (U+4E00-U+9FFF) - allowed for hashtags
  if (codepoint >= 0x4E00 && codepoint <= 0x9FFF) {
    return true;
  }
  // Full-width alphanumeric (U+FF00-U+FF5E)
  if (codepoint >= 0xFF00 && codepoint <= 0xFF5E) {
    return true;
  }
  // Hiragana is NOT allowed - to avoid particles being included
  return false;
}

bool PreTokenizer::tryMatchHashtag(std::string_view text, size_t pos, PreToken& token) const {
  // Match pattern: # + (Japanese chars | alphanumeric | underscore)+
  if (pos >= text.size()) {
    return false;
  }

  // Check for # (ASCII) or ＃ (full-width)
  size_t idx = pos;
  size_t byte_pos = pos;
  char32_t codepoint = normalize::decodeUtf8(text, byte_pos);

  if (codepoint != '#' && codepoint != U'＃') {
    return false;
  }
  idx = byte_pos;

  // Must have at least one valid hashtag character
  if (idx >= text.size()) {
    return false;
  }

  size_t content_start = idx;
  while (idx < text.size()) {
    byte_pos = idx;
    codepoint = normalize::decodeUtf8(text, byte_pos);
    if (isHashtagChar(codepoint)) {
      idx = byte_pos;
    } else {
      break;
    }
  }

  // Must have content after #
  if (idx == content_start) {
    return false;
  }

  setTokenFromRange(token, text, pos, idx, PreTokenType::Hashtag, core::PartOfSpeech::Noun);
  return true;
}

bool PreTokenizer::tryMatchMention(std::string_view text, size_t pos, PreToken& token) const {
  // Match pattern: @ + (alphanumeric | underscore)+
  // Must NOT be followed by domain (that would be email)
  if (pos >= text.size()) {
    return false;
  }

  // Check for @ (ASCII only for mentions)
  if (text[pos] != '@') {
    return false;
  }
  size_t idx = pos + 1;

  // Must have at least one valid character
  if (idx >= text.size()) {
    return false;
  }

  // Parse username: alphanumeric and underscore only
  size_t content_start = idx;
  while (idx < text.size()) {
    char chr = text[idx];
    if (isAsciiAlnum(chr) || chr == '_') {
      ++idx;
    } else {
      break;
    }
  }

  // Must have content after @
  if (idx == content_start) {
    return false;
  }

  // Check this is NOT an email (no @ followed by domain with dot)
  // If followed by @, it's invalid
  // If the content contains a dot followed by more chars, check if it's email-like
  // Simple check: mentions don't have dots in username typically
  // Also check if there's more content that looks like a domain
  if (idx < text.size() && text[idx] == '.') {
    // Might be email-like, check for domain pattern
    size_t check_pos = idx + 1;
    while (check_pos < text.size()) {
      char chr = text[check_pos];
      if (isAsciiAlnum(chr) || chr == '.' || chr == '-') {
        ++check_pos;
      } else {
        break;
      }
    }
    // If we found something that looks like a domain, skip this as mention
    if (check_pos > idx + 1) {
      return false;
    }
  }

  setTokenFromRange(token, text, pos, idx, PreTokenType::Mention, core::PartOfSpeech::Noun);
  return true;
}

bool PreTokenizer::tryMatchAsciiWithDots(std::string_view text, size_t pos, PreToken& token) const {
  // Match ASCII alphanumeric sequences with embedded dots
  // Pattern: alnum+ (. alnum+)+
  // e.g., example.com, foo.bar.baz
  // Must have at least one dot to distinguish from regular ASCII sequences

  if (pos >= text.size() || !isAsciiAlnum(text[pos])) {
    return false;
  }

  size_t start = pos;
  size_t idx = pos;
  bool has_dot = false;

  while (idx < text.size()) {
    char chr = text[idx];
    if (isAsciiAlnum(chr)) {
      ++idx;
    } else if (chr == '.' && idx + 1 < text.size() && isAsciiAlnum(text[idx + 1])) {
      // Dot followed by alphanumeric
      has_dot = true;
      ++idx;
    } else {
      break;
    }
  }

  // Must have at least one dot and not end with dot
  if (!has_dot || idx <= start + 2) {
    return false;
  }

  setTokenFromRange(token, text, start, idx, PreTokenType::AsciiSeq, core::PartOfSpeech::Noun);
  return true;
}

bool PreTokenizer::isSentenceBoundary(char32_t codepoint) const {
  return codepoint == U'。' || codepoint == U'！' || codepoint == U'？' || codepoint == U'!' || codepoint == U'?' ||
         codepoint == U'\n' || codepoint == U'・';  // Nakaguro: token boundary (splits カタカナ・カタカナ)
}

PreTokenResult PreTokenizer::process(std::string_view text) const {
  PreTokenResult result;

  if (text.empty()) {
    return result;
  }

  size_t pos = 0;
  size_t span_start = 0;

  while (pos < text.size()) {
    PreToken token;

    // Try to match patterns in priority order
    // Note: URL must come before Email (URLs contain @ in some cases)
    // Note: Email must come before Mention (emails have @ followed by domain)
    // Note: Percentage must come before Version to avoid "3.14%" being parsed as version
    // Note: Date must come before Time (日付 includes 日 which looks like time suffix)
    if (tryMatchUrl(text, pos, token) || tryMatchEmail(text, pos, token) || tryMatchHashtag(text, pos, token) ||
        tryMatchMention(text, pos, token) || tryMatchDate(text, pos, token) || tryMatchCounter(text, pos, token) ||
        tryMatchTime(text, pos, token) || tryMatchCurrency(text, pos, token) || tryMatchStorage(text, pos, token) ||
        tryMatchPercentage(text, pos, token) || tryMatchAddressNumber(text, pos, token) ||
        tryMatchVersion(text, pos, token) || tryMatchAsciiWithDots(text, pos, token)) {
      // Add span before this token if any
      if (pos > span_start) {
        result.spans.push_back({span_start, pos});
      }

      result.tokens.push_back(token);
      pos = token.end;
      span_start = pos;
      continue;
    }

    // Check for sentence boundary
    size_t byte_pos = pos;
    char32_t codepoint = normalize::decodeUtf8(text, byte_pos);

    if (isSentenceBoundary(codepoint)) {
      // Add span before boundary if any
      if (pos > span_start) {
        result.spans.push_back({span_start, pos});
      }

      // Add boundary token
      setTokenFromRange(token, text, pos, byte_pos, PreTokenType::Boundary, core::PartOfSpeech::Symbol);
      result.tokens.push_back(token);

      pos = byte_pos;
      span_start = pos;
      continue;
    }

    // Move to next character
    pos = byte_pos;
  }

  // Add final span if any
  if (pos > span_start) {
    result.spans.push_back({span_start, pos});
  }

  return result;
}

}  // namespace suzume::pretokenizer
