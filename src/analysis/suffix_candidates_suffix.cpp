/**
 * @file suffix_candidates_suffix.cpp
 * @brief Suffix-based unknown word candidate generation
 */

#include "candidate_constants.h"
#include "core/debug.h"
#include "core/utf8_constants.h"
#include "dictionary/dictionary.h"
#include "grammar/char_patterns.h"
#include "grammar/conjugation.h"
#include "grammar/inflection.h"
#include "normalize/char_type.h"
#include "normalize/exceptions.h"
#include "normalize/utf8.h"
#include "suffix_candidates.h"
#include "tokenizer_utils.h"
#include "unknown.h"
#include "verb_candidates_helpers.h"

namespace suzume::analysis {

// =============================================================================
// Suffix Candidate Factory Helpers
// =============================================================================

/**
 * @brief Create a suffix pattern candidate with lemma
 */
inline UnknownCandidate makeSuffixCandidate(const std::string& surface, size_t start, size_t end,
                                            core::PartOfSpeech pos, float cost, const std::string& lemma,
                                            [[maybe_unused]] float confidence, [[maybe_unused]] const char* pattern,
                                            dictionary::ConjugationType conj_type = dictionary::ConjugationType::None) {
  auto cand = makeCandidate(surface, start, end, pos, cost, true, CandidateOrigin::SuffixPattern);
  cand.lemma = lemma;
  cand.conj_type = conj_type;
#ifdef SUZUME_DEBUG_INFO
  cand.confidence = confidence;
  cand.pattern = pattern;
#endif
  return cand;
}

/**
 * @brief Create a suffix pattern candidate without lemma
 */
inline UnknownCandidate makeSuffixCandidateNoLemma(const std::string& surface, size_t start, size_t end,
                                                   core::PartOfSpeech pos, float cost,
                                                   [[maybe_unused]] float confidence,
                                                   [[maybe_unused]] const char* pattern) {
  auto cand = makeCandidate(surface, start, end, pos, cost, true, CandidateOrigin::SuffixPattern);
#ifdef SUZUME_DEBUG_INFO
  cand.confidence = confidence;
  cand.pattern = pattern;
#endif
  return cand;
}

const std::array<SuffixEntry, 19>& getSuffixEntries() {
  static constexpr std::array<SuffixEntry, 19> kSuffixes = {{
      {"化する", core::PartOfSpeech::Verb},
      // Tokenizer use case: keep X+SUFFIX as one search unit. The following
      // suffixes are merged via kanji-merge normalization, not split here:
      //   家/力/化/法/論/員/式/感/的 (productive but one search unit)
      // {"化", core::PartOfSpeech::Suffix},  // Merge via kanji-merge (国際化, 自動化)
      {"性", core::PartOfSpeech::Suffix},
      // {"率", core::PartOfSpeech::Suffix},  // Removed: causes over-segmentation (降水確率→降水確+率)
      // {"法", core::PartOfSpeech::Suffix},  // Merge via kanji-merge (解決法, 民法)
      // {"論", core::PartOfSpeech::Suffix},  // Merge via kanji-merge (進化論, 理論)
      // {"者", core::PartOfSpeech::Suffix},  // Removed: causes over-segmentation (代表者→代表+者)
      // {"家", core::PartOfSpeech::Suffix},  // Removed: causes over-segmentation (大家/思想家/政治家/etc.)
      // {"員", core::PartOfSpeech::Suffix},  // Merge via kanji-merge (会社員, 公務員)
      // {"式", core::PartOfSpeech::Suffix},  // Merge via kanji-merge (計算式, 結婚式)
      // {"感", core::PartOfSpeech::Suffix},  // Merge via kanji-merge (達成感, 違和感)
      // {"力", core::PartOfSpeech::Suffix},  // Merge via kanji-merge (説得力, 影響力)
      {"度", core::PartOfSpeech::Suffix},
      {"方", core::PartOfSpeech::Suffix},  // 歩き方, やり方 (V連用形+方)
      {"中", core::PartOfSpeech::Suffix},  // 一日中, 今日中 (N+中) - MeCab treats as suffix
      // N中 compounds (今日中, 世界中, 一日中) are handled as compound nouns
      // Administrative suffixes (行政接尾辞)
      {"県", core::PartOfSpeech::Suffix},
      {"都", core::PartOfSpeech::Suffix},
      {"府", core::PartOfSpeech::Suffix},
      {"道", core::PartOfSpeech::Suffix},
      {"市", core::PartOfSpeech::Suffix},
      {"区", core::PartOfSpeech::Suffix},
      {"町", core::PartOfSpeech::Suffix},
      {"村", core::PartOfSpeech::Suffix},
      {"庁", core::PartOfSpeech::Suffix},
      {"署", core::PartOfSpeech::Suffix},
      {"局", core::PartOfSpeech::Suffix},
      {"省", core::PartOfSpeech::Suffix},
      {"院", core::PartOfSpeech::Suffix},
      {"所", core::PartOfSpeech::Suffix},
  }};
  return kSuffixes;
}

const std::array<std::string_view, 1>& getNaAdjSuffixes() {
  static constexpr std::array<std::string_view, 1> kNaAdjSuffixes = {
      "的",  // 理性的, 論理的, etc.
  };
  return kNaAdjSuffixes;
}

// =============================================================================
// Productive Hiragana Suffix Patterns (生産的接尾辞)
// =============================================================================

std::vector<UnknownCandidate> generateProductiveSuffixCandidates(const std::vector<char32_t>& codepoints,
                                                                 size_t start_pos,
                                                                 const std::vector<normalize::CharType>& char_types) {
  std::vector<UnknownCandidate> candidates;

  // Only for hiragana sequences
  if (start_pos >= char_types.size() || char_types[start_pos] != normalize::CharType::Hiragana) {
    return candidates;
  }

  constexpr size_t kPpoiLen = 9;  // "っぽい" = 3 chars * 3 bytes

  // Try different lengths of hiragana (3 to 6 chars for stem + がち/っぽい)
  for (size_t hira_len = 3; hira_len <= 8; ++hira_len) {
    size_t candidate_end = start_pos + hira_len;
    if (candidate_end > char_types.size()) {
      break;
    }

    // Check all positions are hiragana
    bool all_hiragana = true;
    for (size_t i = start_pos; i < candidate_end; ++i) {
      if (char_types[i] != normalize::CharType::Hiragana) {
        all_hiragana = false;
        break;
      }
    }
    if (!all_hiragana) {
      break;  // No more hiragana
    }

    std::string surface = extractSubstring(codepoints, start_pos, candidate_end);

    // がち (tendency suffix) is intentionally NOT merged here: MeCab splits
    // あり|がち, なり|がち (verb renyokei + suffix), so the split path wins.

    // Pattern 2: V連用形 + っぽい (resemblance suffix)
    // Examples: 子供っぽい、安っぽい、忘れっぽい
    if (surface.size() >= kPpoiLen + 3 && utf8::endsWith(surface, "っぽい")) {
      std::string_view stem = std::string_view(surface).substr(0, surface.size() - kPpoiLen);
      // っぽい attaches to nouns and verb stems, less strict check
      if (stem.size() >= 3) {  // At least 1 character stem
        candidates.push_back(makeSuffixCandidate(surface, start_pos, candidate_end, core::PartOfSpeech::Adjective, 0.4F,
                                                 surface, 0.85F, "stem_ppoi", dictionary::ConjugationType::IAdjective));
        return candidates;  // Found valid っぽい candidate
      }
    }

    // Pattern 3: Short hiragana nickname + ちゃん/くん/さん
    // Examples: たっちゃん, ゆうちゃん, けんちゃん, わんちゃん, けんくん
    // Also lexicalized family terms: おねえさん, おにいさん, おかあさん, おとうさん
    // Tokenizer use case: treat as a single search unit. Restrict stem to 1-3
    // hiragana chars so we don't merge full names (e.g., はなこさん stays split).
    if (surface.size() >= 9) {  // at least 1-char stem (3 bytes) + 2+ char honorific
      for (const auto* honorific : {"ちゃん", "くん", "さん"}) {
        std::string_view h(honorific);
        if (!utf8::endsWith(surface, h)) {
          continue;
        }
        std::string_view stem = std::string_view(surface).substr(0, surface.size() - h.size());
        size_t stem_chars = stem.size() / 3;  // Each hiragana = 3 bytes in UTF-8
        if (stem_chars >= 2 && stem_chars <= 3) {
          // Stronger bonus when stem starts with お/ご (lexicalized family terms
          // like おねえさん, おかあさん) so the 1-token path beats お(PREFIX) +
          // nickname split, which gets a -1.3 PREFIX→NOUN bigram bonus.
          bool starts_with_honorific_prefix =
              stem.size() >= 3 && (stem.compare(0, 3, "お") == 0 || stem.compare(0, 3, "ご") == 0);
          float cost = starts_with_honorific_prefix ? -1.5F : -0.5F;
          candidates.push_back(makeSuffixCandidate(surface, start_pos, candidate_end, core::PartOfSpeech::Noun, cost,
                                                   surface, 0.9F, "hira_nickname"));
          return candidates;
        }
        break;
      }
    }
  }

  return candidates;
}

// Administrative suffix codepoints for intermediate boundary detection
const std::array<char32_t, 8>& getAdminSuffixCodepoints() {
  static constexpr std::array<char32_t, 8> kAdminSuffixes = {U'県', U'都', U'府', U'道', U'市', U'区', U'町', U'村'};
  return kAdminSuffixes;
}

std::vector<UnknownCandidate> generateAdminBoundaryCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                                              const std::vector<normalize::CharType>& char_types) {
  std::vector<UnknownCandidate> candidates;

  if (start_pos >= char_types.size() || char_types[start_pos] != normalize::CharType::Kanji) {
    return candidates;
  }

  const auto& admin_suffixes = getAdminSuffixCodepoints();

  // Scan through kanji sequence looking for administrative suffixes
  for (size_t pos = start_pos + 1; pos < char_types.size() && pos < start_pos + 6; ++pos) {
    if (char_types[pos] != normalize::CharType::Kanji) {
      break;
    }

    char32_t cp = codepoints[pos];
    bool is_admin_suffix = std::find(admin_suffixes.begin(), admin_suffixes.end(), cp) != admin_suffixes.end();

    if (is_admin_suffix) {
      // Found administrative suffix at position pos
      // Generate candidate from start_pos to pos+1 (including the suffix)
      size_t end_with_suffix = pos + 1;
      std::string surface = extractSubstring(codepoints, start_pos, end_with_suffix);
      candidates.push_back(makeSuffixCandidateNoLemma(surface, start_pos, end_with_suffix, core::PartOfSpeech::Noun,
                                                      0.3F, 0.95F, "admin_boundary"));
    }
  }

  return candidates;
}

std::vector<UnknownCandidate> generateWithSuffix(const std::vector<char32_t>& codepoints, size_t start_pos,
                                                 const std::vector<normalize::CharType>& char_types,
                                                 const UnknownOptions& options) {
  if (start_pos >= char_types.size() || char_types[start_pos] != normalize::CharType::Kanji) {
    return {};
  }

  // First, generate candidates for administrative boundaries
  std::vector<UnknownCandidate> candidates = generateAdminBoundaryCandidates(codepoints, start_pos, char_types);

  // Find kanji sequence
  size_t end_pos = start_pos;
  while (end_pos < char_types.size() && end_pos - start_pos < options.max_kanji_length &&
         char_types[end_pos] == normalize::CharType::Kanji) {
    ++end_pos;
  }

  if (end_pos <= start_pos + 1) {
    return candidates;
  }

  std::string kanji_seq = extractSubstring(codepoints, start_pos, end_pos);
  const auto& suffixes = getSuffixEntries();

  // Check for suffixes
  for (const auto& [suffix, suffix_pos] : suffixes) {
    if (kanji_seq.size() > suffix.size() &&
        kanji_seq.compare(kanji_seq.size() - suffix.size(), suffix.size(), suffix) == 0) {
      // Calculate stem length in codepoints
      auto suffix_codepoints = normalize::utf8::decode(std::string(suffix));
      size_t stem_end = end_pos - suffix_codepoints.size();
      size_t stem_codepoint_len = stem_end - start_pos;

      // Restrict suffix-stem split to 2-char kanji stems.
      // Typical kango "X+suffix" patterns (思想家, 国際法, 公務員) all have a 2-char stem.
      // 3+ char stems before a 1-char suffix usually indicate the kanji_seq is actually
      // two adjacent kango compounds (e.g., 新規手法 = 新規 + 手法, not 新規手 + 法).
      // Longer stems with a real suffix (大企業家) are reached via the PREFIX path
      // (大 prefix + 企業家 → 企業 + 家).
      if (stem_codepoint_len > 2) {
        continue;
      }

      // Skip suffix-stem when the stem starts with the L1 PREFIX kanji 御.
      // The prefix path (御 + 尽力 NOUN) should win over 御尽 + 力 (suffix path).
      // Without this skip, suffix path cost (0.7) + NOUN→SUFFIX bonus (-0.8) makes
      // 御尽 + 力 cheaper than 御(PREFIX) + 尽力(kanji_seq).
      if (codepoints[start_pos] == U'御') {
        continue;
      }

      if (stem_end > start_pos + 1) {
        // Add stem candidate
        std::string stem_surface = extractSubstring(codepoints, start_pos, stem_end);

        UnknownCandidate stem;
        stem.surface = stem_surface;
        stem.start = start_pos;
        stem.end = stem_end;
        stem.pos = core::PartOfSpeech::Noun;
        stem.cost = 1.0F + options.suffix_separation_bonus;
        stem.has_suffix = false;
#ifdef SUZUME_DEBUG_INFO
        stem.origin = CandidateOrigin::SuffixPattern;
        stem.confidence = 1.0F;
        stem.pattern = "stem_before_" + std::string(suffix);
#endif
        candidates.push_back(stem);

        // Add whole word candidate too
        UnknownCandidate whole;
        whole.surface = kanji_seq;
        whole.start = start_pos;
        whole.end = end_pos;
        whole.pos = core::PartOfSpeech::Noun;
        whole.cost = 1.2F;
        whole.has_suffix = true;
#ifdef SUZUME_DEBUG_INFO
        whole.origin = CandidateOrigin::SuffixPattern;
        whole.confidence = 1.0F;
        whole.pattern = "with_suffix_" + std::string(suffix);
#endif
        candidates.push_back(whole);

        break;  // Use longest matching suffix
      }
    }
  }

  return candidates;
}

}  // namespace suzume::analysis
