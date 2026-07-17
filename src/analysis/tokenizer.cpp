/**
 * @file tokenizer.cpp
 * @brief Tokenizer that builds lattice from text
 *
 * This file orchestrates candidate generation for tokenization:
 * - Dictionary candidates (direct lookup)
 * - Unknown word candidates (delegated to UnknownWordGenerator)
 * - Split candidates (delegated to split_candidates.h)
 * - Join candidates (delegated to join_candidates.h)
 */

#include "analysis/tokenizer.h"

#include "analysis/category_cost.h"
#include "candidate_constants.h"
#include "core/debug.h"
#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "join_candidates.h"
#include "normalize/utf8.h"
#include "split_candidates.h"
#include "suffix_candidates.h"
#include "tokenizer_utils.h"
#include "verb_candidates_helpers.h"

namespace suzume::analysis {

namespace {

// True if every char position in [start, end) has CharType `type`. When
// `allow_choon` is set, the prolonged sound mark (ー) is also accepted as part
// of the run (colloquial すごーい, katakana loanwords). Bounds-checked against
// both char_types and codepoints so callers can pass raw candidate ranges.
bool allCharsAre(const std::vector<normalize::CharType>& char_types, const std::vector<char32_t>& codepoints,
                 size_t start, size_t end, normalize::CharType type, bool allow_choon) {
  for (size_t idx = start; idx < end && idx < char_types.size(); ++idx) {
    if (char_types[idx] == type) {
      continue;
    }
    if (allow_choon && idx < codepoints.size() && normalize::isProlongedSoundMark(codepoints[idx])) {
      continue;
    }
    return false;
  }
  return true;
}

}  // namespace

Tokenizer::Tokenizer(const dictionary::DictionaryManager& dict_manager, const Scorer& scorer,
                     const UnknownWordGenerator& unknown_gen, core::AnalysisMode mode)
    : dict_manager_(dict_manager),
      scorer_(scorer),
      unknown_gen_(unknown_gen),
      inflection_(unknown_gen.inflection()),
      mode_(mode) {}

core::Lattice Tokenizer::buildLattice(std::string_view text, const std::vector<char32_t>& codepoints,
                                      const std::vector<normalize::CharType>& char_types) const {
  core::Lattice lattice(codepoints.size());
  const ByteOffsets byte_offsets = buildByteOffsets(codepoints);

  // Process each position
  for (size_t pos = 0; pos < codepoints.size(); ++pos) {
    // These run at every position
    addDictionaryCandidates(lattice, text, codepoints, byte_offsets, pos);
    addUnknownCandidates(lattice, text, codepoints, byte_offsets, pos, char_types);
    if (mode_ != core::AnalysisMode::Split) {
      addPronounPluralJoinCandidates(lattice, text, codepoints, byte_offsets, pos);
    }
    if (mode_ != core::AnalysisMode::Split) {
      addMixedScriptCandidates(lattice, text, codepoints, byte_offsets, pos, char_types);
    }

    // CharType-based dispatch: skip generators that can't match at this position
    auto ct = char_types[pos];
    if (ct == normalize::CharType::Kanji) {
      addCompoundSplitCandidates(lattice, text, byte_offsets, pos, char_types);
      addNounVerbSplitCandidates(lattice, text, codepoints, byte_offsets, pos, char_types);
      if (mode_ != core::AnalysisMode::Split) {
        addCompoundVerbJoinCandidates(lattice, text, codepoints, byte_offsets, pos, char_types);
        addPrefixNounJoinCandidates(lattice, text, codepoints, byte_offsets, pos, char_types);
        addTaruAdjectiveJoinCandidates(lattice, text, codepoints, byte_offsets, pos, char_types);
        addVerbSuffixNounJoinCandidates(lattice, text, codepoints, byte_offsets, pos, char_types);
      }
    } else if (ct == normalize::CharType::Hiragana) {
      if (mode_ != core::AnalysisMode::Split) {
        addHiraganaCompoundVerbJoinCandidates(lattice, text, codepoints, byte_offsets, pos, char_types);
        addVerbSuffixNounJoinCandidates(lattice, text, codepoints, byte_offsets, pos, char_types);
      }
      addTeFormAuxiliaryCandidates(lattice, text, codepoints, byte_offsets, pos, char_types);
    }

    SUZUME_DEBUG_LOG("[LATTICE] pos=" << pos << " candidates=" << lattice.edgeIdsAt(pos).size() << "\n");
  }

  // Fallback: ensure every position has at least one edge
  // This prevents the lattice from becoming invalid when no candidates are generated
  // (e.g., positions starting with small kana like っ, ゃ, ゅ, ょ)
  for (size_t pos = 0; pos < codepoints.size(); ++pos) {
    if (lattice.edgeIdsAt(pos).empty()) {
      // Generate a single-character fallback candidate with high penalty
      size_t byte_start = byteOffsetAt(byte_offsets, pos);
      size_t byte_end = byteOffsetAt(byte_offsets, pos + 1);
      std::string surface(text.substr(byte_start, byte_end - byte_start));

      lattice.addEdge(surface, static_cast<uint32_t>(pos), static_cast<uint32_t>(pos + 1), core::PartOfSpeech::Other,
                      candidate::kFallbackCandidateCost, core::LatticeEdge::kIsUnknown);
    }
  }

  return lattice;
}

void Tokenizer::addDictionaryCandidates(core::Lattice& lattice, std::string_view text,
                                        const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                        size_t start_pos) const {
  // Convert to byte position for dictionary lookup
  size_t byte_pos = byteOffsetAt(byte_offsets, start_pos);

  // Lookup in dictionary
  auto results = dict_manager_.lookup(text, byte_pos);

  size_t longest_conjunction = 0;
  for (const auto& result : results) {
    if (result.entry != nullptr && result.entry->pos == core::PartOfSpeech::Conjunction) {
      longest_conjunction = std::max(longest_conjunction, result.length);
    }
  }

  for (const auto& result : results) {
    if (result.entry == nullptr) {
      continue;
    }

    // Prefer the maximal closed-class conjunction at this position: 又は,
    // not 又+は. Shorter prefixes remain available when no longer conjunction
    // matches the input.
    if (result.entry->pos == core::PartOfSpeech::Conjunction && result.length < longest_conjunction) {
      continue;
    }

    // Calculate end position in characters
    size_t end_pos = start_pos + result.length;

    // Skip a dictionary adjective ending in double い when its final い is the
    // leading い of the receptive auxiliary いただく: the adjective reading
    // would fuse a wa-row renyokei's い with the auxiliary's onset
    // (お使いいただく → 使い+いただく, not 使+いい+ただく). Plain いい in
    // predicate/attributive position is untouched (no ただ+inflection follows).
    if (result.entry->pos == core::PartOfSpeech::Adjective && result.length >= 2 && codepoints[end_pos - 1] == U'い' &&
        codepoints[end_pos - 2] == U'い' && verb_helpers::itadakuParadigmStartsAt(codepoints, end_pos - 1)) {
      continue;
    }

    // Create edge
    // v0.8: flags derived from extended_pos, cost from getCategoryCost()
    uint8_t flags = core::LatticeEdge::kFromDictionary;
    if (result.from_user_dict) {
      flags |= core::LatticeEdge::kFromUserDict;
    }
    if (result.entry->extended_pos == core::ExtendedPOS::NounFormal) {
      flags |= core::LatticeEdge::kIsFormalNoun;
    }
    // Note: is_low_info removed - can be derived from extended_pos if needed

    // Cost is now derived from ExtendedPOS via getCategoryCost()
    float cost = analysis::getCategoryCost(result.entry->extended_pos);

    // A bare え-row dict-verb imperative closing a clause (書け, 止まれ) is the 命令形 of the
    // base verb, not the potential-verb renyokei; without this the spurious 未然+受身れ split
    // (止ま+れ, lemma 止む) wins. Gated so any auxiliary/ば continuation (走れます/走れば/止まれる)
    // leaves the connection scores byte-identical.
    if (result.entry->pos == core::PartOfSpeech::Verb &&
        (result.entry->extended_pos == core::ExtendedPOS::VerbKateikei ||
         result.entry->extended_pos == core::ExtendedPOS::VerbMeireikei) &&
        grammar::containsKanji(result.entry->surface)) {
      const bool continues = end_pos < codepoints.size() &&
                             (codepoints[end_pos] == U'ば' ||
                              verb_helpers::isPassiveAuxContinuation(codepoints, end_pos, /*strict_masu=*/true));
      if (!continues) {
        cost += candidate::verb_cost::kImperativeFinalBonus;
        // Flag the tuned cost so the scorer honours it even when it lands on exactly 0.0
        // (0.0 is otherwise read as "unset" and falls back to the category cost).
        flags |= core::LatticeEdge::kHasCustomCost;
      }
    }

    // A single-token godan potential (読める) is analyzed as an independent ichidan verb, so its
    // lemma is its surface. The boost lets that dict form beat an unrelated ichidan reading. Excluded: independent
    // ichidan verbs (割れる==割れる have lemma == surface, and 自他 pairs like 切れる are registered
    // as ICHIDAN so no potential form is generated); られる passive/potential (来られる); and
    // irregular L1 forms whose lemma differs for other reasons (す→する) that do not end え-row + る.
    const bool is_godan_potential =
        result.entry->pos == core::PartOfSpeech::Verb &&
        result.entry->extended_pos == core::ExtendedPOS::VerbShuushikei &&
        std::string_view(result.entry->lemma) != std::string_view(result.entry->surface) &&
        utf8::endsWith(result.entry->surface, "る") && !utf8::endsWith(result.entry->surface, "られる") &&
        grammar::endsWithERow(
            std::string_view(result.entry->surface).substr(0, result.entry->surface.size() - core::kJapaneseCharBytes));
    if (is_godan_potential) {
      cost += candidate::verb_cost::kImperativeFinalBonus;
      flags |= core::LatticeEdge::kHasCustomCost;
    }

    const std::string_view lemma =
        is_godan_potential ? std::string_view(result.entry->surface) : std::string_view(result.entry->lemma);
    lattice.addEdge(result.entry->surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(end_pos),
                    result.entry->pos, cost, flags, lemma, dictionary::ConjugationType::None,
                    core::CandidateOrigin::Dictionary, 1.0F, {}, result.entry->extended_pos, "dict");

    // Extend verbs, auxiliaries, and adjectives with colloquial emphasis
    // (ですっ, 行くーー, きたあああ). Unknown candidates use the same matcher.
    if (end_pos < codepoints.size() &&
        (result.entry->pos == core::PartOfSpeech::Verb || result.entry->pos == core::PartOfSpeech::Auxiliary ||
         result.entry->pos == core::PartOfSpeech::Adjective)) {
      const auto emphatic = verb_helpers::matchEmphaticSuffix(codepoints, end_pos, result.entry->pos,
                                                              verb_helpers::SokuonOnsetPolicy::DictionaryEntry);
      if (!emphatic.empty()) {
        // Determine extended_pos for emphatic form
        // Sokuon-ending verb forms should be VerbOnbinkei (音便形)
        core::ExtendedPOS emphatic_epos = result.entry->extended_pos;
        if (result.entry->pos == core::PartOfSpeech::Verb && emphatic.suffix == "っ") {
          // E.g., い(連用形) + っ → いっ(音便形) for と+いっ+て pattern
          emphatic_epos = core::ExtendedPOS::VerbOnbinkei;
        }

        lattice.addEdge(result.entry->surface + emphatic.suffix, static_cast<uint32_t>(start_pos),
                        static_cast<uint32_t>(emphatic.end), result.entry->pos,
                        cost + verb_helpers::emphaticCostAdjustment(emphatic), flags, result.entry->lemma,
                        dictionary::ConjugationType::None, core::CandidateOrigin::Dictionary, 1.0F, {}, emphatic_epos,
                        "dict_emphatic");
      }
    }
  }
}

void Tokenizer::addUnknownCandidates(core::Lattice& lattice, std::string_view text,
                                     const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                     size_t start_pos, const std::vector<normalize::CharType>& char_types) const {
  // Check for dictionary entries at this position to penalize longer unknown words
  size_t byte_pos = byteOffsetAt(byte_offsets, start_pos);
  auto dict_results = dict_manager_.lookup(text, byte_pos);

  size_t max_dict_length = 0;
  for (const auto& result : dict_results) {
    if (result.entry != nullptr) {
      max_dict_length = std::max(max_dict_length, result.length);
    }
  }

  // Generate unknown word candidates
  auto candidates = unknown_gen_.generate(text, codepoints, start_pos, char_types);

  for (const auto& candidate : candidates) {
    bool is_conjunction_prefix = false;
    for (const auto& result : dict_results) {
      if (result.entry != nullptr && result.entry->pos == core::PartOfSpeech::Conjunction &&
          candidate.end - candidate.start <= result.length) {
        is_conjunction_prefix = true;
        break;
      }
    }
    if (is_conjunction_prefix) {
      continue;
    }

    uint8_t flags = core::LatticeEdge::kIsUnknown;
    float adjusted_cost = candidate.cost;

    // Penalize unknown words that extend beyond dictionary entries
    bool skip_penalty = false;
    [[maybe_unused]] const char* skip_reason = nullptr;

    // Skip penalty for adverbs (onomatopoeia like わくわく)
    if (candidate.pos == core::PartOfSpeech::Adverb) {
      skip_penalty = true;
      skip_reason = "adverb";
    }

    if (!skip_penalty &&
        (candidate.pos == core::PartOfSpeech::Verb || candidate.pos == core::PartOfSpeech::Adjective)) {
      // Exception: Don't skip verb candidates ending with ず (adverbialized negatives)
      // e.g., 思わず, 絶えず - these are lexicalized adverbs from verb + ず
      bool ends_with_zu =
          (candidate.surface.size() >= 3 && candidate.surface.substr(candidate.surface.size() - 3) == "ず");
      for (const auto& result : dict_results) {
        if (result.entry != nullptr) {
          // Case 1: Dictionary entry is also a verb/adjective
          // But allow ず-ending candidates (adverbialized forms)
          // Case 1: Dictionary entry is also a verb/adjective
          // But allow ず-ending candidates (adverbialized forms)
          if ((result.entry->pos == core::PartOfSpeech::Verb || result.entry->pos == core::PartOfSpeech::Adjective) &&
              !ends_with_zu) {
            skip_penalty = true;
            skip_reason = "dict_has_verb_adj";
            break;
          }
          // Case 2: Pure hiragana verb candidate vs short dictionary entry
          // Also allow prolonged sound mark (ー) as part of hiragana sequence
          // for colloquial patterns like すごーい, やばーい, かわいー
          if (result.length <= 2 && candidate.end - candidate.start >= 3) {
            if (allCharsAre(char_types, codepoints, candidate.start, candidate.end, normalize::CharType::Hiragana,
                            /*allow_choon=*/true)) {
              skip_penalty = true;
              skip_reason = "pure_hiragana_verb";
              break;
            }
          }
        }
      }
    }

    // Case 3: Colloquial verb contraction (ておく→っとく)
    // っとく is a valid compound verb ending that shouldn't be penalized for length
    // Note: っちゃう/っじゃう are handled by Case 6 (revoke skip for ちゃう endings)
    if (!skip_penalty && candidate.pos == core::PartOfSpeech::Verb) {
      std::string_view surface = candidate.surface;
      if (utf8::endsWith(surface, "っとく")) {
        skip_penalty = true;
        skip_reason = "colloquial_contraction";
      }
    }

    // Case 5: Short hiragana verb candidates ending with te/de-form
    // Handles cases like ねて (寝る), でて (出る), みて (見る) where
    // dictionary only has kanji form but surface is pure hiragana.
    // These 2-char patterns don't meet Case 2's ≥3 char threshold.
    if (!skip_penalty && candidate.pos == core::PartOfSpeech::Verb) {
      std::string_view surface = candidate.surface;
      size_t len = candidate.end - candidate.start;
      // Check for 2-char hiragana verbs ending in て/で
      if (len == 2 && surface.size() >= core::kJapaneseCharBytes) {
        if (allCharsAre(char_types, codepoints, candidate.start, candidate.end, normalize::CharType::Hiragana,
                        /*allow_choon=*/false)) {
          // Check if ends with て or で (te-form markers)
          std::string_view last_char = utf8::lastChar(surface);
          if (utf8::equalsAny(last_char, {"て", "で"})) {
            skip_penalty = true;
            skip_reason = "short_te_form";
          }
        }
      }
    }

    // Case 6: Revoke skip for long hiragana verbs ending with ちゃう/ちゃっ/ちゃい
    // These are auxiliary chains (e.g., されちゃう = さ+れ+ちゃう,
    // なっちゃう = なっ+ちゃう, やっちゃう = やっ+ちゃう) that should split.
    if (skip_penalty && candidate.pos == core::PartOfSpeech::Verb && candidate.end - candidate.start >= 4) {
      std::string_view surface = candidate.surface;
      bool ends_chau =
          utf8::endsWith(surface, "ちゃう") || utf8::endsWith(surface, "ちゃっ") || utf8::endsWith(surface, "ちゃい");
      if (ends_chau) {
        // Check if all hiragana
        if (allCharsAre(char_types, codepoints, candidate.start, candidate.end, normalize::CharType::Hiragana,
                        /*allow_choon=*/false)) {
          skip_penalty = false;
          skip_reason = nullptr;
        }
      }
    }

    // Case 4: Pure hiragana OTHER (likely readings/furigana)
    // Reduce penalty for long varied hiragana sequences
    // Also allow prolonged sound mark (ー) as part of hiragana sequence
    bool reduced_penalty = false;
    bool skip_dict_penalty = false;
    [[maybe_unused]] const char* skip_dict_reason = nullptr;
    if (!skip_penalty && candidate.pos == core::PartOfSpeech::Other && candidate.end - candidate.start >= 4) {
      if (allCharsAre(char_types, codepoints, candidate.start, candidate.end, normalize::CharType::Hiragana,
                      /*allow_choon=*/true)) {
        // Reduce penalty only for varied sequences, not runs of one repeated
        // char (ーーーー, ああああ) which are usually noise.
        bool all_same = true;
        char32_t first_cp = 0;
        for (size_t idx = candidate.start; idx < candidate.end && idx < codepoints.size(); ++idx) {
          if (idx == candidate.start) {
            first_cp = codepoints[idx];
          } else if (codepoints[idx] != first_cp) {
            all_same = false;
            break;
          }
        }
        if (!all_same) {
          reduced_penalty = true;
        }
      }
    }

    // Skip dict length penalty for katakana sequences (loanwords)
    // Loanwords like マスカラ, デスクトップ often exceed dictionary coverage
    if (!skip_penalty && candidate.pos == core::PartOfSpeech::Noun && candidate.end - candidate.start >= 3) {
      if (allCharsAre(char_types, codepoints, candidate.start, candidate.end, normalize::CharType::Katakana,
                      /*allow_choon=*/true)) {
        skip_dict_penalty = true;
        skip_dict_reason = "all_katakana";
      }
    }

    // Skip dict length penalty for kanji compound sequences (2-6 chars)
    // Common compounds like 人工知能, 自然言語処理 may not be in dictionary
    // Keep compounds connected - splitting should be driven by PREFIX/SUFFIX
    // markers or dictionary entries, not length heuristics
    if (!skip_penalty && !skip_dict_penalty && candidate.pos == core::PartOfSpeech::Noun) {
      size_t len = candidate.end - candidate.start;
      if (len >= 2 && len <= 6) {
        if (allCharsAre(char_types, codepoints, candidate.start, candidate.end, normalize::CharType::Kanji,
                        /*allow_choon=*/false)) {
          skip_dict_penalty = true;
          skip_dict_reason = "all_kanji_compound";

          // When a dictionary entry exists as a proper prefix of this compound,
          // add a moderate penalty to prefer the dict-split path.
          // E.g., 第一(dict) + 毛 should beat 第一毛(compound)
          // Only when the prefix covers a significant portion (>= half)
          // to avoid splitting 自然言語処理 at 自然(2/6).
          for (const auto& result : dict_results) {
            if (result.entry != nullptr && result.length >= 2 && result.length < len && result.length * 2 >= len &&
                result.entry->pos != core::PartOfSpeech::Noun) {
              // Exception: na-adjective stem + productive noun-forming suffix
              // (性, 的, etc.) is a genuine compound word (重要性, 必要性),
              // not an accidental dict-prefix overlap like その後(ADV)+猫.
              // The productive suffix mechanism (getSuffixEntries/getNaAdjSuffixes)
              // already scores this pattern on its own merits, so skip the
              // generic dict-prefix penalty here.
              if (result.entry->pos == core::PartOfSpeech::Adjective &&
                  result.entry->extended_pos == core::ExtendedPOS::AdjNaAdj) {
                std::string tail_surface = extractSubstring(codepoints, candidate.start + result.length, candidate.end);
                bool tail_is_productive_suffix = false;
                for (const auto& suffix_entry : getSuffixEntries()) {
                  if (tail_surface == suffix_entry.suffix) {
                    tail_is_productive_suffix = true;
                    break;
                  }
                }
                if (!tail_is_productive_suffix) {
                  for (const auto& na_suffix : getNaAdjSuffixes()) {
                    if (tail_surface == na_suffix) {
                      tail_is_productive_suffix = true;
                      break;
                    }
                  }
                }
                if (tail_is_productive_suffix) {
                  continue;
                }
              }
              constexpr float kDictPrefixPenalty = 1.5F;
              adjusted_cost += kDictPrefixPenalty;
              SUZUME_DEBUG_LOG_VERBOSE("[TOK_UNK] \"" << candidate.surface << "\" (NOUN): +" << kDictPrefixPenalty
                                                      << " (kanji_compound_dict_prefix, dict=\""
                                                      << result.entry->surface << "\")\n");
              break;
            }
          }

          // When a non-NOUN dict entry from a prior position overlaps with
          // this compound's first character, penalize the compound.
          // E.g., その後(dict ADV, pos=0, len=3) overlaps with 後猫(pos=2)
          // → penalize 後猫 to prefer その後+猫 split.
          constexpr size_t kMaxLookback = 4;
          bool found_overlap = false;
          for (size_t back = 1; back <= kMaxLookback && back <= start_pos && !found_overlap; ++back) {
            size_t prev_pos = start_pos - back;
            size_t prev_byte = byteOffsetAt(byte_offsets, prev_pos);
            auto prev_results = dict_manager_.lookup(text, prev_byte);
            for (const auto& result : prev_results) {
              if (result.entry != nullptr && result.length >= 2 && result.length > back &&
                  result.entry->pos != core::PartOfSpeech::Noun && result.entry->pos != core::PartOfSpeech::Pronoun) {
                constexpr float kDictOverlapPenalty = 1.5F;
                adjusted_cost += kDictOverlapPenalty;
                SUZUME_DEBUG_LOG_VERBOSE("[TOK_UNK] \"" << candidate.surface << "\" (NOUN): +" << kDictOverlapPenalty
                                                        << " (kanji_compound_dict_overlap, dict=\""
                                                        << result.entry->surface << "\")\n");
                found_overlap = true;
                break;
              }
            }
          }
        }
      }
    }

    // Skip exceeds_dict_length penalty for suffix pattern candidates
    // These are morphologically recognized patterns (e.g., がち, っぽい)
    // that should not be penalized for exceeding dictionary coverage
    // Also skip for katakana loanwords (マスカラ, デスクトップ)
    // Also skip for Suru verb candidates (所在する, 延期する) - these are productive
    bool is_suru_verb =
        (candidate.pos == core::PartOfSpeech::Verb && candidate.conj_type == dictionary::ConjugationType::Suru);

    // Check for pure hiragana verb (e.g., ねる, もらう, あげる)
    // These should not be penalized heavily - they are legitimate verb forms
    bool is_pure_hiragana_verb = false;
    if (candidate.pos == core::PartOfSpeech::Verb && candidate.end - candidate.start >= 2) {
      // Only skip penalty for short pure hiragana verbs (2-4 chars)
      // Longer ones might be suspicious (e.g., いただきます could be wrong split)
      if (candidate.end - candidate.start <= 4 &&
          allCharsAre(char_types, codepoints, candidate.start, candidate.end, normalize::CharType::Hiragana,
                      /*allow_choon=*/false)) {
        is_pure_hiragana_verb = true;
      }
    }

    // Check for single-kanji stem + hiragana verb (e.g., 残って, 通る, 飛ぶ)
    // Single-kanji verb stems are common in Japanese (残る, 立つ, 打つ, etc.)
    // These should not be penalized for exceeding dict length
    bool is_kanji_stem_verb = false;
    if (candidate.pos == core::PartOfSpeech::Verb && candidate.end - candidate.start >= 2 &&
        candidate.start < char_types.size() && char_types[candidate.start] == normalize::CharType::Kanji) {
      // Check: first char is kanji, rest are hiragana
      if (allCharsAre(char_types, codepoints, candidate.start + 1, candidate.end, normalize::CharType::Hiragana,
                      /*allow_choon=*/false)) {
        is_kanji_stem_verb = true;
      }
    }

    bool exceeds_dict = (max_dict_length > 0 && candidate.end - candidate.start > max_dict_length);
    if (exceeds_dict) {
      if (skip_penalty) {
        SUZUME_DEBUG_LOG_VERBOSE("[TOK_SKIP] \"" << candidate.surface << "\" (" << core::posToString(candidate.pos)
                                                 << "): "
                                                 << "skip exceeds_dict_length (" << skip_reason << ")\n");
      } else if (skip_dict_penalty) {
        SUZUME_DEBUG_LOG_VERBOSE("[TOK_SKIP] \"" << candidate.surface << "\" (" << core::posToString(candidate.pos)
                                                 << "): "
                                                 << "skip exceeds_dict_length (" << skip_dict_reason << ")\n");
      } else if (is_suru_verb) {
        SUZUME_DEBUG_LOG_VERBOSE("[TOK_SKIP] \"" << candidate.surface << "\" (" << core::posToString(candidate.pos)
                                                 << "): "
                                                 << "skip exceeds_dict_length (suru_verb)\n");
      } else if (candidate.has_suffix) {
        SUZUME_DEBUG_LOG_VERBOSE("[TOK_SKIP] \"" << candidate.surface << "\" (" << core::posToString(candidate.pos)
                                                 << "): "
                                                 << "skip exceeds_dict_length (has_suffix)\n");
      } else if (is_pure_hiragana_verb) {
        SUZUME_DEBUG_LOG_VERBOSE("[TOK_SKIP] \"" << candidate.surface << "\" (" << core::posToString(candidate.pos)
                                                 << "): "
                                                 << "skip exceeds_dict_length (pure_hiragana_verb)\n");
      } else if (is_kanji_stem_verb) {
        SUZUME_DEBUG_LOG_VERBOSE("[TOK_SKIP] \"" << candidate.surface << "\" (" << core::posToString(candidate.pos)
                                                 << "): "
                                                 << "skip exceeds_dict_length (kanji_stem_verb)\n");
      } else {
        float penalty = reduced_penalty ? 1.0F : 3.5F;
        adjusted_cost += penalty;
        SUZUME_DEBUG_LOG_VERBOSE("[TOK_UNK] \"" << candidate.surface << "\" (" << core::posToString(candidate.pos)
                                                << "): +" << penalty << " (exceeds_dict_length"
                                                << (reduced_penalty ? ", pure_hiragana" : "")
                                                << ", dict_max=" << max_dict_length << ")\n");
      }
    }

    // For verb candidates, check if the hiragana suffix is a known particle
    if (candidate.pos == core::PartOfSpeech::Verb && candidate.end > candidate.start) {
      size_t hiragana_start = candidate.start;
      while (hiragana_start < candidate.end && hiragana_start < char_types.size() &&
             char_types[hiragana_start] != normalize::CharType::Hiragana) {
        ++hiragana_start;
      }

      if (hiragana_start < candidate.end) {
        size_t suffix_byte_start = byteOffsetAt(byte_offsets, hiragana_start);
        size_t suffix_byte_end = byteOffsetAt(byte_offsets, candidate.end);
        std::string_view hiragana_suffix = text.substr(suffix_byte_start, suffix_byte_end - suffix_byte_start);

        // Don't penalize verb conjugation endings
        // - te-form: て/で/って/んで/いて/いで
        // - renyoukei し: extremely common for suru/godan verbs (分割し, 話し)
        bool is_verb_ending = utf8::equalsAny(hiragana_suffix, {"て", "で", "って", "んで", "いて", "いで", "し"});

        // Skip penalty if:
        // - Known verb conjugation ending (te-form, renyoukei)
        // - Candidate has has_suffix flag (mizenkei for ぬ/れべき patterns)
        if (!is_verb_ending && !candidate.has_suffix) {
          size_t suffix_byte_pos = byteOffsetAt(byte_offsets, hiragana_start);
          auto suffix_results = dict_manager_.lookup(text, suffix_byte_pos);

          for (const auto& result : suffix_results) {
            if (result.entry != nullptr && result.entry->pos == core::PartOfSpeech::Particle) {
              size_t suffix_len = candidate.end - hiragana_start;
              if (result.length == suffix_len) {
                adjusted_cost += 1.5F;
                SUZUME_DEBUG_LOG_VERBOSE("[TOK_UNK] \"" << candidate.surface << "\": +1.5 (particle_suffix=\""
                                                        << hiragana_suffix << "\")\n");
                break;
              }
            }
          }
        }
      }
    }

    std::string surface_str(candidate.surface);

    // Set HasSuffix flag for verb/adj candidates with suffix marking
    if (candidate.has_suffix) {
      flags |= static_cast<uint8_t>(core::EdgeFlags::HasSuffix);
    }
    // Relay dict-verified-lemma marking so the scorer can exempt genuine verb
    // onbin forms from the spurious-onbin penalty.
    if (candidate.lemma_verified) {
      flags |= static_cast<uint8_t>(core::EdgeFlags::LemmaVerified);
    }

    lattice.addEdge(surface_str, static_cast<uint32_t>(candidate.start), static_cast<uint32_t>(candidate.end),
                    candidate.pos, adjusted_cost, flags, candidate.lemma, candidate.conj_type, candidate.origin,
#ifdef SUZUME_DEBUG_INFO
                    candidate.confidence, candidate.pattern, candidate.extended_pos, candidate.epos_source);
#else
                    0.0F, {}, candidate.extended_pos);
#endif
  }
}

void Tokenizer::addMixedScriptCandidates(core::Lattice& lattice, std::string_view text,
                                         const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                         size_t start_pos, const std::vector<normalize::CharType>& char_types) const {
  analysis::addMixedScriptCandidates(lattice, text, codepoints, byte_offsets, start_pos, char_types, scorer_,
                                     dict_manager_);
}

void Tokenizer::addCompoundSplitCandidates(core::Lattice& lattice, std::string_view text,
                                           const ByteOffsets& byte_offsets, size_t start_pos,
                                           const std::vector<normalize::CharType>& char_types) const {
  analysis::addCompoundSplitCandidates(lattice, text, byte_offsets, start_pos, char_types, dict_manager_, scorer_);
}

void Tokenizer::addNounVerbSplitCandidates(core::Lattice& lattice, std::string_view text,
                                           const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                           size_t start_pos, const std::vector<normalize::CharType>& char_types) const {
  analysis::addNounVerbSplitCandidates(lattice, text, codepoints, byte_offsets, start_pos, char_types, dict_manager_,
                                       scorer_, inflection_);
}

void Tokenizer::addCompoundVerbJoinCandidates(core::Lattice& lattice, std::string_view text,
                                              const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                              size_t start_pos,
                                              const std::vector<normalize::CharType>& char_types) const {
  analysis::addCompoundVerbJoinCandidates(lattice, text, codepoints, byte_offsets, start_pos, char_types, dict_manager_,
                                          scorer_, inflection_);
}

void Tokenizer::addHiraganaCompoundVerbJoinCandidates(core::Lattice& lattice, std::string_view text,
                                                      const std::vector<char32_t>& codepoints,
                                                      const ByteOffsets& byte_offsets, size_t start_pos,
                                                      const std::vector<normalize::CharType>& char_types) const {
  analysis::addHiraganaCompoundVerbJoinCandidates(lattice, text, codepoints, byte_offsets, start_pos, char_types,
                                                  dict_manager_, scorer_, inflection_);
}

void Tokenizer::addPrefixNounJoinCandidates(core::Lattice& lattice, std::string_view text,
                                            const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                            size_t start_pos,
                                            const std::vector<normalize::CharType>& char_types) const {
  analysis::addPrefixNounJoinCandidates(lattice, text, codepoints, byte_offsets, start_pos, char_types, dict_manager_,
                                        scorer_);
}

void Tokenizer::addPronounPluralJoinCandidates(core::Lattice& lattice, std::string_view text,
                                               const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                               size_t start_pos) const {
  analysis::addPronounPluralJoinCandidates(lattice, text, codepoints, byte_offsets, start_pos, dict_manager_, scorer_);
}

void Tokenizer::addTeFormAuxiliaryCandidates(core::Lattice& lattice, std::string_view text,
                                             const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                             size_t start_pos,
                                             const std::vector<normalize::CharType>& char_types) const {
  analysis::addTeFormAuxiliaryCandidates(lattice, text, codepoints, byte_offsets, start_pos, char_types, scorer_,
                                         inflection_);
}

void Tokenizer::addTaruAdjectiveJoinCandidates(core::Lattice& lattice, std::string_view text,
                                               const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                               size_t start_pos,
                                               const std::vector<normalize::CharType>& char_types) const {
  analysis::addTaruAdjectiveJoinCandidates(lattice, text, codepoints, byte_offsets, start_pos, char_types, scorer_);
}

void Tokenizer::addVerbSuffixNounJoinCandidates(core::Lattice& lattice, std::string_view text,
                                                const std::vector<char32_t>& codepoints,
                                                const ByteOffsets& byte_offsets, size_t start_pos,
                                                const std::vector<normalize::CharType>& char_types) const {
  analysis::addVerbSuffixNounJoinCandidates(lattice, text, codepoints, byte_offsets, start_pos, char_types,
                                            dict_manager_, scorer_, inflection_);
}

}  // namespace suzume::analysis
