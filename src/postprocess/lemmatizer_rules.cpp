#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "grammar/conjugation.h"
#include "grammar/inflection_scorer_constants.h"
#include "normalize/char_type.h"
#include "normalize/utf8.h"
#include "postprocess/lemmatizer.h"
#include "postprocess/lemmatizer_internal.h"

namespace suzume::postprocess {
namespace lemmatizer_detail {

// Potential verb (可能動詞) ending: godan a-row stem + れる
// E.g., 泊まれる (とまれる)
// Single token 〜れる verbs are treated as potential (ichidan), not passive.
// Passive forms are split (読ま+れる), so single token 〜れる is likely potential.
bool endsWithPotentialVerbSuffix(std::string_view surface) {
  if (surface.size() < core::kThreeJapaneseCharBytes || !utf8::endsWith(surface, "れる")) {
    return false;
  }
  const std::string_view a_row = utf8::lastChar(utf8::dropLast2Chars(surface));
  return !grammar::godanBaseSuffixFromARow(utf8::decodeFirstChar(a_row)).empty();
}

// Verb endings and their base forms
struct VerbEnding {
  std::string_view suffix;
  // Empty only for canonical Godan row reversal; fixed rules store their base.
  std::string_view base;
};

constexpr float kUnverifiedLemmaConfidenceThreshold = 0.5F;

// Common verb conjugation endings (simplified)
// NOTE: Order matters - longer patterns should come first
const VerbEnding kVerbEndings[] = {
    // Polite humble forms with おります (longest first)
    {"しております", "する"},          // している polite humble
    {"しておりました", "する"},        // していた polite humble
    {"いたしております", "いたす"},    // している super polite
    {"いたしておりました", "いたす"},  // していた super polite
    {"ております", "おる"},            // ている polite humble
    {"ておりました", "おる"},          // ていた polite humble
    {"おります", "おる"},              // いる polite humble

    // Suru-verb te-form + subsidiary verbs (longest first)
    // These are compound patterns where [noun]して[subsidiary] → [noun]する
    // Progressive forms of subsidiary verbs (補助動詞進行形) - longest first
    {"してもらっています", "する"},
    {"してもらっていた", "する"},
    {"してもらっている", "する"},
    {"してあげています", "する"},
    {"してあげていた", "する"},
    {"してあげている", "する"},
    {"してくれています", "する"},
    {"してくれていた", "する"},
    {"してくれている", "する"},
    {"してきています", "する"},
    {"してきていた", "する"},
    {"してきている", "する"},
    {"していっています", "する"},
    {"していっていた", "する"},
    {"していっている", "する"},
    // Base forms of subsidiary verbs (補助動詞基本形)
    {"してもらう", "する"},
    {"してもらった", "する"},
    {"してもらって", "する"},
    {"してあげる", "する"},
    {"してあげた", "する"},
    {"してあげて", "する"},
    {"してみる", "する"},
    {"してみた", "する"},
    {"してみて", "する"},
    {"してくれる", "する"},
    {"してくれた", "する"},
    {"してくれて", "する"},
    {"していく", "する"},
    {"していった", "する"},
    {"していって", "する"},
    {"してくる", "する"},
    {"してきた", "する"},
    {"してきて", "する"},
    {"しておく", "する"},
    {"しておいた", "する"},
    {"しておいて", "する"},
    {"してしまう", "する"},
    {"してしまった", "する"},
    {"してしまって", "する"},

    // Suru-verb colloquial contractions (サ変動詞口語縮約形)
    // してしまう → しちゃう/しちまう
    {"しちゃいます", "する"},
    {"しちゃう", "する"},
    {"しちゃった", "する"},
    {"しちゃって", "する"},
    {"しちまう", "する"},
    {"しちまった", "する"},
    {"しちまって", "する"},
    // しておく → しとく
    {"しときます", "する"},
    {"しとく", "する"},
    {"しといた", "する"},
    {"しといて", "する"},
    // している → してる
    {"してました", "する"},
    {"してます", "する"},
    {"してる", "する"},
    {"してた", "する"},
    // Negative te-form (否定て形)
    {"しなくて", "する"},
    {"しないで", "する"},

    // Colloquial とく/どく contractions (ておく → とく)
    // Ichidan: stem + とく → stem + る
    {"とく", "る"},    // 見とく → 見る, 食べとく → 食べる
    {"といた", "る"},  // 見といた → 見る
    {"といて", "る"},  // 見といて → 見る
    // Godan onbinkei: stem + んどく → stem + む/ぶ/ぬ
    {"んどく", "む"},    // 読んどく → 読む
    {"んどいた", "む"},  // 読んどいた → 読む
    {"んどいて", "む"},  // 読んどいて → 読む
    // Godan i-row onbinkei: stem + いとく → stem + く
    {"いとく", "く"},    // 書いとく → 書く
    {"いといた", "く"},  // 書いといた → 書く
    {"いといて", "く"},  // 書いといて → 書く
    // Godan sokuon + とく: stem + っとく → stem + う/つ/る
    {"っとく", "う"},    // 買っとく → 買う
    {"っといた", "う"},  // 買っといた → 買う
    {"っといて", "う"},  // 買っといて → 買う

    // Colloquial てる/でる contractions (ている → てる)
    // Godan sokuon: stem + ってる → stem + う/つ/る
    {"ってる", "う"},  // 買ってる → 買う, 待ってる → 待つ
    {"ってた", "う"},  // 買ってた → 買う
    // Godan i-row: stem + いてる → stem + く
    {"いてる", "く"},  // 書いてる → 書く
    {"いてた", "く"},  // 書いてた → 書く
    // Godan n-row: stem + んでる → stem + む/ぶ/ぬ
    {"んでる", "む"},  // 読んでる → 読む
    {"んでた", "む"},  // 読んでた → 読む
    // Ichidan: stem + てる → stem + る
    {"てる", "る"},  // 見てる → 見る, 食べてる → 食べる
    {"てた", "る"},  // 見てた → 見る

    // Volitional form (意志形)
    // Ichidan: stem + よう → stem + る
    {"めよう", "める"},  // 始めよう → 始める (avoid false positive on godan)
    {"べよう", "べる"},  // 食べよう → 食べる
    {"ねよう", "ねる"},  // 寝よう → 寝る

    // Compound verbs (longest first)
    {"ってしまった", "う"},
    {"ってしまった", "つ"},
    {"ってしまった", "る"},
    {"いてしまった", "く"},
    {"んでしまった", "む"},
    {"してしまった", "す"},
    {"てしまった", "る"},

    {"っておいた", "う"},
    {"っておいた", "つ"},
    {"っておいた", "る"},
    {"いておいた", "く"},
    {"んでおいた", "む"},
    {"しておいた", "す"},
    {"ておいた", "る"},

    {"ってみた", "う"},
    {"ってみた", "つ"},
    {"ってみた", "る"},
    {"いてみた", "く"},
    {"んでみた", "む"},
    {"してみた", "す"},
    {"てみた", "る"},

    {"ってきた", "う"},
    {"ってきた", "つ"},
    {"ってきた", "る"},
    {"いてきた", "く"},
    {"んできた", "む"},
    {"してきた", "す"},
    {"てきた", "る"},

    {"っていった", "う"},
    {"っていった", "つ"},
    {"っていった", "る"},
    {"いていった", "く"},
    {"んでいった", "む"},
    {"していった", "す"},
    {"ていった", "る"},

    // =========================================================================
    // Passive forms (受身形): 五段未然形 + suffix.
    // Pattern: 未然形 + れる/れた/れて/れない/れます/れました/れている
    // =========================================================================
    {"れている", ""},  // Progressive (longest first)
    {"れました", ""},  // Polite past
    {"れない", ""},    // Negative
    {"れます", ""},    // Polite
    {"れる", ""},      // Dictionary
    {"れた", ""},      // Past
    {"れて", ""},      // Te-form

    // =========================================================================
    // Causative forms (使役形): 五段未然形 + suffix.
    // Pattern: 未然形 + せる/せた/せて
    // =========================================================================
    {"せる", ""},  // Dictionary
    {"せた", ""},  // Past
    {"せて", ""},  // Te-form

    // Godan verbs (onbin forms)
    {"った", "う"},
    {"った", "つ"},
    {"った", "る"},
    {"いた", "く"},
    {"いだ", "ぐ"},
    {"んだ", "む"},
    {"んだ", "ぶ"},
    {"んだ", "ぬ"},
    {"した", "す"},

    // Te-form
    {"って", "う"},
    {"って", "つ"},
    {"って", "る"},
    {"いて", "く"},
    {"いで", "ぐ"},
    {"んで", "む"},
    {"んで", "ぶ"},
    {"んで", "ぬ"},
    {"して", "す"},

    // Masu-form
    {"います", "う"},
    {"います", "く"},
    {"います", "す"},
    {"きます", "くる"},
    {"します", "する"},
    {"ます", "る"},

    // Nai-form: 五段未然形 + ない
    {"ない", ""},
    {"ない", "る"},  // Ichidan fallback

    // Potential: 五段え段 + る
    {"る", ""},

    // Passive/Causative (ichidan)
    {"られる", "る"},
    {"させる", "する"},
};

// Adjective endings
const VerbEnding kAdjectiveEndings[] = {
    {"そうだった", "い"}, {"そうです", "い"}, {"そうだ", "い"}, {"そうに", "い"}, {"そうな", "い"}, {"そう", "い"},
    {"くなかった", "い"}, {"くない", "い"},   {"かった", "い"}, {"くて", "い"},   {"く", "い"},     {"さ", "い"},
};

struct ContractedVerbEnding {
  enum class Onbin : uint8_t {
    None,
    I,
    Sokuon,
    Hatsuon,
  };

  std::string_view suffix;
  Onbin onbin;
};

constexpr std::string_view kOnbinSurfaces[] = {"", "い", "っ", "ん"};

using Onbin = ContractedVerbEnding::Onbin;
const ContractedVerbEnding kContractedVerbEndings[] = {
    {"ってしまった", Onbin::Sokuon},
    {"いてしまった", Onbin::I},
    {"んでしまった", Onbin::Hatsuon},
    {"してしまった", Onbin::None},
    {"っておいた", Onbin::Sokuon},
    {"いておいた", Onbin::I},
    {"んでおいた", Onbin::Hatsuon},
    {"しておいた", Onbin::None},
    {"ってみた", Onbin::Sokuon},
    {"いてみた", Onbin::I},
    {"んでみた", Onbin::Hatsuon},
    {"してみた", Onbin::None},
    {"ってきた", Onbin::Sokuon},
    {"いてきた", Onbin::I},
    {"んできた", Onbin::Hatsuon},
    {"してきた", Onbin::None},
    {"っていった", Onbin::Sokuon},
    {"いていった", Onbin::I},
    {"んでいった", Onbin::Hatsuon},
    {"していった", Onbin::None},
    {"っとく", Onbin::Sokuon},
    {"っといた", Onbin::Sokuon},
    {"っといて", Onbin::Sokuon},
    {"いとく", Onbin::I},
    {"いといた", Onbin::I},
    {"いといて", Onbin::I},
    {"んどく", Onbin::Hatsuon},
    {"んどいた", Onbin::Hatsuon},
    {"んどいて", Onbin::Hatsuon},
    {"ってる", Onbin::Sokuon},
    {"ってた", Onbin::Sokuon},
    {"いてる", Onbin::I},
    {"いてた", Onbin::I},
    {"んでる", Onbin::Hatsuon},
    {"んでた", Onbin::Hatsuon},
    {"った", Onbin::Sokuon},
    {"って", Onbin::Sokuon},
    {"いた", Onbin::I},
    {"いて", Onbin::I},
    {"いだ", Onbin::I},
    {"いで", Onbin::I},
    {"んだ", Onbin::Hatsuon},
    {"んで", Onbin::Hatsuon},
    {"した", Onbin::None},
    {"して", Onbin::None},
};

const std::string_view kSuruPassiveEndings[] = {
    "されている", "されました", "されます", "されない", "される", "された", "されて",
};

bool hasExactVerbEntry(const dictionary::DictionaryManager* dict_manager, std::string_view surface) {
  if (dict_manager == nullptr) {
    return false;
  }
  return dict_manager->lookupExact(surface, core::PartOfSpeech::Verb) != nullptr;
}

// =============================================================================
// Shared lemma-correction helpers
//
// These encapsulate corrections applied identically from both lemmatize()
// (single morpheme) and lemmatizeAll() (vector pass) so there is one source of
// truth. Each returns the corrected lemma, or an empty string when the pattern
// does not apply (meaning: leave the existing lemma unchanged). The POS guard
// (Verb / Adverb) stays at the call site.
// =============================================================================

// サ変動詞 classical form: 漢字2文字以上+す → 漢字+する
// e.g., 確認す → 確認する, 運動す → 運動する.
// Single kanji + す (出す, 消す) are GodanSa, not Suru, so require a 2+ kanji stem.
std::string fixSuruClassical(std::string_view lemma) {
  if (!utf8::endsWith(lemma, "す") || utf8::endsWith(lemma, "する")) {
    return "";
  }
  std::string stem(utf8::dropLastChar(lemma));
  if (stem.size() >= core::kTwoJapaneseCharBytes && grammar::isAllKanji(stem)) {
    return stem + "する";
  }
  return "";
}

// Compound verbs analyzed as ichidan but actually サ変/godan-sa: [stem]しる → [stem]する/す
// e.g., 対しる → 対する, 関しる → 関する (single kanji → サ変),
//       やりなおしる → やりなおす (multi-char → godan-sa).
// Note: 応じる, 存じる, 信じる, 感じる are genuine ichidan (じる, not しる) and unaffected.
//
// The single-kanji threshold here is the INVERSE of fixSuruClassical's: there a
// single kanji + す is left alone (出す/消す are real GodanSa), while here a single
// kanji + しる is corrected to する (サ変). The two thresholds are deliberately
// opposite because they key off which conjugation the surface actually witnesses
// (す-shuushi vs しる-misanalysis); do NOT unify them into one rule.
//
// Caveat: the multi-char `stem + "す"` branch is UNVERIFIED against the dictionary,
// so a stem that shadows a GODAN_WA verb yields a non-word (あらしる→あらす where the
// real verb is 洗う) — the same surface-indistinguishable ambiguity as the s88
// あらって→あらる case. Kept as the best available default; adding a dict check here
// is an open question, not a bug to silently "fix".
std::string fixShiru(std::string_view lemma) {
  if (!utf8::endsWith(lemma, "しる")) {
    return "";
  }
  std::string stem(utf8::dropLast2Chars(lemma));
  if (stem.size() < core::kJapaneseCharBytes) {
    return "";
  }
  if (stem.size() == core::kJapaneseCharBytes && grammar::isAllKanji(stem)) {
    return stem + "する";
  }
  return stem + "す";
}

// Special ra-row (ラ行特殊活用) verbs: renyokei ends in い (not り), so the
// inflection analyzer reconstructs ~いる as the base form. Correct ~いる → ~る
// when the ~る form exists as a Verb in the dictionary (ござい → ございる → ござる).
// Returns the corrected lemma, or empty when the pattern does not apply. The
// POS (Verb) guard stays at the call site.
std::string fixSpecialRaRowLemma(std::string_view lemma, const dictionary::DictionaryManager* dict) {
  if (!utf8::endsWith(lemma, "いる") || lemma.size() < core::kThreeJapaneseCharBytes || dict == nullptr) {
    return "";
  }
  std::string ru_form = std::string(utf8::dropLast2Chars(lemma)) + "る";
  if (dict->lookupExact(ru_form, core::PartOfSpeech::Verb) != nullptr) {
    return ru_form;
  }
  return "";
}

// Ichidan renyokei misread as a godan base: a bare renyokei directly followed
// by a bare て/た cannot be godan — every godan verb takes onbin before て/た
// (借る→借って, 過ぐ→過いで), so 借り+て must be ichidan 借りる, not godan-ra 借る.
// Exclusions: し-ending renyokei (godan-sa takes bare て with no onbin: 話し+て
// → 話す) and い-ending surfaces (書い+て is a godan-ka onbin stem, surface-
// indistinguishable from kami-ichidan 老い+て). じ is omitted because it has no
// plain godan う-row counterpart, so it never carries a wrong godan lemma.
// Fires only when the current lemma IS the wrong godan reconstruction of this
// renyokei (stem + う-row kana of the same gyo); any other lemma source (a
// dictionary lemma, an already-correct ichidan base) is left untouched. The
// POS/ExtendedPOS (Verb 連用形) guard stays at the call site.
std::string fixGodanRenyokeiBeforeLiteraryTe(std::string_view surface, std::string_view lemma,
                                             std::string_view next_surface,
                                             const dictionary::DictionaryManager* dict_manager) {
  if (dict_manager == nullptr || next_surface != "て" || lemma != std::string(surface) + "る") {
    return "";
  }
  const auto codepoints = normalize::toCodepoints(surface);
  if (codepoints.empty()) {
    return "";
  }
  const std::string_view godan_ending = grammar::godanBaseSuffixFromIRow(codepoints.back());
  if (godan_ending.empty()) {
    return "";
  }
  std::string candidate = std::string(utf8::dropLastChar(surface)) + std::string(godan_ending);
  return hasExactVerbEntry(dict_manager, candidate) ? candidate : "";
}

std::string fixIchidanRenyokeiBeforeTe(std::string_view surface, std::string_view lemma, std::string_view next_surface,
                                       const dictionary::DictionaryManager* dict_manager) {
  if (!utf8::equalsAny(next_surface, {"て", "た"}) || surface.size() < core::kJapaneseCharBytes) {
    return "";
  }
  // い-row kana → う-row kana of the same gyo (し/い/じ excluded, see above).
  const std::string_view tail = utf8::lastChar(surface);
  if (utf8::equalsAny(tail, {"し", "い", "じ"})) {
    return "";
  }
  std::string_view godan_ending = grammar::godanBaseSuffixFromIRow(utf8::decodeFirstChar(tail));
  // These historical rows are outside the modern Godan table but remain valid
  // for this fallback's same-row ambiguity check.
  if (tail == "ぢ") {
    godan_ending = "づ";
  } else if (tail == "ひ") {
    godan_ending = "ふ";
  } else if (tail == "ぴ") {
    godan_ending = "ぷ";
  }
  if (godan_ending.empty()) {
    return "";
  }

  std::string godan_base = std::string(utf8::dropLastChar(surface)) + std::string(godan_ending);
  if (lemma != godan_base) {
    return "";
  }
  // A dictionary-confirmed godan base is not the fabricated reading this
  // correction targets (走り+て, 読み+て). The remaining ambiguous cases
  // are genuine ichidan renyokei (借り+て, 過ぎ+て).
  if (hasExactVerbEntry(dict_manager, godan_base)) {
    return "";
  }
  return std::string(surface) + "る";
}

// Potential verb (可能動詞): single-token 五段あ段+れる keeps lemma = surface.
// Passive forms are split (読ま+れる), so a single 〜れる token is treated as
// potential (ichidan), whose lemma is the surface itself.
std::string fixPotentialVerb(const core::Morpheme& morpheme) {
  if (morpheme.pos == core::PartOfSpeech::Verb && endsWithPotentialVerbSuffix(morpheme.surface)) {
    return morpheme.surface;
  }
  return "";
}

// Tari-adjective adverb: strip trailing と, e.g., 颯爽と → 颯爽, 堂々と → 堂々.
// Pattern: exactly 漢字2文字 + と (6 bytes kanji + 3 bytes と). MeCab uses the stem as lemma.
std::string fixTariAdverb(std::string_view surface) {
  constexpr size_t kTariAdverbLen = core::kTwoJapaneseCharBytes + core::kJapaneseCharBytes;
  if (surface.size() != kTariAdverbLen || !utf8::endsWith(surface, "と")) {
    return "";
  }
  std::string stem(surface.substr(0, core::kTwoJapaneseCharBytes));
  const auto stem_codepoints = normalize::utf8::decode(stem);
  if (!stem_codepoints.empty() && normalize::isNumeralCodepoint(stem_codepoints.front())) {
    return "";
  }
  if (grammar::isAllKanji(stem) ||
      (stem.size() == core::kTwoJapaneseCharBytes && stem.substr(core::kJapaneseCharBytes) == "々")) {
    return stem;
  }
  return "";
}

// 撥音便 godan base from a stem whose original onbin form ended in ん.
// Tries the canonical ん音便 rows against the dictionary; falls back to む
// (most common 撥音便) when the stem is all kanji. Returns empty if nothing applies.
// e.g., 読ん → 読む, 学ん → 学ぶ, 死ん → 死ぬ.
std::string fixHatsuonbin(std::string_view stem, const dictionary::DictionaryManager* dict_manager) {
  if (stem.empty()) {
    return "";
  }
  if (dict_manager != nullptr) {
    for (const auto& [verb_type, ending] : grammar::Conjugation::getGodanTypesByOnbin("ん")) {
      (void)verb_type;
      std::string base = std::string(stem) + std::string(ending);
      auto results = dict_manager->lookup(base, 0);
      for (const auto& result : results) {
        if (result.entry != nullptr && result.entry->pos == core::PartOfSpeech::Verb) {
          return base;
        }
      }
    }
  }
  // Kanji-fallback guard: assume む for kanji stems when no dictionary match is found.
  if (grammar::isAllKanji(stem)) {
    return std::string(stem) + "む";
  }
  return "";
}

std::string lemmatizeGodanEnding(std::string_view surface, const VerbEnding& ending) {
  const std::string_view stem = surface.substr(0, surface.size() - ending.suffix.size());
  const char32_t row_codepoint = utf8::decodeFirstChar(utf8::lastChar(stem));
  const std::string_view base = ending.suffix == "る" ? grammar::godanBaseSuffixFromERow(row_codepoint)
                                                      : grammar::godanBaseSuffixFromARow(row_codepoint);
  if (base.empty()) {
    return "";
  }
  return std::string(utf8::dropLastChar(stem)) + std::string(base);
}

std::string lemmatizeVerbFallback(std::string_view surface) {
  for (const auto& ending : kVerbEndings) {
    if (!utf8::endsWith(surface, ending.suffix)) {
      continue;
    }
    if (ending.base.empty()) {
      if (std::string result = lemmatizeGodanEnding(surface, ending); !result.empty()) {
        return result;
      }
      continue;
    }
    std::string result(surface.substr(0, surface.size() - ending.suffix.size()));
    result += ending.base;
    return result;
  }
  return std::string(surface);
}

}  // namespace lemmatizer_detail

using namespace lemmatizer_detail;

std::string Lemmatizer::lemmatizeVerb(std::string_view surface) {
  return lemmatizeVerbFallback(surface);
}

std::string Lemmatizer::lemmatizeAdjective(std::string_view surface) {
  // B45: Special handling for ない adjective + さ + そう pattern
  // なさそう = ない + さ + そう (looks like there isn't)
  // Without this, lemmatizer would incorrectly return なさい (from そう → い rule)
  // This pattern also covers: なさそうな, なさそうに, なさそうだ, etc.
  if (surface.find("なさそう") == 0) {
    // Replace なさそう... with ない
    return "ない";
  }
  // Also handle なさ alone (noun form of ない)
  if (surface == "なさ") {
    return "ない";
  }

  for (const auto& ending : kAdjectiveEndings) {
    if (utf8::endsWith(surface, ending.suffix)) {
      std::string result(surface.substr(0, surface.size() - ending.suffix.size()));
      result += ending.base;
      return result;
    }
  }
  return std::string(surface);
}

bool Lemmatizer::verifyCandidateWithDictionary(const grammar::InflectionCandidate& candidate) const {
  if (dict_manager_ == nullptr) {
    return false;
  }

  // Look up the candidate base form in dictionary
  auto results = dict_manager_->lookup(candidate.base_form, 0);

  bool found_verb_or_adj = false;

  for (const auto& result : results) {
    if (result.entry == nullptr) {
      continue;
    }

    // Check if the entry matches exactly (same surface and is a verb/adjective)
    if (result.entry->surface != candidate.base_form) {
      continue;
    }

    // Check if POS is verb or adjective
    if (result.entry->pos != core::PartOfSpeech::Verb && result.entry->pos != core::PartOfSpeech::Adjective) {
      continue;
    }

    // Found a verb/adjective with matching surface
    found_verb_or_adj = true;

    break;
  }

  // Accept if base_form exists as verb/adjective in dictionary
  // Type mismatch is acceptable - inflection analysis may have wrong type
  // but dictionary presence validates the base_form itself
  // e.g., 見せられた → base="見せる" with wrong type=GodanRa should still
  // be accepted because 見せる exists as Ichidan verb in dictionary
  return found_verb_or_adj;
}

std::string Lemmatizer::lemmatizeByGrammar(std::string_view surface, core::PartOfSpeech pos,
                                           dictionary::ConjugationType conj_type) const {
  // First, check if surface itself is a base form (not conjugated form) in dictionary
  // (e.g., 差し上げる should return 差し上げる, not 差し上ぐ)
  // We check that lemma == surface, meaning it's the dictionary form, not a conjugated form
  // Conjugated forms like 使い (from 使う) have lemma != surface (lemma = 使う)
  if (dict_manager_ != nullptr) {
    auto results = dict_manager_->lookup(surface, 0);
    for (const auto& result : results) {
      if (result.entry != nullptr && result.entry->surface == surface &&
          result.entry->lemma == surface &&  // Must be base form, not conjugated
          (result.entry->pos == core::PartOfSpeech::Verb || result.entry->pos == core::PartOfSpeech::Adjective)) {
        // Surface is a valid base form in dictionary
        return std::string(surface);
      }
    }
  }

  // Get all candidates (const reference to cached result)
  const auto& all_candidates = inflection_.analyze(surface);

  if (all_candidates.empty()) {
    return std::string(surface);
  }

  // Apply POS/conjugation filters into a local copy only when needed
  // Otherwise use the cached reference directly to avoid copying
  std::vector<grammar::InflectionCandidate> filtered_storage;
  const std::vector<grammar::InflectionCandidate>* candidates = &all_candidates;

  // Filter candidates by POS if specified
  // For Adjective POS, only accept IAdjective verb_type
  // This prevents 美味しそう (ADJ) from getting lemma 美味する (Suru verb)
  if (pos == core::PartOfSpeech::Adjective) {
    for (const auto& cnd : *candidates) {
      if (cnd.verb_type == grammar::VerbType::IAdjective) {
        filtered_storage.push_back(cnd);
      }
    }
    if (!filtered_storage.empty()) {
      candidates = &filtered_storage;
    } else {
      // No IAdjective candidates → na-adjective (大変, 不思議, etc.)
      // Na-adjectives don't conjugate, lemma = surface
      return std::string(surface);
    }
  }

  // Filter candidates by conjugation type if specified
  // This helps when verb_candidates.cpp has determined the correct verb type
  // e.g., for 話しそう with conj_type=GodanSa, prefer 話す (GodanSa) over 話しい (IAdjective)
  if (conj_type != dictionary::ConjugationType::None) {
    std::vector<grammar::InflectionCandidate> conj_filtered;
    for (const auto& cnd : *candidates) {
      if (grammar::verbTypeToConjType(cnd.verb_type) == conj_type) {
        conj_filtered.push_back(cnd);
      }
    }
    if (!conj_filtered.empty()) {
      filtered_storage = std::move(conj_filtered);
      candidates = &filtered_storage;
    }
  }

  // If dictionary is available, try to find a verified candidate
  // For dictionary-verified candidates, accept confidence above the scorer floor.
  // Dictionary verification compensates for confidence penalties from heuristics
  // (e.g., all-kanji i-adjective stems like 面白 get penalized but are valid)
  if (dict_manager_ != nullptr) {
    for (const auto& candidate : *candidates) {
      if (candidate.confidence > grammar::inflection::kConfidenceFloor && verifyCandidateWithDictionary(candidate)) {
        return candidate.base_form;
      }
    }
  }

  // Fall back to the best candidate if no dictionary match found
  // Use the ordinary unverified threshold since inflection scoring caps common
  // candidates at this level.
  const auto& best = candidates->front();
  if (!best.base_form.empty() && best.confidence >= kUnverifiedLemmaConfidenceThreshold) {
    return best.base_form;
  }

  return std::string(surface);
}

namespace lemmatizer_detail {
std::string lemmatizeContractedVerbWithDictionary(std::string_view surface,
                                                  const dictionary::DictionaryManager* dict_manager) {
  if (dict_manager == nullptr) {
    return "";
  }

  for (const auto& ending : kContractedVerbEndings) {
    if (surface.size() < ending.suffix.size() ||
        surface.compare(surface.size() - ending.suffix.size(), ending.suffix.size(), ending.suffix) != 0) {
      continue;
    }

    std::string stem(surface.substr(0, surface.size() - ending.suffix.size()));
    const std::string_view onbin = kOnbinSurfaces[static_cast<size_t>(ending.onbin)];
    for (const auto& [verb_type, base_suffix] : grammar::Conjugation::getGodanTypesByOnbin(onbin)) {
      (void)verb_type;
      std::string base_form = stem + std::string(base_suffix);
      if (hasExactVerbEntry(dict_manager, base_form)) {
        return base_form;
      }
    }
  }

  return "";
}

std::string lemmatizeSuruPassiveWithDictionary(std::string_view surface,
                                               const dictionary::DictionaryManager* dict_manager) {
  if (dict_manager == nullptr) {
    return "";
  }

  for (std::string_view ending : kSuruPassiveEndings) {
    if (surface.size() < ending.size() || surface.compare(surface.size() - ending.size(), ending.size(), ending) != 0) {
      continue;
    }

    std::string stem(surface.substr(0, surface.size() - ending.size()));
    if (stem.empty()) {
      continue;
    }
    std::string base_form = stem + "する";
    if (hasExactVerbEntry(dict_manager, base_form)) {
      return base_form;
    }
  }

  return "";
}
}  // namespace lemmatizer_detail
}  // namespace suzume::postprocess
