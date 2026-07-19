#include "postprocess/postprocessor.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "core/debug.h"
#include "normalize/char_type.h"
#include "normalize/utf8.h"
#include "postprocess/postprocessor_internal.h"

namespace suzume::postprocess {

namespace {

bool isOnlyProlongedSoundMarks(std::string_view surface) {
  const auto codepoints = normalize::toCodepoints(surface);
  return !codepoints.empty() && std::all_of(codepoints.begin(), codepoints.end(), normalize::isProlongedSoundMark);
}

}  // namespace

Postprocessor::Postprocessor(const PostprocessOptions& options) : options_(options), lemmatizer_() {}

Postprocessor::Postprocessor(const dictionary::DictionaryManager* dict_manager, const PostprocessOptions& options)
    : options_(options), lemmatizer_(dict_manager) {}

std::vector<core::Morpheme> Postprocessor::process(std::vector<core::Morpheme> result) const {
  [[maybe_unused]] size_t before_count = 0;

  // NOUN + SUFFIX merging is intentionally NOT applied: tokens stay separate as
  // PREFIX + NOUN + SUFFIX (e.g., お姉さん → お(PREFIX) + 姉(NOUN) + さん(SUFFIX)).

  // Convert PREFIX + VERB to PREFIX + NOUN (renyoukei nominalization)
  // e.g., お願い → お(PREFIX) + 願い(NOUN), not 願い(VERB)
  convertPrefixVerbToNoun(result);
  // Note: this function logs individual changes, so no summary needed

  // Merge consecutive numeric expressions (always applied)
  before_count = result.size();
  result = mergeNumericExpressions(std::move(result));
  if (result.size() != before_count) {
    SUZUME_DEBUG_LOG("[POSTPROC] mergeNumericExpressions: " << before_count << " → " << result.size() << "\n");
  }

  // Keep a na-adjective stem and the attributive copula な as separate
  // grammatical search units.

  // Apply lemmatization
  if (options_.lemmatize) {
    lemmatizer_.lemmatizeAll(result);
    SUZUME_DEBUG_LOG_VERBOSE("[POSTPROC] lemmatize: applied\n");
  }

  resolvePrePrefixMorphemeRoles(result);
  convertPrefixVerbToNoun(result);
  resolvePostPrefixMorphemeRoles(result);

  // Merge verb renyokei + もの → compound noun (食べもの, 飲みもの, etc.)
  // Must run after lemmatize so conj_form is set
  before_count = result.size();
  result = mergeVerbRenyokeiMono(std::move(result));
  if (result.size() != before_count) {
    SUZUME_DEBUG_LOG("[POSTPROC] mergeVerbRenyokeiMono: " << before_count << " → " << result.size() << "\n");
  }

  // Merge nominal stems with the bound temporal noun 途中 (作業途中、移動途中).
  // This is a search unit regardless of the optional general noun-compound mode.
  before_count = result.size();
  result = mergeNounTemporalFormal(std::move(result));
  if (result.size() != before_count) {
    SUZUME_DEBUG_LOG("[POSTPROC] mergeNounTemporalFormal: " << before_count << " → " << result.size() << "\n");
  }

  // Merge lexicalized 副詞 that the lattice mis-split (決して, 大して, ちゃんと)
  before_count = result.size();
  result = mergeLexicalizedAdverbs(std::move(result));
  if (result.size() != before_count) {
    SUZUME_DEBUG_LOG("[POSTPROC] mergeLexicalizedAdverbs: " << before_count << " → " << result.size() << "\n");
  }

  // Merge noun compounds
  if (options_.merge_noun_compounds) {
    before_count = result.size();
    result = mergeNounCompounds(std::move(result));
    if (result.size() != before_count) {
      SUZUME_DEBUG_LOG("[POSTPROC] mergeNounCompounds: " << before_count << " → " << result.size() << "\n");
    }
  }

  // Merge prolonged sound mark (ー) with preceding token
  before_count = result.size();
  result = mergeProlongedSoundMark(std::move(result));
  if (result.size() != before_count) {
    SUZUME_DEBUG_LOG("[POSTPROC] mergeProlongedSoundMark: " << before_count << " → " << result.size() << "\n");
  }

  // Filter unwanted morphemes
  result = filterMorphemes(std::move(result));

  resolveFinalMorphemeRoles(result);

  return result;
}

std::vector<core::Morpheme> Postprocessor::mergeNounCompounds(std::vector<core::Morpheme> morphemes) {
  if (morphemes.empty()) {
    return morphemes;
  }

  std::vector<core::Morpheme> result;
  result.reserve(morphemes.size());

  size_t idx = 0;
  while (idx < morphemes.size()) {
    const auto& current = morphemes[idx];

    // Check if this is a noun that can be merged
    if (current.pos == core::PartOfSpeech::Noun && !current.features.is_formal_noun) {
      // Collect consecutive nouns
      core::Morpheme merged = current;
      size_t merge_end = idx + 1;
      size_t merge_count = 1;

      while (merge_end < morphemes.size()) {
        const auto& next = morphemes[merge_end];
        if (next.pos == core::PartOfSpeech::Noun && !next.features.is_formal_noun) {
          // Merge surface and lemma
          merged.surface += next.surface;
          if (!next.lemma.empty()) {
            merged.lemma += next.lemma;
          } else {
            merged.lemma += next.surface;
          }
          merged.end = next.end;
          merged.end_pos = next.end_pos;
          ++merge_end;
          ++merge_count;
        } else {
          break;
        }
      }

      SUZUME_DEBUG_IF(merge_count > 1) {
        SUZUME_DEBUG_STREAM << "[POSTPROC] Merged " << merge_count << " nouns: ";
        for (size_t i = idx; i < merge_end; ++i) {
          if (i > idx)
            SUZUME_DEBUG_STREAM << " + ";
          SUZUME_DEBUG_STREAM << "\"" << morphemes[i].surface << "\"";
        }
        SUZUME_DEBUG_STREAM << " → \"" << merged.surface << "\"\n";
      }

      result.push_back(merged);
      idx = merge_end;
    } else {
      result.push_back(std::move(morphemes[idx]));
      ++idx;
    }
  }

  return result;
}

std::vector<core::Morpheme> Postprocessor::filterMorphemes(std::vector<core::Morpheme> morphemes) const {
  std::vector<core::Morpheme> result;
  result.reserve(morphemes.size());

  for (auto& morpheme : morphemes) {
    // Skip symbols if option is set
    if (options_.remove_symbols && morpheme.pos == core::PartOfSpeech::Symbol) {
      continue;
    }

    // Skip short morphemes
    if (normalize::utf8Length(morpheme.surface) < options_.min_surface_length) {
      continue;
    }

    result.push_back(std::move(morpheme));
  }

  return result;
}

std::vector<core::Morpheme> Postprocessor::mergeVerbRenyokeiMono(std::vector<core::Morpheme> morphemes) {
  if (morphemes.size() < 2) {
    return morphemes;
  }

  std::vector<core::Morpheme> result;
  result.reserve(morphemes.size());

  for (size_t i = 0; i < morphemes.size(); ++i) {
    // Check: VERB + もの(formal noun) → compound NOUN
    // e.g., 食べ+もの → 食べもの, 飲み+もの → 飲みもの, 乗り+もの → 乗りもの
    if (i + 1 < morphemes.size() && morphemes[i].pos == core::PartOfSpeech::Verb &&
        morphemes[i].conj_form == grammar::ConjForm::Renyokei && morphemes[i + 1].surface == "もの" &&
        morphemes[i + 1].features.is_formal_noun) {
      core::Morpheme merged = morphemes[i];
      merged.surface += morphemes[i + 1].surface;
      merged.pos = core::PartOfSpeech::Noun;
      merged.extended_pos = core::ExtendedPOS::Noun;
      merged.lemma = merged.surface;
      merged.end = morphemes[i + 1].end;
      merged.end_pos = morphemes[i + 1].end_pos;
      SUZUME_DEBUG_LOG("[POSTPROC] Merged verb+もの: \"" << morphemes[i].surface << "\" + \"もの\" → \""
                                                         << merged.surface << "\"\n");
      result.push_back(merged);
      ++i;  // skip もの
      continue;
    }
    result.push_back(std::move(morphemes[i]));
  }

  return result;
}

std::vector<core::Morpheme> Postprocessor::mergeNounTemporalFormal(std::vector<core::Morpheme> morphemes) {
  if (morphemes.size() < 2) {
    return morphemes;
  }

  std::vector<core::Morpheme> result;
  result.reserve(morphemes.size());

  for (size_t idx = 0; idx < morphemes.size(); ++idx) {
    const bool is_bound_temporal_formal =
        idx + 1 < morphemes.size() && morphemes[idx].pos == core::PartOfSpeech::Noun &&
        !morphemes[idx].features.is_formal_noun && morphemes[idx + 1].pos == core::PartOfSpeech::Noun &&
        morphemes[idx + 1].features.is_formal_noun && morphemes[idx + 1].surface == "途中";
    if (!is_bound_temporal_formal) {
      result.push_back(std::move(morphemes[idx]));
      continue;
    }

    core::Morpheme merged = morphemes[idx];
    merged.surface += morphemes[idx + 1].surface;
    merged.lemma = merged.surface;
    merged.extended_pos = core::ExtendedPOS::Noun;
    merged.features.is_formal_noun = false;
    merged.end = morphemes[idx + 1].end;
    merged.end_pos = morphemes[idx + 1].end_pos;
    SUZUME_DEBUG_LOG("[POSTPROC] Merged noun+途中: \"" << morphemes[idx].surface << "\" + \"途中\" → \""
                                                       << merged.surface << "\"\n");
    result.push_back(std::move(merged));
    ++idx;
  }

  return result;
}

std::vector<core::Morpheme> Postprocessor::mergeLexicalizedAdverbs(std::vector<core::Morpheme> morphemes) {
  if (morphemes.size() < 2) {
    return morphemes;
  }

  std::vector<core::Morpheme> result;
  result.reserve(morphemes.size());

  for (size_t i = 0; i < morphemes.size(); ++i) {
    if (i + 1 < morphemes.size() && morphemes[i + 1].pos == core::PartOfSpeech::Particle) {
      const core::Morpheme& cur = morphemes[i];
      const core::Morpheme& nxt = morphemes[i + 1];

      // 決して/大して: the lattice reads these kanji-initial 副詞 as a non-word サ変 連用形
      // (決す/大す) plus て. They cannot be L1 entries because an L1 決して would swallow the 決 of
      // 解決して. Merging on the already-split lattice is safe: 解決して yields 解決|し|て (no 決し
      // token), so only the genuine 副詞 reading (決し/大し with the non-word lemma) reaches here.
      const bool is_sahen_te =
          cur.pos == core::PartOfSpeech::Verb && nxt.surface == "て" &&
          ((cur.surface == "決し" && cur.lemma == "決す") || (cur.surface == "大し" && cur.lemma == "大す"));

      // ちゃんと: 接尾辞 ちゃん + と. Gated on the previous token not being a Noun so 赤ちゃんと
      // (赤|ちゃん|と) keeps 赤ちゃん together (→ 赤|ちゃんと) rather than merging the ちゃん away.
      const bool is_chanto = cur.surface == "ちゃん" && cur.pos == core::PartOfSpeech::Suffix && nxt.surface == "と" &&
                             (result.empty() || result.back().pos != core::PartOfSpeech::Noun);

      if (is_sahen_te || is_chanto) {
        core::Morpheme merged = cur;
        merged.surface += nxt.surface;
        merged.pos = core::PartOfSpeech::Adverb;
        merged.extended_pos = core::ExtendedPOS::Adverb;
        merged.lemma = merged.surface;
        merged.conj_type = dictionary::ConjugationType::None;
        merged.conj_form = grammar::ConjForm::Base;
        merged.end = nxt.end;
        merged.end_pos = nxt.end_pos;
        SUZUME_DEBUG_LOG("[POSTPROC] Merged lexicalized adverb: \"" << cur.surface << "\"+\"" << nxt.surface
                                                                    << "\" → \"" << merged.surface << "\"\n");
        result.push_back(merged);
        ++i;  // skip the particle
        continue;
      }
    }
    result.push_back(std::move(morphemes[i]));
  }

  return result;
}

std::vector<core::Morpheme> Postprocessor::mergeProlongedSoundMark(std::vector<core::Morpheme> morphemes) {
  if (morphemes.size() < 2) {
    return morphemes;
  }

  std::vector<core::Morpheme> result;
  result.reserve(morphemes.size());

  for (size_t i = 0; i < morphemes.size(); ++i) {
    if (i + 1 < morphemes.size()) {
      const auto& next = morphemes[i + 1];
      if (isOnlyProlongedSoundMarks(next.surface)) {
        const auto& current = morphemes[i];
        if (current.pos != core::PartOfSpeech::Symbol) {
          core::Morpheme merged = current;
          merged.surface += next.surface;
          merged.end = next.end;
          merged.end_pos = next.end_pos;
          if (!merged.lemma.empty()) {
            merged.lemma += next.surface;
          }

          size_t skip = i + 2;
          while (skip < morphemes.size()) {
            if (!isOnlyProlongedSoundMarks(morphemes[skip].surface)) {
              break;
            }
            merged.surface += morphemes[skip].surface;
            if (!merged.lemma.empty()) {
              merged.lemma += morphemes[skip].surface;
            }
            merged.end = morphemes[skip].end;
            merged.end_pos = morphemes[skip].end_pos;
            ++skip;
          }

          SUZUME_DEBUG_LOG("[POSTPROC] Merged prolonged sound mark: \"" << current.surface << "\" + \"ー\" → \""
                                                                        << merged.surface << "\"\n");
          result.push_back(merged);
          i = skip - 1;
          continue;
        }
      }
    }
    result.push_back(std::move(morphemes[i]));
  }

  return result;
}

}  // namespace suzume::postprocess
