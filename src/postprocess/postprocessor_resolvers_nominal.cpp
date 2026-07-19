#include <string_view>
#include <utility>

#include "core/debug.h"
#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "grammar/conjugation.h"
#include "grammar/honorific_verbs.h"
#include "grammar/inflection_scorer_constants.h"
#include "normalize/char_type.h"
#include "normalize/utf8.h"
#include "postprocess/postprocessor.h"
#include "postprocess/postprocessor_resolvers_internal.h"

namespace suzume::postprocess::resolver {

// A duration quantity directly followed by かかる requires the predicate
// reading. The homographic determiner is only available before a noun.
void resolveDurationPredicateKakaru(std::vector<core::Morpheme>& result) {
  for (size_t idx = 1; idx < result.size(); ++idx) {
    const auto& duration = result[idx - 1];
    auto& kakaru = result[idx];
    if (duration.pos != core::PartOfSpeech::Noun || !isCounterDurationNoun(duration.surface) ||
        kakaru.surface != "かかる" || kakaru.pos != core::PartOfSpeech::Determiner) {
      continue;
    }
    kakaru.pos = core::PartOfSpeech::Verb;
    kakaru.extended_pos = core::ExtendedPOS::VerbShuushikei;
    kakaru.lemma = "かかる";
    kakaru.conj_type = dictionary::ConjugationType::GodanRa;
    kakaru.conj_form = grammar::ConjForm::Base;
  }
}

// A compound-verb 連用形 (積み重ね, 組み立て, 話し合い, 繰り返し) is systematically usable as
// a 連用形転成名詞. Its shape: starts with kanji, ends with hiragana, is composed only of
// kanji and hiragana, and contains at least two disjoint kanji runs (V1漢字…V2漢字…). The
// two-run gate is the compound-origin restriction: it excludes single-verb renyokei
// (読み/書き/続け — one kanji run, genuinely verbal far more often) and hiragana-stem forms
// (やり直し), keeping only V1+V2 compounds whose nominal reading is reliable.
bool isCompoundRenyokeiShape(const std::string& surface) {
  auto codepoints = normalize::toCodepoints(surface);
  if (codepoints.size() < 3) {
    return false;
  }
  if (normalize::classifyChar(codepoints.front()) != normalize::CharType::Kanji ||
      normalize::classifyChar(codepoints.back()) != normalize::CharType::Hiragana) {
    return false;
  }
  // A 連用形 ends in an i-row (godan 話し合い→い) or e-row (ichidan 積み重ね→ね, 組み立て→て)
  // mora; a base form (終止形) ends in a う-row godan terminal (呼び出す, 受け継ぐ) or ichidan
  // る. Reject base-form endings so a compound 終止形 the lattice still tags VerbRenyokei
  // (呼び出す, 着付け直す) is not wrongly nominalized — only true renyokei nominalize.
  const char32_t last_cp = codepoints.back();
  if (last_cp == U'う' || last_cp == U'く' || last_cp == U'ぐ' || last_cp == U'す' || last_cp == U'つ' ||
      last_cp == U'ぬ' || last_cp == U'ぶ' || last_cp == U'む' || last_cp == U'る') {
    return false;
  }
  size_t kanji_runs = 0;
  bool in_kanji_run = false;
  for (char32_t code : codepoints) {
    const normalize::CharType type = normalize::classifyChar(code);
    if (type != normalize::CharType::Kanji && type != normalize::CharType::Hiragana) {
      return false;
    }
    const bool is_kanji = (type == normalize::CharType::Kanji);
    if (is_kanji && !in_kanji_run) {
      ++kanji_runs;
    }
    in_kanji_run = is_kanji;
  }
  return kanji_runs >= 2;
}

// Right context that forces the nominal reading of a compound renyokei: a case particle
// (が/を/に/で/へ/…) or a topic/binding particle (は/も) marking it as an argument.
bool isNominalForcingParticle(const core::Morpheme& next) {
  return next.pos == core::PartOfSpeech::Particle && (next.extended_pos == core::ExtendedPOS::ParticleCase ||
                                                      next.extended_pos == core::ExtendedPOS::ParticleTopic);
}

// A noun followed by で is normally the case-particle reading (本で、電車で、
// 雪国であった). Copular continuations (本である、本ではない、本でしかない)
// retain the auxiliary reading, so resolve the ambiguity after both adjacent
// morphemes have been selected.
void resolveNominalCaseDe(std::vector<core::Morpheme>& result) {
  for (size_t idx = 0; idx < result.size(); ++idx) {
    auto& de = result[idx];
    if (de.surface != "で") {
      continue;
    }
    core::Morpheme* successor = idx + 1 < result.size() ? &result[idx + 1] : nullptr;
    if (idx == 0) {
      if (successor != nullptr && utf8::equalsAny(successor->surface, {"ござい", "ござる"})) {
        retagCopulaDa(de);
      }
      continue;
    }
    const auto& predecessor = result[idx - 1];
    const bool is_nominal = predecessor.pos == core::PartOfSpeech::Noun ||
                            predecessor.pos == core::PartOfSpeech::Pronoun ||
                            predecessor.pos == core::PartOfSpeech::Suffix;
    const bool is_na_adjective = predecessor.extended_pos == core::ExtendedPOS::AdjNaAdj;
    const bool is_contracted_negative =
        predecessor.extended_pos == core::ExtendedPOS::AuxNegativeNu && predecessor.surface == "ん";
    const bool is_tendency_suffix = predecessor.extended_pos == core::ExtendedPOS::SuffixTendency;
    const bool is_adverbial_predicate = predecessor.pos == core::PartOfSpeech::Adverb;
    if (!is_nominal && !is_na_adjective && !is_tendency_suffix && !is_adverbial_predicate && !is_contracted_negative) {
      continue;
    }
    const bool follows_negative =
        successor != nullptr && utf8::equalsAny(successor->surface, {"ない", "なく", "なかっ", "なかろ", "なけれ"});
    const bool topic_starts_copular_negative =
        successor != nullptr && successor->extended_pos == core::ExtendedPOS::ParticleTopic &&
        ((successor->surface == "は" && idx + 2 < result.size() &&
          (utf8::equalsAny(result[idx + 2].surface, {"ない", "なく", "なかっ", "なかろ"}) ||
           (is_na_adjective && (utf8::equalsAny(result[idx + 2].surface, {"ござい", "ござる"}) ||
                                result[idx + 2].extended_pos == core::ExtendedPOS::AuxGozaru)))) ||
         (successor->surface == "も" && ((is_na_adjective && idx + 2 < result.size() &&
                                          utf8::equalsAny(result[idx + 2].surface, {"ない", "なく", "なかっ"})) ||
                                         is_adverbial_predicate || is_contracted_negative)));
    const bool binding_is_copular = successor != nullptr &&
                                    successor->extended_pos == core::ExtendedPOS::ParticleBinding &&
                                    (successor->surface != "も" || is_adverbial_predicate || is_contracted_negative);
    const bool is_copular_continuation =
        successor != nullptr &&
        (successor->extended_pos == core::ExtendedPOS::AuxGozaru || follows_negative || topic_starts_copular_negative ||
         binding_is_copular ||
         (successor->extended_pos == core::ExtendedPOS::AuxCopulaDa && successor->surface != "あっ") ||
         (is_na_adjective && successor->surface == "あっ") ||
         utf8::equalsAny(successor->surface, {"ある", "あり", "あろ", "あれ", "ござい", "ござる"}));
    const bool na_adjective_coordination =
        is_na_adjective && successor != nullptr && successor->extended_pos == core::ExtendedPOS::AdjBasic;
    // A na-adjective cannot take the case-particle reading of で. Its
    // continuative form remains the copula even before an independent noun
    // (無鉄砲で小供の時から).
    if (is_na_adjective || is_tendency_suffix || is_copular_continuation || na_adjective_coordination) {
      if (is_tendency_suffix) {
        auto& tendency = result[idx - 1];
        tendency.pos = core::PartOfSpeech::Suffix;
        tendency.lemma = "がち";
      }
      retagCopulaDa(de);
      if (successor != nullptr && successor->surface == "ござる") {
        auto& gozaru = *successor;
        gozaru.pos = core::PartOfSpeech::Auxiliary;
        gozaru.extended_pos = core::ExtendedPOS::AuxGozaru;
        gozaru.lemma = "ござる";
        gozaru.conj_type = dictionary::ConjugationType::GodanRa;
        gozaru.conj_form = grammar::ConjForm::Base;
      }
      continue;
    }
    de.pos = core::PartOfSpeech::Particle;
    de.extended_pos = core::ExtendedPOS::ParticleCase;
    de.lemma = "で";
    de.conj_type = dictionary::ConjugationType::None;
    de.conj_form = grammar::ConjForm::Base;
  }
}

// In the formal conjectural copula であろう, あろ is the irrealis of the
// copular auxiliary ある. The same surface remains a lexical verb outside
// this directly preceding copula context.
void resolveCopularAro(std::vector<core::Morpheme>& result) {
  for (size_t idx = 1; idx < result.size(); ++idx) {
    const auto& copula = result[idx - 1];
    auto& aro = result[idx];
    if (copula.extended_pos != core::ExtendedPOS::AuxCopulaDa || copula.surface != "で" || aro.surface != "あろ" ||
        aro.extended_pos != core::ExtendedPOS::VerbMizenkei || aro.lemma != "ある") {
      continue;
    }
    aro.pos = core::PartOfSpeech::Auxiliary;
    aro.extended_pos = core::ExtendedPOS::AuxCopulaDa;
    aro.conj_type = dictionary::ConjugationType::GodanRa;
    aro.conj_form = grammar::ConjForm::Mizenkei;
  }
}

// A noun plus the conditional particle なら is not the mizenkei of なる.
// Resolve the shared surface after its nominal left context is available.
void resolveNominalConditionalNara(std::vector<core::Morpheme>& result) {
  for (size_t idx = 1; idx < result.size(); ++idx) {
    const auto& predecessor = result[idx - 1];
    auto& nara = result[idx];
    if (predecessor.pos != core::PartOfSpeech::Noun || nara.surface != "なら" ||
        nara.extended_pos != core::ExtendedPOS::VerbMizenkei) {
      continue;
    }
    nara.pos = core::PartOfSpeech::Particle;
    nara.extended_pos = core::ExtendedPOS::ParticleConj;
    nara.lemma = "なら";
    nara.conj_type = dictionary::ConjugationType::None;
    nara.conj_form = grammar::ConjForm::Base;
  }
}

// After the nominal case-particle reading of で, あっ is the lexical verb
// ある in its sokuonbin form (雪国であった).  The lattice shares this surface
// with the formal-copula auxiliary, so retain the selected boundary and
// resolve the public POS from the preceding nominal construction.
void resolveNominalDeAru(std::vector<core::Morpheme>& result) {
  for (size_t idx = 1; idx + 1 < result.size(); ++idx) {
    const auto& de = result[idx - 1];
    auto& aru = result[idx];
    const auto& tense = result[idx + 1];
    if (de.extended_pos != core::ExtendedPOS::ParticleCase || de.surface != "で" || aru.surface != "あっ" ||
        tense.extended_pos != core::ExtendedPOS::AuxTenseTa) {
      continue;
    }
    aru.pos = core::PartOfSpeech::Verb;
    aru.extended_pos = core::ExtendedPOS::VerbOnbinkei;
    aru.lemma = "ある";
    aru.conj_type = dictionary::ConjugationType::GodanRa;
    aru.conj_form = grammar::ConjForm::Onbinkei;
  }
}

// 以上 is a comparison-boundary noun in the public token contract (本である
// 以上、十倍以上). The lattice also emits an adverbial homograph, so restore the
// nominal category after token boundaries have been selected.
void resolveComparisonNoun(std::vector<core::Morpheme>& result) {
  for (auto& morpheme : result) {
    if (morpheme.surface != "以上") {
      continue;
    }
    morpheme.pos = core::PartOfSpeech::Noun;
    morpheme.extended_pos = core::ExtendedPOS::Noun;
    morpheme.lemma = "以上";
    morpheme.conj_type = dictionary::ConjugationType::None;
    morpheme.conj_form = grammar::ConjForm::Base;
  }
}

// ござい followed by the polite auxiliary is either the lexical existential
// verb ござる (ございます, そこにございました) or the dependent honorific
// auxiliary in a copular/polite expression (でございます, ありがとうござい
// ます). The preceding boundary distinguishes the two readings.
void resolveGozaruPoliteAuxiliary(std::vector<core::Morpheme>& result) {
  for (size_t idx = 0; idx + 1 < result.size(); ++idx) {
    auto& gozai = result[idx];
    const auto& polite = result[idx + 1];
    if (gozai.surface != "ござい" || polite.extended_pos != core::ExtendedPOS::AuxTenseMasu) {
      continue;
    }
    const bool lexical_existence = idx == 0 || (result[idx - 1].surface == "に" &&
                                                result[idx - 1].extended_pos == core::ExtendedPOS::ParticleCase);
    if (lexical_existence) {
      gozai.pos = core::PartOfSpeech::Verb;
      gozai.extended_pos = core::ExtendedPOS::VerbRenyokei;
      gozai.lemma = "ござる";
      gozai.conj_type = dictionary::ConjugationType::GodanRa;
      gozai.conj_form = grammar::ConjForm::Renyokei;
      continue;
    }
    gozai.pos = core::PartOfSpeech::Auxiliary;
    gozai.extended_pos = core::ExtendedPOS::AuxGozaru;
    gozai.lemma = "ござる";
    gozai.conj_type = dictionary::ConjugationType::None;
    gozai.conj_form = grammar::ConjForm::Base;
  }
}

// The nominalizing さ follows an adjective stem (高+さ、儚+さ、っぽ+さ).
// It is homographic with the final particle, whose candidate can win before
// the preceding adjective's category is available to the lattice scorer.
void resolveAdjectiveNominalizerSa(std::vector<core::Morpheme>& result) {
  for (size_t idx = 1; idx < result.size(); ++idx) {
    auto& suffix = result[idx];
    const auto& adjective = result[idx - 1];
    if (suffix.surface != "さ" || adjective.pos != core::PartOfSpeech::Adjective) {
      continue;
    }
    suffix.pos = core::PartOfSpeech::Suffix;
    suffix.extended_pos = core::ExtendedPOS::Suffix;
    suffix.lemma = "さ";
    suffix.conj_type = dictionary::ConjugationType::None;
    suffix.conj_form = grammar::ConjForm::Base;
    if (idx + 1 < result.size() && result[idx + 1].surface == "そう") {
      auto& sou = result[idx + 1];
      retagAppearanceSou(sou);
    }
  }
}

// In an indefinite phrase followed by a content predicate, homographic で is
// the case particle (どこ+か+で+確認する), not the copular continuative.
// Keep the copular reading before auxiliaries and lexical ある (何かである).
void resolveIndefiniteCaseDe(std::vector<core::Morpheme>& result) {
  for (size_t idx = 2; idx + 1 < result.size(); ++idx) {
    const auto& interrogative = result[idx - 2];
    const auto& indefinite = result[idx - 1];
    auto& de = result[idx];
    const auto& predicate = result[idx + 1];
    if (interrogative.extended_pos != core::ExtendedPOS::PronounInterrogative ||
        indefinite.extended_pos != core::ExtendedPOS::ParticleAdverbial || indefinite.surface != "か" ||
        de.surface != "で") {
      continue;
    }
    const bool starts_copular_negative = idx + 2 < result.size() &&
                                         predicate.extended_pos == core::ExtendedPOS::ParticleTopic &&
                                         result[idx + 2].extended_pos == core::ExtendedPOS::AuxNegativeNai;
    if (starts_copular_negative) {
      retagCopulaDa(de);
      continue;
    }
    if (de.extended_pos != core::ExtendedPOS::AuxCopulaDa || predicate.pos == core::PartOfSpeech::Auxiliary ||
        (predicate.pos == core::PartOfSpeech::Verb && predicate.lemma == "ある")) {
      continue;
    }
    retag(de, core::PartOfSpeech::Particle, core::ExtendedPOS::ParticleCase, "で", dictionary::ConjugationType::None,
          grammar::ConjForm::Base);
  }
}

// A continuative form is used as a nominal predicate before a copula, a
// nominalizing suffix, or the copular change construction 〜に+なる. The
// lattice keeps a verb candidate for the same surface, but these contexts
// require a nominal reading (疲れ気味だ、丸出しだ、丸出しになる). Honorific
// お/ご+連用形+に+なる remains verbal.
void resolveNominalizedRenyokeiPredicate(std::vector<core::Morpheme>& result) {
  for (size_t idx = 0; idx < result.size(); ++idx) {
    auto& stem = result[idx];
    if (stem.pos != core::PartOfSpeech::Verb || stem.extended_pos != core::ExtendedPOS::VerbRenyokei ||
        idx + 1 >= result.size()) {
      continue;
    }

    const auto& next = result[idx + 1];
    const bool before_copula = next.extended_pos == core::ExtendedPOS::AuxCopulaDa;
    const bool before_nominalizing_suffix =
        next.pos == core::PartOfSpeech::Suffix && grammar::isRenyokeiNominalizingSuffix(next.surface);
    const bool before_naru = next.extended_pos == core::ExtendedPOS::ParticleCase && next.surface == "に" &&
                             idx + 2 < result.size() && result[idx + 2].pos == core::PartOfSpeech::Verb &&
                             result[idx + 2].lemma == "なる";
    // A wa-row continuative directly after に cannot take the negative
    // auxiliary as a finite predicate. It is the nominal stem in the
    // certainty construction (N に + V連用形 + ない), so retain the noun
    // candidate rather than the homographic verb analysis.
    const bool nominal_negative_after_case =
        idx > 0 && result[idx - 1].extended_pos == core::ExtendedPOS::ParticleCase && result[idx - 1].surface == "に" &&
        next.extended_pos == core::ExtendedPOS::AuxNegativeNai &&
        stem.conj_type == dictionary::ConjugationType::GodanWa;
    const bool honorific_naru = before_naru && idx > 0 && result[idx - 1].pos == core::PartOfSpeech::Prefix &&
                                grammar::isHonorificPrefix(result[idx - 1].surface);
    if ((!before_copula && !before_nominalizing_suffix && !before_naru && !nominal_negative_after_case) ||
        honorific_naru) {
      continue;
    }

    stem.pos = core::PartOfSpeech::Noun;
    stem.extended_pos = core::ExtendedPOS::Noun;
    stem.lemma = stem.surface;
    stem.conj_type = dictionary::ConjugationType::None;
    stem.conj_form = grammar::ConjForm::Base;
  }
}

}  // namespace suzume::postprocess::resolver
