#include "normalizer.h"

#include "char_type.h"
#include "unicode_tables.h"
#include "utf8.h"

namespace suzume::normalize {

namespace {

// Full-width ASCII to half-width (with case preservation option)
char32_t fullwidthToHalfwidth(char32_t codepoint, bool preserve_case) {
  // Full-width digits (０-９) -> half-width (0-9)
  if (codepoint >= 0xFF10 && codepoint <= 0xFF19) {
    return codepoint - 0xFF10 + '0';
  }
  // Full-width uppercase (Ａ-Ｚ)
  if (codepoint >= 0xFF21 && codepoint <= 0xFF3A) {
    if (preserve_case) {
      return codepoint - 0xFF21 + 'A';  // Keep uppercase
    }
    return codepoint - 0xFF21 + 'a';  // Convert to lowercase
  }
  // Full-width lowercase (ａ-ｚ) -> half-width lowercase (a-z)
  if (codepoint >= 0xFF41 && codepoint <= 0xFF5A) {
    return codepoint - 0xFF41 + 'a';
  }
  // Half-width uppercase (A-Z)
  if (codepoint >= 'A' && codepoint <= 'Z') {
    if (preserve_case) {
      return codepoint;  // Keep uppercase
    }
    return codepoint - 'A' + 'a';  // Convert to lowercase
  }
  return codepoint;
}

// Full-width ASCII to half-width (default: lowercase)
char32_t fullwidthToHalfwidth(char32_t codepoint) {
  return fullwidthToHalfwidth(codepoint, false);
}

// Half-width katakana to full-width
char32_t halfwidthKatakanaToFullwidth(char32_t codepoint) {
  // Half-width katakana range: U+FF66-U+FF9F
  if (codepoint >= 0xFF66 && codepoint <= 0xFF9F) {
    // Simplified mapping (main characters only)
    // Real implementation would need complete mapping table
    // Keep char32_t even though all current targets are in the BMP. A uint16_t
    // table experiment saved about 83 raw WASM bytes but increased final gzip
    // by about 99 bytes due to the changed data encoding, so the narrower type
    // is counterproductive for the distribution metric.
    static const char32_t kMapping[] = {
        0x30F2,                                  // ｦ -> ヲ
        0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9,  // ｧｨｩｪｫ -> ァィゥェォ
        0x30E3, 0x30E5, 0x30E7,                  // ｬｭｮ -> ャュョ
        0x30C3,                                  // ｯ -> ッ
        0x30FC,                                  // ｰ -> ー
        0x30A2, 0x30A4, 0x30A6, 0x30A8, 0x30AA,  // ｱｲｳｴｵ -> アイウエオ
        0x30AB, 0x30AD, 0x30AF, 0x30B1, 0x30B3,  // ｶｷｸｹｺ -> カキクケコ
        0x30B5, 0x30B7, 0x30B9, 0x30BB, 0x30BD,  // ｻｼｽｾｿ -> サシスセソ
        0x30BF, 0x30C1, 0x30C4, 0x30C6, 0x30C8,  // ﾀﾁﾂﾃﾄ -> タチツテト
        0x30CA, 0x30CB, 0x30CC, 0x30CD, 0x30CE,  // ﾅﾆﾇﾈﾉ -> ナニヌネノ
        0x30CF, 0x30D2, 0x30D5, 0x30D8, 0x30DB,  // ﾊﾋﾌﾍﾎ -> ハヒフヘホ
        0x30DE, 0x30DF, 0x30E0, 0x30E1, 0x30E2,  // ﾏﾐﾑﾒﾓ -> マミムメモ
        0x30E4, 0x30E6, 0x30E8,                  // ﾔﾕﾖ -> ヤユヨ
        0x30E9, 0x30EA, 0x30EB, 0x30EC, 0x30ED,  // ﾗﾘﾙﾚﾛ -> ラリルレロ
        0x30EF, 0x30F3,                          // ﾜﾝ -> ワン
    };
    size_t idx = codepoint - 0xFF66;
    if (idx < sizeof(kMapping) / sizeof(kMapping[0])) {
      return kMapping[idx];
    }
  }
  return codepoint;
}

// Vu-series (ヴ) normalization
// ヴァ→バ, ヴィ→ビ, ヴ→ブ, ヴェ→ベ, ヴォ→ボ
constexpr char32_t kKatakanaVu = 0x30F4;      // ヴ
constexpr char32_t kKatakanaSmallA = 0x30A1;  // ァ
constexpr char32_t kKatakanaSmallI = 0x30A3;  // ィ
constexpr char32_t kKatakanaSmallU = 0x30A5;  // ゥ
constexpr char32_t kKatakanaSmallE = 0x30A7;  // ェ
constexpr char32_t kKatakanaSmallO = 0x30A9;  // ォ

constexpr char32_t kKatakanaBa = 0x30D0;  // バ
constexpr char32_t kKatakanaBi = 0x30D3;  // ビ
constexpr char32_t kKatakanaBu = 0x30D6;  // ブ
constexpr char32_t kKatakanaBe = 0x30D9;  // ベ
constexpr char32_t kKatakanaBo = 0x30DC;  // ボ

// Hiragana vu (rare but exists)
constexpr char32_t kHiraganaVu = 0x3094;      // ゔ
constexpr char32_t kHiraganaSmallA = 0x3041;  // ぁ
constexpr char32_t kHiraganaSmallI = 0x3043;  // ぃ
constexpr char32_t kHiraganaSmallU = 0x3045;  // ぅ
constexpr char32_t kHiraganaSmallE = 0x3047;  // ぇ
constexpr char32_t kHiraganaSmallO = 0x3049;  // ぉ

constexpr char32_t kHiraganaBa = 0x3070;  // ば
constexpr char32_t kHiraganaBi = 0x3073;  // び
constexpr char32_t kHiraganaBu = 0x3076;  // ぶ
constexpr char32_t kHiraganaBe = 0x3079;  // べ
constexpr char32_t kHiraganaBo = 0x307C;  // ぼ

// Half-width dakuten and handakuten
constexpr char32_t kHalfwidthDakuten = 0xFF9E;     // ﾞ
constexpr char32_t kHalfwidthHandakuten = 0xFF9F;  // ﾟ

// Hiragana and katakana counterparts differ by this fixed Unicode-plane
// offset for the voiced forms handled below. Keeping the rule in the hiragana
// plane prevents the two scripts from drifting when a mark rule is added.
constexpr char32_t kKatakanaPlaneOffset = 0x60;

char32_t combineKanaWithSoundMark(char32_t base, bool handakuten) {
  const bool is_katakana = base >= 0x30A0 && base <= 0x30FF;
  const char32_t hiragana_base = is_katakana ? base - kKatakanaPlaneOffset : base;
  char32_t combined = 0;

  // はひふへほ is the only row that accepts both dakuten and handakuten.
  if (hiragana_base >= 0x306F && hiragana_base <= 0x307D && ((hiragana_base - 0x306F) % 3 == 0)) {
    combined = hiragana_base + (handakuten ? 2 : 1);
  } else if (!handakuten) {
    if ((hiragana_base >= 0x304B && hiragana_base <= 0x3053 && ((hiragana_base - 0x304B) % 2 == 0)) ||
        (hiragana_base >= 0x3055 && hiragana_base <= 0x305D && ((hiragana_base - 0x3055) % 2 == 0))) {
      combined = hiragana_base + 1;
    } else if (hiragana_base == 0x305F || hiragana_base == 0x3061 || hiragana_base == 0x3064 ||
               hiragana_base == 0x3066 || hiragana_base == 0x3068) {
      combined = hiragana_base + 1;
    } else if (hiragana_base == 0x3046) {  // う -> ゔ
      combined = 0x3094;
    } else if (hiragana_base == 0x309D) {  // ゝ -> ゞ
      combined = 0x309E;
    }
  }

  // ワ has a precomposed katakana voiced counterpart, but its hiragana
  // counterpart does not. Keep that Unicode asymmetry explicit.
  if (combined == 0 && !handakuten && base == 0x30EF) {
    return 0x30F7;
  }
  return combined == 0 || !is_katakana ? combined : combined + kKatakanaPlaneOffset;
}

// Returns true if the codepoint is any dakuten mark (half-width, combining, or spacing)
bool isDakutenMark(char32_t codepoint) {
  return codepoint == kHalfwidthDakuten || codepoint == kCombiningDakuten || codepoint == kDakuten;
}

// Returns true if the codepoint is any handakuten mark (half-width, combining, or spacing)
bool isHandakutenMark(char32_t codepoint) {
  return codepoint == kHalfwidthHandakuten || codepoint == kCombiningHandakuten || codepoint == kHandakuten;
}

// Returns normalized character for ヴ + small vowel, or 0 if not applicable
char32_t normalizeVuSequence(char32_t vu_char, char32_t next_char) {
  if (vu_char == kKatakanaVu) {
    switch (next_char) {
      case kKatakanaSmallA:
        return kKatakanaBa;
      case kKatakanaSmallI:
        return kKatakanaBi;
      case kKatakanaSmallU:
        return kKatakanaBu;
      case kKatakanaSmallE:
        return kKatakanaBe;
      case kKatakanaSmallO:
        return kKatakanaBo;
      default:
        return 0;
    }
  }
  if (vu_char == kHiraganaVu) {
    switch (next_char) {
      case kHiraganaSmallA:
        return kHiraganaBa;
      case kHiraganaSmallI:
        return kHiraganaBi;
      case kHiraganaSmallU:
        return kHiraganaBu;
      case kHiraganaSmallE:
        return kHiraganaBe;
      case kHiraganaSmallO:
        return kHiraganaBo;
      default:
        return 0;
    }
  }
  return 0;
}

}  // namespace

char32_t Normalizer::normalizeChar(char32_t codepoint) {
  codepoint = fullwidthToHalfwidth(codepoint);
  codepoint = halfwidthKatakanaToFullwidth(codepoint);
  return codepoint;
}

core::Result<std::string> Normalizer::normalize(std::string_view text) const {
  if (!isValidUtf8(text)) {
    return core::Error(core::ErrorCode::InvalidUtf8, "Invalid UTF-8 input");
  }

  std::string result;
  result.reserve(text.size());

  size_t pos = 0;
  while (pos < text.size()) {
    char32_t codepoint = decodeUtf8(text, pos);

    // Apply normalization with options
    char32_t normalized_cp = fullwidthToHalfwidth(codepoint, options_.preserve_case);
    normalized_cp = halfwidthKatakanaToFullwidth(normalized_cp);

    // A half-width dakuten/handakuten reaching this point did not combine with a
    // preceding kana (combinable ones are consumed in the look-ahead below).
    // Map such a stray mark to its full-width standalone form so that both
    // encodings classify and segment identically (e.g. ｱﾞ matches ア゛).
    if (normalized_cp == kHalfwidthDakuten) {
      normalized_cp = kDakuten;
    } else if (normalized_cp == kHalfwidthHandakuten) {
      normalized_cp = kHandakuten;
    }

    // Look ahead for dakuten/handakuten marks (half-width, combining, or spacing)
    size_t next_pos = pos;
    if (next_pos < text.size()) {
      char32_t next_cp = decodeUtf8(text, next_pos);

      // Check if next char is a dakuten or handakuten mark
      if (isDakutenMark(next_cp)) {
        char32_t combined = combineKanaWithSoundMark(normalized_cp, false);
        if (combined != 0) {
          pos = next_pos;  // Consume the dakuten
          // Fall through instead of emitting directly: a combined ヴ/ゔ must be
          // vu-normalized identically to a pre-composed one so that both
          // encodings of the same text yield the same tokens.
          normalized_cp = combined;
        }
      } else if (isHandakutenMark(next_cp)) {
        char32_t combined = combineKanaWithSoundMark(normalized_cp, true);
        if (combined != 0) {
          pos = next_pos;  // Consume the handakuten
          encodeUtf8(combined, result);
          continue;
        }
      }
    }

    codepoint = normalized_cp;

    // Repeated prolonged marks before a kanji token are noise-like separator
    // elongation, not the in-word emphasis used in すごーーい. Retain one
    // mark for the preceding token and collapse only the redundant marks in
    // this boundary context (長いーー音 → 長いー音).
    if (codepoint == 0x30FC) {
      size_t repeated_end = pos;
      while (repeated_end < text.size()) {
        size_t mark_pos = repeated_end;
        if (decodeUtf8(text, mark_pos) != 0x30FC) {
          break;
        }
        repeated_end = mark_pos;
      }
      size_t following_pos = repeated_end;
      if (repeated_end > pos && following_pos < text.size() && isKanjiCodepoint(decodeUtf8(text, following_pos))) {
        pos = repeated_end;
      }
    }

    // Handle vu-series normalization (ヴァ→バ, etc.) - skip if preserve_vu
    if (!options_.preserve_vu && (codepoint == kKatakanaVu || codepoint == kHiraganaVu)) {
      next_pos = pos;
      if (next_pos < text.size()) {
        char32_t next_cp = decodeUtf8(text, next_pos);
        next_cp = fullwidthToHalfwidth(next_cp, options_.preserve_case);
        next_cp = halfwidthKatakanaToFullwidth(next_cp);
        char32_t normalized = normalizeVuSequence(codepoint, next_cp);
        if (normalized != 0) {
          // Consume the small vowel and output normalized character
          pos = next_pos;
          encodeUtf8(normalized, result);
          continue;
        }
      }
      // No small vowel follows, convert ヴ→ブ or ゔ→ぶ
      codepoint = (codepoint == kKatakanaVu) ? kKatakanaBu : kHiraganaBu;
    }

    encodeUtf8(codepoint, result);
  }

  return result;
}

bool Normalizer::needsNormalization(std::string_view text) const {
  auto normalized = normalize(text);
  if (auto* value = core::getValuePtr(normalized)) {
    return *value != text;
  }
  return false;
}

}  // namespace suzume::normalize
