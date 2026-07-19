/**
 * @file join_compound_verb.cpp
 * @brief Kanji-led compound-verb join candidate generation
 */

#include "join_compound_verb_internal.h"

namespace suzume::analysis::compound_verb_detail {

dictionary::ConjugationType compoundConjugationType(V2VerbType verb_type, std::string_view base_ending) {
  if (verb_type == V2VerbType::Ichidan) {
    return dictionary::ConjugationType::Ichidan;
  }
  if (base_ending == "く")
    return dictionary::ConjugationType::GodanKa;
  if (base_ending == "ぐ")
    return dictionary::ConjugationType::GodanGa;
  if (base_ending == "す")
    return dictionary::ConjugationType::GodanSa;
  if (base_ending == "つ")
    return dictionary::ConjugationType::GodanTa;
  if (base_ending == "ぬ")
    return dictionary::ConjugationType::GodanNa;
  if (base_ending == "ぶ")
    return dictionary::ConjugationType::GodanBa;
  if (base_ending == "む")
    return dictionary::ConjugationType::GodanMa;
  if (base_ending == "る")
    return dictionary::ConjugationType::GodanRa;
  return dictionary::ConjugationType::GodanWa;
}

std::string generateRenyokei(std::string_view surface, std::string_view reading, V2VerbType verb_type) {
  std::string_view base = reading.empty() ? surface : reading;
  if (base.empty())
    return "";

  if (verb_type == V2VerbType::Ichidan) {
    return base.size() >= 3 ? std::string(base.substr(0, base.size() - 3)) : "";
  }

  if (base.size() < 3)
    return "";
  const std::string_view i_row = grammar::godanIRowSuffixFromURow(utf8::decodeLastChar(base));
  if (i_row.empty())
    return "";
  std::string result(base.substr(0, base.size() - 3));
  result += i_row;
  return result;
}

std::string generateMizenkei(std::string_view surface, std::string_view reading, V2VerbType verb_type) {
  std::string_view base = reading.empty() ? surface : reading;
  if (base.empty())
    return "";

  if (verb_type == V2VerbType::Ichidan) {
    return base.size() >= 3 ? std::string(base.substr(0, base.size() - 3)) : "";
  }

  if (base.size() < 3)
    return "";
  const std::string_view a_row = grammar::godanARowSuffixFromURow(utf8::decodeLastChar(base));
  if (a_row.empty())
    return "";
  std::string result(base.substr(0, base.size() - 3));
  result += a_row;
  return result;
}

std::string generateKateikei(std::string_view surface, std::string_view reading, V2VerbType verb_type) {
  const std::string_view base = reading.empty() ? surface : reading;
  if (base.size() < core::kJapaneseCharBytes) {
    return "";
  }

  if (verb_type == V2VerbType::Ichidan) {
    return std::string(base.substr(0, base.size() - core::kJapaneseCharBytes)) + "れ";
  }

  const char32_t last_cp = utf8::decodeLastChar(base);
  for (const auto& [row_verb_type, row] : grammar::Conjugation::getGodanRows()) {
    (void)row_verb_type;
    if (row.base_vowel == last_cp) {
      std::string result(base.substr(0, base.size() - core::kJapaneseCharBytes));
      result += normalize::utf8::encode({row.e_row});
      return result;
    }
  }
  return "";
}

std::string generateGodanPotential(std::string_view surface, std::string_view reading, V2VerbType verb_type) {
  if (verb_type != V2VerbType::Godan) {
    return "";
  }

  const std::string_view base = reading.empty() ? surface : reading;
  if (base.size() < core::kJapaneseCharBytes) {
    return "";
  }

  const char32_t last_cp = utf8::decodeLastChar(base);
  for (const auto& [row_verb_type, row] : grammar::Conjugation::getGodanRows()) {
    (void)row_verb_type;
    if (row.base_vowel == last_cp) {
      std::string result(base.substr(0, base.size() - core::kJapaneseCharBytes));
      result += normalize::utf8::encode({row.e_row});
      result += "る";
      return result;
    }
  }
  return "";
}

TeFormType getTeFormType(std::string_view base_ending) {
  if (base_ending == "く" || base_ending == "ぐ")
    return TeFormType::Ionbin;
  if (base_ending == "つ" || base_ending == "う" || base_ending == "る")
    return TeFormType::Sokuonbin;
  if (base_ending == "む" || base_ending == "ぶ" || base_ending == "ぬ")
    return TeFormType::Hatsuonbin;
  if (base_ending == "す")
    return TeFormType::Renyokei;
  return TeFormType::Ichidan;
}

std::pair<std::string, bool> generateTeFormStem(std::string_view surface, std::string_view reading,
                                                V2VerbType verb_type, std::string_view base_ending) {
  const std::string_view base = reading.empty() ? surface : reading;
  if (base.empty() || base.size() < 3)
    return {"", false};

  if (verb_type == V2VerbType::Ichidan) {
    return {std::string(base.substr(0, base.size() - 3)), false};
  }

  std::string result(base.substr(0, base.size() - 3));
  switch (getTeFormType(base_ending)) {
    case TeFormType::Ionbin:
      result += "い";
      return {result, base_ending == "ぐ"};
    case TeFormType::Sokuonbin:
      result += "っ";
      return {result, false};
    case TeFormType::Hatsuonbin:
      result += "ん";
      return {result, true};
    case TeFormType::Renyokei:
      result += "し";
      return {result, false};
    default:
      return {"", false};
  }
}

std::string generateKanjiRenyokei(std::string_view kanji_surface, std::string_view reading, V2VerbType verb_type) {
  if (reading.empty()) {
    return generateRenyokei(kanji_surface, "", verb_type);
  }
  const std::string hiragana_renyokei = generateRenyokei(reading, "", verb_type);
  if (hiragana_renyokei.empty())
    return "";

  size_t kanji_bytes = 0;
  for (size_t scan_pos = 0; scan_pos < kanji_surface.size();) {
    size_t next_pos = scan_pos;
    if (!normalize::isKanjiCodepoint(normalize::decodeUtf8(kanji_surface, next_pos)))
      break;
    kanji_bytes = next_pos;
    scan_pos = next_pos;
  }
  if (kanji_bytes == 0)
    return "";

  std::string result(kanji_surface.substr(0, kanji_bytes));
  const size_t reading_kanji_len = reading.size() - (kanji_surface.size() - kanji_bytes);
  if (reading_kanji_len < hiragana_renyokei.size()) {
    result += hiragana_renyokei.substr(reading_kanji_len);
  }
  return result;
}

char32_t godanRenyokeiBaseCp(char32_t renyokei_cp) {
  const std::string_view base = grammar::godanBaseSuffixFromIRow(renyokei_cp);
  return base.empty() ? 0 : utf8::decodeFirstChar(base);
}

}  // namespace suzume::analysis::compound_verb_detail

namespace suzume::analysis {

namespace {

// A compound-verb candidate needs inflectional evidence, not a bare
// continuative ending. Politeness is intentionally excluded: ます remains a
// separate auxiliary token.
bool hasAuxiliarySuffix(std::string_view suffix) {
  return !suffix.empty() && utf8::containsAny(suffix, {"た", "て", "で", "だ", "ない", "れ"});
}

}  // namespace

using namespace compound_verb_detail;

void addCompoundVerbJoinCandidates(core::Lattice& lattice, std::string_view text,
                                   const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                   size_t start_pos, const std::vector<normalize::CharType>& char_types,
                                   const dictionary::DictionaryManager& dict_manager, const Scorer& scorer,
                                   const grammar::Inflection& inflection) {
  if (start_pos >= char_types.size()) {
    return;
  }

  // Must start with kanji (V1 verb stem)
  if (char_types[start_pos] != CharType::Kanji) {
    return;
  }

  // Find the kanji portion (V1 stem)
  size_t kanji_end = findCharRegionEnd(char_types, start_pos, 4, CharType::Kanji);

  // Next must be hiragana (連用形 ending), except for a single-kanji
  // ichidan V1 directly followed by a kanji V2 (見上げる).
  if (kanji_end >= char_types.size() || char_types[kanji_end] != CharType::Hiragana) {
    return;
  }

  // Get the hiragana character (potential 連用形 ending)
  char32_t renyokei_char = codepoints[kanji_end];

  // A leading one-kanji ichidan stem can be written without its stem vowel
  // before a kanji V2. The initial kanji run then contains both verbs, so
  // start matching V2 after the first kanji instead of treating its last
  // hiragana as V1's stem ending. A Godan renyokei remains a boundary after
  // the full kanji sequence (提出+し), not evidence for a bare Ichidan stem.
  const bool has_kanji_v2_after_bare_ichidan = kanji_end >= start_pos + 2 &&
                                               char_types[start_pos + 1] == CharType::Kanji &&
                                               godanRenyokeiBaseCp(renyokei_char) == 0;

  // A voice auxiliary followed by the continuative subsidiary remains a
  // grammatical tail rather than a lexical V1+V2 compound. Keep the tail as
  // one search unit so 使わ+れ続ける, 見+られ続ける, and 聞かさ+れ続ける do
  // not collapse into a fabricated whole verb. The gate distinguishes this
  // from lexical 〜れ続ける verbs such as 汚れ続ける: only a preceding
  // mizenkei (a-row) or the ichidan passive られ licenses it.
  for (size_t passive_pos = kanji_end; passive_pos + 3 < codepoints.size(); ++passive_pos) {
    if (codepoints[passive_pos] != U'れ' || codepoints[passive_pos + 1] != U'続' ||
        codepoints[passive_pos + 2] != U'け') {
      continue;
    }
    const char32_t tail_ending = codepoints[passive_pos + 3];
    if (tail_ending != U'る' && tail_ending != U'た' && tail_ending != U'て') {
      continue;
    }

    size_t tail_start = passive_pos;
    const bool follows_godan_mizenkei =
        passive_pos > kanji_end && grammar::isARowCodepoint(codepoints[passive_pos - 1]);
    const bool follows_ichidan_passive = passive_pos == kanji_end + 1 && codepoints[kanji_end] == U'ら';
    // Causative-passive chains retain their voice boundaries, but the
    // passive-continuative tail itself remains one search unit: サ変/一段
    // + させ + られ続ける.  The exact three-mora sequence is grammatical
    // evidence; it does not admit an arbitrary られ+続ける join.
    const bool follows_causative_passive = passive_pos >= kanji_end + 3 && codepoints[passive_pos - 3] == U'さ' &&
                                           codepoints[passive_pos - 2] == U'せ' && codepoints[passive_pos - 1] == U'ら';
    if (!follows_godan_mizenkei && !follows_ichidan_passive && !follows_causative_passive) {
      continue;
    }
    if (follows_ichidan_passive || follows_causative_passive) {
      tail_start = kanji_end;
      if (follows_causative_passive) {
        tail_start = passive_pos - 1;
      }
    }

    // A Godan causative mizenkei (聞かさ from 聞く) can itself precede
    // passive-continuative れ続ける. Derive that stem only when the underlying
    // pre-causative verb is dictionary-confirmed, avoiding a free-form
    // kanji+hira guess while preserving the productive voice chain.
    if (follows_godan_mizenkei && codepoints[passive_pos - 1] == U'さ' && passive_pos >= start_pos + 2) {
      const char32_t underlying_a_row = codepoints[passive_pos - 2];
      const std::string_view underlying_suffix = grammar::godanBaseSuffixFromARow(underlying_a_row);
      if (!underlying_suffix.empty()) {
        const std::string underlying_base =
            extractSubstring(codepoints, start_pos, passive_pos - 2) + std::string(underlying_suffix);
        if (dict_manager.lookupExact(underlying_base, core::PartOfSpeech::Verb) != nullptr) {
          const std::string causative_stem = extractSubstring(codepoints, start_pos, passive_pos);
          const std::string causative_lemma = extractSubstring(codepoints, start_pos, passive_pos - 1) + "す";
          lattice.addEdge(causative_stem, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(passive_pos),
                          core::PartOfSpeech::Verb, candidate::verb_cost::kStandardBonus, 0, causative_lemma,
                          dictionary::ConjugationType::GodanSa, core::CandidateOrigin::VerbCompound,
                          candidate::kNoOriginConfidence, "causative_mizenkei_before_passive_continuative",
                          core::ExtendedPOS::VerbMizenkei, "causative_mizenkei_before_passive_continuative");
        }
      }
    }

    // The terminal form remains a single auxiliary-like search unit.  In
    // past and te forms, expose the renyokei and let the regular た/て
    // auxiliary candidate supply the inflectional boundary.
    const size_t tail_end = passive_pos + (tail_ending == U'る' ? 4 : 3);
    const std::string tail_surface = extractSubstring(codepoints, tail_start, tail_end);
    const std::string tail_lemma = tail_ending == U'る' ? tail_surface : tail_surface + "る";
    lattice.addEdge(tail_surface, static_cast<uint32_t>(tail_start), static_cast<uint32_t>(tail_end),
                    core::PartOfSpeech::Verb, candidate::kVerifiedTailCompoundVerbBonus, 0, tail_lemma,
                    dictionary::ConjugationType::Ichidan, core::CandidateOrigin::VerbCompound,
                    candidate::kNoOriginConfidence, "passive_continuative_tail", core::ExtendedPOS::AuxPassive,
                    "passive_continuative_tail");
    return;
  }

  // Check if it's a valid 連用形 ending
  char32_t base_ending = godanRenyokeiBaseCp(renyokei_char);

  // Check for sokuonbin (っ) compound pattern: 突っ込む, 引っ張る, ぶっ壊す
  // Sokuonbin verbs: godan-ka(く), godan-ta(つ), godan-wa(う), godan-ra(る)
  bool is_sokuonbin = (base_ending == 0 && renyokei_char == U'っ');
  SUZUME_DEBUG_LOG_VERBOSE("[COMPOUND] pos=" << start_pos << " kanji_end=" << kanji_end << " renyokei="
                                             << normalize::utf8::encode({renyokei_char}) << " base_ending="
                                             << (base_ending ? normalize::utf8::encode({base_ending}) : "0")
                                             << " sokuonbin=" << is_sokuonbin << "\n");

  // If not a 連用形 ending, this might be an Ichidan verb
  bool is_ichidan = (base_ending == 0 && !is_sokuonbin) || has_kanji_v2_after_bare_ichidan;

  // Position after 連用形 (for Godan) or after stem (for Ichidan)
  size_t v2_start;
  if (has_kanji_v2_after_bare_ichidan) {
    v2_start = start_pos + 1;
  } else if (is_sokuonbin) {
    // For sokuonbin: V2 starts after っ
    v2_start = kanji_end + 1;
  } else if (is_ichidan) {
    // For ichidan verbs, the stem includes the final hiragana character:
    // - Shimo-ichidan (下一段): え-row (抜け from 抜ける, 食べ from 食べる)
    // - Kami-ichidan (上一段): い-row (落ち from 落ちる, 起き from 起きる)
    // - Suru-variant: じ/ぢ (演じ from 演じる, 感じ from 感じる)
    // B63: We need to skip this hiragana when looking for V2
    char32_t hira = codepoints[kanji_end];
    bool is_e_row_stem = grammar::isERowCodepoint(hira);
    // Note: I-row includes some chars that are also Godan renyokei endings
    // (き, ぎ, し, ち, etc.), but by the time we reach this branch
    // (is_ichidan=true) those cases have already set base_ending above.
    bool is_i_row_stem = grammar::isIRowCodepoint(hira);

    if (is_e_row_stem || is_i_row_stem) {
      // Single-char ichidan stem: 食べ+込む, 落ち+着く
      v2_start = kanji_end + 1;
    } else {
      // Check for multi-char ichidan stem: 生まれ+変わる (生まれる has stem まれ)
      // Scan hiragana sequence to see if last char is e-row/i-row (ichidan marker)
      bool found_multi_ichidan = false;
      size_t scan_pos = kanji_end + 1;
      // Limit scan to 3 additional hiragana chars (max stem like まれ = 2 chars)
      size_t scan_limit = std::min(scan_pos + 2, codepoints.size());
      while (scan_pos < scan_limit && char_types[scan_pos] == CharType::Hiragana) {
        char32_t scan_char = codepoints[scan_pos];
        if (grammar::isERowCodepoint(scan_char) || grammar::isIRowCodepoint(scan_char)) {
          // Found e/i-row ending: valid multi-char ichidan stem
          // V2 starts after this character
          v2_start = scan_pos + 1;
          found_multi_ichidan = true;
          break;
        }
        ++scan_pos;
      }
      if (!found_multi_ichidan) {
        // For non-E/I-row, look for V2 starting at the hiragana position
        // This allows patterns like 見 + つける = 見つける where つ is U-row
        v2_start = kanji_end;
      }
    }
  } else {
    v2_start = kanji_end + 1;
  }

  // Dict-verified compound V1: a lexicalized multi-morpheme verb (引きずる, L2) has a
  // renyokei (引きずり) with a hiragana-medial stem that the single-kanji-stem rule above
  // cannot represent, so a following subsidiary V2 never joins (引きずり|出す). If the
  // dictionary holds such a Verb renyokei (lemma≠surface, ≥3 chars, one leading kanji run
  // followed by hiragana) spanning start_pos, adopt it as V1: point V2 past it and mark it
  // ichidan-like so v1_renyokei_end == v2_start (compound base = 引きずり + 出す). Reuses the
  // whole V2-matching / emission path below. Scoped to the hiragana-medial shape so it does
  // not duplicate rule-handled compounds (走り出す, 話し合う).
  bool dict_compound_v1 = false;
  std::string dict_compound_v1_lemma;
  {
    size_t probe_byte = byteOffsetAt(byte_offsets, start_pos);
    size_t best_len = 0;
    for (const auto& res : dict_manager.lookup(text, probe_byte)) {
      if (res.entry == nullptr || res.entry->pos != core::PartOfSpeech::Verb || res.length < 3 ||
          res.entry->lemma.empty() || res.entry->lemma == res.entry->surface ||
          start_pos + res.length >= codepoints.size() || res.length <= best_len) {
        continue;
      }
      // Require exactly one leading kanji run then hiragana (引 + きずり): the shape the rule
      // path mis-segments. Rejects multi-kanji-run compounds (話 し 合 い) already handled.
      if (char_types[start_pos + 1] != CharType::Hiragana) {
        continue;
      }
      size_t kanji_runs = 0;
      bool in_kanji = false;
      bool shape_ok = true;
      for (size_t idx = start_pos; idx < start_pos + res.length; ++idx) {
        const bool is_kanji = char_types[idx] == CharType::Kanji;
        if (is_kanji && !in_kanji) {
          ++kanji_runs;
        }
        in_kanji = is_kanji;
        if (kanji_runs > 1) {
          shape_ok = false;
          break;
        }
      }
      if (!shape_ok || kanji_runs != 1) {
        continue;
      }
      best_len = res.length;
      dict_compound_v1_lemma = res.entry->lemma;
    }
    // Only adopt the dict V1 when a further kanji-written verb follows (出す, 回す): the
    // subsidiary chain is what this path is for. A hiragana continuation is an auxiliary
    // (引きずり|ます) that the plain dict renyokei edge already handles, so leave it alone.
    if (best_len > 0 && char_types[start_pos + best_len] == CharType::Kanji) {
      v2_start = start_pos + best_len;
      is_sokuonbin = false;
      is_ichidan = true;  // makes v1_renyokei_end == v2_start_byte below (multi-hiragana stem)
      dict_compound_v1 = true;
    }
  }

  // A multi-kanji ichidan V1 can end in the first hiragana after the leading
  // kanji run (仕立てる, 心掛ける).  The bare-ichidan branch above starts V2
  // after only the first kanji, which is right for 見上げる but loses that
  // lexical V1 shape.  When the reconstructed V1 is dictionary-verified,
  // retain its entire stem and begin matching the subsidiary V2 after it.
  if (!dict_compound_v1 && has_kanji_v2_after_bare_ichidan && kanji_end > start_pos + 1 &&
      kanji_end + 1 < codepoints.size() && char_types[kanji_end + 1] == CharType::Kanji &&
      (grammar::isERowCodepoint(renyokei_char) || grammar::isIRowCodepoint(renyokei_char))) {
    const std::string multi_kanji_ichidan_base = extractSubstring(codepoints, start_pos, kanji_end + 1) + "る";
    if (dict_manager.lookupExact(multi_kanji_ichidan_base, core::PartOfSpeech::Verb) != nullptr) {
      v2_start = kanji_end + 1;
      is_sokuonbin = false;
      is_ichidan = true;
      dict_compound_v1 = true;
      dict_compound_v1_lemma = multi_kanji_ichidan_base;
    }
  }

  // Sokuonbin compound V1: 単漢字 + っ + 漢字 + 連用形 (引っ張り) whose embedded second verb
  // (張る) is dict-verified. The sokuonbin compound 引っ張る is rule-derivable, not in the
  // dictionary, so the branch above misses it; here the whole 引っ張り becomes V1 so a further
  // subsidiary (出す) chains → 引っ張り出す. The sokuon between two kanji is the compound
  // signal; a single sokuonbin compound with no trailing verb (突っ込み) is left untouched.
  if (!dict_compound_v1 && kanji_end == start_pos + 1 && kanji_end < codepoints.size() &&
      codepoints[kanji_end] == U'っ') {
    size_t k2_start = kanji_end + 1;
    if (k2_start < char_types.size() && char_types[k2_start] == CharType::Kanji) {
      size_t k2_end = findCharRegionEnd(char_types, k2_start, 3, CharType::Kanji);
      // Require a further kanji-written verb after the renyokei (引っ張り + 出す): this path
      // only chains a trailing subsidiary. Without it, 引っ越しました (引っ越す + aux) would be
      // hijacked and the plain 引っ越す compound lost.
      if (k2_end < codepoints.size() && k2_end + 1 < codepoints.size() && char_types[k2_end] == CharType::Hiragana &&
          char_types[k2_end + 1] == CharType::Kanji) {
        char32_t base2 = godanRenyokeiBaseCp(codepoints[k2_end]);
        if (base2 != 0) {
          size_t k2_start_byte = byteOffsetAt(byte_offsets, k2_start);
          size_t k2_end_byte = byteOffsetAt(byte_offsets, k2_end);
          std::string embedded2(text.substr(k2_start_byte, k2_end_byte - k2_start_byte));
          embedded2 += normalize::utf8::encode({base2});
          if (dict_manager.lookupExact(embedded2, core::PartOfSpeech::Verb) != nullptr) {
            v2_start = k2_end + 1;
            is_sokuonbin = false;
            is_ichidan = true;
            dict_compound_v1 = true;
            dict_compound_v1_lemma = embedded2;  // best-effort; unused once verified
          }
        }
      }
    }
  }

  if (v2_start >= codepoints.size()) {
    return;
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
        return;
      }
    }

    // A terminal u-row verb followed by a case particle is a clause boundary,
    // even when the particle is a single character (行く+に+越した).  A
    // continuative ending such as し remains eligible for lexical compounds.
    const auto* particle = dict_manager.lookupExact(particle_probe, core::PartOfSpeech::Particle);
    if (particle != nullptr && particle_start > start_pos && kana::isURowCodepoint(codepoints[particle_start - 1])) {
      return;
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
  struct V2Match {
    size_t matched_len = 0;
    std::string compound_base;
    bool is_renyokei = false;          // true if matched via renyokei entry
    bool renyokei_form = false;        // true if the winning match was a renyokei-form match
    bool is_mizenkei = false;          // true if matched via mizenkei form
    bool is_potential = false;         // true if matched via a Godan potential form
    bool includes_aux = false;         // true if inflection match includes aux suffix
    bool matched_via_reading = false;  // true if V2 was matched via hiragana reading
    std::string v2_reading;            // V2 hiragana reading (for hiragana te-stem generation)
    float confidence = 0.0F;
    V2VerbType v2_verb_type = V2VerbType::Godan;  // V2 verb type
    std::string_view v2_base_ending;              // V2 base form ending (む, す, etc.)
    bool v1_dict_verified = false;                // true if V1 was verified via dictionary (not inflection fallback)
    bool v1_embedded_verified = false;            // true if V1 was verified via an embedded dictionary verb
    bool v1_ichidan_inflection = false;           // true if V1 is a single-kanji ichidan verb verified by inflection
    bool v1_godan_inflection = false;  // true if V1 is a single-kanji godan verb verified by exact inflection
  };
  V2Match best_match;

  for (const auto& v2_verb : kSubsidiaryVerbs) {
    std::string_view v2_surface(v2_verb.surface);
    std::string_view v2_reading(v2_verb.reading ? v2_verb.reading : "");

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

    // Try inflection analysis for inflected V2 forms (e.g., きった, 込んだ, 巡った)
    // Only for base forms (not renyokei entries) to avoid double-matching
    // Skip if already matched via renyokei to prevent aux detection overriding renyokei match
    if (!matched_kanji && !matched_reading && !matched_renyokei && !matched_potential && !v2_reading.empty()) {
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
          auto v2_surface_decoded = normalize::utf8::decode(std::string(v2_surface));
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
    if (!matched_kanji && !matched_reading && !matched_renyokei && !matched_potential && !matched_inflected) {
      // Try kanji mizenkei (込ま from 込む)
      std::string kanji_mizen = generateMizenkei(v2_surface, "", v2_verb.verb_type);
      // Try hiragana mizenkei (こま from こむ)
      std::string hira_mizen = !v2_reading.empty() ? generateMizenkei(v2_reading, "", v2_verb.verb_type) : "";

      auto tryMizenMatch = [&](const std::string& mizen) -> bool {
        if (mizen.empty() || v2_start_byte + mizen.size() > text.size())
          return false;
        std::string_view text_at_v2 = text.substr(v2_start_byte, mizen.size());
        if (text_at_v2 != mizen)
          return false;
        // Must be followed by passive れ, causative せ, or negative な/ず.
        size_t after_byte = v2_start_byte + mizen.size();
        if (after_byte + 3 > text.size())
          return false;
        std::string_view after = text.substr(after_byte, 3);
        return after == "れ" || after == "せ" || after == "な" || after == "ず";
      };

      if (tryMizenMatch(kanji_mizen)) {
        matched_mizenkei = true;
        matched_len = kanji_mizen.size();
      } else if (tryMizenMatch(hira_mizen)) {
        matched_mizenkei = true;
        matched_len = hira_mizen.size();
      }
    }

    if (!matched_kanji && !matched_reading && !matched_renyokei && !matched_potential && !matched_inflected &&
        !matched_mizenkei) {
      continue;
    }

    SUZUME_DEBUG_LOG_VERBOSE("[COMPOUND] V2 matched: "
                             << v2_verb.surface << " kanji=" << matched_kanji << " reading=" << matched_reading
                             << " renyokei=" << matched_renyokei << " potential=" << matched_potential
                             << " inflected=" << matched_inflected << " mizenkei=" << matched_mizenkei
                             << " len=" << matched_len << "\n");

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
        v1_base += normalize::utf8::encode({base_ending});
      } else {
        v1_base += "る";
      }

      if (is_sokuonbin) {
        // Try all sokuonbin-compatible godan endings
        for (char32_t ending : kSokuonbinEndings) {
          std::string candidate = v1_base + normalize::utf8::encode({ending});
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
          std::string candidate = v1_base + normalize::utf8::encode({ending});
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
        auto infl_result = inflection.getBest(v1_renyokei);
        if (infl_result.confidence >= candidate::verb_cost::kConstructedVerbMinConfidence &&
            infl_result.base_form == v1_base) {
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
              std::string candidate = v1_base + normalize::utf8::encode({ending});
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
      compound_base += v2_is_hiragana ? generateGodanPotential(v2_reading, "", v2_verb.verb_type) : potential;
    } else {
      compound_base += v2_is_hiragana ? v2_reading : v2_surface;
    }

    // Compare with best match and update if this is better
    // Priority:
    // 1. Longer renyokei match beats shorter (出し > 出)
    // 2. Renyokei exact match beats inflection match with aux
    // 3. Match without aux beats match with aux
    bool should_update = false;
    if (best_match.matched_len == 0) {
      // First valid match
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
      if (renyokei_end_pos < codepoints.size() &&
          (codepoints[renyokei_end_pos] == U'方' || codepoints[renyokei_end_pos] == U'手' ||
           codepoints[renyokei_end_pos] == U'物' || codepoints[renyokei_end_pos] == U'所' ||
           codepoints[renyokei_end_pos] == U'場')) {
        should_update = true;
      }
    } else if (is_renyokei_entry && (matched_kanji || matched_reading) && best_match.includes_aux &&
               !best_match.is_renyokei) {
      // Renyokei exact match beats inflection match that includes aux
      // This makes 食べすぎ (renyokei) beat 食べすぎた (inflection+aux)
      should_update = true;
    } else if (!inflection_includes_aux && best_match.includes_aux) {
      // Match without aux beats match with aux
      should_update = true;
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
      best_match.is_potential = matched_potential;
      best_match.includes_aux = inflection_includes_aux;
      best_match.matched_via_reading = matched_reading || matched_inflected || matched_renyokei_via_reading;
      best_match.v2_reading = std::string(v2_reading);
      best_match.v2_verb_type = v2_verb.verb_type;
      best_match.v2_base_ending = v2_verb.base_ending;
      best_match.v1_dict_verified = v1_dict_verified;
      best_match.v1_embedded_verified = v1_embedded_verified;
      best_match.v1_ichidan_inflection = v1_ichidan_inflection;
      best_match.v1_godan_inflection = v1_godan_inflection;
    }
  }

  SUZUME_DEBUG_LOG_VERBOSE("[COMPOUND] best_match.len=" << best_match.matched_len
                                                        << " base=" << best_match.compound_base << "\n");

  // After checking all V2 entries, use the best match if found
  if (best_match.matched_len > 0) {
    // Calculate compound verb end position using matched length
    size_t compound_end_byte = v2_start_byte + best_match.matched_len;

    // Find character position for compound end
    size_t compound_end_pos = advanceCharsToBytePos(codepoints, v2_start, v2_start_byte, compound_end_byte);

    // Build the compound verb surface
    std::string compound_surface(text.substr(start_byte, compound_end_byte - start_byte));

    // Causative endings are auxiliary chains, not V2 compound verbs. This
    // also covers a passive followed by causative (書か+れ+させる), where the
    // permissive V2 matcher could otherwise reinterpret させる as a lexical
    // continuation and erase the voice boundary.
    if (verb_helpers::containsPassiveCausativeAuxPattern(compound_surface)) {
      return;
    }

    // An internal te-form followed by a benefactive or request auxiliary is
    // compositional (見+て+あげる), not a lexical V1+V2 compound. The helper
    // preserves ordinary compounds such as 取り上げる, which have no te-form.
    if (verb_helpers::embedsTeFormAuxiliary(compound_surface)) {
      return;
    }

    // Skip if compound surface is registered as NOUN in dictionary,
    // UNLESS followed by an auxiliary suffix (た/て/で/ない) which indicates verb usage.
    // This prevents nominalized compound verbs (売り上げ, 打ち合わせ) from being tokenized as VERB
    // when standalone, while allowing 切り替えた, 打ち合わせて to be parsed as compound verbs.
    if (dict_manager.lookupExact(compound_surface, core::PartOfSpeech::Noun) != nullptr) {
      // Check if followed by auxiliary suffix
      bool followed_by_aux = false;
      if (compound_end_pos < codepoints.size()) {
        char32_t next_cp = codepoints[compound_end_pos];
        // た/て/で/な(い)/れ/ら/ま(す) indicate verb conjugation
        followed_by_aux = (next_cp == U'た' || next_cp == U'て' || next_cp == U'で' || next_cp == U'な' ||
                           next_cp == U'れ' || next_cp == U'ら' || next_cp == U'ま' || next_cp == U'ず');
        // A deverbal suffix also validates the compound continuative.  It is
        // emitted below as a nominal candidate, not retained as a verb.
        followed_by_aux = followed_by_aux || next_cp == U'方' || next_cp == U'手' || next_cp == U'物' ||
                          next_cp == U'所' || next_cp == U'場';
      }
      if (!followed_by_aux) {
        SUZUME_DEBUG_LOG("[COMPOUND_SKIP] \"" << compound_surface << "\" is dict NOUN, skipping compound verb\n");
        return;
      }
      SUZUME_DEBUG_LOG("[COMPOUND] \"" << compound_surface << "\" is dict NOUN but followed by aux, allowing\n");
    }

    // Skip if a hiragana-V2 variant of compound_base is registered as VERB in dictionary.
    // E.g., compound_base=取り掛かる but dict has 取りかかる: prefer dict's hiragana lemma.
    // This handles modern Japanese where mixed kanji+hiragana compounds (取りかかる, 引き起こす)
    // are conventionally written without normalizing V2 to kanji.
    if (best_match.matched_via_reading && !best_match.v2_reading.empty() &&
        best_match.compound_base.size() > best_match.v2_reading.size()) {
      // V1 portion = compound_base minus the V2 kanji suffix; replace with V2 reading.
      // We can't compute v2 kanji length directly here, so reconstruct from compound_base.
      // Find where compound_base differs from v2_reading at the end.
      // Simpler approach: v1_portion length = compound_base.size() - len(kanji_v2_in_compound_base).
      // The kanji V2 portion is what was appended via `compound_base += v2_verb.surface` in the loop.
      // We saved that as the suffix after the V1 renyokei. Length-wise it's v2_kanji_len_bytes,
      // which equals compound_base.size() - (v2_start_byte - start_byte).
      // v1 renyokei text = text[start_byte .. v2_start_byte] (independent of is_ichidan because
      // for ichidan v1_renyokei_end == v2_start_byte; for godan v1_renyokei_end == v2_start_byte
      // since the renyokei character is the last char of v1, inside the start..v2_start span).
      std::string v1_renyokei_text(text.substr(start_byte, v2_start_byte - start_byte));
      std::string hira_v2_compound = v1_renyokei_text + best_match.v2_reading;
      if (hira_v2_compound != best_match.compound_base) {
        if (dict_manager.lookupExact(hira_v2_compound, core::PartOfSpeech::Verb) != nullptr) {
          SUZUME_DEBUG_LOG("[COMPOUND_SKIP] kanji compound \""
                           << best_match.compound_base << "\" yields to dict verb \"" << hira_v2_compound << "\"\n");
          return;
        }
      }
    }

    // Calculate cost
    float base_cost = scorer.posPrior(core::PartOfSpeech::Verb);
    const auto& opts = scorer.joinOpts();
    // Dict-verified V1 gets full bonus; inflection-only V1 gets a penalty.
    // Inflection analysis can verify verb forms that aren't real words (e.g., 進す),
    // so unverified compounds should be more expensive to prevent false positives
    // like 進し続ける winning over 前進+し+続ける.
    // A single-kanji ichidan V1 confirmed by inflection (受ける, 植える, 投げる)
    // is an unambiguous open-class verb, absent from the dictionary only because
    // it is rule-derivable — as strong as a dict-confirmed V1. It shares the full
    // bonus so its compound (受け入れ, 投げ入れ) beats a spurious split under a
    // following passive/potential られる (受け+入れ+られる).
    // Embedded-verified V1 (a dictionary verb embedded after a leading kanji,
    // e.g., 仕立てる = 仕 + 立てる) is weaker evidence: the leading kanji is
    // unconstrained, so it keeps the reduced penalty relative to a dict-confirmed V1.
    float v1_bonus = 0.0F;
    if (best_match.v1_dict_verified || best_match.v1_ichidan_inflection || best_match.v1_godan_inflection) {
      v1_bonus = opts.verified_v1_bonus;  // -0.3: reward for a confirmed real V1
    } else if (best_match.v1_embedded_verified) {
      v1_bonus = bigram_cost::kMinor;  // +0.5: reduced penalty for partial-evidence V1
    } else {
      v1_bonus = bigram_cost::kRare;  // +1.0: penalty for inflection-only V1
    }
    float final_cost = base_cost + opts.compound_verb_bonus + v1_bonus;

    // A compound whose complete lemma is attested in the dictionary is a
    // lexical search unit.  Prefer it over a coincidental noun + する or
    // verb + verb decomposition, while leaving productive, unregistered
    // compounds to their ordinary compositional scoring.
    const bool compound_lemma_verified =
        dict_manager.lookupExact(best_match.compound_base, core::PartOfSpeech::Verb) != nullptr;
    const std::string nominalized_compound = generateRenyokei(best_match.compound_base, "", best_match.v2_verb_type);
    const bool compound_nominalization_verified =
        !nominalized_compound.empty() &&
        dict_manager.lookupExact(nominalized_compound, core::PartOfSpeech::Noun) != nullptr;
    if (compound_lemma_verified ||
        (compound_nominalization_verified && !best_match.includes_aux && !best_match.is_mizenkei)) {
      final_cost += bigram_cost::kStrongBonus;
    }

    // Penalty for compound verbs that absorb auxiliary suffixes (た/て/れる/etc.)
    // When includes_aux is true, the compound has absorbed an inflectional suffix
    // that should split off (e.g., 語り継がれる → 語り継が|れる).
    // Te-stem and mizenkei candidates are generated separately (below) to provide
    // the split path; this penalty ensures the split path wins over the merged form.
    if (best_match.includes_aux) {
      final_cost += bigram_cost::kStrong;
    }

    uint8_t flags = core::LatticeEdge::kFromDictionary;
    if (compound_lemma_verified) {
      flags |= core::LatticeEdge::kLemmaVerified;
    }

    // Compound_base preserves the V2's input orthography. Potential forms are
    // lexical terminal forms in the public token contract, so retain their
    // surface lemma (取り戻せる) rather than their Godan source form.
    std::string compound_lemma = best_match.compound_base;
    dictionary::ConjugationType compound_conj_type =
        compoundConjugationType(best_match.v2_verb_type, best_match.v2_base_ending);

    // Mizenkei match: add VerbMizenkei edge and return (no te-stem/mizenkei derivation)
    // E.g., 打ち込ま (mizenkei of 打ち込む) for passive 打ち込まれ
    if (best_match.is_mizenkei) {
      lattice.addEdge(compound_surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(compound_end_pos),
                      core::PartOfSpeech::Verb, final_cost, flags, compound_lemma, compound_conj_type,
                      core::CandidateOrigin::Unknown, 0.0F, "compound_mizenkei", core::ExtendedPOS::VerbMizenkei,
                      "compound_mizenkei");
      return;
    }

    // Renyokei-form compound (組み立て, 打ち立て): its surface can end in て/た/る, whose
    // auto-detected verb form would be VerbTeForm and wrongly trigger the te-form split
    // penalty in the scorer. Tag such matches explicitly as VerbRenyokei so the whole
    // compound competes fairly with the V1連用+V2 split; base-form matches keep the
    // pos-derived default (Unknown → auto-detect, e.g. 組み立てる → VerbShuushikei).
    // Require a 2+char V2 renyokei (立て, 重ね): a single-mora V2 renyokei ending in
    // て/で (出る→で) is genuinely te-form-ambiguous, so 持ち+で(出) must keep the penalty
    // and lose to 気持ち+で rather than winning as a spurious 持ちで compound.
    bool renyokei_multichar = best_match.renyokei_form && best_match.matched_len >= core::kTwoJapaneseCharBytes;
    core::ExtendedPOS compound_epos = renyokei_multichar ? core::ExtendedPOS::VerbRenyokei : core::ExtendedPOS::Unknown;
    lattice.addEdge(compound_surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(compound_end_pos),
                    core::PartOfSpeech::Verb, final_cost, flags, compound_lemma, compound_conj_type,
                    core::CandidateOrigin::VerbCompound, candidate::kNoOriginConfidence, "compound", compound_epos,
                    "compound");

    // A compound verb continuative followed by a deverbal suffix is a single
    // nominal search unit.  The V1/V2 verification above keeps this productive
    // rule from absorbing arbitrary kanji-hiragana sequences.
    if (best_match.renyokei_form && compound_end_pos < codepoints.size() &&
        (codepoints[compound_end_pos] == U'方' || codepoints[compound_end_pos] == U'物' ||
         codepoints[compound_end_pos] == U'所' || codepoints[compound_end_pos] == U'場')) {
      const size_t noun_end_pos = compound_end_pos + 1;
      const size_t noun_end_byte = byteOffsetAt(byte_offsets, noun_end_pos);
      const std::string noun_surface(text.substr(start_byte, noun_end_byte - start_byte));
      const float noun_cost = scorer.posPrior(core::PartOfSpeech::Noun) + candidate::kCompoundVerbSuffixNounBonus;
      lattice.addEdge(noun_surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(noun_end_pos),
                      core::PartOfSpeech::Noun, noun_cost, flags, noun_surface, dictionary::ConjugationType::None,
                      core::CandidateOrigin::VerbCompound, candidate::kNoOriginConfidence,
                      "compound_renyokei_suffix_noun", core::ExtendedPOS::NounVerbal, "compound_renyokei_suffix_noun");
    }

    // The agentive 手 remains a suffix search unit, while the preceding
    // compound continuative is nominalized (引き受け+手).
    if (best_match.renyokei_form && compound_end_pos < codepoints.size() && codepoints[compound_end_pos] == U'手') {
      const float noun_cost = scorer.posPrior(core::PartOfSpeech::Noun) + candidate::kCompoundVerbSuffixNounBonus;
      lattice.addEdge(compound_surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(compound_end_pos),
                      core::PartOfSpeech::Noun, noun_cost, flags, compound_surface, dictionary::ConjugationType::None,
                      core::CandidateOrigin::VerbCompound, candidate::kNoOriginConfidence,
                      "compound_renyokei_agentive_suffix", core::ExtendedPOS::NounVerbal,
                      "compound_renyokei_agentive_suffix");
    }

    // An ichidan V2 forms its conditional from the compound renyokei plus
    // れば.  Keep that inflectional boundary available for every lexical or
    // productive compound (言い換えれ+ば, 組み合わせれ+ば) instead of
    // reinterpreting れ as a passive auxiliary.
    if (best_match.v2_verb_type == V2VerbType::Ichidan && best_match.renyokei_form &&
        compound_end_pos + 1 < codepoints.size() && codepoints[compound_end_pos] == U'れ' &&
        codepoints[compound_end_pos + 1] == U'ば') {
      const std::string kateikei_surface = compound_surface + "れ";
      lattice.addEdge(kateikei_surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(compound_end_pos + 1),
                      core::PartOfSpeech::Verb, final_cost, flags, compound_lemma, compound_conj_type,
                      core::CandidateOrigin::VerbCompound, candidate::kNoOriginConfidence, "compound_kateikei",
                      core::ExtendedPOS::VerbKateikei, "compound_kateikei");
    }

    // Generate a te-form euphonic stem candidate so the conjunctive particle
    // remains separate: 話し合って → 話し合っ|て.
    // Without this, the compound verb te-form (話し合って) would be a single token.
    auto [te_stem, uses_de] =
        generateTeFormStem(best_match.compound_base, "", best_match.v2_verb_type, best_match.v2_base_ending);

    // The te-form stem (e.g., 受け取っ before た) is itself the desired split
    // candidate, so it must not carry the includes_aux merge penalty. That
    // penalty exists only to keep the fully-merged inflected form (受け取った as
    // one token) from beating the stem+auxiliary split; applying it to the stem
    // as well would wrongly hand the win back to the full V1+V2+aux split when
    // V1 is an inflection-only verb (e.g., ichidan 受ける, not in the dictionary).
    float te_stem_cost = final_cost;
    if (best_match.includes_aux) {
      te_stem_cost -= bigram_cost::kStrong;
    }

    // Helper lambda to add te-stem edge
    auto addTeStemEdge = [&](const std::string& stem) {
      if (stem.empty() || stem.size() >= compound_surface.size())
        return false;
      std::string_view text_prefix = text.substr(start_byte, stem.size());
      if (text_prefix != stem)
        return false;

      auto stem_decoded = normalize::utf8::decode(stem);
      size_t stem_end_pos = start_pos + stem_decoded.size();
      if (stem_end_pos > codepoints.size())
        return false;

      // Determine ExtendedPOS based on te-form type
      auto te_type = getTeFormType(best_match.v2_base_ending);
      core::ExtendedPOS epos;
      if (te_type == TeFormType::Renyokei) {
        epos = core::ExtendedPOS::VerbRenyokei;  // 話し (サ行)
      } else {
        epos = core::ExtendedPOS::VerbOnbinkei;  // 書い, 買っ, 読ん, etc.
      }

      lattice.addEdge(stem, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(stem_end_pos),
                      core::PartOfSpeech::Verb, te_stem_cost, flags, compound_lemma, compound_conj_type,
                      core::CandidateOrigin::VerbCompound, 0.0F, "compound_te_stem", epos, "compound_te_stem");
      return true;
    };

    // Try kanji te-stem first
    bool added_te_stem = addTeStemEdge(te_stem);

    // If kanji te-stem didn't match and V2 was matched via hiragana reading,
    // also try a hiragana te-stem. This handles cases like:
    // 演じきった (input uses hiragana き, not kanji 切)
    if (!added_te_stem && best_match.matched_via_reading && !best_match.v2_reading.empty()) {
      // Build hiragana compound base: V1 renyokei + V2 hiragana reading
      // compound_base is V1 renyokei + V2 kanji surface
      // We need to replace V2 kanji with V2 hiragana
      // Find which V2 verb was matched to get its surface length
      for (const auto& v2_verb : kSubsidiaryVerbs) {
        std::string_view v2_surface(v2_verb.surface);
        if (best_match.compound_base.size() >= v2_surface.size() &&
            best_match.compound_base.compare(best_match.compound_base.size() - v2_surface.size(), v2_surface.size(),
                                             v2_surface) == 0) {
          size_t v1_len = best_match.compound_base.size() - v2_surface.size();
          std::string v1_part = best_match.compound_base.substr(0, v1_len);
          std::string hira_compound_base = v1_part + best_match.v2_reading;
          auto [hira_te_stem, hira_uses_de] =
              generateTeFormStem(hira_compound_base, "", best_match.v2_verb_type, best_match.v2_base_ending);
          addTeStemEdge(hira_te_stem);
          break;
        }
      }
    }

    // Generate a mizenkei candidate for a following passive/causative auxiliary:
    // 読み込まれる → 読み込ま|れる.
    // Without this, the compound verb passive form would be a single token
    // or split as 読み + 込まれる.
    {
      // Generate compound mizenkei: V1 renyokei + V2 mizenkei
      std::string mizenkei = generateMizenkei(best_match.compound_base, "", best_match.v2_verb_type);
      // For V2 matched via hiragana reading, also try hiragana mizenkei
      std::string hira_mizenkei;
      if (best_match.matched_via_reading && !best_match.v2_reading.empty()) {
        for (const auto& v2_verb : kSubsidiaryVerbs) {
          std::string_view v2_surface(v2_verb.surface);
          if (best_match.compound_base.size() >= v2_surface.size() &&
              best_match.compound_base.compare(best_match.compound_base.size() - v2_surface.size(), v2_surface.size(),
                                               v2_surface) == 0) {
            size_t v1_len = best_match.compound_base.size() - v2_surface.size();
            std::string v1_part = best_match.compound_base.substr(0, v1_len);
            std::string hira_base = v1_part + best_match.v2_reading;
            hira_mizenkei = generateMizenkei(hira_base, "", best_match.v2_verb_type);
            break;
          }
        }
      }

      auto addMizenkeiEdge = [&](const std::string& stem) {
        if (stem.empty() || stem.size() >= compound_surface.size())
          return false;
        std::string_view text_prefix = text.substr(start_byte, stem.size());
        if (text_prefix != stem)
          return false;

        auto stem_decoded = normalize::utf8::decode(stem);
        size_t stem_end_pos = start_pos + stem_decoded.size();
        if (stem_end_pos > codepoints.size())
          return false;

        // Check that what follows attaches to mizenkei: a passive/causative
        // marker (れ/せ) or a negative auxiliary (な of ない/なけれ/なかっ, or
        // ず). Without the negative case the split path is missing and the
        // fully merged compound wins (話し合わなければ → one token instead of
        // 話し合わ|なけれ|ば).
        if (stem_end_pos < codepoints.size()) {
          char32_t next_char = codepoints[stem_end_pos];
          if (next_char != U'れ' && next_char != U'せ' && next_char != U'な' && next_char != U'ず')
            return false;
        }

        // Use base cost without passive+te penalty
        float mizenkei_cost = base_cost + opts.compound_verb_bonus + opts.verified_v1_bonus;
        lattice.addEdge(stem, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(stem_end_pos),
                        core::PartOfSpeech::Verb, mizenkei_cost, flags, compound_lemma, compound_conj_type,
                        core::CandidateOrigin::Unknown, 0.0F, "compound_mizenkei", core::ExtendedPOS::VerbMizenkei,
                        "compound_mizenkei");
        return true;
      };

      bool added_mizenkei = addMizenkeiEdge(mizenkei);
      if (!added_mizenkei && !hira_mizenkei.empty()) {
        addMizenkeiEdge(hira_mizenkei);
      }
    }
  }
}

}  // namespace suzume::analysis
