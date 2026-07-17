/**
 * @file verb_candidates_kanji_inflection.cpp
 * @brief General inflection-analyzed kanji verb candidates
 */

#include <algorithm>
#include <cmath>

#include "analysis/bigram_table.h"
#include "analysis/candidate_constants.h"
#include "analysis/scorer_constants.h"
#include "analysis/verb_candidates_helpers.h"
#include "analysis/verb_candidates_kanji_internal.h"
#include "core/debug.h"
#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "grammar/conjugation.h"
#include "grammar/inflection_scorer_constants.h"
#include "normalize/char_type.h"
#include "normalize/exceptions.h"
#include "normalize/utf8.h"
#include "suffix_candidates.h"
#include "unknown.h"
#include "verb_candidates.h"

namespace suzume::analysis::kanji_verb_detail {
namespace vh = verb_helpers;

void appendAnalyzedKanjiVerbCandidates(const std::vector<char32_t>& codepoints, size_t start_pos, size_t kanji_end,
                                       size_t hiragana_end, const grammar::Inflection& inflection,
                                       const dictionary::DictionaryManager* dict_manager,
                                       const VerbCandidateOptions& verb_opts, bool sokuonbin_stem_verified,
                                       const std::string& sokuonbin_lemma, std::vector<UnknownCandidate>& candidates) {
  // Try different stem lengths (kanji only, or kanji + 1 hiragana for ichidan)
  // This handles both godan (kanji stem) and ichidan (kanji + hiragana stem)
  for (size_t stem_end = kanji_end; stem_end <= kanji_end + 1 && stem_end < hiragana_end; ++stem_end) {
    // Try different ending lengths, starting from longest
    for (size_t end_pos = hiragana_end; end_pos > stem_end; --end_pos) {
      std::string surface = extractSubstring(codepoints, start_pos, end_pos);

      if (surface.empty()) {
        continue;
      }

      // Check for particle/copula patterns that should NOT be treated as verbs
      // Kanji + particle or copula (で, に, を, が, は, も, へ, と, や, か, の, etc.)
      std::string hiragana_part = extractSubstring(codepoints, kanji_end, end_pos);
      if (normalize::isParticleOrCopula(hiragana_part)) {
        continue;  // Skip particle/copula patterns
      }

      // Skip patterns where hiragana part is a known suffix in dictionary
      // (e.g., たち, さん, ら, etc.) - let NOUN+suffix split win instead
      // For multi-kanji stems (2+ kanji), skip any suffix pattern
      // For single-kanji stems, only skip Suffix POS entries (さん, 様, etc.)
      // This allows verb renyokei like 立ち (立つ) while blocking 姉さん
      // Note: Only skip for OTHER (suffixes), not VERB (する is a verb, not suffix)
      // Exception: さ followed by れ/せ is godan-sa mizenkei + passive/causative,
      // not nominalization suffix (騙される, 話される, 殺させる)
      bool is_suffix_pattern = false;
      if (dict_manager != nullptr) {
        auto suffix_results = dict_manager->lookup(hiragana_part, 0);
        for (const auto& result : suffix_results) {
          if (result.entry != nullptr && result.entry->surface == hiragana_part) {
            // For single-kanji stems, only skip if POS is Suffix (honorifics like さん)
            // For multi-kanji stems, skip any suffix pattern
            bool is_suffix_pos = (result.entry->pos == core::PartOfSpeech::Suffix);
            bool is_multi_kanji = (kanji_end - start_pos >= 2);
            if (is_suffix_pos || (is_multi_kanji && (result.entry->extended_pos == core::ExtendedPOS::Suffix ||
                                                     result.entry->pos == core::PartOfSpeech::Other))) {
              // Exception: さ + れ/せ is godan-sa mizenkei + passive/causative
              if (hiragana_part == "さ" && end_pos < codepoints.size()) {
                char32_t next_char = codepoints[end_pos];
                if (next_char == U'れ' || next_char == U'せ') {
                  break;  // Not a suffix - godan-sa verb pattern
                }
              }
              // This hiragana part is a registered suffix - skip verb candidate
              is_suffix_pattern = true;
              break;
            }
          }
        }
      }
      if (is_suffix_pattern) {
        continue;
      }

      // Skip patterns that contain ください (polite request auxiliary)
      // e.g., 待ちください → 待ち + ください, not 待ちく + ださい
      // This prevents false compound verb analysis like 待ちく (待つ+来る renyokei)
      if (hiragana_part.find("ください") != std::string::npos || hiragana_part.find("くださ") != std::string::npos) {
        continue;  // Skip - let VERB + ください split win
      }

      // Skip patterns that extend past te-form boundary into auxiliaries
      // e.g., 履いてない → 履い + て + ない, not a single verb
      //        着ている → 着 + て + いる, not a single verb
      //        飲んでいた → 飲ん + で + いた, not a single verb
      // Detect: onbin ending (い/っ/ん) + て/で + auxiliary content (ない/いる/いた/ある/しまう etc.)
      {
        auto te_pos = hiragana_part.find("て");
        if (te_pos == std::string::npos) {
          te_pos = hiragana_part.find("で");
        }
        if (te_pos != std::string::npos && te_pos >= core::kJapaneseCharBytes) {
          // Check if there's auxiliary content after て/で
          std::string after_te = hiragana_part.substr(te_pos + core::kJapaneseCharBytes);
          if (!after_te.empty()) {
            // Check if char before て/で is onbin ending (い/っ/ん) or
            // godan-sa renyokei (し) — e.g., 過ごしてみた → 過ごし+て+み+た
            std::string_view before_te(hiragana_part.data() + te_pos - core::kJapaneseCharBytes,
                                       core::kJapaneseCharBytes);
            if (before_te == "い" || before_te == "っ" || before_te == "ん" || before_te == "し") {
              continue;  // Skip - let verb + て + auxiliary split win
            }
          }
        }
      }

      // Skip patterns ending with く when followed by ださ/ださい (part of ください)
      // e.g., 待ちく when followed by ださい → should be 待ち + ください
      {
        size_t hira_size = hiragana_part.size();
        if (hira_size >= core::kJapaneseCharBytes) {
          std::string_view last_char_view(hiragana_part.data() + hira_size - core::kJapaneseCharBytes,
                                          core::kJapaneseCharBytes);
          if (last_char_view == "く" && end_pos < codepoints.size()) {
            // Check if followed by だ or ださ or ださい
            std::string remaining = extractSubstring(codepoints, end_pos, std::min(end_pos + 3, codepoints.size()));
            if (remaining.compare(0, 6, "ださ") == 0 || remaining.compare(0, 3, "だ") == 0) {
              continue;  // Skip - likely part of ください pattern
            }
          }
        }
      }

      // Skip patterns that end with particles (noun renyokei + particle)
      // e.g., 切りに (切り + に), 飲みに (飲み + に), 行きに (行き + に)
      // These are nominalized verb stems followed by particles, not verb forms
      size_t hp_size = hiragana_part.size();
      if (hp_size >= core::kTwoJapaneseCharBytes) {  // At least 2 hiragana
        // Get last hiragana character (particle candidate)
        char32_t last_char = codepoints[end_pos - 1];
        if (normalize::isParticleCodepoint(last_char)) {
          // Check if the preceding part could be a valid verb renyokei
          // Renyokei typically ends in い/り/き/ぎ/し/み/び/ち/に
          char32_t second_last_char = codepoints[end_pos - 2];
          if (second_last_char == U'い' || second_last_char == U'り' || second_last_char == U'き' ||
              second_last_char == U'ぎ' || second_last_char == U'し' || second_last_char == U'み' ||
              second_last_char == U'び' || second_last_char == U'ち') {
            continue;  // Skip - likely nominalized noun + particle
          }
        }
      }

      // Check if this looks like a conjugated verb
      // Get all inflection candidates, not just the best one
      // This handles cases where the best candidate has wrong stem but a lower-ranked
      // candidate has the correct stem (e.g., 見なければ where 見なける wins over 見る)
      const auto& inflection_results = inflection.analyze(surface);
      std::string expected_stem = extractSubstring(codepoints, start_pos, stem_end);

      // Find a candidate with matching stem and sufficient confidence
      // Prefer dictionary-verified candidates when multiple have similar confidence
      // This handles ambiguous っ-onbin patterns like 待って (待つ/待る/待う)
      grammar::InflectionCandidate best;
      best.confidence = 0.0F;
      grammar::InflectionCandidate dict_verified_best;
      dict_verified_best.confidence = 0.0F;

      for (const auto& cand : inflection_results) {
        // Skip candidates from のだ/んだ stripping — these should be split tokens
        if (cand.has_explanatory_suffix)
          continue;

        // Use lower threshold for ichidan verbs with i-row stems (感じる, 信じる)
        // These get ichidan_kanji_i_row_stem penalty which reduces confidence
        // But NOT for e-row stems (て/で), which are often te-form splits
        // Also NOT for single-kanji + い patterns (人い → 人 + いる, not a verb)
        // Single-kanji + い patterns (人い) are excluded: almost always NOUN + いる,
        // not a single verb. Valid ichidan stems are multi-char (感じ, 信じ, etc.).
        bool is_i_row_ichidan = cand.verb_type == grammar::VerbType::Ichidan && vh::isValidIRowIchidanStem(cand.stem);
        float conf_threshold = (is_i_row_ichidan || sokuonbin_stem_verified) ? verb_opts.confidence_ichidan_dict
                                                                             : verb_opts.confidence_standard;
        bool is_multi_kanji_godan_wa_renyokei = cand.verb_type == grammar::VerbType::GodanWa &&
                                                utf8::endsWith(surface, "い") &&
                                                normalize::utf8Length(cand.stem) >= 2 && end_pos < codepoints.size() &&
                                                normalize::isKanjiCodepoint(codepoints[end_pos]);
        if (cand.stem == expected_stem &&
            (cand.confidence > conf_threshold ||
             (is_multi_kanji_godan_wa_renyokei && cand.confidence >= verb_opts.confidence_ichidan_dict)) &&
            cand.verb_type != grammar::VerbType::IAdjective) {
          // Check whether this candidate's base form exists in the dictionary as a
          // verb. The lookup is by surface, so disambiguation among っ-onbin types
          // (GodanRa/Ta/Wa/Ka) comes from each candidate carrying its own base_form
          // (e.g. 経る vs 経つ), not from a type-aware lookup.
          bool in_dict = vh::isVerbInDictionary(dict_manager, cand.base_form);

          if (in_dict) {
            // Prefer dictionary-verified candidates
            if (cand.confidence > dict_verified_best.confidence) {
              dict_verified_best = cand;
            }
          }
          if (cand.confidence > best.confidence) {
            best = cand;
          }
        }
      }

      // Use dictionary-verified candidate if available
      // Dictionary verification trumps confidence penalties from hiragana stems
      bool is_dict_verified = dict_verified_best.confidence > 0.0F;
      if (is_dict_verified) {
        best = dict_verified_best;
      }
      // A sokuonbin compound (突っ走る) is absent from the dictionary but was built
      // over a verified embedded verb; treat it as verified for the proceed gate so
      // its bare 終止形/意志形 (conf ~0.45) is not dropped by the standard threshold.
      is_dict_verified = is_dict_verified || sokuonbin_stem_verified;

      // Only proceed if we found a matching candidate
      // Use lower threshold for valid i-row ichidan stems (感じ, 信じ, etc.)
      // but not single-kanji + い patterns (人い → 人 + いる)
      bool proceed_is_i_row_ichidan =
          best.verb_type == grammar::VerbType::Ichidan && vh::isValidIRowIchidanStem(best.stem);
      // A multi-kanji godan-wa renyokei ending in い can be the first half of
      // a productive compound predicate (背負い進む). The inflection scorer
      // conservatively lowers its confidence because the same shape is often
      // an i-adjective. Admit the candidate at the dictionary threshold when
      // a kanji continuation follows; connection scoring will retain it only
      // before a verified verb.
      bool is_multi_kanji_godan_wa_renyokei = best.verb_type == grammar::VerbType::GodanWa &&
                                              utf8::endsWith(surface, "い") && normalize::utf8Length(best.stem) >= 2 &&
                                              end_pos < codepoints.size() &&
                                              normalize::isKanjiCodepoint(codepoints[end_pos]);

      // A case particle followed by する in its て/た form is a productive
      // nominal construction. Do not reinterpret a short noun plus that
      // closed-class sequence as an unregistered GodanSa verb (本+と+し+た,
      // 紙+に+し+て). Registered lexical verbs remain available.
      bool is_unregistered_godan_sa =
          best.verb_type == grammar::VerbType::GodanSa && !vh::isVerbInDictionary(dict_manager, best.base_form);
      if (is_unregistered_godan_sa && stem_end == kanji_end + 1 && (best.suffix == "して" || best.suffix == "した") &&
          vh::hasParticleDictionaryEntry(dict_manager, normalize::encodeUtf8(codepoints[stem_end - 1]))) {
        SUZUME_DEBUG_LOG("[VERB_SKIP] \"" << surface << "\" godan_sa case-particle+する pattern\n");
        continue;
      }

      // Skip fake ichidan candidates with stem ending in さ (a-row)
      // These are typically suru-verb causative/passive patterns:
      //   勉強させられた → 勉強 + さ + せ + られ + た (NOT ichidan 勉強さる)
      // Valid ichidan stems end in e-row or i-row, not a-row
      if (best.verb_type == grammar::VerbType::Ichidan && !best.stem.empty() &&
          best.stem.size() >= 2 * core::kJapaneseCharBytes) {
        std::string_view last_char(best.stem.data() + best.stem.size() - core::kJapaneseCharBytes,
                                   core::kJapaneseCharBytes);
        // さ is a-row hiragana (not valid for ichidan verb stems)
        if (last_char == "さ") {
          SUZUME_DEBUG_LOG_VERBOSE("[VERB_SKIP] \"" << surface
                                                    << "\" stem ends with さ (suru-verb causative pattern)\n");
          continue;
        }
      }
      // Dictionary-verified candidates use lower threshold (0.3)
      // This allows hiragana verbs like いわれる (conf=0.33) to be recognized
      float proceed_threshold = (is_dict_verified || proceed_is_i_row_ichidan || is_multi_kanji_godan_wa_renyokei)
                                    ? verb_opts.confidence_ichidan_dict
                                    : verb_opts.confidence_standard;
      if (best.confidence > proceed_threshold ||
          (is_multi_kanji_godan_wa_renyokei && best.confidence >= proceed_threshold)) {
        if (surface == "付け" && end_pos < codepoints.size() && codepoints[end_pos] == U'で') {
          continue;  // 付けで is formal noun + particle, not 付ける renyokei.
        }

        // Reject Godan verbs with stems ending in e-row hiragana
        // E-row endings (え,け,せ,て,ね,へ,め,れ) are typically ichidan stems
        // E.g., "伝えいた" falsely matches as GodanKa "伝えく" but 伝える is ichidan
        // Exception: GodanRa (passive/causative) with "られ" suffix is valid
        // E.g., "定められた" has stem "定め" (ichidan) + passive suffix
        bool is_godan = grammar::isGodanVerbType(best.verb_type);
        if (is_godan && stem_end > kanji_end && stem_end <= codepoints.size()) {
          // Check if the last character of the stem is e-row hiragana
          char32_t last_char = codepoints[stem_end - 1];
          if (grammar::isERowCodepoint(last_char)) {
            // Exception: GodanRa with passive/causative suffix (られ) is valid
            // This occurs with ichidan verb stem + passive auxiliary
            bool is_passive_pattern = (best.verb_type == grammar::VerbType::GodanRa && utf8::contains(surface, "られ"));
            if (!is_passive_pattern) {
              continue;  // Skip - e-row stem is typically ichidan, not godan
            }
          }
        }

        // Skip Suru verb renyokei (し) if followed by te/ta form particles
        // e.g., "勉強して" should be parsed as single token, not "勉強し" + "て"
        if (best.verb_type == grammar::VerbType::Suru && hiragana_part == "し" && end_pos < codepoints.size()) {
          char32_t next_char = codepoints[end_pos];
          if (next_char == U'て' || next_char == U'た' || next_char == U'で' || next_char == U'だ') {
            continue;  // Skip - let the longer te-form candidate win
          }
        }

        // Skip GodanSa renyokei (漢字+し/漢字+とし etc.) when not in dictionary
        // e.g., "得し" misrecognized as GodanSa "得す" renyokei, but actually "得+し"
        // e.g., "証とし" misrecognized as GodanSa "証とす" renyokei, but actually "証+として"
        // MeCab splits as: 得(名詞) + し(する連用形) + た(過去)
        // Exception: Real GodanSa verbs like "愛す", "汚す" should not be skipped
        if (best.verb_type == grammar::VerbType::GodanSa && utf8::endsWith(hiragana_part, "し") &&
            kanji_end - start_pos <= 3) {
          // Check if the base form (stem+す) is a registered GodanSa verb
          // For single-char hiragana_part "し": base = kanji + す
          // For multi-char like "とし": base = kanji + と + す = 証とす
          std::string base_stem = extractSubstring(codepoints, start_pos, stem_end);
          std::string base_form = base_stem + "す";
          if (!vh::isVerbInDictionary(dict_manager, base_form)) {
            // Not a registered verb - likely サ変 or compound particle pattern
            continue;
          }
        }

        // Skip GodanMa renyokei (漢字+み) when base form is not in dictionary.
        // Renyokei み competes with auxiliary みたい — without dict verification,
        // 猫みたい (noun+aux) cannot be distinguished from 読みたい (verb+aux).
        // Requires all single-kanji GODAN_MA verbs to be enumerated in L2 dict.
        if (best.verb_type == grammar::VerbType::GodanMa && hiragana_part == "み" && kanji_end - start_pos <= 3) {
          std::string base_form = extractSubstring(codepoints, start_pos, kanji_end) + "む";
          if (!vh::isVerbInDictionary(dict_manager, base_form)) {
            continue;
          }
        }

        // Skip verb + ます auxiliary patterns
        if (vh::shouldSkipMasuAuxPattern(surface, best.verb_type)) {
          continue;  // Skip - let the split (verb + dictionary aux) win
        }

        // Skip verb + そう auxiliary patterns
        if (vh::shouldSkipSouPattern(surface, best.verb_type)) {
          continue;  // Skip - let the split (verb renyokei + そう) win
        }

        // Skip verb + passive auxiliary patterns (れる, れた, etc.)
        // For auxiliary separation: 書かれる → 書か + れる
        if (vh::shouldSkipPassiveAuxPattern(surface, best.verb_type)) {
          continue;  // Skip - let the split (verb mizenkei + passive aux) win
        }

        // Skip verb + causative auxiliary patterns (せる, させる, etc.)
        // For auxiliary separation: 書かせる → 書か + せる
        if (vh::shouldSkipCausativeAuxPattern(surface, best.verb_type)) {
          continue;  // Skip - let the split (verb mizenkei + causative aux) win
        }

        // Skip suru-verb auxiliary patterns (して, した, している, etc.)
        // Preserve the noun and suru-verb boundary: 勉強して → 勉強 + して.
        size_t kanji_count = kanji_end - start_pos;
        if (vh::shouldSkipSuruVerbAuxPattern(surface, kanji_count, inflection)) {
          continue;  // Skip - let the split (noun + suru-aux) win
        }

        // Skip te-form + subsidiary/aspect verb patterns (てもらう, てくれ, てあげ,
        // ていく, ている, てお, ...): these split as verb te-form + auxiliary
        // (助けてもらう → 助け+て+もらう, 食べていく → 食べ+て+いく).
        // @see fabricated closed-class absorption guards (verb_candidates_helpers.h)
        if (vh::embedsTeFormAuxiliary(surface)) {
          continue;  // Skip - let the split (verb te-form + subsidiary verb) win
        }

        // Coordinate particles たり/だり close a past predicate and must not
        // be absorbed by a fabricated unknown verb (紙だったりする,
        // 高かったりする). Their preceding predicate is handled by the
        // dedicated copula/adjective/verb candidate paths.
        if (utf8::containsAny(surface, {"たり", "だり"})) {
          continue;
        }

        // Skip volitional patterns ending with よう (e.g., 食べよう)
        // Preserve the volitional stem and auxiliary boundary: 食べよう → 食べよ + う.
        if (surface.size() >= 6 && surface.compare(surface.size() - 6, 6, "よう") == 0) {
          continue;  // Skip - let the split (verb + volitional aux) win
        }

        // Skip godan volitional patterns ending with おう (e.g., 行こう, 書こう)
        // Preserve the volitional stem and auxiliary boundary: 行こう → 行こ + う.
        // O-row + う patterns: こう, ごう, そう, とう, のう, ぼう, もう, ろう, おう.
        // This is a DELIBERATE Godan-mizenkei subset, not the full o-row: よう is
        // the ichidan volitional (handled just above), and を/ど/ほ/ぞ never form a
        // Godan mizenkei. Do NOT widen to kana::isORowCodepoint — it would over-match
        // をう/どう/ほう and wrongly skip valid verb candidates.
        if (surface.size() >= 6) {
          std::string last_two = surface.substr(surface.size() - 6);  // 2 hiragana = 6 bytes
          if (last_two == "こう" || last_two == "ごう" || last_two == "そう" || last_two == "とう" ||
              last_two == "のう" || last_two == "ぼう" || last_two == "もう" || last_two == "ろう" ||
              last_two == "おう") {
            continue;  // Skip - let the split (verb mizenkei + う) win
          }
        }

        // Skip sokuonbin + auxiliary verb patterns (買っとく, 行っちゃう)
        // Preserve the onbin stem and contracted auxiliary boundary: 買っとく → 買っ + とく.
        // Check if suffix after っ is a registered auxiliary verb (とく, ちゃう, ちまう)
        bool skip_sokuonbin_aux = false;
        if (dict_manager && surface.size() >= 9) {  // っ(3) + 2char auxiliary minimum
          // Find っ position and check if suffix is auxiliary verb in dictionary
          auto surface_cps = normalize::utf8::decode(surface);
          for (size_t i = 1; i < surface_cps.size() && !skip_sokuonbin_aux; ++i) {
            if (surface_cps[i] == U'っ' && i + 1 < surface_cps.size()) {
              // Get suffix after っ
              std::vector<char32_t> suffix_cps(surface_cps.begin() + i + 1, surface_cps.end());
              std::string suffix = normalize::utf8::encode(suffix_cps);
              // Check if suffix is an auxiliary verb (AuxAspectOku: とく, AuxAspectShimau: ちゃう/ちまう)
              auto results = dict_manager->lookup(suffix, 0);
              for (const auto& r : results) {
                if (r.entry && r.entry->surface == suffix &&
                    (r.entry->extended_pos == core::ExtendedPOS::AuxAspectOku ||
                     r.entry->extended_pos == core::ExtendedPOS::AuxAspectShimau)) {
                  SUZUME_DEBUG_LOG_VERBOSE("[VERB_SKIP] \"" << surface << "\" sokuonbin+aux (" << suffix << ")\n");
                  skip_sokuonbin_aux = true;
                  break;
                }
              }
            }
          }
        }
        if (skip_sokuonbin_aux) {
          continue;  // Skip - let the split (verb sokuonbin + auxiliary) win
        }

        // Lower cost for higher confidence matches
        float base_cost = candidate::confidenceScaledCost(verb_opts.base_cost_standard, best.confidence,
                                                          verb_opts.confidence_cost_scale);
        // Suru verbs are compositional noun + する units; penalize the unified candidate.
        // e.g., 勉強する → 勉強 + する (split preferred)
        if (best.verb_type == grammar::VerbType::Suru && best.stem.size() >= core::kTwoJapaneseCharBytes) {
          // Penalize unified suru-verb to prefer noun + する/される/させる split
          base_cost += 3.0F;
          SUZUME_DEBUG_LOG_VERBOSE("[COST_ADJ] \"" << surface << "\" +3.0 (suru_split_penalty)\n");
        }
        // Penalize ALL verb candidates with prefix-like kanji at start
        // e.g., 今何する/今何してる should split, not be single verb
        // This applies to all verb types (suru, ichidan, godan)
        if (best.stem.size() >= core::kTwoJapaneseCharBytes) {
          auto stem_codepoints = normalize::utf8::decode(best.stem);
          if (!stem_codepoints.empty() && isPrefixLikeKanji(stem_codepoints[0])) {
            // Heavy penalty to force split
            base_cost += 3.0F;
            SUZUME_DEBUG_LOG_VERBOSE("[COST_ADJ] \"" << surface << "\" +3.0 (prefix_kanji_penalty)\n");
          }
        }
        // Penalize verb candidates starting with interrogative kanji (何, 誰, 幾)
        // e.g., 何してる should split as 何|し|てる, not be single verb
        // Interrogatives are standalone words, not verb stems
        {
          auto stem_codepoints = normalize::utf8::decode(best.stem);
          if (!stem_codepoints.empty() && isInterrogativeKanji(stem_codepoints[0])) {
            // Heavy penalty to force split
            base_cost += 3.0F;
            SUZUME_DEBUG_LOG_VERBOSE("[COST_ADJ] \"" << surface << "\" +3.0 (interrogative_kanji_penalty)\n");
          }
        }
        // Skip patterns where removing first kanji leaves a valid dictionary verb
        // e.g., 本買った → 本 + 買った, where 買う is a dict verb
        // This handles particleless noun+verb patterns: 本買った, 服買った, 車買った
        if (dict_manager != nullptr && kanji_count == 2) {
          auto stem_cps = normalize::utf8::decode(best.stem);
          if (stem_cps.size() == 2) {
            // Get second kanji as potential verb stem
            std::string remainder_stem = normalize::utf8::encode({stem_cps[1]});
            // Check if remainder + verb ending is a dictionary verb
            std::string conj_suffix = vh::baseFormSuffix(best.verb_type);
            if (!conj_suffix.empty()) {
              std::string remainder_base = remainder_stem + conj_suffix;
              if (vh::isVerbInDictionary(dict_manager, remainder_base)) {
                // Skip this candidate - prefer noun + verb split
                SUZUME_DEBUG_LOG_VERBOSE("[VERB_SKIP] \"" << surface << "\" remainder \"" << remainder_base
                                                          << "\" is dict verb\n");
                continue;
              }
            }
          }
        }
        // Penalize single-kanji + いる verb candidates (both godan-ra and ichidan)
        // e.g., 人いる should split as 人 + いる (noun + verb), not be verb
        // Most single kanji + いる patterns are NOUN + existence verb いる
        // Valid single-kanji verbs: 入る, 走る, 居る (いる), 見る, etc.
        // These should be in dictionary, so dictionary bonus will override
        {
          auto surface_cps = normalize::utf8::decode(surface);
          // Check if pattern is: 1 kanji + いる
          if (surface_cps.size() == 3 && normalize::isKanjiCodepoint(surface_cps[0]) && surface_cps[1] == U'い' &&
              surface_cps[2] == U'る') {
            // Single kanji + いる pattern - penalize to prefer NOUN + いる split
            base_cost += 2.5F;
            SUZUME_DEBUG_LOG_VERBOSE("[COST_ADJ] \"" << surface << "\" +2.5 (single_kanji_iru_penalty)\n");
          }
        }
        // Check if base form exists in dictionary - significant bonus for known verbs
        // This helps 行われた (base=行う) beat 行(suffix)+われた split
        // Skip compound adjective patterns (verb renyoukei + にくい/やすい/がたい)
        // Skip suru-verbs because noun and する are separate search units.
        bool is_comp_adj = vh::isCompoundAdjectivePattern(surface);
        bool in_dict = vh::isVerbInDictionary(dict_manager, best.base_form);
        bool is_suru = (best.verb_type == grammar::VerbType::Suru);
        // Reject a fabricated conjugation that merely absorbs a trailing
        // focus particle (+ optional negative): 水しかない is noun + 副助詞
        // しか + ない, never a form of the non-word godan-ka verb 水しく, and
        // お金さえない is noun + 係助詞 さえ + ない, never a form of the non-word
        // godan-wa verb 金さう. Real verbs whose surface embeds a particle
        // string (押さえ from 押さえる, 起こそ from 起こす) are protected by
        // their dictionary base form (in_dict).
        // @see fabricated closed-class absorption guards (verb_candidates_helpers.h)
        if (!in_dict && vh::endsWithFocusParticleTail(dict_manager, codepoints, start_pos, end_pos)) {
          SUZUME_DEBUG_LOG("[VERB_SKIP] \"" << surface << "\" fabricated verb absorbing focus particle\n");
          continue;
        }
        // Reject a fabricated conjugation that spans a te-form + the subsidiary
        // verb みる: an internal て/で followed by み is always [verb te-form] +
        // みる (食べてみれば = 食べ + て + みれ + ば), never one conjugated verb.
        // @see fabricated closed-class absorption guards (verb_candidates_helpers.h)
        if (!in_dict && vh::embedsTeFormMiruAuxiliary(codepoints, start_pos, end_pos)) {
          SUZUME_DEBUG_LOG("[VERB_SKIP] \"" << surface << "\" fabricated verb spanning te-form + みる\n");
          continue;
        }
        if (!is_comp_adj && in_dict && !is_suru) {
          // Found in dictionary - give strong bonus (not for suru-verbs)
          base_cost = candidate::confidenceScaledCost(verb_opts.base_cost_verified, best.confidence,
                                                      verb_opts.confidence_cost_scale_medium);
          // Godan-ra renyokei ambiguity: 降り can be from 降る(godan-ra) or
          // 降りる(ichidan). When ichidan form exists in dict, penalize godan-ra
          // so the more specific ichidan interpretation wins.
          if (best.verb_type == grammar::VerbType::GodanRa && utf8::endsWith(surface, "り")) {
            std::string ichidan_base = surface + "る";
            if (vh::isVerbInDictionary(dict_manager, ichidan_base)) {
              base_cost += 1.0F;
              SUZUME_DEBUG_LOG_VERBOSE("[COST_ADJ] \"" << surface << "\" +1.0 (godan_ra_ichidan_ambiguity, "
                                                       << ichidan_base << " in dict)\n");
            }
          }
        }
        // Penalty for compound adjective patterns (verb renyokei + やすい/にくい/がたい)
        // MeCab splits these: 使いにくい → 使い + にくい
        if (is_comp_adj) {
          base_cost += 2.0F;  // Strong penalty to force split
          SUZUME_DEBUG_LOG_VERBOSE("[COST_ADJ] \"" << surface << "\" +2.0 (compound_adj_penalty)\n");
        }
        // Penalize 2+-kanji verb candidates whose base form is not in dict
        // Most real 2-kanji verbs (行う, 伴う, etc.) are in the dictionary.
        // False 2-kanji patterns like 柿食えば (柿 + 食えば) have base 柿食う
        // which is not a real verb. Apply penalty so noun + verb split wins.
        // Extended from ==2 to >=2: a 3+-leading-kanji "verb" whose base is not
        // in any dictionary is likewise noun+verb over-merge or a suru-compound
        // (全部食べちゃった misparsed with 全部食 as a fake verb stem); real 2-kanji
        // verbs are dict entries, so they are unaffected by widening the range.
        if (kanji_count >= 2 && !in_dict && !is_multi_kanji_godan_wa_renyokei) {
          base_cost += bigram_cost::kRare;
          SUZUME_DEBUG_LOG_VERBOSE("[COST_ADJ] \"" << surface << "\" +1.0 (two_kanji_non_dict_penalty)\n");
        }
        // A multi-kanji stem followed by the classical サ変 terminal す is
        // compositional for search: 前進+す, 説明+す. The inflection analyzer can
        // also hypothesize an unregistered GodanSa word spanning the boundary;
        // apply a small class-level penalty so the independently generated noun
        // and closed-class す entry win. Registered lexical GodanSa verbs and
        // single-kanji stems such as 愛す are unaffected.
        if (!in_dict && kanji_count >= 2 && best.verb_type == grammar::VerbType::GodanSa && best.base_form == surface) {
          base_cost += bigram_cost::kMinor;
          SUZUME_DEBUG_LOG_VERBOSE("[COST_ADJ] \"" << surface << "\" +" << bigram_cost::kMinor
                                                   << " (multi_kanji_classical_suru_penalty)\n");
        }
        // Penalize ichidan verb candidates with pure single-kanji stem (no hiragana)
        // when base form is not in dict.
        // Real ichidan verbs with single-kanji stems (見る, 着る, 居る, etc.) are in
        // the dictionary, while real multi-char stems like 食べ (食べる) need no penalty.
        // False patterns like 心る (from "心なく" misparsed as ichidan) have a pure
        // single-kanji stem and should be penalized to favor noun + aux split.
        if (!in_dict && best.verb_type == grammar::VerbType::Ichidan && !best.stem.empty() &&
            best.stem.size() == core::kJapaneseCharBytes) {
          base_cost += bigram_cost::kRare;
          SUZUME_DEBUG_LOG_VERBOSE("[COST_ADJ] \"" << surface
                                                   << "\" +1.0 (single_kanji_stem_ichidan_non_dict_penalty)\n");
        }
        // Penalize unverified godan candidates that look like NOUN+AUX/VERB misanalysis.
        // Skip ichidan (handled above) and Suru (handled earlier).
        // Two patterns are penalized:
        //   (a) godan-ka with stem ending in な (心なく → 心+なく): なく is AUX_過去 of ない.
        //   (b) hiragana-only portion of base form is a 2+ char dict AUX/VERB
        //       (e.g., 我ある — ある is dict VERB).
        if (!in_dict && kanji_count == 1 && dict_manager != nullptr && best.verb_type != grammar::VerbType::Ichidan &&
            best.verb_type != grammar::VerbType::Suru) {
          bool penalized = false;
          // Pattern (a): godan-ka with stem ending in な
          if (best.verb_type == grammar::VerbType::GodanKa && !best.stem.empty() && utf8::endsWith(best.stem, "な")) {
            base_cost += bigram_cost::kRare;
            SUZUME_DEBUG_LOG_VERBOSE("[COST_ADJ] \"" << surface
                                                     << "\" +1.0 (godan_ka_kanji_na_suffix_non_dict_penalty)\n");
            penalized = true;
          }
          // Pattern (b): hiragana-only portion of base form is a 2+ char dict AUX/VERB.
          // Restricted to 2+ chars to avoid false matches with single-char endings
          // (う = AuxVolitional, but all real godan-wa verbs end in う: 思う, 戦う etc.)
          if (!penalized && !best.base_form.empty()) {
            auto base_cps = normalize::utf8::decode(best.base_form);
            if (base_cps.size() >= 3 && normalize::isKanjiCodepoint(base_cps[0])) {
              std::vector<char32_t> hira_only(base_cps.begin() + 1, base_cps.end());
              std::string hira_portion = normalize::utf8::encode(hira_only);
              if (verb_helpers::hasDictionaryEntry(dict_manager, hira_portion, core::PartOfSpeech::Auxiliary) ||
                  verb_helpers::isVerbInDictionary(dict_manager, hira_portion)) {
                base_cost += bigram_cost::kRare;
                SUZUME_DEBUG_LOG_VERBOSE("[COST_ADJ] \"" << surface
                                                         << "\" +1.0 (single_kanji_godan_hira_is_dict_word_penalty)\n");
              }
            }
          }
        }
        // Penalty for verb candidates containing みたい suffix
        // みたい is a na-adjective (like ~, seems ~), not a verb suffix
        // E.g., 猫みたい should be 猫 + みたい, not VERB 猫む
        if (utf8::contains(surface, "みたい")) {
          base_cost += verb_opts.penalty_single_char;
        }
        // Penalty for verb candidates ending with auxiliary たい/たく/たかっ
        // MeCab splits verb + auxiliary たい (desiderative)
        // E.g., 戦いたい = 戦い + たい, not single VERB
        if (utf8::endsWith(surface, "たい") || utf8::endsWith(surface, "たく") || utf8::endsWith(surface, "たかっ")) {
          base_cost += bigram_cost::kRare;  // Penalize to favor split path
        }
        // Penalty for verb candidates containing causative auxiliary chains
        // MeCab splits: 欠かせない → 欠か+せ+ない, 食べさせた → 食べ+させ+た
        if (vh::containsCausativeAuxPattern(surface)) {
          base_cost += bigram_cost::kStrong;  // Penalize to favor split path
        }
        // Penalty for verb candidates ending with auxiliary まい (negative volitional)
        // MeCab splits verb + auxiliary まい
        // E.g., 出来まい = 出来 + まい, 行くまい = 行く + まい
        if (utf8::endsWith(surface, "まい")) {
          base_cost += bigram_cost::kStrong;  // Penalize to favor split path
        }
        // Penalty for verb candidates ending with らしい (conjecture auxiliary)
        // MeCab splits verb/adj + らしい
        // E.g., 帰りたいらしい = 帰り + たい + らしい
        if (utf8::endsWith(surface, "らしい") || utf8::endsWith(surface, "らしく") ||
            utf8::endsWith(surface, "らしかっ")) {
          base_cost += bigram_cost::kStrong;  // Penalize to favor split path
        }
        // Penalty for verb candidates ending with passive+te form (〜まれて/〜られて)
        // MeCab splits compound verb passive+te: 読み込まれて → 読み込ま|れ|て
        // E.g., 読み込まれていない = 読み込ま + れ + て + い + ない
        if (utf8::endsWith(surface, "まれて") || utf8::endsWith(surface, "まれた") ||
            utf8::endsWith(surface, "られて") || utf8::endsWith(surface, "られた")) {
          base_cost += bigram_cost::kVeryRare + bigram_cost::kNegligible;  // 2.0F
        }
        // Penalty for verb candidates containing て+auxiliary verb chains
        // MeCab splits: 付いてくる → 付い+て+くる, 集まってくる → 集まっ+て+くる
        // These are syntactic constructions (V-te + auxiliary), not single verb forms
        if (vh::containsTeFormAuxPattern(surface)) {
          base_cost += bigram_cost::kStrong;  // Penalize to favor split path
        }
        // Penalty for verb candidates absorbing post-verbal particles
        // たばかり/だばかり = V-ta + bakari ("just did V"), always separate tokens
        // E.g., 生まれたばかりだ should be 生まれ+た+ばかり+だ, not one token
        if (utf8::contains(surface, "たばかり") || utf8::contains(surface, "だばかり")) {
          base_cost += bigram_cost::kSevere;
        }
        // Set has_suffix to skip exceeds_dict_length penalty in tokenizer.cpp
        // This applies when:
        // 1. Base form exists in dictionary as verb (in_dict)
        // 2. OR: Ichidan verb with valid i-row stem (感じる, not 人いる)
        //    that passes confidence threshold
        // Valid i-row ichidan stems end in i-row hiragana (not e-row te-form/copula)
        // and exclude single-kanji + い patterns (人い → 人 + いる).
        bool is_ichidan = (best.verb_type == grammar::VerbType::Ichidan);
        bool has_valid_ichidan_stem = is_ichidan && vh::isValidIRowIchidanStem(best.stem);
        bool recognized_ichidan =
            is_ichidan && has_valid_ichidan_stem && best.confidence > verb_opts.confidence_ichidan_dict;
        // Godan verbs with single-kanji stem + high confidence are also
        // recognized (残る, 立つ, 打つ, etc.)
        bool recognized_godan = !is_ichidan && !in_dict && !best.stem.empty() &&
                                best.stem.size() == core::kJapaneseCharBytes &&
                                best.confidence >= verb_opts.confidence_ichidan_dict;
        // A sokuonbin compound built over a verified embedded verb (突っ走り) is a
        // genuine verb even though its multi-kanji stem is absent from the
        // dictionary; exempt it from the exceeds_dict_length penalty so its
        // renyokei competes with the noun split before the ます auxiliary.
        bool has_suffix = in_dict || recognized_ichidan || recognized_godan || sokuonbin_stem_verified ||
                          is_multi_kanji_godan_wa_renyokei;
        // Determine extended_pos based on verb type and surface ending
        // Godan-wa verbs ending in い are renyokei (戦い), not onbinkei
        // Godan-ka/ga verbs ending in い are onbinkei (書い, 泳い)
        core::ExtendedPOS verb_epos = core::ExtendedPOS::Unknown;  // Auto-detect
        if (grammar::isGodanVerbType(best.verb_type) && best.base_form == surface) {
          // A complete Godan dictionary form ends in its u-row base suffix.
          // The surface-only fallback intentionally cannot infer every u-row
          // ending because short stems are ambiguous, but the inflection result
          // already supplies that evidence here (立つ, 書く, 一つ-as-a-competing
          // hypothesis). Marking it as terminal prevents case-particle rules for
          // true renyokei from spuriously boosting the hypothesis.
          verb_epos = core::ExtendedPOS::VerbShuushikei;
        } else if (utf8::endsWith(surface, "い")) {
          // Skip godan readings of known kami-ichidan renyokei stems (率い,
          // 老い, 強い, ...): the godan lemma would be wrong (率く/率う).
          // The ichidan_renyokei path generates the correct 〜いる candidate.
          if (grammar::inflection::isValidKanjiIStemException(surface)) {
            SUZUME_DEBUG_LOG("[VERB_SKIP] \"" << surface << "\" is kami-ichidan renyokei, skipping godan reading\n");
            continue;
          }
          if (best.verb_type == grammar::VerbType::GodanWa) {
            verb_epos = core::ExtendedPOS::VerbRenyokei;
          } else if (best.verb_type == grammar::VerbType::GodanKa || best.verb_type == grammar::VerbType::GodanGa) {
            verb_epos = core::ExtendedPOS::VerbOnbinkei;
          }
        }
        // Skip an unverified bare Godan form when the whole surface is an exact
        // dictionary noun/adjective. This covers nominalized renyokei (思い,
        // 戦い) and u-row homographs (向う) without suppressing a verified verb
        // entry that legitimately shares the surface.
        if (!in_dict &&
            (verb_epos == core::ExtendedPOS::VerbRenyokei || verb_epos == core::ExtendedPOS::VerbShuushikei) &&
            vh::isNounOrAdjectiveInDictionary(dict_manager, surface)) {
          SUZUME_DEBUG_LOG("[VERB_SKIP] \"" << surface << "\" is dict NOUN/ADJ, skipping unverified godan form\n");
          continue;  // Skip this candidate, use dictionary entry instead
        }
        // Skip fabricated godan-wa renyokei whose trailing い is really the
        // leading い of the receptive auxiliary いただく. Unverified wa-row
        // hypotheses (覧い ← 覧う) would otherwise absorb the auxiliary's
        // onset (ご覧いただき → 覧い+ただき); dictionary-verified wa-row verbs
        // (使い ← 使う) keep their candidate.
        if (verb_epos == core::ExtendedPOS::VerbRenyokei && !in_dict && end_pos > 0 &&
            vh::itadakuParadigmStartsAt(codepoints, end_pos - 1)) {
          SUZUME_DEBUG_LOG("[VERB_SKIP] \"" << surface << "\" trailing い starts いただく paradigm\n");
          continue;  // Skip - keep the い with いただく
        }
        // Skip ichidan ta-form if stem is registered as NOUN in dictionary
        // e.g., 感じた → stem 感じ is dict NOUN, so skip (prefer 感じ(NOUN) + た(AUX))
        // This prevents nominalized verb renyokei forms from appearing as conjugated verbs
        // The stem for ichidan ta-form is the renyokei (e.g., 感じ for 感じた)
        if (best.verb_type == grammar::VerbType::Ichidan && !best.stem.empty() &&
            vh::isNounInDictionary(dict_manager, best.stem)) {
          SUZUME_DEBUG_LOG("[VERB_SKIP] \"" << surface << "\" stem \"" << best.stem
                                            << "\" is dict NOUN, skipping ichidan ta-form\n");
          continue;  // Skip this candidate, prefer NOUN + た split
        }
        // Skip if surface is already a registered VERB in dictionary
        // The dict entry has correct lemma; this unknown candidate would have wrong lemma
        // E.g., 下さい is dict verb (lemma=下さる), skip godan-wa candidate (lemma=下さう)
        if (!in_dict && vh::isVerbInDictionary(dict_manager, surface)) {
          SUZUME_DEBUG_LOG("[VERB_SKIP] \"" << surface << "\" is dict VERB, skipping unknown candidate\n");
          continue;
        }
        // Skip fake verb candidates homographic with the i-adjective 未然形.
        // Xかろ(+う) can be a verb volitional stem (分かる → 分かろ+う) or the
        // i-adjective 未然形 (高い → 高かろ+う); inflection alone yields a
        // plausible fake base (ichidan 高かる). The lexical signal decides:
        // when the base form is not a known verb and stem + い is a known
        // dictionary adjective, prefer the ADJ 未然形 candidate.
        if (!in_dict && dict_manager != nullptr && utf8::endsWith(surface, "かろ")) {
          std::string iadj_base = surface.substr(0, surface.size() - 2 * core::kJapaneseCharBytes) + "い";
          if (vh::isAdjectiveInDictionary(dict_manager, iadj_base)) {
            SUZUME_DEBUG_LOG("[VERB_SKIP] \"" << surface << "\" ends かろ and " << iadj_base
                                              << " is i-adjective (prefer ADJ 未然形)\n");
            continue;
          }
        }
        // Skip fake verb candidates homographic with the classical i-adjective
        // 連体形 (文語). Xき is usually a godan-ka 連用形 (書き ← 書く), but when
        // the hypothesized base verb is not in the dictionary and stem + い is a
        // known dictionary adjective (美しき → 美しい), the surface is the
        // classical attributive form — prefer the ADJ 連体形 candidate.
        if (!in_dict && dict_manager != nullptr && utf8::endsWith(surface, "き")) {
          std::string iadj_base = surface.substr(0, surface.size() - core::kJapaneseCharBytes) + "い";
          if (vh::isAdjectiveInDictionary(dict_manager, iadj_base)) {
            SUZUME_DEBUG_LOG("[VERB_SKIP] \"" << surface << "\" ends き and " << iadj_base
                                              << " is i-adjective (prefer ADJ 連体形)\n");
            continue;
          }
        }
        // Penalize verb candidates absorbing adj く-form + なる suffix chain
        // e.g., 得なくなった should split as 得+なく+なっ+た, not merge as 得る(ichidan)
        // The suffix contains くなっ/くなり/くなる/くなれ = adj renyokei + なる conjugation
        if (!best.suffix.empty() && vh::containsKuNaruPattern(best.suffix)) {
          base_cost += bigram_cost::kSevere;  // Force split
          SUZUME_DEBUG_LOG("[COST_ADJ] \"" << surface << "\" +" << bigram_cost::kSevere << " (ku_naru_verb_suffix)\n");
        }
        // Penalize verb candidates absorbing the negative adverbial なく (ない's 連用形).
        // MeCab splits mizenkei + なく: 行かなくて → 行か + なく + て, not 行かなく(verb).
        // The mizenkei-split candidate (is_naku_pattern above) supplies the split path;
        // this penalty stops the inflection analyzer's whole-span reading from winning.
        if (best.suffix.find("なく") != std::string::npos) {
          base_cost += bigram_cost::kSevere;  // Force split
          SUZUME_DEBUG_LOG("[COST_ADJ] \"" << surface << "\" +" << bigram_cost::kSevere << " (negative_naku_suffix)\n");
        }
        // Penalize unverified godan-wa candidates that extend beyond a
        // shorter dict verb at the same position. These false positives
        // absorb い from the next word (いただく, いく, etc.)
        // e.g., 待ちい (base=待ちう) extends beyond 待ち (dict verb 待つ)
        if (best.verb_type == grammar::VerbType::GodanWa && !in_dict && dict_manager != nullptr) {
          auto prefix_results = dict_manager->lookup(surface, 0);
          for (const auto& result : prefix_results) {
            if (result.entry != nullptr && result.entry->pos == core::PartOfSpeech::Verb &&
                result.length < normalize::utf8Length(surface)) {
              base_cost += 2.0F;
              SUZUME_DEBUG_LOG("[COST_ADJ] \"" << surface << "\" +2.0 (godan_wa_exceeds_dict_verb)\n");
              break;
            }
          }
        }
        SUZUME_DEBUG_VERBOSE_BLOCK {
          SUZUME_DEBUG_STREAM << "[VERB_CAND] " << surface << " base=" << best.base_form << " cost=" << base_cost
                              << " in_dict=" << in_dict << " has_suffix=" << has_suffix << "\n";
        }
        // Don't set lemma here - let lemmatizer derive it with dictionary verification
        // The lemmatizer will use stem-matching logic to pick the correct base form.
        // Exception: sokuonbin compounds carry the lemma built from the embedded verb
        // base, which the surface-based lemmatizer cannot recover past the onbin.
        const char* forced_lemma = sokuonbin_stem_verified
                                       ? sokuonbin_lemma.c_str()
                                       : (is_multi_kanji_godan_wa_renyokei ? best.base_form.c_str() : "");
        auto verb_candidate =
            makeVerbCandidate(surface, start_pos, end_pos, base_cost, forced_lemma,
                              grammar::verbTypeToConjType(best.verb_type), has_suffix, CandidateOrigin::VerbKanji,
                              best.confidence, grammar::verbTypeToString(best.verb_type).data(), verb_epos);
        verb_candidate.lemma_verified = in_dict;
        candidates.push_back(std::move(verb_candidate));
        // Don't break - try other stem lengths too
      }
    }
  }
}

}  // namespace suzume::analysis::kanji_verb_detail
