/**
 * @file verb_candidates_helpers.cpp
 * @brief Implementation of internal helpers for verb candidate generation
 */

#include "verb_candidates_helpers.h"

#include <utility>

#include "analysis/candidate_constants.h"
#include "analysis/scorer_constants.h"
#include "core/debug.h"
#include "core/kana_constants.h"
#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "grammar/conjugation.h"
#include "normalize/char_type.h"
#include "normalize/utf8.h"

namespace suzume::analysis::verb_helpers {

bool startsInsideKanjiRunBeforeShi(const std::vector<char32_t>& codepoints, size_t start_pos) {
  if (start_pos == 0 || start_pos >= codepoints.size() || !normalize::isKanjiCodepoint(codepoints[start_pos - 1]) ||
      !normalize::isKanjiCodepoint(codepoints[start_pos])) {
    return false;
  }

  size_t kanji_end = start_pos + 1;
  while (kanji_end < codepoints.size() && normalize::isKanjiCodepoint(codepoints[kanji_end])) {
    ++kanji_end;
  }
  return kanji_end < codepoints.size() && codepoints[kanji_end] == U'し';
}

bool embedsTeFormAuxiliary(std::string_view surface) {
  static constexpr std::string_view kPatterns[] = {
      "ていく", "ていっ", "ていけ", "ていか",                // 〜ていく directional aspect
      "てもら", "てくれ", "てあげ", "てほしい", "てくださ",  // benefactive / request
      "てある", "である",                                    // completed-state existential
  };
  for (const std::string_view pattern : kPatterns) {
    if (surface.find(pattern) != std::string_view::npos) {
      return true;
    }
  }
  return false;
}

bool embedsTeFormMiruAuxiliary(const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos) {
  if (end_pos > codepoints.size()) {
    return false;
  }
  for (size_t pos = start_pos + 1; pos + 1 < end_pos; ++pos) {
    if ((codepoints[pos] == core::hiragana::kTe || codepoints[pos] == U'で') && codepoints[pos + 1] == U'み') {
      return true;
    }
  }
  return false;
}

bool masuAuxFollowsAt(const std::vector<char32_t>& codepoints, size_t pos) {
  if (pos + 1 >= codepoints.size() || codepoints[pos] != U'ま') {
    return false;
  }
  const char32_t next = codepoints[pos + 1];
  return next == U'す' || next == U'し' || next == U'せ';
}

size_t finiteMasuFormLengthAt(const std::vector<char32_t>& codepoints, size_t pos) {
  if (pos + 1 >= codepoints.size() || codepoints[pos] != U'ま') {
    return 0;
  }
  if (codepoints[pos + 1] == U'す') {
    if (pos + 3 < codepoints.size() && codepoints[pos + 2] == U'れ' && codepoints[pos + 3] == U'ば') {
      return 4;  // ますれば
    }
    return 2;  // ます
  }
  if (codepoints[pos + 1] == U'し') {
    if (pos + 2 < codepoints.size() && (codepoints[pos + 2] == U'た' || codepoints[pos + 2] == U'て')) {
      return 3;  // ました / まして
    }
    if (pos + 3 < codepoints.size() && codepoints[pos + 2] == U'ょ' && codepoints[pos + 3] == U'う') {
      return 4;  // ましょう
    }
  }
  if (pos + 2 < codepoints.size() && codepoints[pos + 1] == U'せ' && codepoints[pos + 2] == U'ん') {
    return 3;  // ません
  }
  return 0;
}

bool causativeSaseFollowsAt(const std::vector<char32_t>& codepoints, size_t pos) {
  return pos + 1 < codepoints.size() && codepoints[pos] == U'さ' && codepoints[pos + 1] == U'せ';
}

bool isSuruAuxiliaryStarter(char32_t next_char) {
  return next_char == U'ち' || next_char == U'て' || next_char == U'た' || next_char == U'な' || next_char == U'ま' ||
         next_char == U'よ' || next_char == U'ろ' || next_char == U'そ' || next_char == U'と' || next_char == U'か' ||
         next_char == U'つ';
}

size_t naiNegativeFormLengthAt(const std::vector<char32_t>& codepoints, size_t pos) {
  if (pos + 1 >= codepoints.size() || codepoints[pos] != U'な') {
    return 0;
  }
  const char32_t second = codepoints[pos + 1];
  if (second == U'い') {
    return 2;
  }
  if (second == U'く') {
    return pos + 2 < codepoints.size() && codepoints[pos + 2] == U'て' ? 3 : 2;
  }
  if (pos + 2 >= codepoints.size()) {
    return 0;
  }
  const char32_t third = codepoints[pos + 2];
  if (second == U'か' && third == U'っ') {
    return pos + 3 < codepoints.size() && codepoints[pos + 3] == U'た' ? 4 : 3;
  }
  if (second == U'け' && third == U'れ') {
    return pos + 3 < codepoints.size() && codepoints[pos + 3] == U'ば' ? 4 : 3;
  }
  if (second == U'け' && third == U'り') {
    return pos + 3 < codepoints.size() && codepoints[pos + 3] == U'ゃ' ? 4 : 3;
  }
  return second == U'き' && third == U'ゃ' ? 3 : 0;
}

bool naiNegativeFollowsAt(const std::vector<char32_t>& codepoints, size_t pos) {
  return naiNegativeFormLengthAt(codepoints, pos) != 0;
}

bool naiConditionalFollowsAt(const std::vector<char32_t>& codepoints, size_t pos) {
  return pos + 2 < codepoints.size() && codepoints[pos] == U'な' && codepoints[pos + 1] == U'け' &&
         codepoints[pos + 2] == U'れ';
}

bool itadakuParadigmStartsAt(const std::vector<char32_t>& codepoints, size_t pos) {
  if (pos + 3 >= codepoints.size() || codepoints[pos] != U'い' || codepoints[pos + 1] != U'た' ||
      codepoints[pos + 2] != U'だ') {
    return false;
  }
  const char32_t inflected = codepoints[pos + 3];
  return inflected == U'か' || inflected == U'き' || inflected == U'く' || inflected == U'け' || inflected == U'こ' ||
         inflected == U'い';
}

bool hasInternalVerbChainBoundary(const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos,
                                  const grammar::Inflection& inflection,
                                  const dictionary::DictionaryManager* dict_manager) {
  if (dict_manager == nullptr || end_pos <= start_pos + 3) {
    return false;
  }
  auto is_verified_verb_form = [&](size_t form_start, size_t form_end) {
    const std::string form = extractSubstring(codepoints, form_start, form_end);
    if (hasDictionaryEntry(dict_manager, form, core::PartOfSpeech::Verb) ||
        hasDictionaryEntry(dict_manager, form, core::PartOfSpeech::Auxiliary)) {
      return true;
    }
    if (form_end > form_start + 1 && kana::isOnbinCodepoint(codepoints[form_end - 1])) {
      const std::string stem = extractSubstring(codepoints, form_start, form_end - 1);
      const std::string onbin = extractSubstring(codepoints, form_end - 1, form_end);
      if (firstGodanOnbinDictBase(dict_manager, stem, onbin).matched) {
        return true;
      }
    }
    for (const auto& candidate : inflection.analyze(form)) {
      if (candidate.verb_type != grammar::VerbType::IAdjective &&
          (isVerbInDictionary(dict_manager, candidate.base_form) ||
           hasDictionaryEntry(dict_manager, candidate.base_form, core::PartOfSpeech::Auxiliary))) {
        return true;
      }
    }
    if (form_end > form_start) {
      const std::string_view base_suffix = grammar::godanBaseSuffixFromARow(codepoints[form_end - 1]);
      if (!base_suffix.empty()) {
        const std::string stem = extractSubstring(codepoints, form_start, form_end - 1);
        if (isVerbInDictionary(dict_manager, stem + std::string(base_suffix))) {
          return true;
        }
      }
    }
    return false;
  };

  for (size_t connective_pos = start_pos + 2; connective_pos + 2 < end_pos; ++connective_pos) {
    if (codepoints[connective_pos] != U'て' && codepoints[connective_pos] != U'で') {
      continue;
    }
    if (is_verified_verb_form(start_pos, connective_pos) && is_verified_verb_form(connective_pos + 1, end_pos)) {
      return true;
    }
  }
  for (size_t negative_pos = start_pos + 2; negative_pos + 1 < end_pos; ++negative_pos) {
    if (codepoints[negative_pos] == U'ず' && is_verified_verb_form(start_pos, negative_pos) &&
        is_verified_verb_form(negative_pos + 1, end_pos)) {
      return true;
    }
  }
  return false;
}

// =============================================================================
// Single-kanji Ichidan verbs
// =============================================================================

namespace {
constexpr char32_t kSingleKanjiIchidanList[] = {U'見', U'居', U'着', U'寝', U'煮', U'似',
                                                U'経', U'干', U'射', U'得', U'出', U'鋳'};
}  // namespace

bool isSingleKanjiIchidan(char32_t c) {
  for (char32_t k : kSingleKanjiIchidanList) {
    if (c == k)
      return true;
  }
  return false;
}

bool isSingleKanjiPoliteStem(char32_t c) {
  return isSingleKanjiIchidan(c) || c == U'来';
}

bool isSingleKanjiIchidanSurface(std::string_view surface) {
  if (normalize::utf8Length(surface) != 1) {
    return false;
  }
  auto codepoints = normalize::toCodepoints(surface);
  return !codepoints.empty() && isSingleKanjiIchidan(codepoints[0]);
}

// =============================================================================
// Candidate Sorting
// =============================================================================

void sortCandidatesByCost(std::vector<UnknownCandidate>& candidates, size_t first_index) {
  // Candidate lists are small and already close to generation order. A stable
  // insertion sort avoids pulling the generic introsort implementation into
  // WASM while keeping equal-cost candidates deterministic.
  for (size_t idx = first_index + 1; idx < candidates.size(); ++idx) {
    UnknownCandidate candidate = std::move(candidates[idx]);
    size_t insert_at = idx;
    while (insert_at > first_index && candidates[insert_at - 1].cost > candidate.cost) {
      candidates[insert_at] = std::move(candidates[insert_at - 1]);
      --insert_at;
    }
    candidates[insert_at] = std::move(candidate);
  }
}

// =============================================================================
// Pattern Skip Helpers
// =============================================================================

bool shouldSkipMasuAuxPattern(std::string_view surface, grammar::VerbType verb_type) {
  // Check if surface ends with ます/ました/ましょう/ません
  bool has_masu_aux = utf8::endsWith(surface, "ましょう") || utf8::endsWith(surface, "ました") ||
                      utf8::endsWith(surface, "ません") || utf8::endsWith(surface, "ます");

  if (!has_masu_aux) {
    return false;
  }

  // Don't skip suru-verb passive/causative patterns (され, させ)
  bool is_suru_passive_causative =
      (verb_type == grammar::VerbType::Suru && utf8::containsAny(surface, {"され", "させ"}));

  return !is_suru_passive_causative;
}

bool shouldSkipSouPattern(std::string_view surface, grammar::VerbType verb_type) {
  // Check for そう/そうです/そうだ at end
  bool has_sou_pattern = utf8::endsWith(surface, "そうです") || utf8::endsWith(surface, "そうだ") ||
                         utf8::endsWith(surface, scorer::kSuffixSou);

  // Don't skip i-adjective patterns
  return has_sou_pattern && verb_type != grammar::VerbType::IAdjective;
}

bool isCompoundAdjectivePattern(std::string_view surface) {
  if (surface.size() < core::kFourJapaneseCharBytes) {
    return false;
  }
  // Check for auxiliary adjective patterns in various conjugation forms
  if (utf8::containsAny(surface, {
                                     "にくい", "にくく", "にくか", "にくけ", "にくさ",  // difficult to do
                                     "やすい", "やすく", "やすか", "やすけ", "やすさ",  // easy to do
                                     "がたい", "がたく", "がたか", "がたけ", "がたさ"   // hard to do
                                 })) {
    return true;
  }
  // Also check stem forms at end of surface (e.g., 使いにく for 使いにく+い split)
  return utf8::endsWith(surface, "にく") || utf8::endsWith(surface, "やす") || utf8::endsWith(surface, "がた");
}

bool containsKuNaruPattern(std::string_view surface) {
  return surface.find("くなっ") != std::string::npos || surface.find("くなり") != std::string::npos ||
         surface.find("くなる") != std::string::npos || surface.find("くなれ") != std::string::npos ||
         surface.find("くなら") != std::string::npos;
}

bool isReduplicatedShiiAdjectiveHead(const std::vector<char32_t>& codepoints, size_t start_pos) {
  if (start_pos + 5 >= codepoints.size()) {
    return false;
  }
  // Doubled two-character unit XYXY, compared by codepoint so the same rule
  // serves kanji and both kana scripts.
  if (codepoints[start_pos] != codepoints[start_pos + 2] || codepoints[start_pos + 1] != codepoints[start_pos + 3]) {
    return false;
  }
  if (codepoints[start_pos + 4] != U'し') {
    return false;
  }
  // い/く/か/け start the i-adjective inflection endings after し:
  // しい, しく(ない/て), しかっ(た)/しかろ(う), しけれ(ば).
  const char32_t onset = codepoints[start_pos + 5];
  return onset == U'い' || onset == U'く' || onset == U'か' || onset == U'け';
}

grammar::GodanOnbinRange getGodanTypesByOnbin(std::string_view onbin) {
  return grammar::Conjugation::getGodanTypesByOnbin(onbin);
}

GodanOnbinDictMatch firstGodanOnbinDictBase(const dictionary::DictionaryManager* dict_manager, std::string_view stem,
                                            std::string_view onbin) {
  for (const auto& [verb_type, base_suffix] : getGodanTypesByOnbin(onbin)) {
    std::string base_form = std::string(stem) + std::string(base_suffix);
    if (isVerbInDictionary(dict_manager, base_form)) {
      return GodanOnbinDictMatch{verb_type, std::move(base_form), base_suffix, true};
    }
  }
  return GodanOnbinDictMatch{};
}

bool shouldSkipPassiveAuxPattern(std::string_view surface, grammar::VerbType verb_type) {
  // Skip patterns containing classical passive + べき
  if (utf8::endsWith(surface, "れべき")) {
    return true;
  }

  // Sahen predicates are search-tokenized as a nominal stem plus the する
  // mizenkei and passive auxiliary: 勉強+さ+れる. Retaining a unified
  // 勉強される candidate hides that grammatical chain.
  if (verb_type == grammar::VerbType::Suru) {
    const auto codepoints = normalize::utf8::decode(surface);
    for (size_t index = 1; index + 1 < codepoints.size(); ++index) {
      if (codepoints[index - 1] == U'さ' && codepoints[index] == U'れ') {
        const size_t negative_length = naiNegativeFormLengthAt(codepoints, index + 1);
        if (negative_length != 0 && index + 1 + negative_length == codepoints.size()) {
          return true;
        }
      }
    }
    return utf8::endsWith(surface, "される") || utf8::endsWith(surface, "された") ||
           utf8::endsWith(surface, "されて") || utf8::endsWith(surface, "されます") ||
           utf8::endsWith(surface, "されたい") || utf8::endsWith(surface, "されたく");
  }

  // Only apply remaining checks to Godan verbs
  if (!grammar::isGodanVerbType(verb_type)) {
    return false;
  }

  const auto codepoints = normalize::utf8::decode(surface);
  for (size_t index = 1; index + 1 < codepoints.size(); ++index) {
    if (grammar::isARowCodepoint(codepoints[index - 1]) && codepoints[index] == U'れ') {
      const size_t negative_length = naiNegativeFormLengthAt(codepoints, index + 1);
      if (negative_length != 0 && index + 1 + negative_length == codepoints.size()) {
        return true;
      }
    }
  }

  // Passive forms outside the ない family; desiderative forms stay explicit.
  return utf8::endsWith(surface, "れる") || utf8::endsWith(surface, "れた") || utf8::endsWith(surface, "れて") ||
         utf8::endsWith(surface, "れます") || utf8::endsWith(surface, "れたい") || utf8::endsWith(surface, "れたく");
}

bool isPassiveAuxContinuation(const std::vector<char32_t>& codepoints, size_t pos_after_re, bool strict_masu) {
  if (pos_after_re >= codepoints.size()) {
    return false;
  }
  char32_t after_re = codepoints[pos_after_re];
  // れる, れた, れて
  if (after_re == U'る' || after_re == U'た' || after_re == U'て') {
    return true;
  }
  // れ + ない family (れない, れなかった, れなくて, れなければ, ...)
  if (naiNegativeFollowsAt(codepoints, pos_after_re)) {
    return true;
  }
  // れま (れます, れました); the strict form requires す/せ (excludes bare ま)
  if (after_re == U'ま') {
    if (!strict_masu) {
      return true;
    }
    return pos_after_re + 1 < codepoints.size() &&
           (codepoints[pos_after_re + 1] == U'す' || codepoints[pos_after_re + 1] == U'せ');
  }
  return false;
}

bool shouldSkipCausativeAuxPattern(std::string_view surface, grammar::VerbType verb_type) {
  // Suru verb causative/passive: stay as single tokens
  if (verb_type == grammar::VerbType::Suru) {
    return false;
  }

  // Godan causative: せる, せた, せて
  if (grammar::isGodanVerbType(verb_type)) {
    return utf8::endsWith(surface, "せる") || utf8::endsWith(surface, "せた") || utf8::endsWith(surface, "せて");
  }

  // An unverified Ichidan candidate ending in A-row + せ is the stem of a
  // Godan causative (読ま+せ, 書か+せ), not an independent verb. Dictionary
  // candidates remain available for lexicalized derivatives such as 泳がせる.
  if (verb_type == grammar::VerbType::Ichidan) {
    const auto codepoints = normalize::utf8::decode(surface);
    if (codepoints.size() >= 2 && grammar::isARowCodepoint(codepoints[codepoints.size() - 2]) &&
        codepoints.back() == U'せ') {
      return true;
    }
  }

  // Causative-passive and passive-causative patterns for all verb types
  // (including Ichidan). These look like Ichidan verbs but contain a voice
  // auxiliary chain, so retain each auxiliary boundary.
  // E.g., 聞かせられた → 聞か + せ + られ + た;
  //       書かれさせる → 書か + れ + させる.
  if (utf8::endsWith(surface, "せられる") || utf8::endsWith(surface, "せられた") ||
      utf8::endsWith(surface, "せられて") || utf8::endsWith(surface, "せられない") ||
      utf8::containsAny(surface, {"れさせ", "られさせ"})) {
    return true;
  }
  return false;
}

namespace {

// Check if a hiragana tail analyzes as a conjugation of する with an auxiliary
// chain (して, しました, してもらっている); bare し and plain する have none.
bool isSuruAuxChainTail(std::string_view tail, const grammar::Inflection& inflection) {
  // Empty-stem する conjugations start with し/す/せ; される/させる need the
  // mizenkei さ with a stem (whole-surface check in the caller covers them)
  if (!utf8::startsWithAny(tail, {"し", "す", "せ"})) {
    return false;
  }
  if (utf8::equalsAny(tail, {"しろ", "せよ"})) {  // Imperatives carry no auxiliary chain
    return true;
  }
  for (const auto& cand : inflection.analyze(tail)) {
    if (cand.verb_type == grammar::VerbType::Suru && cand.stem.empty() && !cand.morphemes.empty()) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool shouldSkipSuruVerbAuxPattern(std::string_view surface, size_t kanji_count, const grammar::Inflection& inflection) {
  // Only apply to patterns with 2+ kanji (typical サ変 noun stems: 勉強, 対応)
  if (kanji_count < 2) {
    return false;
  }
  // Scan codepoint suffixes of the hiragana tail after the kanji run for a
  // する-auxiliary chain (勉強して, 空回りして) — ends-with semantics
  size_t tail_start = normalize::charToByteOffset(surface, kanji_count);
  std::string_view tail = surface.substr(std::min(tail_start, surface.size()));
  // Plain する is itself the productive サ変 predicate following the nominal
  // stem.  Preserve that search boundary even without a further auxiliary;
  // one-kanji lexical verbs such as 愛する are excluded by kanji_count above.
  if (tail == "する") {
    return true;
  }
  for (size_t pos = 0; pos < tail.size(); normalize::decodeUtf8(tail, pos)) {
    if (isSuruAuxChainTail(tail.substr(pos), inflection)) {
      return true;
    }
  }
  // される/させる need the mizenkei さ with a stem: use a whole-surface サ変
  // parse whose conjugated part starts with さ (対応される, 実行させた)
  for (const auto& cand : inflection.analyze(surface)) {
    if (cand.verb_type == grammar::VerbType::Suru && !cand.morphemes.empty() && utf8::startsWith(cand.suffix, "さ")) {
      return true;
    }
  }
  return false;
}

// =============================================================================
// Verb Type / Stem Analysis Helpers
// =============================================================================

std::string baseFormSuffix(grammar::VerbType verb_type) {
  if (verb_type == grammar::VerbType::Ichidan) {
    return "る";
  }
  const auto* row = grammar::Conjugation::getGodanRow(verb_type);
  if (row == nullptr) {
    return "";
  }
  return normalize::encodeUtf8(row->base_vowel);
}

bool isValidIRowIchidanStem(std::string_view stem) {
  if (stem.size() < 2 * core::kJapaneseCharBytes) {
    return false;
  }
  std::string_view last_char(stem.data() + stem.size() - core::kJapaneseCharBytes, core::kJapaneseCharBytes);
  if (!grammar::endsWithIRow(last_char)) {
    return false;
  }
  std::string_view kanji_part(stem.data(), stem.size() - core::kJapaneseCharBytes);
  bool is_single_kanji_i = (kanji_part.size() == core::kJapaneseCharBytes && last_char == "い");
  return !is_single_kanji_i;
}

bool containsTeFormAuxPattern(std::string_view surface) {
  return utf8::containsAny(surface, scorer::kTeFormAuxPenaltyPatterns);
}

bool containsCausativeAuxPattern(std::string_view surface) {
  return utf8::containsAny(surface, scorer::kCausativeAuxPenaltyPatterns);
}

bool containsPassiveCausativeAuxPattern(std::string_view surface) {
  return utf8::containsAny(surface, {"れさせ", "られさせ"});
}

VerbClassBests bestByVerbClass(const std::vector<grammar::InflectionCandidate>& candidates) {
  // Value-initialize so every field (including each accumulator's confidence) starts
  // at zero; the loop then keeps the highest-confidence candidate per verb class.
  VerbClassBests bests{};
  for (const auto& cand : candidates) {
    if (cand.has_explanatory_suffix) {
      continue;
    }
    if (cand.verb_type == grammar::VerbType::Ichidan && cand.confidence > bests.ichidan.confidence) {
      bests.ichidan = cand;
    }
    if (cand.verb_type == grammar::VerbType::Suru && cand.confidence > bests.suru.confidence) {
      bests.suru = cand;
    }
    if (grammar::isGodanVerbType(cand.verb_type) && cand.confidence > bests.godan.confidence) {
      bests.godan = cand;
    }
  }
  return bests;
}

}  // namespace suzume::analysis::verb_helpers
