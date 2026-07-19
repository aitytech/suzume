/**
 * @file tokenizer_unknown.cpp
 * @brief Unknown-word candidate generation for the tokenizer
 */

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

#include "analysis/category_cost.h"
#include "analysis/tokenizer.h"
#include "candidate_constants.h"
#include "core/debug.h"
#include "core/kana_constants.h"
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

// A copular irrealis form followed by the volitional auxiliary is a
// grammatical auxiliary sequence, not an unknown lexical verb.  Keeping the
// sequence visible prevents a short pure-hiragana verb candidate from hiding
// a dictionary-backed copula + volitional analysis.
bool isCopulaVolitionalSequence(const dictionary::DictionaryManager& dict_manager,
                                const std::vector<char32_t>& codepoints, size_t start, size_t end) {
  constexpr size_t kMinimumMorphemeCount = 2;
  if (end - start < kMinimumMorphemeCount) {
    return false;
  }

  const std::string prefix = extractSubstring(codepoints, start, end - 1);
  const std::string suffix = extractSubstring(codepoints, end - 1, end);
  const auto* copula = dict_manager.lookupExact(prefix, core::PartOfSpeech::Auxiliary);
  const auto* volitional = dict_manager.lookupExact(suffix, core::PartOfSpeech::Auxiliary);
  return copula != nullptr && copula->extended_pos == core::ExtendedPOS::AuxCopulaDa && volitional != nullptr &&
         volitional->extended_pos == core::ExtendedPOS::AuxVolitional;
}

}  // namespace

void Tokenizer::addUnknownCandidates(core::Lattice& lattice, std::string_view text,
                                     const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                     size_t start_pos, const std::vector<normalize::CharType>& char_types) const {
  // A pure-hiragana sequence enclosed by brackets is a parenthetical reading
  // (東京（とうきょう）). It is annotation text, so retain it as one searchable
  // content token instead of a sequence of incidental particles and auxiliaries.
  if (start_pos > 0 && normalize::isOpeningBracket(codepoints[start_pos - 1])) {
    size_t reading_end = start_pos;
    while (reading_end < codepoints.size() && reading_end - start_pos < candidate::kParentheticalReadingMaxLength &&
           char_types[reading_end] == normalize::CharType::Hiragana) {
      ++reading_end;
    }
    if (reading_end > start_pos && reading_end < codepoints.size() &&
        normalize::isClosingBracket(codepoints[reading_end])) {
      lattice.addEdge(extractSubstring(codepoints, start_pos, reading_end), static_cast<uint32_t>(start_pos),
                      static_cast<uint32_t>(reading_end), core::PartOfSpeech::Noun,
                      candidate::kParentheticalReadingCandidateCost, core::LatticeEdge::kIsUnknown, {},
                      dictionary::ConjugationType::None, core::CandidateOrigin::Unknown, candidate::kNoOriginConfidence,
                      {}, core::ExtendedPOS::Noun, "parenthetical_reading");
    }
  }

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
              !ends_with_zu && !candidate.lemma_verified) {
            skip_penalty = true;
            skip_reason = "dict_has_verb_adj";
            break;
          }
          // Case 2: Pure hiragana verb candidate vs short dictionary entry
          // Also allow prolonged sound mark (ー) as part of hiragana sequence
          // for colloquial patterns like すごーい, やばーい, かわいー
          if (result.length <= 2 && candidate.end - candidate.start >= 3) {
            if (allCharsAre(char_types, codepoints, candidate.start, candidate.end, normalize::CharType::Hiragana,
                            /*allow_choon=*/true) &&
                !isCopulaVolitionalSequence(dict_manager_, codepoints, candidate.start, candidate.end)) {
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
          if (grammar::isTeDeSurface(last_char)) {
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
            const auto prefix_codepoints =
                normalize::toCodepoints(result.entry != nullptr ? result.entry->surface : "");
            const bool is_ordinal_noun_prefix =
                result.entry != nullptr && result.entry->pos == core::PartOfSpeech::Noun &&
                prefix_codepoints.size() >= 2 && prefix_codepoints.front() == U'第' &&
                std::all_of(prefix_codepoints.begin() + 1, prefix_codepoints.end(), normalize::isNumeralCodepoint);
            if (result.entry != nullptr && result.length >= 2 && result.length < len && result.length * 2 >= len &&
                (result.entry->pos != core::PartOfSpeech::Noun || is_ordinal_noun_prefix)) {
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
                // A na-adjective stem also forms a lexical comparison compound
                // with 以上 (必要以上, 予想以上). Numeral+counter expressions
                // retain their dedicated split candidates in the counter layer.
                bool tail_is_comparison_bound = (tail_surface == "以上");
                if (tail_is_productive_suffix || tail_is_comparison_bound) {
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
    if (is_pure_hiragana_verb &&
        isCopulaVolitionalSequence(dict_manager_, codepoints, candidate.start, candidate.end)) {
      is_pure_hiragana_verb = false;
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
    bool absorbs_suru_imperative = false;
    if (candidate.pos == core::PartOfSpeech::Verb && candidate.end - candidate.start >= 4) {
      for (size_t split_pos = candidate.start + 2; split_pos < candidate.end; ++split_pos) {
        if (!allCharsAre(char_types, codepoints, candidate.start, split_pos, normalize::CharType::Kanji,
                         /*allow_choon=*/false)) {
          continue;
        }
        if (grammar::isSuruImperativeSurface(extractSubstring(codepoints, split_pos, candidate.end))) {
          absorbs_suru_imperative = true;
          break;
        }
      }
    }
    if (absorbs_suru_imperative) {
      continue;
    }
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
        bool is_verb_ending = utf8::equalsAny(hiragana_suffix, {"て", "で", "って", "んで", "いて", "いで", "し"}) ||
                              candidate.extended_pos == core::ExtendedPOS::VerbRenyokei;

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

}  // namespace suzume::analysis
