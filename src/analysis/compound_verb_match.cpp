/**
 * @file compound_verb_match.cpp
 * @brief V1 verification and V2 matching for compound verbs
 */

#include "join_compound_verb_internal.h"

namespace suzume::analysis::compound_verb_detail {

namespace {

// A compound-verb candidate needs inflectional evidence, not a bare
// continuative ending. Politeness is intentionally excluded: ます remains a
// separate auxiliary token.
bool hasAuxiliarySuffix(std::string_view suffix) {
  return !suffix.empty() && utf8::containsAny(suffix, {"た", "て", "で", "だ", "ない", "れ"});
}

// A continuative can be ambiguous across conjugation classes (降り → 降りる /
// 降る). Compound-verb generation has already reconstructed the V1 base from
// the continuative ending, so validate that base against every inflection
// candidate instead of discarding it merely because another analysis scores
// higher in isolation.
bool hasInflectionCandidateForBase(const grammar::Inflection& inflection, std::string_view surface,
                                   std::string_view base_form, float min_confidence) {
  for (const auto& candidate : inflection.analyze(surface)) {
    if (candidate.confidence >= min_confidence && candidate.base_form == base_form) {
      return true;
    }
  }
  return false;
}

bool beginsMizenkeiAuxiliary(std::string_view text, size_t start_byte, std::string_view mizenkei) {
  if (mizenkei.empty() || start_byte + mizenkei.size() + core::kJapaneseCharBytes > text.size() ||
      text.substr(start_byte, mizenkei.size()) != mizenkei) {
    return false;
  }
  const std::string_view following = text.substr(start_byte + mizenkei.size(), core::kJapaneseCharBytes);
  return following == "れ" || following == "せ" || following == "な" || following == "ず";
}

}  // namespace

CompoundVerbMatch findCompoundVerbMatch(std::string_view text, const std::vector<char32_t>& codepoints,
                                        const ByteOffsets& byte_offsets, size_t start_pos,
                                        const std::vector<normalize::CharType>& char_types, size_t kanji_end,
                                        size_t v2_start, char32_t base_ending, bool is_sokuonbin, bool is_ichidan,
                                        bool has_kanji_v2_after_bare_ichidan, bool dict_compound_v1,
                                        std::string_view dict_compound_v1_lemma,
                                        const dictionary::DictionaryManager& dict_manager,
                                        const grammar::Inflection& inflection) {
  if (v2_start >= codepoints.size()) {
    return {};
  }

  // A compound verb joins two verbal components directly. If a closed-class
  // particle occurs between the prospective V1 and V2 boundaries, the span is
  // compositional instead (読む+だけ+あって), not a compound verb.
  for (size_t particle_start = start_pos; particle_start < v2_start; ++particle_start) {
    const std::string particle_probe = extractSubstring(codepoints, particle_start, v2_start);
    for (const auto& match : dict_manager.lookup(particle_probe, 0)) {
      if (match.entry != nullptr && match.entry->pos == core::PartOfSpeech::Particle &&
          normalize::utf8Length(match.entry->surface) > 1 &&
          particle_start + normalize::utf8Length(match.entry->surface) <= v2_start) {
        return {};
      }
    }

    // A terminal u-row verb followed by a case particle is a clause boundary,
    // even when the particle is a single character (行く+に+越した).  A
    // continuative ending such as し remains eligible for lexical compounds.
    const auto* particle = dict_manager.lookupExact(particle_probe, core::PartOfSpeech::Particle);
    if (particle != nullptr && particle_start > start_pos && kana::isURowCodepoint(codepoints[particle_start - 1])) {
      return {};
    }
  }

  // Get byte positions
  size_t start_byte = byteOffsetAt(byte_offsets, start_pos);
  size_t v2_start_byte = byteOffsetAt(byte_offsets, v2_start);

  // Inflection analyzer for V2 detection (shared instance from Tokenizer)

  // Find extent of hiragana after v2_start for inflection analysis
  size_t v2_hiragana_end = findCharRegionEnd(char_types, v2_start, 8, CharType::Hiragana);

  // Look for V2 (subsidiary verb)
  // We collect the best match rather than returning immediately.
  // This allows renyokei matches (すぎ) to take precedence over inflection
  // matches (すぎた) when the inflection match includes an auxiliary suffix.
  CompoundVerbMatch best_match;

  for (const auto& v2_verb : subsidiaryVerbs()) {
    if (!v2_verb.joins_general) {
      continue;
    }
    std::string_view v2_surface(v2_verb.surface);
    std::string_view v2_reading(v2_verb.joins_reading && v2_verb.reading ? v2_verb.reading : "");

    // The hiragana reading of compound V2 入る overlaps with the aspect
    // auxiliary いる.  A preceding て/で is a grammatical boundary
    // (異なっ|て|いる), not a renyokei stem for a compound ending in 入る.
    // Keep real compounds such as 立ち入る eligible: their V2 begins directly
    // after the V1 renyokei and therefore has no te-form particle before it.
    if (v2_reading == "いる" && v2_start > start_pos &&
        (codepoints[v2_start - 1] == U'て' || codepoints[v2_start - 1] == U'で')) {
      continue;
    }

    // The hiragana V2 reading 切る also overlaps with the lexical potential
    // verb できる.  Its leading で completes that word, rather than forming an
    // ichidan V1 stem (化でる) before a compound-verb V2.  Kanji-written V2
    // compounds such as 撫で切る are unaffected.
    if (v2_reading == "きる" && v2_start > start_pos && codepoints[v2_start - 1] == U'で') {
      continue;
    }

    // Determine if this is a renyokei entry by checking if base_form != surface
    // Renyokei entries: 過ぎ (base 過ぎる), 出し (base 出す), etc.
    bool is_renyokei_entry = false;

    // Check if text at v2_start matches this V2 verb (kanji or reading)
    bool matched_kanji = false;
    bool matched_reading = false;
    bool matched_inflected = false;
    bool matched_kateikei = false;
    bool matched_potential = false;
    bool matched_renyokei_via_reading = false;
    size_t matched_len = 0;
    bool inflection_includes_aux = false;

    // Try kanji match first
    if (v2_start_byte + v2_surface.size() <= text.size()) {
      std::string_view text_at_v2 = text.substr(v2_start_byte, v2_surface.size());
      if (text_at_v2 == v2_surface) {
        matched_kanji = true;
        matched_len = v2_surface.size();
      }
    }

    // The hiragana spellings とる and どる are contracted progressive
    // auxiliaries after a verb stem. Keep the lexical V2 entries available
    // in their kanji spelling (受け取る), but do not build a false compound
    // candidate over the productive auxiliary sequence (食べとった).
    if (!matched_kanji && char_types[v2_start] == CharType::Hiragana &&
        (v2_reading == "とる" || v2_reading == "どる")) {
      continue;
    }

    // Try reading (hiragana) match if kanji didn't match
    if (!matched_kanji && !v2_reading.empty() && v2_start_byte + v2_reading.size() <= text.size()) {
      std::string_view text_at_v2 = text.substr(v2_start_byte, v2_reading.size());
      if (text_at_v2 == v2_reading) {
        matched_reading = true;
        matched_len = v2_reading.size();
      }
    }

    // Try a V2 renyokei match so a following auxiliary stays separate.
    // e.g., 申し上げます → 申し上げ + ます (match V2 renyokei "上げ", not full "上げます")
    bool matched_renyokei = false;
    if (!matched_kanji && !matched_reading) {
      // Generate V2 renyokei
      std::string kanji_renyokei = generateKanjiRenyokei(v2_surface, v2_reading, v2_verb.verb_type);
      std::string hira_renyokei = generateRenyokei(v2_reading, "", v2_verb.verb_type);

      // Try kanji renyokei match
      if (!kanji_renyokei.empty() && v2_start_byte + kanji_renyokei.size() <= text.size()) {
        std::string_view text_at_v2 = text.substr(v2_start_byte, kanji_renyokei.size());
        if (text_at_v2 == kanji_renyokei) {
          matched_renyokei = true;
          matched_len = kanji_renyokei.size();
          is_renyokei_entry = true;  // Mark as renyokei match
        }
      }

      // Try hiragana renyokei match if kanji didn't match
      if (!matched_renyokei && !hira_renyokei.empty() && v2_start_byte + hira_renyokei.size() <= text.size()) {
        std::string_view text_at_v2 = text.substr(v2_start_byte, hira_renyokei.size());
        if (text_at_v2 == hira_renyokei) {
          // Skip Ichidan V1 + V2「出る」renyokei (で) match
          // Ichidan verbs use て for te-form, never で.
          // E.g., 付けで should be 付け(VERB)+で(PARTICLE), not 付け出る (compound)
          // But Godan+出る is valid: 飛び出る (飛ぶ→飛び+出る)
          if (is_ichidan && hira_renyokei == "で") {
            continue;  // Skip V2「出る」for Ichidan V1
          }
          matched_renyokei = true;
          matched_renyokei_via_reading = true;
          matched_len = hira_renyokei.size();
          is_renyokei_entry = true;  // Mark as renyokei match
        }
      }
    }

    // A Godan potential form belongs to the same search unit as its compound
    // base: 取り + 戻せる → 取り戻せる. Generate it from every allowlisted
    // V2 rather than adding per-verb potential entries.
    if (!matched_kanji && !matched_reading && !matched_renyokei) {
      std::string kanji_potential = generateGodanPotential(v2_surface, "", v2_verb.verb_type);
      std::string hira_potential = generateGodanPotential(v2_reading, "", v2_verb.verb_type);
      if (!kanji_potential.empty() && v2_start_byte + kanji_potential.size() <= text.size() &&
          text.substr(v2_start_byte, kanji_potential.size()) == kanji_potential) {
        matched_potential = true;
        matched_len = kanji_potential.size();
      } else if (!hira_potential.empty() && v2_start_byte + hira_potential.size() <= text.size() &&
                 text.substr(v2_start_byte, hira_potential.size()) == hira_potential) {
        matched_potential = true;
        matched_len = hira_potential.size();
        matched_renyokei_via_reading = true;
      }

      // Potential forms conjugate as Ichidan. Expose their stem before a
      // negative auxiliary so compound boundaries survive 取れ+ない/なかっ/なけれ.
      auto tryPotentialStem = [&](const std::string& potential, bool via_reading) {
        if (matched_potential || potential.size() <= core::kJapaneseCharBytes) {
          return;
        }
        std::string stem = potential.substr(0, potential.size() - core::kJapaneseCharBytes);
        size_t after_stem = v2_start_byte + stem.size();
        if (text.substr(v2_start_byte, stem.size()) != stem || after_stem >= text.size()) {
          return;
        }
        std::string_view following = text.substr(after_stem);
        if (utf8::startsWithAny(following, {"ない", "なかっ", "なけれ"})) {
          matched_potential = true;
          matched_len = stem.size();
          matched_renyokei_via_reading = via_reading;
        }
      };
      tryPotentialStem(kanji_potential, false);
      tryPotentialStem(hira_potential, true);
    }

    // A Godan V2 forms its conditional from the e-row stem plus ば
    // (踏み外せ+ば, 行き違え+ば).  The e-row surface is also the stem of a
    // potential verb, so require the following ば before treating it as
    // kateikei; unrestricted matching would incorrectly absorb an independent
    // potential predicate.
    if (!matched_kanji && !matched_reading && !matched_renyokei && !matched_potential &&
        v2_verb.verb_type == V2VerbType::Godan) {
      const std::string kanji_kateikei = generateKateikei(v2_surface, "", v2_verb.verb_type);
      const std::string hira_kateikei = !v2_reading.empty() ? generateKateikei(v2_reading, "", v2_verb.verb_type) : "";
      auto tryKateikei = [&](const std::string& kateikei, bool via_reading) {
        if (matched_kateikei || kateikei.empty() ||
            v2_start_byte + kateikei.size() + core::kJapaneseCharBytes > text.size()) {
          return;
        }
        const size_t after_kateikei = v2_start_byte + kateikei.size();
        if (text.substr(v2_start_byte, kateikei.size()) == kateikei &&
            text.substr(after_kateikei, core::kJapaneseCharBytes) == "ば") {
          matched_kateikei = true;
          matched_len = kateikei.size();
          matched_renyokei_via_reading = via_reading;
        }
      };
      tryKateikei(kanji_kateikei, false);
      tryKateikei(hira_kateikei, true);
    }

    // Keep a Godan mizenkei before its auxiliary separate.  Otherwise an
    // inflection match over the longer span (しきらない) would hide the
    // grammatical boundary that the mizenkei candidate below represents.
    const std::string kanji_mizen = generateMizenkei(v2_surface, "", v2_verb.verb_type);
    const std::string hira_mizen = !v2_reading.empty() ? generateMizenkei(v2_reading, "", v2_verb.verb_type) : "";
    const bool mizenkei_before_aux = beginsMizenkeiAuxiliary(text, v2_start_byte, kanji_mizen) ||
                                     beginsMizenkeiAuxiliary(text, v2_start_byte, hira_mizen);

    // Try inflection analysis for inflected V2 forms (e.g., きった, 込んだ, 巡った)
    // Only for base forms (not renyokei entries) to avoid double-matching
    // Skip if already matched via renyokei to prevent aux detection overriding renyokei match
    if (!matched_kanji && !matched_reading && !matched_renyokei && !matched_potential && !matched_kateikei &&
        !mizenkei_before_aux && !v2_reading.empty()) {
      std::string_view base_ending(v2_verb.base_ending);
      // Only try inflection for base forms (ending in る/す/く/う/む/つ/ぶ/ぐ/ぬ or ichidan endings)
      if (utf8::equalsAny(base_ending, {"る", "す", "く", "う", "む", "つ", "ぶ", "ぐ", "ぬ", "める", "ける", "れる",
                                        "える", "げる", "てる", "せる", "ちる"})) {
        // Case 1: Hiragana V2 inflected forms (e.g., きった from きる, かった from かう)
        // Try different lengths for V2 inflected form (shortest match first)
        for (size_t v2_end = v2_start + 2; v2_end <= v2_hiragana_end; ++v2_end) {
          size_t v2_end_byte = byteOffsetAt(byte_offsets, v2_end);
          std::string v2_text(text.substr(v2_start_byte, v2_end_byte - v2_start_byte));

          // Use analyze() to get all candidates, not just the best one.
          // This is needed because for ambiguous stems (e.g., かった could be
          // from かる, かつ, or かう), we need to find the one matching our V2.
          const auto& infl_results = inflection.analyze(v2_text);
          std::string expected_base = std::string(v2_reading);

          for (const auto& infl_result : infl_results) {
            // Check if this matches the V2 base form (using reading for comparison)
            // Use 0.3 threshold for inflected forms since short stems get lower confidence
            // Require the suffix to contain actual auxiliary patterns (た/て/etc.),
            // not just renyokei endings (し/み/etc.) to ensure complete inflected form
            //
            // Verify verb type consistency: if V2 is godan, reject ichidan
            // inflection matches (and vice versa). This prevents e.g. いた
            // (ichidan いる ta-form) from falsely matching godan 入る(いる).
            if (infl_result.confidence >= 0.3F && infl_result.base_form == expected_base &&
                hasAuxiliarySuffix(infl_result.suffix) &&
                !(v2_verb.verb_type == V2VerbType::Godan && infl_result.verb_type == grammar::VerbType::Ichidan) &&
                !(v2_verb.verb_type == V2VerbType::Ichidan && infl_result.verb_type != grammar::VerbType::Ichidan)) {
              matched_inflected = true;
              matched_len = v2_end_byte - v2_start_byte;
              inflection_includes_aux = true;  // Mark that this match includes aux
              break;
            }
          }
          if (matched_inflected)
            break;
        }

        // Case 2: Kanji V2 inflected forms (e.g., 巡った from 巡る)
        // Check if text starts with V2 kanji prefix, then analyze hiragana suffix
        if (!matched_inflected && char_types[v2_start] == CharType::Kanji) {
          // Extract kanji prefix from V2 surface (e.g., "巡" from "巡る")
          auto v2_surface_decoded = normalize::utf8::decode(v2_surface);
          size_t kanji_prefix_len = 0;
          for (size_t idx = 0; idx < v2_surface_decoded.size(); ++idx) {
            char32_t c = v2_surface_decoded[idx];
            if (kana::isKanjiCodepoint(c)) {
              ++kanji_prefix_len;
            } else {
              break;
            }
          }

          if (kanji_prefix_len > 0 && kanji_prefix_len < v2_surface_decoded.size()) {
            // Check if text at v2_start matches the kanji prefix
            // (kanji prefixes here are all 3-byte CJK codepoints).
            size_t kanji_prefix_byte_len = kanji_prefix_len * core::kJapaneseCharBytes;

            if (v2_start_byte + kanji_prefix_byte_len <= text.size()) {
              std::string_view text_kanji_prefix = text.substr(v2_start_byte, kanji_prefix_byte_len);
              std::string v2_kanji_prefix = normalize::utf8::encode(
                  std::vector<char32_t>(v2_surface_decoded.begin(), v2_surface_decoded.begin() + kanji_prefix_len));

              if (text_kanji_prefix == v2_kanji_prefix) {
                // Find the hiragana suffix after the kanji prefix
                size_t hira_start = v2_start + kanji_prefix_len;
                if (hira_start < codepoints.size() && char_types[hira_start] == CharType::Hiragana) {
                  size_t hira_end = findCharRegionEnd(char_types, hira_start, 6, CharType::Hiragana);

                  // Try inflection on kanji+hiragana portion (shortest match first)
                  for (size_t v2_end = hira_start + 1; v2_end <= hira_end; ++v2_end) {
                    size_t v2_end_byte = byteOffsetAt(byte_offsets, v2_end);
                    std::string v2_text(text.substr(v2_start_byte, v2_end_byte - v2_start_byte));

                    // Use analyze() to search all candidates for matching base form
                    const auto& infl_results = inflection.analyze(v2_text);
                    for (const auto& infl_result : infl_results) {
                      // Check if base form matches V2 surface (kanji form)
                      // Require the suffix to contain actual auxiliary patterns
                      if (infl_result.confidence >= 0.35F && infl_result.base_form == v2_surface &&
                          hasAuxiliarySuffix(infl_result.suffix)) {
                        matched_inflected = true;
                        matched_len = v2_end_byte - v2_start_byte;
                        inflection_includes_aux = true;  // Mark that this match includes aux
                        break;
                      }
                    }
                    if (matched_inflected)
                      break;
                  }
                }
              }
            }
          }
        }
      }
    }

    // Case 3: V2 mizenkei form match before passive, causative, or negative auxiliaries.
    // E.g., 打ち込まれ, 取り込ませ, 見当たらない.
    bool matched_mizenkei = false;
    if (!matched_kanji && !matched_reading && !matched_renyokei && !matched_potential && !matched_kateikei &&
        !matched_inflected) {
      auto tryMizenMatch = [&](const std::string& mizen) -> bool {
        return beginsMizenkeiAuxiliary(text, v2_start_byte, mizen);
      };

      if (tryMizenMatch(kanji_mizen)) {
        matched_mizenkei = true;
        matched_len = kanji_mizen.size();
      } else if (tryMizenMatch(hira_mizen)) {
        matched_mizenkei = true;
        matched_len = hira_mizen.size();
      }
    }

    if (!matched_kanji && !matched_reading && !matched_renyokei && !matched_potential && !matched_kateikei &&
        !matched_inflected && !matched_mizenkei) {
      continue;
    }

    // Do not let a kanji V2 ending in 「く」consume the first mora of the
    // polite request auxiliary 「ください」.  In ご理解ください, for example,
    // 解く must not create the spurious compound candidate 理解く; the
    // remaining ださい is not a valid auxiliary boundary.  The check is
    // surface-independent and leaves real V2 compounds untouched.
    if (matched_len >= core::kJapaneseCharBytes && v2_start_byte + matched_len < text.size() &&
        text[v2_start_byte + matched_len - core::kJapaneseCharBytes] == '\xE3' &&
        text.substr(v2_start_byte + matched_len - core::kJapaneseCharBytes, core::kJapaneseCharBytes) == "く" &&
        utf8::startsWith(text.substr(v2_start_byte + matched_len), "ださい")) {
      continue;
    }

    SUZUME_DEBUG_LOG_VERBOSE("[COMPOUND] V2 matched: "
                             << v2_verb.surface << " kanji=" << matched_kanji << " reading=" << matched_reading
                             << " renyokei=" << matched_renyokei << " potential=" << matched_potential
                             << " kateikei=" << matched_kateikei << " inflected=" << matched_inflected
                             << " mizenkei=" << matched_mizenkei << " len=" << matched_len << "\n");

    // Build the V1 base form for verification
    std::string v1_base;
    size_t v1_end_byte = is_ichidan ? v2_start_byte : byteOffsetAt(byte_offsets, kanji_end);
    // Check if V1 base form is in dictionary
    bool v1_verified = dict_compound_v1;
    bool v1_dict_verified = dict_compound_v1;  // tracks dict verification for cost calculation
    bool v1_embedded_verified = false;         // tracks embedded dict verb verification for cost calculation
    bool v1_ichidan_inflection = false;        // single-kanji ichidan V1 confirmed only by inflection
    bool v1_godan_inflection = false;          // single-kanji godan V1 confirmed by exact inflection
    if (dict_compound_v1) {
      // Already resolved: V1 is the dict-verified compound verb (引きずる).
      v1_base = dict_compound_v1_lemma;
    } else {
      v1_base = std::string(text.substr(start_byte, v1_end_byte - start_byte));

      if (is_sokuonbin) {
        // Sokuonbin: try く/つ/う/る endings to find dictionary match
        // E.g., 突 + く = 突く, 打 + つ = 打つ
        // Leave base_ending = 0 for now, will be set if match found
      } else if (!is_ichidan) {
        v1_base += normalize::encodeUtf8(base_ending);
      } else {
        v1_base += "る";
      }

      if (is_sokuonbin) {
        // Try all sokuonbin-compatible godan endings
        for (char32_t ending : kSokuonbinEndings) {
          std::string candidate = v1_base + normalize::encodeUtf8(ending);
          if (dict_manager.lookupExact(candidate, core::PartOfSpeech::Verb) != nullptr) {
            v1_verified = true;
            v1_dict_verified = true;
            v1_base = candidate;
            base_ending = ending;
            break;
          }
        }
      } else {
        if (dict_manager.lookupExact(v1_base, core::PartOfSpeech::Verb) != nullptr) {
          v1_verified = true;
          v1_dict_verified = true;
        }
      }
    }

    // A kanji-led V1 can have more than one kana before its continuative
    // ending (混じり+合う). The first kana may look like an Ichidan stem, but
    // the complete span can instead prove a Godan continuative. Preserve the
    // Ichidan reading unless inflection recognizes the whole span as Godan
    // and its final kana is that row's continuative form.
    if (!v1_verified && !dict_compound_v1 && is_ichidan && v2_start > kanji_end + 1) {
      const std::string v1_renyokei(text.substr(start_byte, v2_start_byte - start_byte));
      const auto inflection_candidate = inflection.getBest(v1_renyokei);
      const auto* godan_row = grammar::Conjugation::getGodanRow(inflection_candidate.verb_type);
      if (godan_row != nullptr &&
          inflection_candidate.confidence >= candidate::verb_cost::kConstructedVerbMinConfidence &&
          codepoints[v2_start - 1] == godan_row->i_row) {
        v1_base = inflection_candidate.base_form;
        v1_verified = true;
        v1_godan_inflection = true;
      }
    }

    SUZUME_DEBUG_LOG_VERBOSE("[COMPOUND] V1 base=" << v1_base << " verified=" << v1_verified
                                                   << " sokuonbin=" << is_sokuonbin << "\n");

    // Fallback: use inflection analysis for unknown V1 verbs
    // This allows compound verbs like 読み込む where 読む is not in dictionary
    // but is recognizable as a verb by inflection patterns
    if (!v1_verified) {
      size_t kanji_count = has_kanji_v2_after_bare_ichidan ? 1 : kanji_end - start_pos;

      // For sokuonbin with single-kanji V1 (e.g., 引っ+張る, 突っ+込む):
      // Accept without full verification. The combination of single kanji +
      // っ (sokuonbin marker) + verified V2 is strong evidence of a compound verb.
      // False positives are prevented by V2 matching (V2 must follow っ).
      if (is_sokuonbin && kanji_count == 1) {
        v1_verified = true;
        // Try to determine V1 base form for compound lemma
        for (char32_t ending : kSokuonbinEndings) {
          std::string candidate = v1_base + normalize::encodeUtf8(ending);
          if (dict_manager.lookupExact(candidate) != nullptr) {
            v1_base = candidate;
            base_ending = ending;
            break;
          }
        }
      }

      // For multi-kanji ichidan V1 stems, accept when stripping the leading
      // kanji yields a dictionary verb (e.g., 仕立てる = 仕 + 立てる).
      // Lexicalized prefix+verb compounds are often absent from the dictionary
      // as a whole, but an embedded dictionary verb combined with a verified
      // V2 is strong evidence that the V1 is a real verb rather than a noun.
      if (!v1_verified && is_ichidan && kanji_count >= 2) {
        size_t v1_second_char_byte = byteOffsetAt(byte_offsets, start_pos + 1);
        std::string embedded_base(text.substr(v1_second_char_byte, v1_end_byte - v1_second_char_byte));
        embedded_base += "る";
        if (dict_manager.lookupExact(embedded_base, core::PartOfSpeech::Verb) != nullptr) {
          v1_verified = true;
          v1_embedded_verified = true;
          SUZUME_DEBUG_LOG_VERBOSE("[COMPOUND] V1 verified via embedded dict verb \"" << embedded_base << "\"\n");
        }
      }

      bool use_inflection_fallback = !v1_verified;

      // B65: For multi-kanji stems (2+ kanji), require dictionary match.
      // This prevents spurious compound verbs like 大体分交う where 大体分 is
      // incorrectly analyzed as a verb stem. The inflection analyzer is too lenient
      // for long kanji sequences, accepting them with low confidence.
      // Single-kanji stems like 見 (from 見つける) are more likely to be real verbs.
      if (use_inflection_fallback && kanji_count >= 2) {
        // Multi-kanji stem: don't use inflection fallback
        use_inflection_fallback = false;
      }

      // Special case: single-kanji + に patterns
      // に is both a common particle and the renyokei of Godan-Na verbs (死に→死ぬ).
      // But Godan-Na verbs are rare, while kanji+に+VERB is a very common pattern
      // (e.g., 本について = 本 + に + ついて, not 本ぬ compound).
      // Block inflection fallback for single-kanji + に to prevent false positives.
      char32_t renyokei_char = codepoints[kanji_end];
      if (!is_ichidan && kanji_count == 1 && renyokei_char == U'に') {
        use_inflection_fallback = false;
      }

      // Check if V1 renyokei is known as a non-verb (noun, adjective, etc.)
      // If so, don't form compound verb. E.g., 好き is ADJ, not verb renyokei of 好く.
      if (use_inflection_fallback) {
        size_t v1_renyokei_end = is_ichidan ? v2_start_byte : byteOffsetAt(byte_offsets, kanji_end + 1);
        std::string v1_renyokei(text.substr(start_byte, v1_renyokei_end - start_byte));
        if (verb_helpers::hasNonVerbDictionaryEntry(&dict_manager, v1_renyokei)) {
          // A single-kanji Godan continuative can be both a nominalized word
          // and the productive first half of a compound verb (思い+出す,
          // 問い+合わせる). Keep the verbal analysis only when inflection
          // reconstructs the exact Godan base; unrelated noun homographs
          // remain blocked.
          const auto inflection_candidate = inflection.getBest(v1_renyokei);
          const bool productive_godan_compound =
              !is_ichidan && kanji_count == 1 &&
              inflection_candidate.confidence >= candidate::verb_cost::kConstructedVerbMinConfidence &&
              inflection_candidate.base_form == v1_base;
          if (!productive_godan_compound) {
            use_inflection_fallback = false;
          }
        }
      }

      // A single-kanji ichidan stem followed by a verified compound V2 is a
      // productive compound-verb shape.  Unlike a free-form inflection guess,
      // the e/i-row stem, the absence of a competing non-verb entry, and the
      // V2 constraint together identify the boundary (受け取る, 見上げる).
      bool starts_inside_formal_noun = false;
      if (use_inflection_fallback && is_ichidan && kanji_count == 1 && start_pos > 0) {
        const std::string enclosing_surface = extractSubstring(codepoints, start_pos - 1, start_pos + 1);
        const auto* enclosing_entry = dict_manager.lookupExact(enclosing_surface, core::PartOfSpeech::Noun);
        starts_inside_formal_noun =
            enclosing_entry != nullptr && enclosing_entry->extended_pos == core::ExtendedPOS::NounFormal;
      }
      if (use_inflection_fallback && is_ichidan && kanji_count == 1) {
        // A non-dictionary kanji+で stem is overwhelmingly a nominal copula
        // (本であった, 事実である), not an open-class ichidan verb before
        // the hiragana spelling of the V2 合う.  Dictionary-verified verbs
        // such as 撫でる have already bypassed this fallback above, so this
        // guard preserves real lexical compounds while preventing a copular
        // predicate from being fabricated as *本であう.
        // Likewise, ん is a Godan hatsuonbin marker, never an Ichidan
        // renyokei. A following compound V2 must not turn 読んで+あげられる
        // into the fabricated lexical verb 読んであげる.
        const bool bare_ichidan_stem = v2_start == kanji_end;
        if (bare_ichidan_stem && !verb_helpers::isSingleKanjiIchidan(codepoints[start_pos])) {
          // A bare single-kanji Ichidan stem is a closed class. Without an
          // e/i-row stem vowel, arbitrary kanji must not be promoted to one.
          use_inflection_fallback = false;
        } else if (renyokei_char == U'で' || renyokei_char == U'ん') {
          use_inflection_fallback = false;
        } else if (starts_inside_formal_noun) {
          use_inflection_fallback = false;
        } else {
          v1_verified = true;
          v1_ichidan_inflection = true;
          use_inflection_fallback = false;
        }
      }

      // A single-kanji Godan renyokei before a verified V2 is likewise a
      // productive compound shape (押し込む、読み返す), provided inflection
      // analysis reconstructs exactly the V1 base. This keeps open-class V1
      // verbs out of the dictionary while retaining a grammatical boundary.
      if (use_inflection_fallback && !is_ichidan && kanji_count == 1) {
        size_t v1_renyokei_end = byteOffsetAt(byte_offsets, kanji_end + 1);
        std::string v1_renyokei(text.substr(start_byte, v1_renyokei_end - start_byte));
        if (hasInflectionCandidateForBase(inflection, v1_renyokei, v1_base,
                                          candidate::verb_cost::kConstructedVerbMinConfidence)) {
          v1_verified = true;
          v1_godan_inflection = true;
          use_inflection_fallback = false;
        }
      }

      if (use_inflection_fallback) {
        size_t v1_renyokei_end = is_ichidan ? v2_start_byte : byteOffsetAt(byte_offsets, kanji_end + 1);
        std::string v1_renyokei(text.substr(start_byte, v1_renyokei_end - start_byte));
        // A particle at the end of the proposed V1 marks a grammatical boundary,
        // not a compound-verb stem (読む + だけ + あっ + て).
        for (size_t split = core::kJapaneseCharBytes; split < v1_renyokei.size(); split += core::kJapaneseCharBytes) {
          std::string_view suffix(v1_renyokei.data() + split, v1_renyokei.size() - split);
          if (dict_manager.lookupExact(suffix, core::PartOfSpeech::Particle) != nullptr) {
            use_inflection_fallback = false;
            break;
          }
        }
      }

      if (use_inflection_fallback) {
        // Get V1 renyokei form for inflection analysis
        size_t v1_renyokei_end = is_ichidan ? v2_start_byte : byteOffsetAt(byte_offsets, kanji_end + 1);
        std::string v1_renyokei(text.substr(start_byte, v1_renyokei_end - start_byte));

        auto infl_result = inflection.getBest(v1_renyokei);

        // Accept if inflection analysis identifies it as a verb with reasonable confidence
        // and the base form matches our constructed v1_base
        // B63: For ichidan verbs in compound verb context, use lower threshold (0.25)
        // because ichidan patterns get penalized by inflection analyzer's potential/godan ambiguity,
        // but the compound verb context (kanji + e-row + known V2) strongly suggests ichidan verb
        float min_confidence = is_ichidan ? 0.25F : 0.5F;
        if (infl_result.confidence >= min_confidence) {
          if (infl_result.base_form == v1_base) {
            v1_verified = true;
            // A single-kanji ichidan V1 (受ける, 逃げる, 助ける) confirmed by
            // inflection is strong evidence of a real verb, on par with an
            // embedded dictionary verb: give it the reduced penalty rather than
            // the full inflection-only penalty so that its compounds (受け取っ+た)
            // beat a spurious three-way split (受け+取っ+た). These common ichidan
            // verbs are open-class and therefore absent from the dictionary.
            if (is_ichidan && kanji_count == 1) {
              v1_ichidan_inflection = true;
            }
          } else if (is_sokuonbin) {
            // For sokuonbin, v1_base is just the kanji stem (e.g., 引).
            // Inflection analysis of っ-form (e.g., 引っ) gives base_form
            // like 引く. Accept if it matches any sokuonbin candidate.
            for (char32_t ending : kSokuonbinEndings) {
              std::string candidate = v1_base + normalize::encodeUtf8(ending);
              if (infl_result.base_form == candidate) {
                v1_verified = true;
                v1_base = candidate;
                base_ending = ending;
                break;
              }
            }
          }
        }
      }
    }

    // Only generate compound verb candidates when V1 is a verified verb
    // This prevents false positives like 試験に落ちる (試験 is not a verb)
    if (!v1_verified) {
      continue;
    }

    // A compound may begin inside a preceding kanji noun only when its V1 is
    // independently dictionary-verified (蛙+飛び込む, 報告+申し上げる).  An
    // inflection-only V1 in that position can instead fabricate a compound
    // across the noun/verb boundary (生+涯忘れる).
    const bool starts_inside_kanji_run = start_pos > 0 && normalize::isKanjiCodepoint(codepoints[start_pos - 1]);
    if (starts_inside_kanji_run && !v1_dict_verified && !dict_compound_v1) {
      continue;
    }

    // A nominal base followed by an independently registered suffix form
    // (税+抜き, 水+抜き) is not evidence for a lexical compound verb.  The
    // productive single-kanji V1 fallback is deliberately dictionary-free,
    // so let the suffix analysis own this boundary unless V1 itself was
    // dictionary-verified.
    if (!v1_dict_verified && !dict_compound_v1 && !is_sokuonbin && kanji_end < codepoints.size()) {
      const std::string v2_form = extractSubstring(codepoints, v2_start, kanji_end + 1);
      if (dict_manager.lookupExact(v2_form, core::PartOfSpeech::Suffix) != nullptr ||
          dict_manager.lookupExact(v2_form, core::PartOfSpeech::Noun) != nullptr) {
        continue;
      }
    }

    // For inflected V2 matches (Case 1/2), check if the full surface could be
    // an adjective instead of a compound verb. This prevents false positives
    // like 美しかった (adjective) being parsed as 美し+交った (compound verb).
    if (matched_inflected && inflection_includes_aux) {
      // Calculate full compound surface
      size_t compound_end_byte = v2_start_byte + matched_len;
      std::string full_surface(text.substr(start_byte, compound_end_byte - start_byte));

      // Check if full surface could be an i-adjective
      auto full_infl = inflection.getBest(full_surface);
      if (full_infl.confidence >= 0.5F && full_infl.verb_type == grammar::VerbType::IAdjective) {
        // Full surface is likely an adjective, skip compound verb
        continue;
      }
    }

    // Build the compound base while preserving the V2 orthography supplied by
    // the input. A hiragana V2 is a deliberate spelling choice (読みかける),
    // not an instruction to normalize it to the table's kanji representative
    // (読み掛ける).
    std::string compound_base;
    size_t v1_renyokei_end = is_ichidan ? v2_start_byte : byteOffsetAt(byte_offsets, kanji_end + 1);
    compound_base = std::string(text.substr(start_byte, v1_renyokei_end - start_byte));
    const bool v2_is_hiragana = char_types[v2_start] == CharType::Hiragana;
    if (matched_potential) {
      std::string potential = generateGodanPotential(v2_surface, "", v2_verb.verb_type);
      compound_base +=
          v2_is_hiragana && !v2_reading.empty() ? generateGodanPotential(v2_reading, "", v2_verb.verb_type) : potential;
    } else {
      compound_base += v2_is_hiragana && !v2_reading.empty() ? v2_reading : v2_surface;
    }

    // Compare with best match and update if this is better
    // Priority:
    // 1. Longer renyokei match beats shorter (出し > 出)
    // 2. Renyokei exact match beats inflection match with aux
    // 3. Match without aux beats match with aux
    auto compoundIsAttested = [&](const std::string& base, V2VerbType verb_type) {
      if (base.empty()) {
        return false;
      }
      if (dict_manager.lookupExact(base, core::PartOfSpeech::Verb) != nullptr) {
        return true;
      }
      const std::string nominalized = generateRenyokei(base, "", verb_type);
      return !nominalized.empty() && dict_manager.lookupExact(nominalized, core::PartOfSpeech::Noun) != nullptr;
    };
    const bool current_compound_attested = compoundIsAttested(compound_base, v2_verb.verb_type);
    const bool best_compound_attested =
        best_match.v2_verb != nullptr && compoundIsAttested(best_match.compound_base, best_match.v2_verb->verb_type);
    bool should_update = false;
    if (best_match.matched_len == 0) {
      // First valid match
      should_update = true;
    } else if (current_compound_attested && !best_compound_attested) {
      // An attested full compound (降りしきる) must not lose to a shorter
      // overlapping V2 continuative (敷く → しき). Both readings are
      // grammatically possible locally, but only the full compound has
      // lexical evidence.
      should_update = true;
    } else if (matched_renyokei && best_match.is_renyokei && matched_len > best_match.matched_len) {
      // Longer renyokei match beats shorter renyokei match
      // This makes 出し (6 bytes) beat 出 (3 bytes) for V1+V2 compounds
      should_update = true;
    } else if (matched_renyokei && best_match.is_mizenkei && matched_len > best_match.matched_len) {
      // A longer ichidan continuative can overlap with the mizenkei of a
      // different V2 (組み合わせ vs. 組み合わ).  A deverbal suffix immediately
      // after it establishes the nominalized compound reading.
      const size_t renyokei_end_pos =
          advanceCharsToBytePos(codepoints, v2_start, v2_start_byte, v2_start_byte + matched_len);
      const bool followed_by_deverbal_suffix =
          renyokei_end_pos < codepoints.size() &&
          (codepoints[renyokei_end_pos] == U'方' || codepoints[renyokei_end_pos] == U'手' ||
           codepoints[renyokei_end_pos] == U'物' || codepoints[renyokei_end_pos] == U'所' ||
           codepoints[renyokei_end_pos] == U'場');
      const bool followed_by_ichidan_conditional =
          v2_verb.verb_type == V2VerbType::Ichidan && renyokei_end_pos + 1 < codepoints.size() &&
          codepoints[renyokei_end_pos] == U'れ' && codepoints[renyokei_end_pos + 1] == U'ば';
      if (followed_by_deverbal_suffix || (followed_by_ichidan_conditional && current_compound_attested)) {
        should_update = true;
      }
    } else if (is_renyokei_entry && (matched_kanji || matched_reading) && best_match.includes_aux &&
               !best_match.is_renyokei) {
      // Renyokei exact match beats inflection match that includes aux
      // This makes 食べすぎ (renyokei) beat 食べすぎた (inflection+aux)
      should_update = true;
    } else if (!inflection_includes_aux && best_match.includes_aux) {
      // Usually a lexical match without auxiliaries beats a candidate that
      // absorbed an auxiliary. The 合う+使役せる / 合わせる overlap is the
      // exception: preserve an attested V1+合う causative unless the competing
      // 合わせる compound (or its nominalized 連用形) is itself attested.
      should_update = !best_compound_attested || current_compound_attested;
    } else if ((matched_kanji || matched_reading) && best_match.is_potential) {
      // A lexical V2 base form (続ける) takes precedence over an overlapping
      // potential form generated from a different Godan V2 (続く→続ける).
      should_update = true;
    } else if (best_match.is_mizenkei && (matched_kanji || matched_reading)) {
      // A full V2 base-form match (組み合わせる via ichidan 合わせる) competes
      // with a shorter V2-mizenkei causative/passive reading of another table
      // entry (組み合わ + せる via godan 合う). Prefer the whole compound only
      // when the dictionary attests it as an established lexeme — as a verb, or
      // via its nominalized renyokei (組み合わせ, 問い合わせ are dict nouns).
      // Otherwise the mizenkei reading is the grammatically correct one
      // (話し合わせる = 話し合う + せる causative).
      std::string nominalized = generateRenyokei(compound_base, "", v2_verb.verb_type);
      if (dict_manager.lookupExact(compound_base, core::PartOfSpeech::Verb) != nullptr ||
          (!nominalized.empty() && dict_manager.lookupExact(nominalized, core::PartOfSpeech::Noun) != nullptr)) {
        should_update = true;
      }
    }

    if (should_update) {
      best_match.matched_len = matched_len;
      best_match.compound_base = compound_base;
      best_match.is_renyokei = is_renyokei_entry && (matched_kanji || matched_reading);
      best_match.renyokei_form = matched_renyokei;
      best_match.is_mizenkei = matched_mizenkei;
      best_match.is_kateikei = matched_kateikei;
      best_match.is_potential = matched_potential;
      best_match.includes_aux = inflection_includes_aux;
      best_match.matched_via_reading = matched_reading || matched_inflected || matched_renyokei_via_reading;
      best_match.v2_verb = &v2_verb;
      best_match.v1_dict_verified = v1_dict_verified;
      best_match.v1_embedded_verified = v1_embedded_verified;
      best_match.v1_ichidan_inflection = v1_ichidan_inflection;
      best_match.v1_bare_ichidan = has_kanji_v2_after_bare_ichidan && v1_ichidan_inflection;
      best_match.v1_godan_inflection = v1_godan_inflection;
    }
  }

  SUZUME_DEBUG_LOG_VERBOSE("[COMPOUND] best_match.len=" << best_match.matched_len
                                                        << " base=" << best_match.compound_base << "\n");

  return best_match;
}

}  // namespace suzume::analysis::compound_verb_detail
