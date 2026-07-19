/**
 * @file join_compound_verb_hiragana.cpp
 * @brief Hiragana compound-verb join candidate generation
 */

#include "join_compound_verb_internal.h"

namespace suzume::analysis {

using namespace compound_verb_detail;

void addHiraganaCompoundVerbJoinCandidates(core::Lattice& lattice, std::string_view text,
                                           const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                           size_t start_pos, const std::vector<normalize::CharType>& char_types,
                                           const dictionary::DictionaryManager& dict_manager, const Scorer& scorer,
                                           const grammar::Inflection& inflection) {
  if (start_pos >= char_types.size()) {
    return;
  }

  // Must start with hiragana (for all-hiragana compound verbs like やりなおす)
  if (char_types[start_pos] != CharType::Hiragana) {
    return;
  }

  if (verb_helpers::startsInsideDictionaryParticle(codepoints, start_pos, &dict_manager) ||
      verb_helpers::startsWithMultiMoraDictionaryParticle(codepoints, start_pos, &dict_manager)) {
    return;
  }

  // Do not reinterpret a closed particle plus an independently attested verb
  // as a hiragana V1+V2 compound (結果+と+ひきかえる). A whole dictionary verb
  // at the same position still wins this ambiguity (できる, not で+きる).
  size_t hiragana_end = start_pos;
  while (hiragana_end < char_types.size() && char_types[hiragana_end] == CharType::Hiragana) {
    ++hiragana_end;
  }
  if (start_pos + 2 < hiragana_end) {
    const std::string leading = extractSubstring(codepoints, start_pos, start_pos + 1);
    const auto* particle = dict_manager.lookupExact(leading, core::PartOfSpeech::Particle);
    const std::string whole = extractSubstring(codepoints, start_pos, hiragana_end);
    const std::string remainder = extractSubstring(codepoints, start_pos + 1, hiragana_end);
    if (particle != nullptr && particle->extended_pos != core::ExtendedPOS::ParticleFinal &&
        dict_manager.lookupExact(whole, core::PartOfSpeech::Verb) == nullptr &&
        dict_manager.lookupExact(remainder, core::PartOfSpeech::Verb) != nullptr) {
      return;
    }
  }

  // Get byte position for start
  size_t start_byte = byteOffsetAt(byte_offsets, start_pos);

  // For each V2 subsidiary verb, check if it appears after a potential V1
  for (const auto& v2_verb : kSubsidiaryVerbs) {
    if (!v2_verb.joins_general) {
      continue;
    }
    // Only consider V2 with readings (hiragana patterns)
    if (v2_verb.reading == nullptr) {
      continue;
    }
    std::string_view v2_reading(v2_verb.reading);

    // Productive hiragana V1 forms need at least two characters. The irregular
    // サ変 renyokei し is the sole one-character exception (し続ける).
    const size_t min_v1_len = codepoints[start_pos] == U'し' ? 1 : 2;
    for (size_t v1_len = min_v1_len; v1_len <= 4; ++v1_len) {
      size_t v2_start = start_pos + v1_len;
      if (v2_start >= codepoints.size()) {
        break;
      }

      // All characters in V1 must be hiragana
      bool all_hiragana = true;
      for (size_t idx = start_pos; idx < v2_start; ++idx) {
        if (char_types[idx] != CharType::Hiragana) {
          all_hiragana = false;
          break;
        }
      }
      if (!all_hiragana) {
        continue;
      }

      size_t v2_start_byte = byteOffsetAt(byte_offsets, v2_start);

      // Check if V2 reading (hiragana) or surface (kanji) matches at v2_start
      std::string_view v2_surface(v2_verb.surface);
      size_t matched_v2_len = 0;
      bool matched_v2_renyokei = false;
      bool matched_v2_via_reading = false;

      // Try hiragana reading match first
      if (v2_start_byte + v2_reading.size() <= text.size()) {
        std::string_view text_at_v2 = text.substr(v2_start_byte, v2_reading.size());
        if (text_at_v2 == v2_reading) {
          matched_v2_len = v2_reading.size();
          matched_v2_via_reading = true;
        }
      }

      // Try kanji surface match if hiragana didn't match
      // This handles patterns like やり + 直す (hiragana V1 + kanji V2)
      if (matched_v2_len == 0 && v2_start_byte + v2_surface.size() <= text.size()) {
        std::string_view text_at_v2 = text.substr(v2_start_byte, v2_surface.size());
        if (text_at_v2 == v2_surface) {
          matched_v2_len = v2_surface.size();
        }
      }

      // Try V2 renyokei match (e.g., あげ from あげる for とりあげない)
      if (matched_v2_len == 0) {
        std::string v2_renyokei = generateRenyokei(v2_surface, v2_reading, v2_verb.verb_type);
        // Require V2 renyokei to be 2+ chars to avoid false matches
        // (single-char で/し/き are ambiguous as particles/auxiliaries)
        if (v2_renyokei.size() > core::kJapaneseCharBytes && v2_start_byte + v2_renyokei.size() <= text.size()) {
          std::string_view text_at_v2 = text.substr(v2_start_byte, v2_renyokei.size());
          if (text_at_v2 == v2_renyokei) {
            matched_v2_len = v2_renyokei.size();
            matched_v2_renyokei = true;
            matched_v2_via_reading = true;
          }
        }
      }

      // Try V2 te-form euphonic stem match (e.g., こもっ from こもる for とじこもって).
      // Restricted to an exact match against the promised euphonic stem, followed by
      // て/た (or で/だ for voiced onbin), so that arbitrary inflected forms cannot
      // over-join. Godan only: an ichidan te-stem equals its renyokei (matched above),
      // and the single-char で stem of 出る is particle-ambiguous.
      bool matched_v2_te_stem = false;
      if (matched_v2_len == 0 && v2_verb.verb_type == V2VerbType::Godan) {
        auto [v2_te_stem, v2_te_uses_de] =
            generateTeFormStem(v2_surface, v2_reading, v2_verb.verb_type, v2_verb.base_ending);
        if (v2_te_stem.size() > core::kJapaneseCharBytes &&
            v2_start_byte + v2_te_stem.size() + core::kJapaneseCharBytes <= text.size() &&
            text.substr(v2_start_byte, v2_te_stem.size()) == v2_te_stem) {
          std::string_view next_char = text.substr(v2_start_byte + v2_te_stem.size(), core::kJapaneseCharBytes);
          bool next_matches_te_form =
              v2_te_uses_de ? (next_char == "で" || next_char == "だ") : (next_char == "て" || next_char == "た");
          if (next_matches_te_form) {
            matched_v2_len = v2_te_stem.size();
            matched_v2_te_stem = true;
            matched_v2_via_reading = true;
          }
        }
      }

      if (matched_v2_len == 0) {
        continue;
      }

      // Extract V1 portion and determine its base form
      std::string_view v1_surface = text.substr(start_byte, v2_start_byte - start_byte);

      // Skip V1 starting with case/binding particles (not や/か/と which can be verb stems)
      // E.g., をかきたてる should be を + かきたてる, not をかく + 立てる
      // But やり直す (やる), かき回す (かく), とりあげる (とる) should match
      // Note: と excluded from filter because とる is a common V1 verb,
      // and V1 minimum length of 2 chars prevents particle と (1 char) from matching
      char32_t first_char = codepoints[start_pos];
      if (first_char == U'を' || first_char == U'が' || first_char == U'は' || first_char == U'に' ||
          first_char == U'で' || first_char == U'へ' || first_char == U'の' || first_char == U'も') {
        continue;
      }

      // Get the last character of V1 to determine verb type
      char32_t last_char = codepoints[v2_start - 1];

      // Check if it's a valid renyokei ending
      char32_t base_ending = godanRenyokeiBaseCp(last_char);

      // Build V1 base form
      std::string v1_base;
      bool is_ichidan = (base_ending == 0);

      if (v1_len == 1 && last_char == U'し') {
        v1_base = "する";
      } else if (!is_ichidan) {
        // Godan: replace last char with base ending
        v1_base =
            std::string(v1_surface.substr(0, v1_surface.size() - core::kJapaneseCharBytes));  // Remove last hiragana
        v1_base += normalize::encodeUtf8(base_ending);
      } else {
        // Ichidan: add る
        v1_base = std::string(v1_surface) + "る";
      }

      // An e-row surface is shared by an Ichidan renyokei and a Godan
      // kateikei.  Immediately after the topic particle は, a dictionary-
      // verified Godan kateikei introduces an independent predicate and must
      // not be absorbed into a following compound V2 (とは+いえ+続ける).
      // Outside that grammatical context, keep genuinely ambiguous/productive
      // compounds available.
      if (is_ichidan && grammar::isERowCodepoint(last_char)) {
        const std::string_view godan_suffix = grammar::godanBaseSuffixFromERow(last_char);
        if (!godan_suffix.empty()) {
          std::string godan_base(v1_surface.substr(0, v1_surface.size() - core::kJapaneseCharBytes));
          godan_base += godan_suffix;
          const bool has_godan_base = dict_manager.lookupExact(godan_base, core::PartOfSpeech::Verb) != nullptr;
          const bool follows_topic = start_pos > 0 && codepoints[start_pos - 1] == U'は';
          if (has_godan_base && follows_topic) {
            continue;
          }
        }
      }

      // A closed-class particle is never the first verb in a compound. This
      // must be checked before the inflection fallback: particle surfaces can
      // otherwise receive a mechanically plausible unknown Godan reading
      // (しか+い → しかい) and swallow a following auxiliary.
      if (!grammar::isSuruRenyokeiSurface(v1_surface) &&
          dict_manager.lookupExact(v1_surface, core::PartOfSpeech::Particle) != nullptr) {
        continue;
      }

      // Verify V1 is in dictionary as a verb
      bool v1_verified = grammar::isSuruRenyokeiSurface(v1_surface) ||
                         dict_manager.lookupExact(v1_base, core::PartOfSpeech::Verb) != nullptr;

      // Fallback: use inflection analysis for unknown V1 verbs
      if (!v1_verified) {
        auto infl_result = inflection.getBest(std::string(v1_surface));

        if (infl_result.confidence >= 0.5F && infl_result.base_form == v1_base) {
          v1_verified = true;
        }
      }

      if (!v1_verified) {
        continue;  // V1 must be a known verb for hiragana compounds
      }

      // Calculate compound verb end position
      size_t compound_end_byte = v2_start_byte + matched_v2_len;

      // Find character position for compound end
      size_t compound_end_pos = advanceCharsToBytePos(codepoints, v2_start, v2_start_byte, compound_end_byte);

      // Build compound verb surface and base form
      std::string compound_surface(text.substr(start_byte, compound_end_byte - start_byte));

      // A 〜かっ te-stem (V2 = かう/かつ/かる) whose full 〜かった form is a confident
      // i-adjective past is really an adjective, not a hiragana compound: うれしかっ is
      // うれしい past (V1 うれし only "verifies" as a verb via the non-word godan reading
      // うれす). Suppress the spurious compound so the i-adjective candidate wins.
      if (utf8::endsWith(compound_surface, "かっ")) {
        auto adj_past = inflection.getBest(compound_surface + "た");
        if (adj_past.verb_type == grammar::VerbType::IAdjective && adj_past.confidence >= candidate::kIAdjConfMin) {
          continue;
        }
      }

      // Compound base (lemma) = V1 renyokei + V2 in appropriate form
      // When V2 matched via kanji surface: use kanji (やり + 直す = やり直す)
      // When V2 matched via hiragana reading: use hiragana (やり + なおす = やりなおす)
      std::string compound_base =
          std::string(v1_surface) + std::string(matched_v2_via_reading ? v2_reading : v2_surface);

      // A lexical non-verb at the complete surface outranks an unverified
      // productive compound reading. This prevents a fixed adverb such as
      // とりわけ from being fabricated as a continuative verb; attested
      // compound verbs retain their candidate through the verified lemma.
      const bool compound_lemma_verified = dict_manager.lookupExact(compound_base, core::PartOfSpeech::Verb) != nullptr;
      if (!compound_lemma_verified && verb_helpers::hasNonVerbDictionaryEntry(&dict_manager, compound_surface)) {
        continue;
      }

      // Calculate cost
      float base_cost = scorer.posPrior(core::PartOfSpeech::Verb);
      const auto& opts = scorer.joinOpts();
      float final_cost = base_cost + opts.compound_verb_bonus + opts.verified_v1_bonus;

      // An attested compound is a lexical search unit.  Apply the same
      // preference used for kanji compounds so a verified compound stem wins
      // over an accidental V1 + subsidiary-verb analysis.
      if (compound_lemma_verified) {
        final_cost += bigram_cost::kStrongBonus;
      }

      uint8_t flags = core::LatticeEdge::kFromDictionary;
      dictionary::ConjugationType compound_conj_type = compoundConjugationType(v2_verb.verb_type, v2_verb.base_ending);

      if (matched_v2_te_stem) {
        // V2 matched via te-form euphonic stem — the compound surface is the
        // te-stem itself (とじこもっ before て), so tag it with the onbin EPOS
        // (renyokei for し-stems) mirroring the kanji compound te-stem edge.
        core::ExtendedPOS te_stem_epos = getTeFormType(v2_verb.base_ending) == TeFormType::Renyokei
                                             ? core::ExtendedPOS::VerbRenyokei
                                             : core::ExtendedPOS::VerbOnbinkei;
        lattice.addEdge(compound_surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(compound_end_pos),
                        core::PartOfSpeech::Verb, final_cost, flags, compound_base, compound_conj_type,
                        core::CandidateOrigin::VerbCompound, candidate::kNoOriginConfidence, "hira_compound_te_stem",
                        te_stem_epos, "hira_compound_te_stem");
      } else if (matched_v2_renyokei) {
        // V2 matched in renyokei form — add compound renyokei candidate
        // e.g., とりあげ (from とりあげる) for とりあげない
        lattice.addEdge(compound_surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(compound_end_pos),
                        core::PartOfSpeech::Verb, final_cost, flags, compound_base, compound_conj_type,
                        core::CandidateOrigin::Unknown, 0.0F, "hira_compound_renyokei", core::ExtendedPOS::VerbRenyokei,
                        "hira_compound_renyokei");
      } else {
        lattice.addEdge(compound_surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(compound_end_pos),
                        core::PartOfSpeech::Verb, final_cost, flags, compound_base, compound_conj_type);
      }

      return;  // Found a match, stop searching
    }
  }
}

}  // namespace suzume::analysis
