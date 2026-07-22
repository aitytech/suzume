/**
 * @file verb_candidates_hiragana_derived.cpp
 * @brief Derived pure-hiragana verb candidate families
 */

#include <algorithm>
#include <cmath>

#include "analysis/bigram_table.h"
#include "analysis/candidate_constants.h"
#include "analysis/scorer_constants.h"
#include "analysis/verb_candidates_helpers.h"
#include "analysis/verb_candidates_hiragana_internal.h"
#include "core/debug.h"
#include "core/kana_constants.h"
#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "grammar/conjugation.h"
#include "normalize/char_type.h"
#include "normalize/exceptions.h"
#include "normalize/utf8.h"
#include "suffix_candidates.h"
#include "unknown.h"
#include "verb_candidates.h"

namespace suzume::analysis::hiragana_verb_detail {
namespace vh = verb_helpers;

// A dictionary hit alone is insufficient for an euphonic Godan candidate:
// the reconstructed base must also have the Godan class that licenses the
// observed onbin. This excludes non-Godan homographs such as する from a
// fabricated すっ+て path.
bool hasMatchingGodanInflection(const grammar::Inflection& inflection, std::string_view base_form,
                                grammar::VerbType expected_type) {
  for (const auto& analysis : inflection.analyze(base_form)) {
    if (analysis.base_form == base_form && analysis.verb_type == expected_type) {
      return true;
    }
  }
  return false;
}

void appendHiraganaDerivedCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t hiragana_end,
                                     const std::vector<normalize::CharType>& char_types,
                                     const grammar::Inflection& inflection,
                                     const dictionary::DictionaryManager* dict_manager,
                                     std::vector<UnknownCandidate>& candidates) {
  // Generate Godan mizenkei stem candidates for hiragana passive patterns
  // E.g., いわれる → いわ (mizenkei of いう) + れる (passive AUX)
  appendPassiveMizenkeiCandidates(codepoints, start_pos, hiragana_end, inflection, dict_manager, candidates);

  // Generate Ichidan verb stem candidates for hiragana られる pattern
  // E.g., いられる → い (renyokei of いる) + られる (potential/passive AUX)
  appendIchidanRareruCandidates(codepoints, start_pos, hiragana_end, inflection, dict_manager, candidates);

  // Generate Godan mizenkei stem candidates for contracted negative ん pattern
  // E.g., くだらん → くだら (mizenkei of くだる) + ん (contracted negative)
  appendMizenkeiNCandidates(codepoints, start_pos, hiragana_end, dict_manager, candidates);

  // Generate Godan mizenkei stem candidates for negative auxiliary ない pattern
  // E.g., わからない → わから (mizenkei of わかる) + ない (negative auxiliary)
  appendMizenkeiNaiCandidates(codepoints, start_pos, hiragana_end, inflection, dict_manager, candidates);

  // Generate Godan mizenkei stem candidates before なきゃ/なければ contraction
  // E.g., やらなきゃ → やら (mizenkei of やる) + なきゃ (contraction of なければ)
  appendMizenkeiNakyaCandidates(codepoints, start_pos, hiragana_end, inflection, dict_manager, candidates);

  // Generate Godan-ra ん音便 stem candidates for colloquial ん+ない pattern
  // E.g., たまんない → たまん (ん音便 of たまる) + ない (negative auxiliary)
  appendNOnbinNaiCandidates(codepoints, start_pos, hiragana_end, inflection, dict_manager, candidates);

  // Generate Godan onbin stem candidates for contraction auxiliary patterns
  // E.g., やっとく → やっ (onbin of やる) + とく (ておく contraction), 読んでる → 読ん + でる
  appendOnbinContractionCandidates(codepoints, start_pos, hiragana_end, inflection, dict_manager, candidates);

  // Generate 1-char ichidan renyokei stem candidates
  // E.g., ねて → ね (renyokei of ねる) + て (particle)
  appendIchidanRenyokei1CharCandidates(codepoints, start_pos, dict_manager, candidates);

  // Generate 2+ char ichidan renyokei stem candidates
  // E.g., つけて → つけ (renyokei of つける) + て (particle)
  //       たべて → たべ (renyokei of たべる) + て (particle)
  //       あけて → あけ (renyokei of あける) + て (particle)
  //       すぎて → すぎ (renyokei of すぎる) + て (particle)
  // MeCab splits: つけて → つけ(動詞,一段,連用形) + て(助詞,接続助詞)
  // Pattern: 2+ char sequence ending with e-row or i-row hiragana followed by て or た
  // Note: Ichidan verbs have both e-row stems (食べる) and i-row stems (感じる, 過ぎる)
  // Uses inflection analysis confidence to validate (dictionary lookup as bonus)
  //
  // A run that starts with で right after a hatsuonbin ん is the voiced te-form
  // particle of the preceding verb (読ん+で, 飲ん+で), so its でき is
  // で(particle)+き(くる), never the renyokei of できる. Only the ます-family
  // licenser is suppressed for this shape (see is_followed_by_masu below) so
  // 読んできました stays 読ん+で+き+まし+た. The plain て/た path is deliberately
  // left intact — 取り組んできた already resolves to …+で+き+た on its own, and
  // blocking the reading there would instead flip it to でき+た.
  const bool leading_de_after_hatsuonbin =
      codepoints[start_pos] == U'で' && start_pos > 0 && codepoints[start_pos - 1] == U'ん';
  for (size_t end_pos = start_pos + 2; end_pos < hiragana_end; ++end_pos) {
    // Check if position end_pos-1 is e-row or i-row hiragana (ichidan renyokei ending)
    // E-row: 食べる, 見える → 食べ, 見え
    // I-row: 感じる, 過ぎる → 感じ, すぎ
    char32_t stem_end_char = codepoints[end_pos - 1];
    if (!grammar::isERowCodepoint(stem_end_char) && !grammar::isIRowCodepoint(stem_end_char)) {
      continue;
    }

    // Exclude て and で which are more commonly particles
    if (stem_end_char == U'て' || stem_end_char == U'で') {
      continue;
    }

    // Check if followed by te/ta particle, polite ます auxiliary, or conditional れば
    if (end_pos >= codepoints.size()) {
      continue;
    }
    char32_t next_char = codepoints[end_pos];
    bool is_followed_by_te_ta = (next_char == U'て' || next_char == U'た');
    bool is_followed_by_renyokei_conj = next_char == U'な' && end_pos + 2 < codepoints.size() &&
                                        codepoints[end_pos + 1] == U'が' && codepoints[end_pos + 2] == U'ら';
    if (!is_followed_by_renyokei_conj) {
      is_followed_by_renyokei_conj =
          next_char == U'つ' && end_pos + 1 < codepoints.size() && codepoints[end_pos + 1] == U'つ';
    }
    bool is_followed_by_reba = false;
    // Check for a polite ます-family auxiliary (ます / まし / ませ). All three
    // attach only to a verb renyokei, so they license the renyokei reading of a
    // renyokei/aux homograph (つかい→使う, かい→買う before ました/ません). The
    // ん+で te-form shape is excluded so でき before まし is not mis-read as the
    // renyokei of できる (読んできました → 読ん+で+き+まし+た).
    bool is_followed_by_masu = vh::masuAuxFollowsAt(codepoints, end_pos) && !leading_de_after_hatsuonbin;
    // Check for conditional れば pattern (e.g., できれば → でき + れ + ば)
    // This case is handled separately below for kateikei stem generation
    if (next_char == U'れ' && end_pos + 1 < codepoints.size() && codepoints[end_pos + 1] == U'ば') {
      is_followed_by_reba = true;
    }
    // Check for negative ない pattern (e.g., できない → でき + ない)
    bool is_followed_by_nai = false;
    if (next_char == U'な' && end_pos + 1 < codepoints.size() && codepoints[end_pos + 1] == U'い') {
      is_followed_by_nai = true;
    }
    if (!is_followed_by_te_ta && !is_followed_by_masu && !is_followed_by_renyokei_conj && !is_followed_by_reba &&
        !is_followed_by_nai) {
      continue;
    }

    // Construct stem and base form. Small-kana starts (っぱいし, ゃい, …) are
    // already rejected by the function-entry guard.
    std::string stem_surface = extractSubstring(codepoints, start_pos, end_pos);
    std::string base_form = stem_surface + "る";

    // Use inflection analysis to validate - check if stem is recognized as ichidan
    const auto& stem_analysis = inflection.analyze(stem_surface);
    bool found_ichidan = false;
    float ichidan_confidence = 0.0F;
    for (const auto& cand : stem_analysis) {
      if (cand.verb_type == grammar::VerbType::Ichidan && cand.base_form == base_form) {
        found_ichidan = true;
        ichidan_confidence = cand.confidence;
        break;
      }
    }

    // Skip if not recognized as ichidan stem by inflection analysis
    // Threshold 0.3 catches most valid cases while filtering noise
    if (!found_ichidan || ichidan_confidence < 0.3F) {
      continue;
    }

    // Default to the ichidan interpretation (stem + る). For the +ます follower a
    // godan renyokei reading is equally licensed (泳ぎ→泳ぐ, 踊り→踊る), so prefer it
    // when at least as confident. Godan-sa is the sole row whose ordinary
    // continuative also attaches directly to て/た (話し+た); preserve that
    // analysis instead of fabricating 話しる. Other godan rows require onbin or
    // an a-row mizenkei in the corresponding contexts.
    std::string chosen_base = base_form;
    dictionary::ConjugationType chosen_conj = dictionary::ConjugationType::Ichidan;
    float chosen_confidence = ichidan_confidence;
    const bool godan_sa_before_te_ta = is_followed_by_te_ta && stem_end_char == U'し';
    if (is_followed_by_masu || is_followed_by_renyokei_conj || godan_sa_before_te_ta) {
      grammar::VerbType godan_type = grammar::verbTypeFromIRowCodepoint(stem_end_char);
      if (godan_type != grammar::VerbType::Unknown) {
        std::string godan_base = extractSubstring(codepoints, start_pos, end_pos - 1) +
                                 std::string(grammar::godanBaseSuffixFromIRow(stem_end_char));
        for (const auto& cand : stem_analysis) {
          if (cand.verb_type == godan_type && cand.base_form == godan_base && cand.confidence >= chosen_confidence) {
            chosen_base = godan_base;
            chosen_conj = grammar::verbTypeToConjType(godan_type);
            chosen_confidence = cand.confidence;
            break;
          }
        }
      }
    }

    // Check if base form is in dictionary (gives confidence boost)
    bool is_dict_verb = vh::isVerbInDictionary(dict_manager, chosen_base);

    // Skip causative+passive auxiliary chain patterns
    // E.g., "せられ" should be split as せ(causative) + られ(passive), not single verb
    // Preserve the causative, passive, and tense morpheme boundaries.
    if (utf8::endsWith(stem_surface, "せられ")) {
      continue;
    }

    // Skip stems ending in なけ - this is the negative auxiliary ない kateikei (なけれ),
    // not an ichidan verb なける. Prevents a false single-verb reading for
    // mizenkei + なければ: やらなければ must split as やら + なけれ(ない) + ば,
    // never become a fabricated ichidan やらなける.
    if (utf8::endsWith(stem_surface, "なけ")) {
      continue;
    }

    // Skip stems ending in し where the prefix is a dictionary noun (サ変 pattern)
    // E.g., しっぱいし → しっぱい(dict NOUN) + し(する連用), not しっぱいしる
    // This prevents false ichidan candidates from サ変 noun + する patterns
    if (!is_dict_verb && utf8::endsWith(stem_surface, "し") && stem_surface.size() > 3) {  // More than just し
      std::string prefix = stem_surface.substr(0, stem_surface.size() - 3);
      if (vh::hasNonVerbDictionaryEntry(dict_manager, prefix)) {
        continue;
      }
      // Pure hiragana stems with sokuon ending in し are almost always
      // false サ変 patterns (noun+する where noun contains っ)
      if (stem_surface.find("っ") != std::string::npos) {
        continue;
      }
    }

    // Skip て+subsidiary verb patterns that should be split
    // E.g., "してくれ" should be し + て + くれ, not single verb
    //       "してもら" should be し + て + もら, not single verb
    // These patterns contain て-form (して) followed by subsidiary verb stem
    if (stem_surface.find("てくれ") != std::string::npos || stem_surface.find("てもら") != std::string::npos ||
        stem_surface.find("てあげ") != std::string::npos) {
      continue;
    }

    // Skip te-form + subsidiary みる spans: an internal て/で followed by み is
    // always [verb te-form] + みる (やってみ = やっ + て + み, われてみ =
    // われ + て + み), never a single ichidan verb やってみる. This also
    // suppresses the kateikei variant below (やってみれ from やってみれば).
    // @see fabricated closed-class absorption guards (verb_candidates_helpers.h)
    if (!is_dict_verb && vh::embedsTeFormMiruAuxiliary(codepoints, start_pos, end_pos)) {
      continue;
    }

    // Skip a fabricated verb that spans an auxiliary prefix + auxiliary tail:
    // でござい → で(AuxCopulaDa) + ござい(AuxGozaru). MeCab always keeps a
    // closed-class auxiliary chain split, so an open-class verb whose leading
    // codepoint is itself an AUX and whose remainder after that codepoint is
    // exactly an AUX is re-merging what must stay apart. Both halves must be
    // dictionary auxiliaries: the remainder condition alone would wrongly skip
    // real verbs like しまう (し is a particle, not an AUX → しまい stays), and
    // the 2-codepoint floor on the remainder protects genuine short stems かい
    // (い = いる 連用形) and でき (き = くる 連用形).
    if (!is_dict_verb && dict_manager != nullptr && end_pos - start_pos >= 3) {
      std::string aux_first = extractSubstring(codepoints, start_pos, start_pos + 1);
      std::string aux_remainder = extractSubstring(codepoints, start_pos + 1, end_pos);
      if (dict_manager->lookupExact(aux_first, core::PartOfSpeech::Auxiliary) != nullptr &&
          vh::hasDictionaryEntry(dict_manager, aux_remainder, core::PartOfSpeech::Auxiliary)) {
        continue;
      }
    }

    // Strong negative cost to beat NOUN + て(VERB from てる) split
    // Dictionary-verified verbs get stronger bonus
    // Non-dictionary verbs get moderate positive cost to avoid spurious candidates
    // competing with dictionary compound particles like について
    // But not too high to break valid patterns like してほしい
    float cost = is_dict_verb ? -0.8F : 0.5F;
    // A godan-wa renyokei starting か…い immediately after a pronoun (誰かい, なにかい)
    // is spurious: the か is the particle か and い is いる's renyokei
    // (誰か + い + ます). Discourage it so the particle reading wins.
    if (codepoints[start_pos] == U'か' && pronounEndsAt(dict_manager, codepoints, start_pos)) {
      cost += bigram_cost::kStrong;
    }
    SUZUME_DEBUG_VERBOSE_BLOCK {
      SUZUME_DEBUG_STREAM << "[VERB_CAND] " << stem_surface << " hiragana_renyokei lemma=" << chosen_base
                          << " conf=" << chosen_confidence << (is_dict_verb ? " [dict]" : "") << " cost=" << cost
                          << "\n";
    }
    const std::string following = extractSubstring(codepoints, end_pos, std::min(end_pos + 2, codepoints.size()));
    const bool is_negative_continuation = utf8::startsWith(following, "ない") || utf8::startsWith(following, "なか");
    // A stem after a clear te/de boundary belongs to a subsidiary-verb
    // construction.  Leave that category to its dedicated candidate so an
    // otherwise valid Ichidan reconstruction cannot turn 〜てやらない into a
    // lexical predicate.  Outside that boundary, the negative confirms that
    // the ambiguous Ichidan stem is mizenkei (さけ+ない, かけ+ない).
    const bool is_lexical_negative_continuation =
        is_negative_continuation && !isClearTeFormBeforeSubsidiary(codepoints, start_pos, true);
    // A following て/た validates the inflectional shape, but it does not prove
    // a word boundary when the candidate begins immediately after kanji. In
    // that position the hiragana can instead be the okurigana tail of a
    // kanji-starting predicate. Keep the ordinary candidate, but reserve the
    // context-validated origin (and its strong auxiliary-connection evidence)
    // for starts that are not inside that mixed-script predicate shape.
    const bool has_kanji_immediately_before = start_pos > 0 && normalize::isKanjiCodepoint(codepoints[start_pos - 1]);
    const core::CandidateOrigin origin =
        is_lexical_negative_continuation
            ? CandidateOrigin::VerbHiraganaNegativeRenyokei
            : (is_followed_by_te_ta && !has_kanji_immediately_before ? CandidateOrigin::VerbHiraganaInflectedRenyokei
                                                                     : CandidateOrigin::VerbHiragana);
    // Ichidan stems share their surface in renyokei and mizenkei. A following
    // negative auxiliary determines the latter, which must receive the normal
    // VerbMizenkei → AuxNegativeNai connection instead of competing as a
    // continuative verb (さけ+ない, かけ+ない).
    const core::ExtendedPOS extended_pos =
        is_lexical_negative_continuation ? core::ExtendedPOS::VerbMizenkei : core::ExtendedPOS::VerbRenyokei;
    candidates.push_back(makeVerbCandidate(stem_surface, start_pos, end_pos, cost, chosen_base, chosen_conj, true,
                                           origin, chosen_confidence, "hiragana_renyokei", extended_pos));

    // Also generate kateikei stem if followed by れば
    // E.g., できれば → できれ (kateikei of できる) + ば
    // MeCab splits: できれば → できれ(動詞,仮定形) + ば(接続助詞)
    // Skip suru-verb negative patterns: しなけれ should be し + なけれ, not single verb
    // Pattern: し + な (negative stem prefix)
    // stem_surface = しなけ → base_form = しなける (false ichidan)
    bool is_suru_negative_pattern = (stem_surface.size() >= 6 &&  // しな = 6 bytes
                                     stem_surface.substr(0, 3) == "し" && stem_surface.substr(3, 3) == "な");
    if (is_followed_by_reba && !is_suru_negative_pattern) {
      std::string kateikei_surface = stem_surface + "れ";  // 連用形 + れ = 仮定形
      size_t kateikei_end = end_pos + 1;                   // renyokei + れ
      constexpr float kKateikeiCost = candidate::verb_cost::kStrongBonus;
      SUZUME_DEBUG_VERBOSE_BLOCK {
        SUZUME_DEBUG_STREAM << "[VERB_CAND] " << kateikei_surface << " hiragana_ichidan_kateikei lemma=" << base_form
                            << " conf=" << ichidan_confidence << " cost=" << kKateikeiCost << "\n";
      }
      candidates.push_back(makeVerbCandidate(kateikei_surface, start_pos, kateikei_end, kKateikeiCost, base_form,
                                             dictionary::ConjugationType::Ichidan, true, CandidateOrigin::VerbHiragana,
                                             ichidan_confidence, "hiragana_ichidan_kateikei",
                                             core::ExtendedPOS::VerbKateikei));
    }
  }

  // Generate Godan sokuonbin (っ) candidates for hiragana verbs
  // E.g., しまった → しまっ (onbin of しまう) + た (auxiliary)
  //       なくなった → なくなっ (onbin of なくなる) + た (auxiliary)
  // This separates the verb's onbin stem from the tense auxiliary.
  {
    // Find the complete hiragana run from start_pos.
    const size_t remaining_chars = char_types.size() - start_pos;
    const size_t hira_extent_end =
        vh::findCharRegionEnd(char_types, start_pos, remaining_chars, normalize::CharType::Hiragana);
    size_t hira_len = hira_extent_end - start_pos;

    // Need at least 3 chars: stem(1+) + っ + た/て
    if (hira_len >= 3) {
      char32_t second_last = codepoints[hira_extent_end - 2];
      char32_t last_char = codepoints[hira_extent_end - 1];
      bool is_sokuonbin_te_ta = (second_last == U'っ' && (last_char == U'た' || last_char == U'て'));
      if (is_sokuonbin_te_ta) {
        // Generate candidate for stem + っ (without the た/て)
        size_t onbin_end = hira_extent_end - 1;  // Position after っ
        std::string onbin_surface = extractSubstring(codepoints, start_pos, onbin_end);
        std::string stem = extractSubstring(codepoints, start_pos, onbin_end - 1);

        auto sokuonbin_match = vh::firstGodanOnbinDictBase(dict_manager, stem, "っ");
        if (sokuonbin_match.matched &&
            (grammar::isSuruBaseForm(sokuonbin_match.base_form) ||
             !hasMatchingGodanInflection(inflection, sokuonbin_match.base_form, sokuonbin_match.verb_type))) {
          sokuonbin_match.matched = false;
        }
        bool found_dict_match = sokuonbin_match.matched;
        if (found_dict_match) {
          constexpr float kHiraganaSokuonbinCost = candidate::verb_cost::kStandardBonus;
          SUZUME_DEBUG_VERBOSE_BLOCK {
            SUZUME_DEBUG_STREAM << "[VERB_CAND] " << onbin_surface
                                << " hiragana_sokuonbin lemma=" << sokuonbin_match.base_form
                                << " type=" << grammar::verbTypeToString(sokuonbin_match.verb_type)
                                << " cost=" << kHiraganaSokuonbinCost << "\n";
          }
          auto sokuonbin_cand = makeVerbCandidate(
              onbin_surface, start_pos, onbin_end, kHiraganaSokuonbinCost, sokuonbin_match.base_form,
              grammar::verbTypeToConjType(sokuonbin_match.verb_type), true, CandidateOrigin::VerbHiragana, 0.9F,
              "hiragana_sokuonbin", core::ExtendedPOS::VerbOnbinkei);
          sokuonbin_cand.lemma_verified = true;
          candidates.push_back(std::move(sokuonbin_cand));
        }
        // The general onbin generator already emits the first inflection-
        // verified Godan row for unregistered hiragana stems.  Do not add a
        // second fallback with a different row preference here: duplicate
        // candidates for the same っ-form otherwise disagree on the lemma.
      }
    }
  }

  // Generate Godan hatsuonbin (ん) candidates for hiragana verbs
  // E.g., こんだ → こん (onbin of こむ) + だ (auxiliary)
  //       こんで → こん (onbin of こむ) + で (particle)
  //       よんだ → よん (onbin of よむ) + だ (auxiliary)
  // This separates the onbin stem of godan-ma/ba/na verbs from the auxiliary.
  {
    // Find the complete hiragana run from start_pos.
    const size_t remaining_chars = char_types.size() - start_pos;
    const size_t hira_extent_end =
        vh::findCharRegionEnd(char_types, start_pos, remaining_chars, normalize::CharType::Hiragana);
    size_t hira_len = hira_extent_end - start_pos;

    // Need at least 3 chars: stem(1+) + ん + だ/で
    if (hira_len >= 3) {
      char32_t second_last = codepoints[hira_extent_end - 2];
      char32_t last_char = codepoints[hira_extent_end - 1];
      bool is_hatsuonbin_de_da = (second_last == U'ん' && (last_char == U'だ' || last_char == U'で'));
      if (is_hatsuonbin_de_da) {
        // Generate candidate for stem + ん (without the だ/で)
        size_t onbin_end = hira_extent_end - 1;  // Position after ん
        std::string onbin_surface = extractSubstring(codepoints, start_pos, onbin_end);
        std::string stem = extractSubstring(codepoints, start_pos, onbin_end - 1);

        auto hatsuonbin_match = vh::firstGodanOnbinDictBase(dict_manager, stem, "ん");
        if (hatsuonbin_match.matched) {
          constexpr float kHiraganaHatsuonbinCost = candidate::verb_cost::kStandardBonus;
          SUZUME_DEBUG_VERBOSE_BLOCK {
            SUZUME_DEBUG_STREAM << "[VERB_CAND] " << onbin_surface
                                << " hiragana_hatsuonbin lemma=" << hatsuonbin_match.base_form
                                << " type=" << grammar::verbTypeToString(hatsuonbin_match.verb_type)
                                << " cost=" << kHiraganaHatsuonbinCost << "\n";
          }
          auto hatsuonbin_cand = makeVerbCandidate(
              onbin_surface, start_pos, onbin_end, kHiraganaHatsuonbinCost, hatsuonbin_match.base_form,
              grammar::verbTypeToConjType(hatsuonbin_match.verb_type), true, CandidateOrigin::VerbHiragana, 0.9F,
              "hiragana_hatsuonbin", core::ExtendedPOS::VerbOnbinkei);
          hatsuonbin_cand.lemma_verified = true;
          candidates.push_back(std::move(hatsuonbin_cand));
        }
      }
    }
  }
}

}  // namespace suzume::analysis::hiragana_verb_detail
