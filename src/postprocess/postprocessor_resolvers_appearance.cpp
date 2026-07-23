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

// The appearance construction is ambiguous at the lattice level: 形容詞語幹+
// そう can use either the appearance auxiliary or the na-adjectival そう
// candidate. An adjectival predicate with a predicative copula takes the
// na-adjectival reading, while a verbal predicate takes the appearance
// auxiliary. Attributive な is deliberately excluded because it is not a
// predicative copula onset.
bool isPredicativeCopula(const core::Morpheme& morpheme) {
  if (morpheme.extended_pos != core::ExtendedPOS::AuxCopulaDa &&
      morpheme.extended_pos != core::ExtendedPOS::AuxCopulaDesu) {
    return false;
  }
  return morpheme.surface == "だ" || morpheme.surface == "だっ" || morpheme.surface == "です" ||
         morpheme.surface == "でし";
}

void resolveAppearanceSouPredicate(std::vector<core::Morpheme>& result) {
  if (result.size() >= 3 && result[0].surface == "そう" && result[1].surface == "で" && result[2].surface == "ござい") {
    auto& sou = result[0];
    retagAdverbialSou(sou);
  }
  if (result.size() >= 2 && result[0].surface == "そう" && result[0].pos == core::PartOfSpeech::Adverb &&
      result[1].surface == "で" && (result.size() < 3 || result[2].surface != "ござい")) {
    auto& sou = result[0];
    retagNaAdjectivalSou(sou);
  }

  for (size_t idx = 1; idx < result.size(); ++idx) {
    const auto& predicate = result[idx - 1];
    auto& sou = result[idx];
    if (predicate.extended_pos != core::ExtendedPOS::AuxAspectShimau || sou.surface != "そう" ||
        sou.pos != core::PartOfSpeech::Adverb) {
      continue;
    }
    retagAppearanceSou(sou);
  }

  // A productive -しい adjective loses its final い before appearance そう.
  // The remaining ...し stem can be selected as a plain noun homograph, but
  // NounVerbal (話し+そう) must keep its verbal reading.  The closed そう
  // follower resolves only the plain-noun homograph without enumerating
  // adjective lexemes.
  for (size_t idx = 0; idx + 1 < result.size(); ++idx) {
    auto& stem = result[idx];
    const auto& sou = result[idx + 1];
    if (stem.extended_pos != core::ExtendedPOS::Noun || !utf8::endsWith(stem.surface, "し") || sou.surface != "そう") {
      continue;
    }
    retag(stem, core::PartOfSpeech::Adjective, core::ExtendedPOS::AdjStem, stem.surface + "い",
          dictionary::ConjugationType::IAdjective, grammar::ConjForm::Base);
  }

  for (size_t idx = 1; idx + 1 < result.size(); ++idx) {
    auto& sou = result[idx];
    auto& predicate = result[idx - 1];
    const auto& copula = result[idx + 1];
    if (sou.surface != "そう" || !isPredicativeCopula(copula)) {
      continue;
    }

    const bool follows_nominalized_adjective = predicate.pos == core::PartOfSpeech::Suffix &&
                                               predicate.surface == "さ" && idx >= 2 &&
                                               result[idx - 2].pos == core::PartOfSpeech::Adjective;
    if ((predicate.pos == core::PartOfSpeech::Adjective || follows_nominalized_adjective) &&
        sou.extended_pos == core::ExtendedPOS::AuxAppearanceSou) {
      retagNaAdjectivalSou(sou);
      continue;
    }
    if ((predicate.extended_pos == core::ExtendedPOS::VerbRenyokei ||
         predicate.extended_pos == core::ExtendedPOS::VerbShuushikei ||
         predicate.extended_pos == core::ExtendedPOS::AuxAspectIru) &&
        sou.extended_pos == core::ExtendedPOS::AdjNaAdj) {
      retagAppearanceSou(sou);
    }
    if (sou.pos == core::PartOfSpeech::Adverb) {
      const bool verbal_predicate = predicate.extended_pos == core::ExtendedPOS::VerbRenyokei ||
                                    predicate.extended_pos == core::ExtendedPOS::VerbShuushikei ||
                                    predicate.extended_pos == core::ExtendedPOS::AuxAspectShimau ||
                                    predicate.extended_pos == core::ExtendedPOS::AuxAspectIru;
      if (verbal_predicate) {
        retagAppearanceSou(sou);
      } else {
        retagNaAdjectivalSou(sou);
      }
    }
  }

  for (size_t idx = 1; idx + 1 < result.size(); ++idx) {
    const auto& preceding = result[idx - 1];
    auto& sou = result[idx];
    const auto& na = result[idx + 1];
    const bool has_lexical_predicate = preceding.extended_pos == core::ExtendedPOS::VerbRenyokei ||
                                       preceding.extended_pos == core::ExtendedPOS::VerbShuushikei ||
                                       preceding.extended_pos == core::ExtendedPOS::AdjStem ||
                                       preceding.extended_pos == core::ExtendedPOS::AuxAspectShimau ||
                                       preceding.extended_pos == core::ExtendedPOS::AuxAspectIru;
    if (sou.surface != "そう" || sou.pos != core::PartOfSpeech::Adverb || na.surface != "な" || has_lexical_predicate) {
      continue;
    }
    retagNaAdjectivalSou(sou);
    auto& copula = result[idx + 1];
    retagCopulaDa(copula);
  }

  for (size_t idx = 1; idx + 1 < result.size(); ++idx) {
    const auto& stem = result[idx - 1];
    auto& sou = result[idx];
    auto& na = result[idx + 1];
    if (sou.surface != "そう" || na.surface != "な" ||
        (stem.pos != core::PartOfSpeech::Noun && stem.pos != core::PartOfSpeech::Verb &&
         stem.pos != core::PartOfSpeech::Adjective)) {
      continue;
    }
    retagAppearanceSou(sou);
    retagCopulaDa(na);
  }

  for (size_t idx = 1; idx + 1 < result.size(); ++idx) {
    const auto& stem = result[idx - 1];
    auto& sou = result[idx];
    const auto& particle = result[idx + 1];
    if (stem.extended_pos != core::ExtendedPOS::AdjStem || sou.surface != "そう" || particle.surface != "に" ||
        sou.pos != core::PartOfSpeech::Adverb) {
      continue;
    }
    retagAppearanceSou(sou);
  }
}

void resolveProgressiveContractionNominalizer(std::vector<core::Morpheme>& result) {
  for (size_t idx = 1; idx + 1 < result.size(); ++idx) {
    auto& contraction = result[idx];
    const auto& connective = result[idx - 1];
    const auto& continuation = result[idx + 1];
    if (contraction.surface != "ん" || contraction.extended_pos != core::ExtendedPOS::AuxNegativeNu ||
        connective.extended_pos != core::ExtendedPOS::ParticleConj || !grammar::isTeDeSurface(connective.surface) ||
        (continuation.extended_pos != core::ExtendedPOS::ParticleNo &&
         continuation.extended_pos != core::ExtendedPOS::AuxCopulaDa)) {
      continue;
    }
    retag(contraction, core::PartOfSpeech::Particle, core::ExtendedPOS::ParticleNo, "の",
          dictionary::ConjugationType::None, grammar::ConjForm::Base);
  }
}

// In the predicative pattern XにYない, a regular noun Y takes the independent
// negative adjective (本に相違ない), not the verbal negative auxiliary. Keep
// renyokei-derived and specially classified nouns out of this rule: their
// auxiliary analyses remain available for expressions such as 間違いない.
void resolveNominalPredicateNai(std::vector<core::Morpheme>& result) {
  for (size_t idx = 1; idx < result.size(); ++idx) {
    auto& negative = result[idx];
    auto& predicate = result[idx - 1];
    if (negative.surface != "ない") {
      continue;
    }

    const bool is_godan_renyokei = predicate.extended_pos == core::ExtendedPOS::VerbRenyokei &&
                                   predicate.conj_type >= dictionary::ConjugationType::GodanKa &&
                                   predicate.conj_type <= dictionary::ConjugationType::GodanWa &&
                                   grammar::endsWithIRow(predicate.surface);
    // A Godan continuative stem cannot select the negative auxiliary; Godan
    // negation requires the a-row mizenkei (頼ら+ない, not 頼り+ない). When
    // that impossible path is selected, the homographic stem is a deverbal
    // noun followed by the independent adjective.
    if (predicate.pos == core::PartOfSpeech::Verb && is_godan_renyokei &&
        negative.extended_pos == core::ExtendedPOS::AuxNegativeNai) {
      retagNounSurface(predicate);
      retagBasicNegativeAdjective(negative);
      continue;
    }

    // The open-class kanji candidate generator can occasionally create an
    // unattested 〜る verb from a nominal predicate (相違→相違る). In XにYない,
    // its all-kanji, non-dictionary renyokei shape is unambiguously nominal.
    // Restore that reading before resolving the independent negative.
    if (idx >= 2 && predicate.pos == core::PartOfSpeech::Verb &&
        predicate.extended_pos == core::ExtendedPOS::VerbRenyokei && !predicate.is_from_dictionary &&
        grammar::isAllKanji(predicate.surface) && utf8::endsWith(predicate.lemma, "る")) {
      const auto& marker = result[idx - 2];
      if (marker.extended_pos == core::ExtendedPOS::ParticleCase && marker.surface == "に") {
        retagNounSurface(predicate);
      }
    }

    if (predicate.pos != core::PartOfSpeech::Noun) {
      continue;
    }

    // The negative auxiliary attaches to inflecting predicates, never
    // directly to a noun. Bare nominal negatives therefore use the
    // independent adjective ない (問題+ない, 関係+ない).
    if (negative.extended_pos == core::ExtendedPOS::AuxNegativeNai &&
        predicate.extended_pos != core::ExtendedPOS::ParticleBinding) {
      retagBasicNegativeAdjective(negative);
      continue;
    }

    // In an attributive nominal-negative expression (頼り+ない+返事,
    // 問題+ない+方法), ない is the independent basic adjective modifying the
    // following nominal, not a dependent verbal auxiliary.
    if (idx + 1 < result.size() && result[idx + 1].pos == core::PartOfSpeech::Noun &&
        negative.extended_pos == core::ExtendedPOS::AuxNegativeNai) {
      retagBasicNegativeAdjective(negative);
      continue;
    }

    // A noun must not inherit the binding-particle category. When a legacy
    // dictionary entry does so, restore the negative auxiliary reading that
    // belongs to the nominalized predicate (間違い+ない).
    if (predicate.extended_pos == core::ExtendedPOS::ParticleBinding &&
        negative.extended_pos == core::ExtendedPOS::AdjBasic) {
      negative.pos = core::PartOfSpeech::Auxiliary;
      negative.extended_pos = core::ExtendedPOS::AuxNegativeNai;
      negative.lemma = "ない";
      continue;
    }

    if (idx < 2 || negative.extended_pos != core::ExtendedPOS::AuxNegativeNai ||
        predicate.extended_pos != core::ExtendedPOS::Noun || predicate.surface == "ちがい") {
      continue;
    }
    const auto& marker = result[idx - 2];
    if (marker.extended_pos != core::ExtendedPOS::ParticleCase || marker.surface != "に") {
      continue;
    }
    retagBasicNegativeAdjective(negative);
  }
}

// A nominal predicate followed by ではない forms a copular negative chain
// (本+で+は+ない, 静か+で+は+ない, はず+で+は+ない). Preserve the
// lattice's copula reading and give the final ない its auxiliary reading; the
// generic topic→ない bias otherwise treats it as an existence adjective.
void resolveCopularNegative(std::vector<core::Morpheme>& result) {
  if (result.size() >= 2 && result[0].surface == "で" && result[1].surface == "ない") {
    auto& de = result[0];
    auto& negative = result[1];
    retagCopulaDa(de);
    retagNegativeNai(negative);
  }
  for (size_t idx = 1; idx + 2 < result.size(); ++idx) {
    const auto& de = result[idx];
    const auto& predicate = result[idx - 1];
    const auto& topic = result[idx + 1];
    auto& negative = result[idx + 2];
    const bool is_nominal_predicate =
        predicate.pos == core::PartOfSpeech::Noun || predicate.pos == core::PartOfSpeech::Pronoun ||
        predicate.pos == core::PartOfSpeech::Suffix || predicate.extended_pos == core::ExtendedPOS::AdjNaAdj ||
        predicate.extended_pos == core::ExtendedPOS::ParticleNo ||
        predicate.extended_pos == core::ExtendedPOS::ParticleAdverbial ||
        predicate.extended_pos == core::ExtendedPOS::ParticleBinding;
    if (!is_nominal_predicate || de.surface != "で" || de.extended_pos != core::ExtendedPOS::AuxCopulaDa ||
        topic.extended_pos != core::ExtendedPOS::ParticleTopic || topic.surface != "は" || negative.surface != "ない") {
      continue;
    }
    retagNegativeNai(negative);
  }

  // The direct copular negative (静かでない, 好きでない) has no topical
  // particle, but its final negative is still the dependent auxiliary.
  for (size_t idx = 1; idx < result.size(); ++idx) {
    const auto& de = result[idx - 1];
    auto& negative = result[idx];
    if (de.surface != "で" || de.extended_pos != core::ExtendedPOS::AuxCopulaDa || negative.surface != "ない") {
      continue;
    }
    retagNegativeNai(negative);
  }
}

// A na-adjective followed by であった uses the formal copula in its
// sokuonbin form. The same あっ remains lexical after a nominal case particle.
void resolveNaAdjectiveCopularPast(std::vector<core::Morpheme>& result) {
  for (size_t idx = 1; idx + 1 < result.size(); ++idx) {
    const auto& de = result[idx];
    const auto& predicate = result[idx - 1];
    auto& aru = result[idx + 1];
    if (predicate.extended_pos != core::ExtendedPOS::AdjNaAdj || de.surface != "で" ||
        de.extended_pos != core::ExtendedPOS::AuxCopulaDa || aru.surface != "あっ") {
      continue;
    }
    retag(aru, core::PartOfSpeech::Auxiliary, core::ExtendedPOS::AuxCopulaDa, "ある",
          dictionary::ConjugationType::GodanRa, grammar::ConjForm::Onbinkei);
  }
}

// The conditional allomorph たら is the past auxiliary after the sokuonbin
// copula (本+だっ+たら, 静か+だっ+たら), not an independent conjunction.
void resolveCopularPastConditional(std::vector<core::Morpheme>& result) {
  for (size_t idx = 1; idx < result.size(); ++idx) {
    const auto& copula = result[idx - 1];
    auto& conditional = result[idx];
    if (copula.extended_pos != core::ExtendedPOS::AuxCopulaDa || !utf8::endsWith(copula.surface, "っ") ||
        conditional.surface != "たら" || conditional.extended_pos != core::ExtendedPOS::ParticleConj) {
      continue;
    }
    retag(conditional, core::PartOfSpeech::Auxiliary, core::ExtendedPOS::AuxTenseTa, "た",
          dictionary::ConjugationType::None, grammar::ConjForm::Base);
  }
}

// These forms can expose one another through local retagging.  Keep their
// ordering together so callers can deliberately run the same copular pass
// again after an intervening resolver changes the available context.
void resolveCopularForms(std::vector<core::Morpheme>& result) {
  resolveCopularNegative(result);
  resolveNaAdjectiveCopularPast(result);
  resolveCopularPastConditional(result);
}

// The closed suffix がち is a na-adjectival predicate before copular で and
// an i-adjective continuation (がちでうまい). The lattice also offers a noun
// homograph, so resolve the suffix category from the complete local chain.
void resolveTendencySuffixCopula(std::vector<core::Morpheme>& result) {
  for (size_t idx = 0; idx + 2 < result.size(); ++idx) {
    auto& tendency = result[idx];
    auto& de = result[idx + 1];
    const auto& adjective = result[idx + 2];
    if (tendency.surface != "がち" || de.surface != "で" || adjective.extended_pos != core::ExtendedPOS::AdjBasic) {
      continue;
    }
    retag(tendency, core::PartOfSpeech::Suffix, core::ExtendedPOS::SuffixTendency, "がち",
          dictionary::ConjugationType::None, grammar::ConjForm::Base);
    retagCopulaDa(de);
  }
}

// The closed-class negative ない can reach the lattice as the homographic
// sequence な+い. AuxCopulaDa(な) cannot be followed directly by
// AuxAspectIru(い), so this pair is always the split negative rather than a
// valid auxiliary chain. Restore its single auxiliary token before POS
// resolution.
void mergeSplitCopularNegative(std::vector<core::Morpheme>& result) {
  for (size_t idx = 0; idx + 1 < result.size(); ++idx) {
    auto& na = result[idx];
    const auto& i = result[idx + 1];
    if (na.extended_pos != core::ExtendedPOS::AuxCopulaDa || na.surface != "な" ||
        i.extended_pos != core::ExtendedPOS::AuxAspectIru || i.surface != "い") {
      continue;
    }
    na.surface = "ない";
    retagNegativeNai(na);
    na.end = i.end;
    na.end_pos = i.end_pos;
    if (idx == 0 || result[idx - 1].pos == core::PartOfSpeech::Noun ||
        result[idx - 1].pos == core::PartOfSpeech::Particle) {
      na.pos = core::PartOfSpeech::Adjective;
      na.extended_pos = core::ExtendedPOS::AdjBasic;
      na.conj_type = dictionary::ConjugationType::IAdjective;
    }
    result.erase(result.begin() + static_cast<std::ptrdiff_t>(idx + 1));
  }
}

// At clause end, the negative renyokei after the formal noun こと can lose by
// a near tie to the impossible copula+aspect split な+く. Restore the closed
// grammatical form without affecting ordinary attributive copula な.
void mergeSplitFormalNounNegativeRenyokei(std::vector<core::Morpheme>& result) {
  for (size_t idx = 1; idx + 1 < result.size(); ++idx) {
    const auto& formal_noun = result[idx - 1];
    auto& na = result[idx];
    const auto& ku = result[idx + 1];
    if (formal_noun.surface != "こと" || formal_noun.extended_pos != core::ExtendedPOS::NounFormal ||
        na.surface != "な" || na.extended_pos != core::ExtendedPOS::AuxCopulaDa || ku.surface != "く" ||
        ku.pos != core::PartOfSpeech::Auxiliary) {
      continue;
    }
    na.surface = "なく";
    na.lemma = "ない";
    na.pos = core::PartOfSpeech::Adjective;
    na.extended_pos = core::ExtendedPOS::AdjRenyokei;
    na.conj_type = dictionary::ConjugationType::IAdjective;
    na.conj_form = grammar::ConjForm::Renyokei;
    na.end = ku.end;
    na.end_pos = ku.end_pos;
    result.erase(result.begin() + static_cast<std::ptrdiff_t>(idx + 1));
  }
}

// At the beginning of a clause, ない followed by a concessive conjunction is
// the independent i-adjective (ないのに), rather than the verbal negative
// auxiliary.  The lattice cannot use that following conjunction to resolve the
// homograph while choosing the first token.
void resolveInitialNegativeAdjective(std::vector<core::Morpheme>& result) {
  if (result.size() < 2 || result.front().surface != "ない" || result[1].surface != "のに") {
    return;
  }
  auto& negative = result.front();
  retagBasicNegativeAdjective(negative);
}

bool isObligationVerbStem(const core::Morpheme& morpheme) {
  // The complete right-hand chain supplies the form evidence. Ichidan
  // mizenkei/renyokei homographs and some generated Godan stems do not yet
  // carry a stable ExtendedPOS at this resolver stage.
  return morpheme.pos == core::PartOfSpeech::Verb;
}

// Contracted obligation/prohibition chains contain several lexical
// homographs. Resolve their roles only when the complete closed construction
// and its selecting verb stem are present. This leaves independent ない,
// conditional なら, and lexical いける untouched.
void resolveObligationNaranai(std::vector<core::Morpheme>& result) {
  // V未然 + なきゃ/なけりゃ + なら + ない.
  for (size_t idx = 1; idx + 2 < result.size(); ++idx) {
    const auto& verb = result[idx - 1];
    auto& conditional_negative = result[idx];
    auto& naru = result[idx + 1];
    auto& negative = result[idx + 2];
    if (!isObligationVerbStem(verb) || !utf8::equalsAny(conditional_negative.surface, {"なきゃ", "なけりゃ"}) ||
        !utf8::equalsAny(naru.surface, {"いけ", "なら"}) || negative.surface != "ない") {
      continue;
    }
    retagNegativeNai(conditional_negative);
    if (naru.surface == "いけ") {
      retag(naru, core::PartOfSpeech::Auxiliary, core::ExtendedPOS::AuxPotential, "いける",
            dictionary::ConjugationType::Ichidan, grammar::ConjForm::Mizenkei);
    } else {
      retag(naru, core::PartOfSpeech::Verb, core::ExtendedPOS::VerbMizenkei, "なる",
            dictionary::ConjugationType::GodanRa, grammar::ConjForm::Mizenkei);
    }
    retagNegativeNai(negative);
  }

  // V未然 + なく + ちゃ + いけ/なら + ない.
  for (size_t idx = 1; idx + 3 < result.size(); ++idx) {
    const auto& verb = result[idx - 1];
    auto& negative_continuative = result[idx];
    const auto& contracted_te = result[idx + 1];
    auto& obligation = result[idx + 2];
    auto& negative = result[idx + 3];
    if (!isObligationVerbStem(verb) || negative_continuative.surface != "なく" || contracted_te.surface != "ちゃ" ||
        !utf8::equalsAny(obligation.surface, {"いけ", "なら"}) || negative.surface != "ない") {
      continue;
    }
    retagNegativeNai(negative_continuative);
    if (obligation.surface == "いけ") {
      retag(obligation, core::PartOfSpeech::Auxiliary, core::ExtendedPOS::AuxPotential, "いける",
            dictionary::ConjugationType::Ichidan, grammar::ConjForm::Mizenkei);
    } else {
      retag(obligation, core::PartOfSpeech::Verb, core::ExtendedPOS::VerbMizenkei, "なる",
            dictionary::ConjugationType::GodanRa, grammar::ConjForm::Mizenkei);
    }
    retagNegativeNai(negative);
  }

  // V音便 + て/で + は + いけ + ない.
  for (size_t idx = 1; idx + 3 < result.size(); ++idx) {
    const auto& verb = result[idx - 1];
    const auto& connective = result[idx];
    const auto& topic = result[idx + 1];
    auto& obligation = result[idx + 2];
    auto& negative = result[idx + 3];
    if (!isObligationVerbStem(verb) || connective.extended_pos != core::ExtendedPOS::ParticleConj ||
        !grammar::isTeDeSurface(connective.surface) || topic.surface != "は" || obligation.surface != "いけ" ||
        negative.surface != "ない") {
      continue;
    }
    retag(obligation, core::PartOfSpeech::Auxiliary, core::ExtendedPOS::AuxPotential, "いける",
          dictionary::ConjugationType::Ichidan, grammar::ConjForm::Mizenkei);
    retagNegativeNai(negative);
  }
}

// The sequence な+さ+そう is the negative adjective stem, nominalizer, and
// appearance auxiliary (なさそう、食べなさそう).  Each mora has a competing
// closed-class homograph, so resolve the complete inflectional chain after
// boundary selection.
void resolveNegativeAppearanceChain(std::vector<core::Morpheme>& result) {
  for (size_t idx = 0; idx + 2 < result.size(); ++idx) {
    auto& na = result[idx];
    auto& sa = result[idx + 1];
    auto& sou = result[idx + 2];
    if (na.surface != "な" || sa.surface != "さ" || sou.surface != "そう") {
      continue;
    }
    retag(na, core::PartOfSpeech::Adjective, core::ExtendedPOS::AdjStem, "ない",
          dictionary::ConjugationType::IAdjective, grammar::ConjForm::Renyokei);
    retag(sa, core::PartOfSpeech::Suffix, core::ExtendedPOS::Suffix, "さ", dictionary::ConjugationType::None,
          grammar::ConjForm::Base);
    retagAppearanceSou(sou);
  }
}

// The surface なく is the continuative adjective form of ない in the public
// token contract (なくては, なくとも, 食べ+なく+て, 読ま+れ+なく+て). A
// copular topic predicate (ではなく) instead retains the auxiliary reading.
// The lattice shares those categories while scoring, so resolve the inflected
// surface after boundaries have been selected.
void resolveNegativeRenyokei(std::vector<core::Morpheme>& result) {
  for (size_t idx = 0; idx < result.size(); ++idx) {
    auto& negative = result[idx];
    // In the colloquial V-renyokei + っ + こ + ない + noun construction, the
    // final negative is the auxiliary modifying the following noun rather
    // than the independent negative adjective.
    if (negative.surface == "ない" && idx >= 2 && idx + 1 < result.size() &&
        result[idx - 1].extended_pos == core::ExtendedPOS::AuxAspectKuru &&
        result[idx - 2].extended_pos == core::ExtendedPOS::AuxNegativeMai &&
        result[idx + 1].pos == core::PartOfSpeech::Noun) {
      retagNegativeNai(negative);
      continue;
    }
    if (negative.surface != "なく" || negative.extended_pos != core::ExtendedPOS::AuxNegativeNai) {
      continue;
    }
    // The aspectual change construction ～なくなる retains the negative
    // auxiliary reading; the independent adjective form applies to its
    // conjunction and adverbial continuations (なくて、なくとも).
    if (idx + 1 < result.size() && result[idx + 1].lemma == "なる") {
      continue;
    }
    // The colloquial negative-conjecture chain V連用形+っ+こ+なく keeps
    // なく as a verbal negative auxiliary. The preceding two tokens are
    // context-gated candidates for this construction, so this exemption does
    // not affect ordinary adverbial uses of the independent adjective.
    const bool follows_negative_conjecture = idx >= 2 &&
                                             result[idx - 1].extended_pos == core::ExtendedPOS::AuxAspectKuru &&
                                             result[idx - 2].extended_pos == core::ExtendedPOS::AuxNegativeMai;
    if (follows_negative_conjecture) {
      continue;
    }
    const bool follows_copular_topic = idx >= 2 && result[idx - 1].extended_pos == core::ExtendedPOS::ParticleTopic &&
                                       result[idx - 2].extended_pos == core::ExtendedPOS::AuxCopulaDa;
    if (follows_copular_topic) {
      continue;
    }
    // In the double-negative predicate ～なくはない, the first なく remains
    // the negative auxiliary of the preceding verb. The final ない retains
    // its independent adjective reading after the topical particle.
    const bool follows_double_negative = idx + 2 < result.size() &&
                                         result[idx + 1].extended_pos == core::ExtendedPOS::ParticleTopic &&
                                         result[idx + 1].surface == "は" && result[idx + 2].surface == "ない";
    if (follows_double_negative) {
      continue;
    }
    retag(negative, core::PartOfSpeech::Adjective, core::ExtendedPOS::AdjRenyokei, "ない",
          dictionary::ConjugationType::IAdjective, grammar::ConjForm::Renyokei);
  }
}

// In the explanatory or causal pattern ADV+な+の/ので, な is the attributive
// form of the copula, not the homographic sentence-final particle
// (なぜ+な+の、せっかく+な+ので). The lattice cannot use the following
// particle while scoring the ADV→な connection, so resolve the POS once the
// complete three-token context is available.
void resolveAdverbExplanatoryCopula(std::vector<core::Morpheme>& result) {
  for (size_t idx = 1; idx + 1 < result.size(); ++idx) {
    auto& copula = result[idx];
    const auto& adverb = result[idx - 1];
    const auto& nominalizer = result[idx + 1];
    const bool explanatory_no =
        nominalizer.surface == "の" && nominalizer.extended_pos == core::ExtendedPOS::ParticleNo;
    const bool causal_node =
        nominalizer.surface == "ので" && nominalizer.extended_pos == core::ExtendedPOS::ParticleConj;
    if (adverb.pos != core::PartOfSpeech::Adverb || copula.surface != "な" ||
        copula.pos != core::PartOfSpeech::Particle || (!explanatory_no && !causal_node)) {
      continue;
    }
    retagCopulaDa(copula);
  }
}

// Formal よう remains a noun in similitude/purpose constructions
// (読む+よう+だ, 次+の+よう+に, この+よう+な).  The lattice already
// distinguishes it from the true volitional auxiliary (見+よう).  Only the
// homographic following な may still need its copular role restored here.
void resolveSimilitudeYou(std::vector<core::Morpheme>& result) {
  // The polite auxiliary is already finite, so a following よう cannot be the
  // homographic volitional ending. It is the formal noun introducing a wished-
  // for state (お待ちくださいます+よう+お願いします).
  for (size_t idx = 1; idx < result.size(); ++idx) {
    const auto& polite = result[idx - 1];
    auto& purpose = result[idx];
    if (polite.extended_pos != core::ExtendedPOS::AuxTenseMasu || purpose.surface != "よう" ||
        purpose.extended_pos != core::ExtendedPOS::AuxVolitional) {
      continue;
    }
    retag(purpose, core::PartOfSpeech::Noun, core::ExtendedPOS::NounFormal, "よう", dictionary::ConjugationType::None,
          grammar::ConjForm::Base);
    purpose.features.is_formal_noun = true;
  }

  for (size_t idx = 0; idx + 1 < result.size(); ++idx) {
    auto& similitude = result[idx];
    auto& next = result[idx + 1];
    if (similitude.surface != "よう" || similitude.extended_pos != core::ExtendedPOS::NounFormal) {
      continue;
    }
    const bool attributive_na_follows = next.surface == "な" && next.pos == core::PartOfSpeech::Particle;
    if (attributive_na_follows) {
      retagCopulaDa(next);
    }
  }
}

}  // namespace suzume::postprocess::resolver
