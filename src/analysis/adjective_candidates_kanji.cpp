/**
 * @file adjective_candidates_kanji.cpp
 * @brief Kanji i-adjective and na-adjective candidate generation
 */

#include <algorithm>

#include "adjective_candidates.h"
#include "adjective_candidates_internal.h"
#include "analysis/candidate_constants.h"
#include "analysis/scorer_constants.h"
#include "core/debug.h"
#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "grammar/patterns.h"
#include "normalize/char_type.h"
#include "normalize/exceptions.h"
#include "normalize/utf8.h"
#include "suffix_candidates.h"
#include "unknown.h"
#include "verb_candidates_helpers.h"

namespace suzume::analysis {

using verb_helpers::addEmphaticVariants;
using verb_helpers::findCharRegionEnd;
using verb_helpers::isAdjectiveInDictionary;
using verb_helpers::isVerbInDictionary;

using adj_detail::makeIAdjCandidate;
using adj_detail::makeTrimmedAdjVariant;

namespace {

// Na-adjective forming suffixes (〜的 patterns)
const std::vector<std::string_view> kNaAdjSuffixes = {
    "的",  // 理性的, 論理的, etc.
};

/**
 * @brief Create a na-adjective candidate
 */
inline UnknownCandidate makeNaAdjCandidate(const std::string& surface, size_t start, size_t end, float cost,
                                           bool has_suffix, [[maybe_unused]] float confidence,
                                           [[maybe_unused]] const char* pattern) {
  auto cand = makeCandidate(surface, start, end, core::PartOfSpeech::Adjective, cost, has_suffix,
                            CandidateOrigin::AdjectiveNa, core::ExtendedPOS::AdjNaAdj);
#ifdef SUZUME_DEBUG_INFO
  cand.confidence = confidence;
  cand.pattern = pattern;
#endif
  return cand;
}

// =============================================================================
// Pattern Skip Helpers for I-Adjective Candidate Generation
// =============================================================================

/**
 * @brief Check if a pattern should be skipped based on simple pattern matching
 *
 * Checks for patterns that are clearly NOT i-adjectives:
 * - Empty surface
 * - Single kanji + single hiragana い (godan verb renyokei like 伴い, 用い)
 * - Patterns starting with っ (te-form contractions like 待ってく)
 * - Patterns ending with んでい/でい (te-form + auxiliary like 学んでい)
 * - Passive/causative negative renyokei (られなく, させなく)
 * - Negative become patterns (れなくなった)
 * - なく followed by なった/なる (verb negative + become)
 * - Causative stem patterns (べさ, べさせ)
 * - Godan verb renyokei + そう (飲みそう, 降りそう)
 *
 * @param surface Full surface string (kanji + hiragana)
 * @param hiragana_part Hiragana portion only
 * @param codepoints Full text codepoints
 * @param start_pos Start position in codepoints
 * @param kanji_end End of kanji portion
 * @param end_pos Current end position being checked
 * @return true if the pattern should be skipped
 */
bool shouldSkipSimplePatterns(const std::string& surface, const std::string& hiragana_part,
                              const std::vector<char32_t>& codepoints, size_t start_pos, size_t kanji_end,
                              size_t end_pos) {
  // Empty surface
  if (surface.empty()) {
    return true;
  }

  // Single-kanji + single hiragana い patterns - likely godan verb renyokei
  // Real single-kanji i-adjectives (怖い, 酸い) should be in dictionary
  if (kanji_end == start_pos + 1 && end_pos == kanji_end + 1) {
    return true;
  }

  // Copula negation patterns (kanji + じゃな...): 嫌じゃない, 嫌じゃなかった
  // These are na-adjective + じゃ(copula) + ない(negation), not i-adjectives
  if (utf8::startsWith(hiragana_part, "じゃな")) {
    return true;
  }

  // Patterns starting with っ (te-form contractions like 待ってく = 待っていく)
  if (utf8::startsWith(hiragana_part, "っ")) {
    return true;
  }

  // Patterns ending with んでい or でい (te-form + auxiliary like 学んでいく)
  if (utf8::endsWith(hiragana_part, "んでい") || utf8::endsWith(hiragana_part, "でい")) {
    return true;
  }

  // Passive/potential/causative negative renyokei (られなく, させなく, etc.)
  if (grammar::endsWithPassiveCausativeNegativeRenyokei(hiragana_part)) {
    return true;
  }

  // Negative become pattern (れなくなった)
  if (grammar::endsWithNegativeBecomePattern(hiragana_part)) {
    return true;
  }

  // なく followed by なった/なる/なって (verb negative + become)
  if (utf8::endsWith(hiragana_part, "なく") && end_pos < codepoints.size()) {
    if (codepoints[end_pos] == U'な') {
      return true;
    }
  }

  // Causative stem patterns (べさ, べさせ, etc.) - ichidan causative
  if (utf8::equalsAny(hiragana_part, {"べさ", "べさせ", "べさせら", "べさせられ"})) {
    return true;
  }

  // Godan verb renyokei + そう patterns (飲みそう, 降りそう, etc.)
  // Single kanji + renyokei suffix (i-row: み/ぎ/ち/び/り/に) + そう
  // Note: し and き are handled separately with dictionary validation
  if (kanji_end == start_pos + 1 && hiragana_part.size() >= core::kThreeJapaneseCharBytes) {
    std::string_view renyokei_char = std::string_view(hiragana_part).substr(0, core::kJapaneseCharBytes);
    if (utf8::equalsAny(renyokei_char, {"み", "ぎ", "ち", "び", "り", "に"}) &&
        hiragana_part.substr(core::kJapaneseCharBytes, core::kTwoJapaneseCharBytes) == scorer::kSuffixSou) {
      return true;
    }
  }

  return false;
}

/**
 * @brief Check if the character at pos puts a preceding い in verb-onbin context
 *
 * い followed by て/た/だ/や belongs to a godan onbin form (届いて, 泳いだ,
 * 続いた), not an i-adjective base. で also counts as onbin (泳いで) unless it
 * starts です (良いです = ADJ + AUX).
 *
 * @param codepoints Full text codepoints
 * @param pos Position of the character right after the い
 * @return true if the い is a verb-onbin surface, not an adjective ending
 */
bool isVerbOnbinContextAfterI(const std::vector<char32_t>& codepoints, size_t pos) {
  if (pos >= codepoints.size()) {
    return false;
  }
  char32_t next = codepoints[pos];
  if (next == U'て' || next == U'た' || next == U'だ' || next == U'や') {
    return true;
  }
  if (next == U'で') {
    // で is verb onbin context (泳いで) UNLESS followed by す (です)
    bool is_desu = (pos + 1 < codepoints.size() && codepoints[pos + 1] == U'す');
    return !is_desu;
  }
  return false;
}

}  // namespace

std::vector<UnknownCandidate> generateAdjectiveCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                                          const std::vector<normalize::CharType>& char_types,
                                                          const grammar::Inflection& inflection,
                                                          const dictionary::DictionaryManager* dict_manager) {
  std::vector<UnknownCandidate> candidates;

  // Lexicalized adverbial adjective 間もなく (連用形 of 間もない, "soon"). Emitted as one
  // token ONLY when 間 is not preceded by a kanji, so 時間もなく / 居間もなく still split as
  // 時間|も|なく — a plain dictionary entry cannot express "not after kanji", but a guarded
  // candidate can. The base form 間もない is deliberately not lexicalized (MeCab splits it
  // as 間|も|ない), so only the 連用形 is recognized here.
  if (start_pos + 3 < codepoints.size() && codepoints[start_pos] == U'間' && codepoints[start_pos + 1] == U'も' &&
      codepoints[start_pos + 2] == U'な' && codepoints[start_pos + 3] == U'く' &&
      (start_pos == 0 || start_pos - 1 >= char_types.size() ||
       char_types[start_pos - 1] != normalize::CharType::Kanji)) {
    candidates.push_back(makeIAdjCandidate("間もなく", start_pos, start_pos + 4, "間もない",
                                           candidate::kCompoundAdjBaseCost, CandidateOrigin::AdjectiveI,
                                           candidate::kDictFallbackAdjConfidence, "ma_mo_naku"));
  }

  if (start_pos >= char_types.size() || char_types[start_pos] != normalize::CharType::Kanji) {
    return candidates;
  }

  // Find kanji portion (1-2 characters for i-adjectives; no 3-char kanji stems exist).
  // The only longer stems are fully spelled-out reduplications (馬鹿馬鹿しい), whose
  // doubled 2-kanji unit + し + inflection onset is detected explicitly so the
  // 4-kanji stem is not truncated to 馬鹿馬 + 鹿しい.
  size_t kanji_end = findCharRegionEnd(char_types, start_pos, 2, normalize::CharType::Kanji);
  if (verb_helpers::isReduplicatedShiiAdjectiveHead(codepoints, start_pos) &&
      findCharRegionEnd(char_types, start_pos, 4, normalize::CharType::Kanji) == start_pos + 4) {
    kanji_end = start_pos + 4;
  }

  if (kanji_end == start_pos) {
    return candidates;
  }

  // Look for hiragana after kanji (adjective endings like い, かった, くない)
  // Note: Some adjectives have hiragana in the stem (美しい, 楽しい, 涼しい, etc.)
  // so we allow any hiragana and let the inflection module decide
  if (kanji_end >= char_types.size() || char_types[kanji_end] != normalize::CharType::Hiragana) {
    return candidates;
  }

  // Check if first hiragana is a particle that can NEVER be part of an adjective
  // Note: て is the te-form particle (接続助詞), not part of adjective stems
  // This prevents "来てい" from being parsed as an adjective (来ている = verb)
  char32_t first_hiragana = codepoints[kanji_end];
  if (normalize::isNeverAdjectiveStemAfterKanji(first_hiragana)) {
    // Exception: medial も in a lexical i-adjective (頼もしい, 好もしい, and their
    // conjugations 頼もしく/頼もしかっ…). The adjective-forming stem consonant し
    // immediately follows も; the 係助詞 reading (本もない = 本 + も + ない) never
    // places し after も, so require it here. する cases that also read も+し
    // (見もしない) are dropped downstream by the loop's verb-negative filter.
    bool medial_mo_adjective = first_hiragana == U'も' && kanji_end == start_pos + 1 &&
                               kanji_end + 1 < codepoints.size() && codepoints[kanji_end + 1] == U'し';
    if (!medial_mo_adjective) {
      return candidates;  // These particles follow nouns/verbs, not adjective stems
    }
  }

  size_t hiragana_end = findCharRegionEnd(char_types, kanji_end, 8, normalize::CharType::Hiragana);

  if (hiragana_end <= kanji_end) {
    return candidates;
  }

  // A kanji verb stem followed by すぎ is a verb-plus-auxiliary construction,
  // not a single adjective.
  // Pattern: kanji + (き/ぎ/し/ち/に/び/み/り/い) + すぎ...
  std::string hira_part = extractSubstring(codepoints, kanji_end, hiragana_end);
  // C++17 compatible: check if hiragana contains "すぎ" (6 bytes)
  if (hira_part.find("すぎ") != std::string::npos) {
    return candidates;  // Skip this candidate - force split path
  }

  // Special handling for single-kanji + い patterns (高い, 辛い, 甘い, etc.)
  // These are common i-adjectives that may not be recognized by inflection analysis
  // due to penalty_i_adj_single_kanji reducing confidence below threshold.
  // Generate candidate directly without relying on inflection analysis.
  // Also handles in-context cases like 甘いもの where hiragana_end extends past い.
  // Skip if already registered as NOUN in dictionary (e.g. 勢い) to avoid POS conflict.
  if (kanji_end == start_pos + 1 && codepoints[kanji_end] == U'い') {
    size_t adj_end = kanji_end + 1;
    // Skip if い is followed by て/た/だ/で/や (verb onbin context, not adjective)
    // e.g., 届いて(verb te-form), 泳いだ(verb ta-form), 泳いで(godan-ga te-form),
    //        使いやすい(verb renyokei)
    // Exception: で followed by す (part of です) is NOT verb context
    //   良いです = ADJ + AUX, not VERB onbin
    bool is_verb_context = isVerbOnbinContextAfterI(codepoints, adj_end);
    if (!is_verb_context) {
      std::string surface = extractSubstring(codepoints, start_pos, adj_end);
      bool is_dict_noun = verb_helpers::isNounInDictionary(dict_manager, surface);
      // A surface that is itself a dictionary verb conjugation (来い = 来る 命令形,
      // or a godan-wa renyokei like 買い) is not an adjective — 来 is a verb stem,
      // unlike a genuine single-kanji adjective stem (濃い, 良い).
      bool is_dict_verb = verb_helpers::hasDictionaryEntry(dict_manager, surface, core::PartOfSpeech::Verb);
      if (is_dict_noun) {
        SUZUME_DEBUG_LOG_VERBOSE("[ADJ_SINGLE] \"" << surface << "\" is dict NOUN, skipping ADJ candidate\n");
      } else if (is_dict_verb) {
        SUZUME_DEBUG_LOG_VERBOSE("[ADJ_SINGLE] \"" << surface << "\" is dict VERB, skipping ADJ candidate\n");
      } else {
        // Use moderate cost to compete with verb candidates (尊う has cost ~0.5)
        // Lower cost wins, so 0.35 should beat verb candidates
        SUZUME_DEBUG_LOG_VERBOSE("[ADJ_SINGLE] \"" << surface << "\" cost=" << candidate::kSingleKanjiICost << "\n");
        candidates.push_back(makeIAdjCandidate(surface, start_pos, adj_end, surface, candidate::kSingleKanjiICost,
                                               CandidateOrigin::AdjectiveI, 0.5F, "single_kanji_i"));
      }
    }
  }

  // Special handling for single-kanji + く patterns (甘く, 辛く, 暗く, etc.)
  // Only generate ADJ renyokei candidate when followed by adjective-renyokei
  // continuations (て/ない/なっ/なる/も), which disambiguate from godan-ka verbs.
  // Without this context check, 歩く/叩く etc. would get false ADJ candidates.
  if (kanji_end == start_pos + 1 && codepoints[kanji_end] == U'く') {
    size_t adj_end = kanji_end + 1;
    bool is_adj_context = false;
    if (adj_end < codepoints.size()) {
      char32_t next = codepoints[adj_end];
      // A bare も is ambiguous with the first character of the formal noun
      // もの (動くもの). Treat it as adjective evidence only when it opens a
      // negative continuation such as 高くもない.
      bool is_mo_negative = next == U'も' && adj_end + 1 < codepoints.size() && codepoints[adj_end + 1] == U'な';
      is_adj_context = (next == U'て' || next == U'な' || is_mo_negative);
    }
    if (is_adj_context) {
      std::string surface = extractSubstring(codepoints, start_pos, adj_end);
      std::string lemma = extractSubstring(codepoints, start_pos, kanji_end) + "い";
      SUZUME_DEBUG_LOG_VERBOSE("[ADJ_SINGLE_KU] \"" << surface << "\" cost=" << candidate::kSingleKanjiKuCost << "\n");
      candidates.push_back(makeIAdjCandidate(surface, start_pos, adj_end, lemma, candidate::kSingleKanjiKuCost,
                                             CandidateOrigin::AdjectiveI, 0.5F, "single_kanji_ku"));
    }
  }

  // Try different ending lengths
  for (size_t end_pos = hiragana_end; end_pos > kanji_end; --end_pos) {
    std::string surface = extractSubstring(codepoints, start_pos, end_pos);
    std::string hiragana_part = extractSubstring(codepoints, kanji_end, end_pos);

    // Skip patterns that are clearly not i-adjectives
    if (shouldSkipSimplePatterns(surface, hiragana_part, codepoints, start_pos, kanji_end, end_pos)) {
      continue;
    }

    // B57: For single kanji + ければ patterns (叩ければ, 引ければ, etc.),
    // check if the kanji + く is a verb. If so, this is likely verb potential + conditional,
    // not an adjective pattern.
    // 叩ければ → 叩く (verb exists) → skip adjective (叩い is not a real adjective)
    // 寒ければ → 寒い (adjective) - handled separately as hiragana_part starts with け
    if (kanji_end == start_pos + 1 && hiragana_part == "ければ") {
      std::string kanji_stem = extractSubstring(codepoints, start_pos, kanji_end);
      std::string verb_form = kanji_stem + "く";
      if (isVerbInDictionary(dict_manager, verb_form)) {
        continue;  // Verb exists, this is verb potential-conditional (叩ける + ば)
      }
    }

    // Skip patterns that are clearly verb negatives, not adjectives
    // 〜かない, 〜がない, etc. are Godan verb mizenkei + ない patterns
    // 〜しない is Suru verb + ない, 〜べない is Ichidan verb + ない
    // Exception: dictionary-confirmed adjectives (情けない, 味気ない, etc.)
    // These are genuine i-adjectives whose okurigana coincidentally matches
    // verb negative patterns but must not be blocked.
    if (grammar::endsWithVerbNegative(hiragana_part)) {
      if (!isAdjectiveInDictionary(dict_manager, surface)) {
        continue;  // Skip - verb negative pattern, not adjective
      }
      SUZUME_DEBUG_LOG_VERBOSE("[ADJ_NAI] dict-confirmed nai-adj: \"" << surface << "\"\n");
    }

    // A 2+ consecutive-kanji stem directly followed by exactly ない is a noun plus the
    // adjective/auxiliary ない (問題+ない, 関係+ない, 心配+ない), never a single
    // i-adjective — a genuine i-adjective stem is never a multi-kanji noun. Suppress the
    // fused candidate so the 名詞|ない split path wins (as 仕方ない already does).
    // Dictionary-confirmed 2-kanji nai-adjectives (味気ない etc.) keep their fused form.
    if (hiragana_part == "ない" && (kanji_end - start_pos) >= 2 && !isAdjectiveInDictionary(dict_manager, surface)) {
      continue;
    }

    // Skip patterns that are サ変動詞 + て + auxiliary
    // E.g., 説明してほしい = 説明(noun) + し(suru renyokei) + て + ほしい
    // These should split, not be treated as single adjectives
    if (surface.size() >= 15 &&  // At least 5 chars (kanji + してほしい)
        surface.find("してほしい") != std::string::npos) {
      continue;  // Skip - suru verb + te + hoshii pattern
    }

    // Skip surfaces that are known dictionary verbs
    // E.g., 下さい(=ください) is a verb (くださる), not an i-adjective
    if (isVerbInDictionary(dict_manager, surface)) {
      continue;
    }

    // Check all candidates for IAdjective, not just the best one
    // This handles cases like 美味しそう where Suru (美味する) may have higher
    // confidence than IAdjective (美味しい), but we still want to generate
    // an adjective candidate for the lattice to choose from
    const auto& all_candidates = inflection.analyze(surface);

    for (const auto& cand : all_candidates) {
      // Require confidence >= 0.5 for i-adjectives
      // Base forms like 寒い get exactly 0.5, conjugated forms like 美しかった get 0.68+
      if (cand.confidence >= candidate::kIAdjConfMin && cand.verb_type == grammar::VerbType::IAdjective) {
        // Filter out false positives: いたす honorific pattern
        // Invalid patterns (all have た after the candidate):
        //   - サ変名詞 + いたす: 検討いたします, 勉強いたしました
        //   - Verb renyokei + いたす: 伝えいたします, 申しいたします
        // Valid patterns:
        //   - 面白いな (next char is な)
        //   - 寒いよ (next char is よ)
        //   - 面白い (end of text)
        // Key insight: if minimum confidence (0.5) and next char is た, skip
        if (cand.confidence <= candidate::kIAdjConfMin) {
          if (end_pos < codepoints.size() && codepoints[end_pos] == U'た') {
            continue;  // Skip - likely いたす honorific pattern
          }
        }

        // Skip verb renyokei + たい patterns (desiderative auxiliary)
        // The inflection engine may identify verb+たい conjugations as i-adjectives:
        //   行きたくなかった → base_form=行きたくない (contains たくない)
        //   行きたかった → base_form=行きたい (ends with たい)
        // Real adjective: 冷たくなかった → base_form=冷たくない, char before たくない is 冷 (kanji)
        // False adjective: 行きたくなかった → base_form=行きたくない, char before たくない is き (hiragana)
        {
          std::string_view base_sv(cand.base_form);
          // Determine the stem before the たい-related suffix
          std::string_view before_tai;
          if (utf8::endsWith(base_sv, "たくない") && base_sv.size() > 4 * core::kJapaneseCharBytes) {
            // Negative form: 行きたくない → check char before たくない
            before_tai = base_sv.substr(0, base_sv.size() - 4 * core::kJapaneseCharBytes);
          } else if (utf8::endsWith(base_sv, "たい") && base_sv.size() > 2 * core::kJapaneseCharBytes) {
            // Base form: 行きたい → check char before たい
            before_tai = base_sv.substr(0, base_sv.size() - 2 * core::kJapaneseCharBytes);
          }
          if (!before_tai.empty()) {
            auto last_cp = utf8::decodeFirstChar(utf8::lastChar(before_tai));
            if (last_cp != 0 && kana::isHiraganaCodepoint(last_cp)) {
              continue;  // Verb renyokei + たい, not a real adjective
            }
          }
        }

        // 様態 そう span guard: an i-adjective never inflects through そう —
        // そう is always a separate appearance auxiliary. When the inflection
        // engine reconstructed the base by reading そう(な/だ/に…) as an
        // adjective ending (surface = stem + そう…, base = stem + い), the span
        // over-reaches the stem: the AdjStem generator emits the bare stem
        // (優し, 高, 大き) and そう attaches as its own token. This also covers
        // verb renyokei + そう (書きそう, 遅刻しそう), whose hypothesized base
        // stem + い is a non-word.
        {
          std::string_view base_sv(cand.base_form);
          std::string_view surf_sv(surface);
          if (utf8::endsWith(base_sv, "い")) {
            std::string_view stem_sv = base_sv.substr(0, base_sv.size() - core::kJapaneseCharBytes);
            if (surf_sv.size() > stem_sv.size() && utf8::startsWith(surf_sv, stem_sv) &&
                utf8::startsWith(surf_sv.substr(stem_sv.size()), scorer::kSuffixSou)) {
              SUZUME_DEBUG_LOG_VERBOSE("[ADJ_SKIP] \"" << surface << "\" spans 様態そう, stem path handles split\n");
              continue;
            }
          }
        }

        // Lower base cost (0.2F) to beat verb candidates after POS prior adjustment
        // ADJ prior (0.3) is higher than VERB prior (0.2), so we need lower edge cost
        float cost = candidate::confidenceScaledCost(candidate::kKanjiAdjBaseCost, cand.confidence,
                                                     candidate::kKanjiAdjConfScale);
        // Penalty for non-dictionary i-adjective nominalization (さ ending)
        // This prevents false positives like 勉強さ (from non-existent 勉強い)
        // from beating suru-verb split path (勉強 + さ + れる)
        if (surface.size() >= 3 && utf8::endsWith(surface, "さ")) {
          cost += candidate::kAdjModeratePenalty;  // Unconfirmed さ nominalization
          SUZUME_DEBUG_LOG_VERBOSE("[COST_ADJ] \"" << surface << "\" +1.5 (unconfirmed_sa_nom)\n");
        }
        // Penalty for compound adjective patterns (verb renyokei + やすい/にくい/がたい)
        // MeCab splits these: 使いにくい → 使い + にくい
        // Must be non-dictionary adjectives with >= 4 characters to avoid penalizing
        // standalone やすい/にくい/がたい which are in the dictionary
        if (surface.size() >= 4 * core::kJapaneseCharBytes && verb_helpers::isCompoundAdjectivePattern(surface)) {
          cost += candidate::kAdjSplitForcePenalty;  // Force split
          SUZUME_DEBUG_LOG_VERBOSE("[COST_ADJ] \"" << surface << "\" +2.0 (compound_adj_penalty)\n");
        }
        // Penalty for く + なる patterns (i-adjective adverbial + なる verb)
        // MeCab splits these: 良くなる → 良く + なる, 高くなった → 高く + なっ + た.
        // Scan the whole surface (not just its end) so trailing auxiliaries after
        // the absorbed なる (寒くなってきた = 寒く+なっ+て+き+た) are still caught.
        // Must have at least 2 chars before くなる to avoid penalizing standalone patterns
        if (surface.size() >= 3 * core::kJapaneseCharBytes) {
          if (verb_helpers::containsKuNaruPattern(surface)) {
            cost += candidate::kAdjSplitForcePenalty;  // Force adj く-form + なる split
            SUZUME_DEBUG_LOG_VERBOSE("[COST_ADJ] \"" << surface << "\" +2.0 (ku_naru_split)\n");
          }
        }
        // Penalty for とい/という endings (noun + quotative patterns, not adjectives)
        // E.g., 友人という → 友人 + という (determiner), not 友人とい(adj) + う
        if (surface.size() >= 3 * core::kJapaneseCharBytes) {
          if (utf8::endsWith(surface, "とい") || utf8::endsWith(surface, "という")) {
            cost += candidate::kAdjSplitForcePenalty;  // Protect NOUN + という pattern
            SUZUME_DEBUG_LOG_VERBOSE("[COST_ADJ] \"" << surface << "\" +2.0 (toiu_pattern)\n");
          }
        }
        // Penalty for らしい endings (adj + conjecture auxiliary patterns)
        // E.g., 美しいらしい → 美しい + らしい, not 美しいらし(adj) + い
        // 春らしい → 春 + らしい, not 春らし(adj) + い
        // Must have at least 2 chars before らしい to avoid penalizing standalone らしい
        if (surface.size() >= 3 * core::kJapaneseCharBytes) {
          // Also match the らしく + negative forms (らしくない/らしくなかっ/らしくなかった):
          // their surface ends in the negative, not らしく, so the ku-form trimmed
          // variant would otherwise inherit an unpenalized cost and keep 子供らしく
          // merged (子供らしくない → 子供 + らしく + ない). A genuine adjective whose
          // stem before らしく is a non-word (素晴らしい) stays merged because splitting
          // it off leaves the costly non-word 素晴.
          if (utf8::endsWith(surface, "らしい") || utf8::endsWith(surface, "らしく") ||
              utf8::endsWith(surface, "らしかっ") || utf8::endsWith(surface, "らしくない") ||
              utf8::endsWith(surface, "らしくなかっ") || utf8::endsWith(surface, "らしくなかった")) {
            cost += candidate::kAdjModeratePenalty;  // Promote adj/noun + らしい split
            SUZUME_DEBUG_LOG_VERBOSE("[COST_ADJ] \"" << surface << "\" +1.5 (rashii_conjecture)\n");
          }
        }
        // Penalty for まい endings (verb + negative volitional auxiliary)
        // E.g., 知るまい → 知る + まい, 出来まい → 出来 + まい
        // まい is an auxiliary attached to verb dictionary form, not an i-adjective suffix
        if (surface.size() >= 2 * core::kJapaneseCharBytes) {
          if (utf8::endsWith(surface, "まい")) {
            cost += candidate::kAdjSplitForcePenalty;  // Promote verb + まい split
            SUZUME_DEBUG_LOG_VERBOSE("[COST_ADJ] \"" << surface << "\" +2.0 (mai_auxiliary)\n");
          }
        }
        // Skip a fake i-adjective that is really [noun] + a dictionary verb whose
        // onbin tail reconstructs a non-word かい/たい-shaped base: 手間+かかった →
        // 手間かい, 2時間半+かかった → 半かい. These share the [X]+かい shape with
        // genuine adjectives (細かい), so distinguish by dictionary — skip only when
        // the base is not a dictionary adjective, its stem ends in hiragana (excludes
        // pure-kanji stems like 高い), and the hiragana tail is itself a complete
        // conjugation of a dictionary verb (かかった → かかる). [noun][verb] is not
        // an adjective, so skip rather than penalize (mirrors the ゆく/いく case).
        if (!isAdjectiveInDictionary(dict_manager, cand.base_form)) {
          std::string_view base_sv(cand.base_form);
          if (base_sv.size() > core::kJapaneseCharBytes && utf8::endsWith(base_sv, "い")) {
            std::string_view stem = base_sv.substr(0, base_sv.size() - core::kJapaneseCharBytes);
            char32_t stem_last = utf8::decodeFirstChar(utf8::lastChar(stem));
            if (stem_last != 0 && kana::isHiraganaCodepoint(stem_last)) {
              bool tail_is_dict_verb = false;
              for (const auto& vres : inflection.analyze(hiragana_part)) {
                if (vres.verb_type == grammar::VerbType::IAdjective) {
                  continue;
                }
                if (isVerbInDictionary(dict_manager, vres.base_form)) {
                  tail_is_dict_verb = true;
                  break;
                }
              }
              if (tail_is_dict_verb) {
                SUZUME_DEBUG_LOG_VERBOSE("[ADJ_SKIP] \"" << surface
                                                         << "\" tail is dict verb, skipping fake adjective\n");
                continue;
              }
            }
          }
        }
        // Skip subsidiary-verb ゆく/いく compounds misread as i-adjectives.
        // Verb 連用形 + ゆく (散りゆく, 消えゆく) ends in く, so inflection
        // hypothesizes a fake i-adjective base (散りゆい). When the base is
        // not a dictionary adjective and the part before ゆく/いく is itself
        // a dictionary verb form, this is the compound-verb construction —
        // leave it to the verb paths (散り + ゆく).
        if (surface.size() > 2 * core::kJapaneseCharBytes &&
            (utf8::endsWith(surface, "ゆく") || utf8::endsWith(surface, "いく")) &&
            !isAdjectiveInDictionary(dict_manager, cand.base_form)) {
          std::string v1_prefix = surface.substr(0, surface.size() - 2 * core::kJapaneseCharBytes);
          // The prefix is a verb 連用形 when it is a dictionary surface itself
          // (散り) or when inflection confidently reconstructs a verb from it
          // (消え → 消える, 過ぎ → 過ぎる). Dictionary verification lowers the
          // bar; a confident inflection hypothesis alone is also accepted since
          // the competing i-adjective base (Xゆい) is already known to be fake.
          bool prefix_is_verb = verb_helpers::hasDictionaryEntry(dict_manager, v1_prefix, core::PartOfSpeech::Verb);
          if (!prefix_is_verb) {
            // Low bar: the preconditions (ゆく/いく ending, fake adjective base)
            // already exclude real adjectives, so any plausible verb hypothesis
            // (消え → 消える 0.74, 暮れ → 暮れる 0.3 after e-row ambiguity
            // penalty) marks the prefix as a 連用形.
            const auto& v1_results = inflection.analyze(v1_prefix);
            for (const auto& v1_res : v1_results) {
              if (v1_res.verb_type == grammar::VerbType::IAdjective) {
                continue;
              }
              if (isVerbInDictionary(dict_manager, v1_res.base_form) ||
                  v1_res.confidence >= candidate::kV1PrefixMinConfidence) {
                prefix_is_verb = true;
                break;
              }
            }
          }
          if (prefix_is_verb) {
            SUZUME_DEBUG_LOG_VERBOSE("[ADJ_SKIP] \"" << surface << "\" is verb renyokei + subsidiary ゆく/いく\n");
            continue;  // Skip - compound verb, not adjective
          }
        }
        // Skip さそう endings (adj nominalization + appearance auxiliary)
        // E.g., 気持ちよさそうに → 気持ちよ + さ + そう + に
        //        なさそう → な + さ + そう (handled separately in hiragana adj)
        // adj-stem + さ(nominalizer) + そう(appearance) should be split
        if (surface.size() >= 3 * core::kJapaneseCharBytes) {
          if (utf8::endsWith(surface, "さそう") || utf8::endsWith(surface, "さそうに") ||
              utf8::endsWith(surface, "さそうな") || utf8::endsWith(surface, "さそうだ")) {
            continue;  // Skip - force adj + さ + そう split
          }
        }
        // Set lemma to base form from inflection analysis (e.g., 使いやすく → 使いやすい)
        auto adj_cand = makeIAdjCandidate(surface, start_pos, end_pos, cand.base_form, cost,
                                          CandidateOrigin::AdjectiveI, cand.confidence, "i_adjective");
        // Note: 2-kanji stem compound adjectives (薄暗い, 物悲しく) need
        // has_suffix to skip exceeds_dict_length penalty. This is handled
        // in the compound adjective section below (with tighter guards).
        candidates.push_back(std::move(adj_cand));
        break;  // Only add one adjective candidate per surface
      }
    }
  }

  // Compound adjective: set has_suffix on existing 2-kanji stem ADJ candidates
  // to skip exceeds_dict_length penalty in tokenizer (薄暗い, 物悲しく, etc.)
  // Guards prevent false positives on suru-verb patterns (遅刻しそう, 確認して):
  //  1. First hiragana must be valid i-adj inflection char (い,く,け,か,し)
  //  2. Hiragana portion must be short (≤5 chars)
  if (kanji_end == start_pos + 2 && kanji_end < codepoints.size()) {
    char32_t first_hira = codepoints[kanji_end];
    // A period/duration formal-noun suffix (間/分/秒/中) must not head an
    // i-adjective compound stem: "3分間続いた" would split as 3分 + 間続い(fake
    // ADJ) instead of 3分間 + 続い(verb), and "長い間続いた" likewise severs 間.
    // Allow only when the second kanji itself forms a genuine i-adjective
    // (間近い → 近い, 分厚い → 厚い), otherwise the compound is masking a verb
    // renyokei (間続い ← 続く). Common tail adjectives are open-class and
    // rule-derived, so a dictionary hit alone is too narrow: accept the tail
    // by rule when it is not a dictionary noun/verb form itself (勢い, 洗い,
    // 違い are nominalizations, not adjectives), inflection recognizes
    // kanji+い as an i-adjective, and the compound's い is not a verb-onbin
    // surface (間続いた, 分置いて).
    char32_t head_char = codepoints[start_pos];
    if (normalize::isDurationSuffixKanji(head_char)) {
      std::string tail_adj = extractSubstring(codepoints, start_pos + 1, kanji_end) + "い";
      bool tail_is_dict_adj = isAdjectiveInDictionary(dict_manager, tail_adj);
      bool tail_is_i_adj = tail_is_dict_adj;
      float tail_adj_confidence = candidate::kNoOriginConfidence;
      if (!tail_is_i_adj && !(first_hira == U'い' && isVerbOnbinContextAfterI(codepoints, kanji_end + 1)) &&
          !verb_helpers::isNounInDictionary(dict_manager, tail_adj) &&
          !verb_helpers::hasDictionaryEntry(dict_manager, tail_adj, core::PartOfSpeech::Verb)) {
        for (const auto& tail_res : inflection.analyze(tail_adj)) {
          if (tail_res.verb_type == grammar::VerbType::IAdjective &&
              tail_res.confidence >= candidate::kCompoundAdjConfMin) {
            tail_is_i_adj = true;
            tail_adj_confidence = std::max(tail_adj_confidence, tail_res.confidence);
          }
        }
      }
      // For a rule-derived tail, reject the compound reading when the same
      // second-kanji + okurigana span has stronger evidence as a terminal Godan
      // verb (中+働く). Dictionary-backed adjectives such as 間近い/分厚い are
      // authoritative and bypass this ambiguity check.
      if (tail_is_i_adj && !tail_is_dict_adj) {
        std::string tail_surface = extractSubstring(codepoints, start_pos + 1, kanji_end + 1);
        for (const auto& tail_res : inflection.analyze(tail_surface)) {
          if (grammar::isGodanVerbType(tail_res.verb_type) && tail_res.base_form == tail_surface &&
              tail_res.confidence > tail_adj_confidence) {
            tail_is_i_adj = false;
            break;
          }
        }
      }
      if (!tail_is_i_adj) {
        SUZUME_DEBUG_LOG_VERBOSE("[ADJ_SKIP] duration-suffix head \"" << head_char << "\" not an i-adj compound\n");
        goto skip_compound_adj;
      }
    }
    {
      // For し: must be followed by い/く/け/か (しい-adj conjugation),
      // NOT そ/な/て/た (suru verb + auxiliary)
      bool valid_adj_start = (first_hira == U'い' || first_hira == U'く' || first_hira == U'け' || first_hira == U'か');
      if (first_hira == U'し' && kanji_end + 1 < codepoints.size()) {
        char32_t second_hira = codepoints[kanji_end + 1];
        valid_adj_start =
            (second_hira == U'い' || second_hira == U'く' || second_hira == U'け' || second_hira == U'か');
      }
      if (valid_adj_start) {
        constexpr size_t kMaxHiraganaLen = 5;
        // Mark existing candidates with has_suffix if they fit the compound pattern
        for (auto& cand : candidates) {
          size_t hira_len = cand.end - kanji_end;
          if (hira_len <= kMaxHiraganaLen) {
            cand.has_suffix = true;
          }
        }
        // Generate new compound candidate if main loop didn't produce one.
        // The 2-kanji penalty drops inflection confidence below the main loop's
        // 0.5 threshold for compound adjectives like 薄暗い, 物悲しく.
        // Use tighter hiragana limits for い/く/か/け (max 2) to prevent
        if (candidates.empty()) {
          size_t hira_limit = (first_hira == U'し') ? kMaxHiraganaLen : 2;
          size_t max_end = std::min(hiragana_end, kanji_end + hira_limit);
          for (size_t end_pos = max_end; end_pos > kanji_end; --end_pos) {
            std::string surface = extractSubstring(codepoints, start_pos, end_pos);
            if (surface.empty())
              continue;
            // The 副助詞 しか is not an adjective conjugation: a genuine しい-
            // adjective past keeps っ right after しか (美味しかっ + た), while
            // noun + しか(…ない) never has the っ. Skip surfaces whose hiragana
            // portion opens with an adverbial particle not followed by っ.
            // @see fabricated closed-class absorption guards (verb_candidates_helpers.h)
            if (end_pos >= kanji_end + 2 && dict_manager != nullptr) {
              std::string leading_hira = extractSubstring(codepoints, kanji_end, kanji_end + 2);
              const dictionary::DictionaryEntry* particle_entry = dict_manager->lookupExact(leading_hira);
              if (particle_entry != nullptr && particle_entry->extended_pos == core::ExtendedPOS::ParticleAdverbial &&
                  (end_pos == kanji_end + 2 || codepoints[kanji_end + 2] != U'っ')) {
                SUZUME_DEBUG_LOG_VERBOSE("[ADJ_SKIP] \"" << surface << "\" hiragana head is adverbial particle\n");
                continue;
              }
            }
            const auto& all_cands = inflection.analyze(surface);
            for (const auto& ic : all_cands) {
              if (ic.confidence >= candidate::kCompoundAdjConfMin && ic.verb_type == grammar::VerbType::IAdjective) {
                float cost = candidate::confidenceScaledCost(candidate::kCompoundAdjBaseCost, ic.confidence,
                                                             candidate::kKanjiAdjConfScale);
                SUZUME_DEBUG_LOG_VERBOSE("[ADJ_COMPOUND] \"" << surface << "\" cost=" << cost
                                                             << " conf=" << ic.confidence << "\n");
                auto adj_cand = makeIAdjCandidate(surface, start_pos, end_pos, ic.base_form, cost,
                                                  CandidateOrigin::AdjectiveI, ic.confidence, "i_adjective_compound");
                adj_cand.has_suffix = true;
                candidates.push_back(std::move(adj_cand));
                goto compound_adj_done;
              }
            }
          }
        compound_adj_done:;
        }
      }
    }
  skip_compound_adj:;
  }

  // Add emphatic variants (すごい → すごいっっ, etc.)
  addEmphaticVariants(candidates, codepoints);

  // Add ku-form candidates for kunai/kunakatta patterns (negative split)
  // Preserve the adjective renyokei and negative auxiliary boundary:
  //   良くない → 良く + ない
  //   良くなかった → 良く + なかっ + た
  // For each candidate ending with くない/くなかった/くなかっ, generate ku-form variant
  std::vector<UnknownCandidate> ku_neg_candidates;
  for (const auto& cand : candidates) {
    // Check if surface ends with くない (negative form)
    if (utf8::endsWith(cand.surface, "くない")) {
      // Generate ku-form variant: 良くない → 良く
      ku_neg_candidates.push_back(makeTrimmedAdjVariant(cand, 2, candidate::kAdjKuSplitBonus,
                                                        core::ExtendedPOS::AdjRenyokei, "i_adjective_ku_nai"));
    }
    // Check if surface ends with くなかった (negative past full form)
    else if (utf8::endsWith(cand.surface, "くなかった")) {
      // Generate ku-form variant: 良くなかった → 良く
      ku_neg_candidates.push_back(makeTrimmedAdjVariant(cand, 4, candidate::kAdjKuSplitBonus,
                                                        core::ExtendedPOS::AdjRenyokei, "i_adjective_ku_nakatta"));
    }
    // Check if surface ends with くなかっ (negative past before た)
    else if (utf8::endsWith(cand.surface, "くなかっ")) {
      // Generate ku-form variant: 良くなかっ → 良く
      ku_neg_candidates.push_back(makeTrimmedAdjVariant(cand, 3, candidate::kAdjKuSplitBonus,
                                                        core::ExtendedPOS::AdjRenyokei, "i_adjective_ku_nakatt"));
    }
  }

  // Add all ku-negative-form candidates
  for (auto& var : ku_neg_candidates) {
    candidates.push_back(std::move(var));
  }

  // Add ku-form candidates for kute patterns (te-form split)
  // Preserve the adjective renyokei and conjunctive particle boundary:
  //   ウザくて → ウザく + て
  //   美しくて → 美しく + て
  // For each candidate ending with くて, generate ku-form variant
  std::vector<UnknownCandidate> ku_te_candidates;
  for (const auto& cand : candidates) {
    // Check if surface ends with くて (te-form)
    if (utf8::endsWith(cand.surface, "くて")) {
      // Generate ku-form variant: ウザくて → ウザく
      ku_te_candidates.push_back(makeTrimmedAdjVariant(cand, 1, candidate::kAdjKuSplitBonus,
                                                       core::ExtendedPOS::AdjRenyokei, "i_adjective_ku_te"));
    }
  }
  // Add all ku-te-form candidates
  for (auto& var : ku_te_candidates) {
    candidates.push_back(std::move(var));
  }

  // Add katt-form candidates for katta patterns (BUG-036)
  // Preserve adjective stem, tense auxiliary, and polite copula boundaries.
  // For each candidate ending with かった, generate a katt-form variant ending with かっ
  std::vector<UnknownCandidate> katt_form_candidates;
  for (const auto& cand : candidates) {
    // Check if surface ends with かった (i-adjective past form)
    if (utf8::endsWith(cand.surface, "かった")) {
      // Generate katt-form variant: 美しかった → 美しかっ (連用タ接続; AdjKatt→AuxTenseTa)
      katt_form_candidates.push_back(makeTrimmedAdjVariant(cand, 1, candidate::kAdjKattSplitBonus,
                                                           core::ExtendedPOS::AdjKatt, "i_adjective_katt"));
    }
  }

  // Add all katt-form candidates
  for (auto& var : katt_form_candidates) {
    candidates.push_back(std::move(var));
  }

  // The past た is always a separate auxiliary: an i-adjective past never stands
  // as one かった token (難しかっ|た, 良くなかっ|た). Every span ending in かった
  // produced its trimmed かっ variant above, so drop the merged span itself —
  // it only ever wins over the split when a preceding modifier's connection
  // bonus favors the terminal-form EPOS, which is exactly the wrong parse.
  candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                  [](const UnknownCandidate& cand) { return utf8::endsWith(cand.surface, "かった"); }),
                   candidates.end());

  // Add ke-form candidates for kereba patterns
  // Preserve the adjective conditional stem and conjunctive particle boundary.
  // For each candidate ending with ければ, generate a ke-form variant ending with けれ
  std::vector<UnknownCandidate> ke_form_candidates;
  for (const auto& cand : candidates) {
    // Check if surface ends with ければ (i-adjective conditional form)
    if (utf8::endsWith(cand.surface, "ければ")) {
      // Generate ke-form variant: 美しければ → 美しけれ (仮定形; AdjKeForm→ParticleConj)
      auto ke_cand =
          makeTrimmedAdjVariant(cand, 1, candidate::kAdjKeSplitBonus, core::ExtendedPOS::AdjKeForm, "i_adjective_kere");
      // Disambiguate 〜ければ against the homographic ichidan verb 仮定形
      // (高ければ=高い vs 受ければ=受ける). For an all-kanji stem, inflection alone
      // produces a plausible fake ichidan (高ける). When the i-adjective base is a
      // known dictionary adjective, this is decisively the adjective 仮定形, so make
      // the ke-form win over the fake ichidan renyokei/kateikei verb candidates.
      if (dict_manager != nullptr && isAdjectiveInDictionary(dict_manager, cand.lemma)) {
        ke_cand.cost = candidate::verb_cost::kStrongBonus;  // -0.8, beats fake verb paths
      }
      ke_form_candidates.push_back(ke_cand);
    }
  }

  // Add all ke-form candidates
  for (auto& var : ke_form_candidates) {
    candidates.push_back(std::move(var));
  }

  // Add mizenkei (かろ) candidates for the conjectural pattern: stem + かろ + う
  // (高かろう, 美しかろう). Shared with the pure-hiragana generator.
  appendIAdjKaroCandidates(codepoints, start_pos, kanji_end, hiragana_end, inflection, dict_manager, candidates);

  // Add classical attributive (文語連体形) き candidates: stem + き + 体言
  // I-adjective 連体形 in classical Japanese: 美しい → 美しき(花), 古い → 古き(良き時代)
  // Inflection analysis does not produce this form, and the surface Xき is
  // homographic with godan-ka verb 連用形 (書き ← 書く), so generate only when
  // the lexical signal is decisive: the reconstructed base (stem + い) is a
  // known dictionary adjective. The lemma normalizes to the modern base form.
  if (dict_manager != nullptr) {
    for (size_t ki_pos = kanji_end; ki_pos < hiragana_end; ++ki_pos) {
      if (codepoints[ki_pos] != U'き') {
        continue;
      }
      std::string ki_stem = extractSubstring(codepoints, start_pos, ki_pos);
      std::string ki_lemma = ki_stem + "い";
      if (!isAdjectiveInDictionary(dict_manager, ki_lemma)) {
        continue;
      }
      // If stem + く is a real godan-ka verb, Xき is its 連用形 (行き, 焼き),
      // not the classical adjective form — leave it to the verb paths.
      if (isVerbInDictionary(dict_manager, ki_stem + "く")) {
        continue;
      }
      // If the surface itself is a dictionary entry (好き, 大好き), the
      // dictionary interpretation wins — do not shadow it.
      std::string ki_surface = extractSubstring(codepoints, start_pos, ki_pos + 1);
      if (verb_helpers::hasNonVerbDictionaryEntry(dict_manager, ki_surface) ||
          isVerbInDictionary(dict_manager, ki_surface)) {
        continue;
      }
      UnknownCandidate ki_cand;
      ki_cand.surface = ki_surface;
      ki_cand.start = start_pos;
      ki_cand.end = ki_pos + 1;
      ki_cand.pos = core::PartOfSpeech::Adjective;
      ki_cand.lemma = ki_lemma;
      // Dictionary-verified adjective: make the 連体形 win over fake verb
      // interpretations (godan-ka 美しく etc.), mirroring the ke-form handling.
      ki_cand.cost = candidate::verb_cost::kStrongBonus;
      ki_cand.has_suffix = true;  // Conjugated form (連体形)
      // Attributive form connects like the basic form (ADJ + 体言)
      ki_cand.extended_pos = core::ExtendedPOS::AdjBasic;
#ifdef SUZUME_DEBUG_INFO
      ki_cand.origin = CandidateOrigin::AdjectiveI;
      ki_cand.confidence = 0.8F;
      ki_cand.pattern = "i_adjective_classical_ki";
#endif
      candidates.push_back(std::move(ki_cand));
    }
  }

  verb_helpers::sortCandidatesByCost(candidates);

  return candidates;
}

std::vector<UnknownCandidate> generateNaAdjectiveCandidates(const std::vector<char32_t>& codepoints, size_t start_pos,
                                                            const std::vector<normalize::CharType>& char_types,
                                                            const UnknownOptions& /*options*/) {
  std::vector<UnknownCandidate> candidates;

  if (start_pos >= char_types.size() || char_types[start_pos] != normalize::CharType::Kanji) {
    return candidates;
  }

  // Find kanji sequence (max 3 chars for na-adjectives: 獰猛, 不器用)
  constexpr size_t kMaxNaAdjKanjiLength = 3;
  size_t kanji_end = findCharRegionEnd(char_types, start_pos, kMaxNaAdjKanjiLength, normalize::CharType::Kanji);

  size_t kanji_len = kanji_end - start_pos;

  // Pattern 0: Kanji(1) + やか/らか/か + な patterns (e.g., 華やかな, 豊かな, 静かな)
  // These are common na-adjective patterns with kanji stem + hiragana suffix
  if (kanji_len == 1 && kanji_end < char_types.size() && char_types[kanji_end] == normalize::CharType::Hiragana) {
    // Check for やか/らか/か patterns
    size_t hira_end = findCharRegionEnd(char_types, kanji_end, 4, normalize::CharType::Hiragana);

    std::string full_surface = extractSubstring(codepoints, start_pos, hira_end);
    size_t hira_len = hira_end - kanji_end;

    // Check if ends with な (na-adjective conjugation)
    bool ends_with_na = (hira_end > kanji_end && codepoints[hira_end - 1] == U'な');

    if (ends_with_na && hira_len >= 2) {
      // Extract stem (without な)
      std::string stem = extractSubstring(codepoints, start_pos, hira_end - 1);
      size_t stem_hira_len = hira_len - 1;

      // Check for やか/らか patterns (productive OOV derivation).
      // Bare single-か na-adjectives (静か/豊か/厳か等) are a closed lexical class served
      // by L2, NOT emitted here: the bare-か branch also fired on the 終助詞 かな after a
      // noun (東京かな → 東|京か|な, 犬かな → 犬|か|な broke likewise).
      bool is_yaka_pattern = false;
      if (stem_hira_len >= 2) {
        std::string stem_suffix = extractSubstring(codepoints, kanji_end, hira_end - 1);
        is_yaka_pattern = utf8::equalsAny(stem_suffix, {"やか", "らか"});
      }

      if (is_yaka_pattern) {
        // Stem without な, low cost for common pattern
        candidates.push_back(makeNaAdjCandidate(stem, start_pos, hira_end - 1, candidate::kNaAdjYakaCost, true, 0.9F,
                                                "na_adj_yaka_raka"));
        return candidates;  // Return early for clear pattern match
      }
    }
  }

  // Need at least 2 kanji for other patterns
  if (kanji_len < 2) {
    return candidates;
  }

  std::string kanji_seq = extractSubstring(codepoints, start_pos, kanji_end);

  // Pattern 1: Check for na-adjective suffixes (的)
  // NOTE: MeCab splits 論理的な as 論理+的+な, not 論理的+な
  // So we generate this candidate with higher cost to allow NOUN+SUFFIX path to win
  // The candidate is still useful for cases where no split path exists
  for (const auto& suffix : kNaAdjSuffixes) {
    // Check if kanji_seq ends with suffix
    if (kanji_seq.size() >= suffix.size()) {
      std::string_view kanji_suffix(kanji_seq.data() + kanji_seq.size() - suffix.size(), suffix.size());
      if (kanji_suffix == suffix) {
        // Found a na-adjective pattern like 理性的, 論理的
        // Higher cost favors the compositional NOUN + 的(SUFFIX) + な path.
        candidates.push_back(makeNaAdjCandidate(kanji_seq, start_pos, kanji_end, candidate::kNaAdjTekiCost, true, 1.0F,
                                                "na_adjective_teki"));
        break;  // Use first matching suffix
      }
    }
  }

  // Pattern 2: Check for kanji compound + な pattern (e.g., 獰猛な)
  // A direct appearance そう also licenses a na-adjective stem when the
  // open-class lexeme is absent from the dictionary.
  // A bare な licenses an attributive na-adjective stem, but なら does not:
  // nouns and na-adjectives both take conditional なら, so generating an
  // adjective for every unknown kanji compound would destroy that ambiguity.
  const bool followed_by_na = kanji_end < codepoints.size() && codepoints[kanji_end] == U'な' &&
                              (kanji_end + 1 >= codepoints.size() || codepoints[kanji_end + 1] != U'ら');
  const bool followed_by_sou =
      kanji_end + 1 < codepoints.size() && codepoints[kanji_end] == U'そ' && codepoints[kanji_end + 1] == U'う';
  if (followed_by_na || followed_by_sou) {
    // Skip if first character is a formal noun (形式名詞)
    // e.g., 時妙な should be 時+妙な, not 時妙(ADJ)+な
    // Formal nouns (時, 事, 所, etc.) are standalone grammatical words
    std::string first_char_str;
    normalize::encodeUtf8(codepoints[start_pos], first_char_str);
    if (normalize::isFormalNounSurface(first_char_str)) {
      // Don't generate na-adjective candidate for formal noun + kanji patterns
      return candidates;
    }

    // Skip if kanji ends with 的 - MeCab splits as NOUN + 的(SUFFIX) + な
    // e.g., 論理的な should be 論理+的+な, not 論理的+な
    char32_t last_kanji = codepoints[kanji_end - 1];
    if (last_kanji == U'的') {
      return candidates;
    }

    // Skip if な is followed by く/い/か — these indicate ない (auxiliary/adjective)
    // attached to the preceding noun, not a な-adjective stem.
    // Examples:
    //   私心なく → 私心 + ない連用 (not 私心(ADJ_NA) + く)
    //   仕方ない → 仕方 + ない (not 仕方(ADJ_NA) + い)
    //   関係なかった → 関係 + なかっ (か triggers naかった past form)
    // Real な-adjectives followed by these forms (静かなく) are not standard Japanese.
    if (followed_by_na && kanji_end + 1 < codepoints.size()) {
      char32_t after_na = codepoints[kanji_end + 1];
      if (after_na == U'く' || after_na == U'い' || after_na == U'か') {
        return candidates;
      }
    }

    // Found kanji compound + な - potential na-adjective stem
    // Cost similar to dictionary na-adjectives but with small penalty for unknown
    candidates.push_back(makeNaAdjCandidate(kanji_seq, start_pos, kanji_end, candidate::kNaAdjStemCost, true, 0.8F,
                                            "na_adjective_stem"));
  }

  return candidates;
}

}  // namespace suzume::analysis
