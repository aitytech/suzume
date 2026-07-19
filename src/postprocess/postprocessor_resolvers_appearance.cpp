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
    if (predicate.extended_pos == core::ExtendedPOS::VerbRenyokei && sou.extended_pos == core::ExtendedPOS::AdjNaAdj) {
      retagAppearanceSou(sou);
    }
    if (sou.pos == core::PartOfSpeech::Adverb) {
      const bool verbal_predicate = predicate.extended_pos == core::ExtendedPOS::VerbRenyokei ||
                                    predicate.extended_pos == core::ExtendedPOS::VerbShuushikei ||
                                    predicate.extended_pos == core::ExtendedPOS::AuxAspectShimau;
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
                                       preceding.extended_pos == core::ExtendedPOS::AuxAspectShimau;
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
        (stem.pos != core::PartOfSpeech::Noun && stem.pos != core::PartOfSpeech::Adjective)) {
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
    contraction.pos = core::PartOfSpeech::Particle;
    contraction.extended_pos = core::ExtendedPOS::ParticleNo;
    contraction.lemma = "の";
    contraction.conj_type = dictionary::ConjugationType::None;
    contraction.conj_form = grammar::ConjForm::Base;
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

    // The open-class kanji candidate generator can occasionally create an
    // unattested 〜る verb from a nominal predicate (相違→相違る). In XにYない,
    // its all-kanji, non-dictionary renyokei shape is unambiguously nominal.
    // Restore that reading before resolving the independent negative.
    if (idx >= 2 && predicate.pos == core::PartOfSpeech::Verb &&
        predicate.extended_pos == core::ExtendedPOS::VerbRenyokei && !predicate.is_from_dictionary &&
        grammar::isAllKanji(predicate.surface) && utf8::endsWith(predicate.lemma, "る")) {
      const auto& marker = result[idx - 2];
      if (marker.extended_pos == core::ExtendedPOS::ParticleCase && marker.surface == "に") {
        predicate.pos = core::PartOfSpeech::Noun;
        predicate.extended_pos = core::ExtendedPOS::Noun;
        predicate.lemma = predicate.surface;
        predicate.conj_type = dictionary::ConjugationType::None;
        predicate.conj_form = grammar::ConjForm::Base;
      }
    }

    if (predicate.pos != core::PartOfSpeech::Noun) {
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

// The certainty predicate にちがいない keeps ない as a dependent negative
// auxiliary; it is not the independent adjective used in ordinary noun
// predicates.
void resolveCertaintyChigaiNai(std::vector<core::Morpheme>& result) {
  for (size_t idx = 0; idx + 1 < result.size(); ++idx) {
    const auto& chigai = result[idx];
    auto& negative = result[idx + 1];
    if (chigai.surface != "ちがい" || negative.surface != "ない") {
      continue;
    }
    retagNegativeNai(negative);
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
    aru.pos = core::PartOfSpeech::Auxiliary;
    aru.extended_pos = core::ExtendedPOS::AuxCopulaDa;
    aru.lemma = "ある";
    aru.conj_type = dictionary::ConjugationType::GodanRa;
    aru.conj_form = grammar::ConjForm::Onbinkei;
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
    conditional.pos = core::PartOfSpeech::Auxiliary;
    conditional.extended_pos = core::ExtendedPOS::AuxTenseTa;
    conditional.lemma = "た";
    conditional.conj_type = dictionary::ConjugationType::None;
    conditional.conj_form = grammar::ConjForm::Base;
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
    tendency.pos = core::PartOfSpeech::Suffix;
    tendency.extended_pos = core::ExtendedPOS::SuffixTendency;
    tendency.lemma = "がち";
    tendency.conj_type = dictionary::ConjugationType::None;
    tendency.conj_form = grammar::ConjForm::Base;
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

// The negative conditional + ならない is the obligation construction
// (見なけりゃならない, 読まなきゃならない).  Its middle token is the mizenkei
// of なる and the final ない is the negative auxiliary, not a conditional
// particle followed by the independent adjective.
void resolveObligationNaranai(std::vector<core::Morpheme>& result) {
  for (size_t idx = 1; idx + 1 < result.size(); ++idx) {
    const auto& conditional = result[idx - 1];
    auto& naru = result[idx];
    auto& negative = result[idx + 1];
    if (!utf8::equalsAny(conditional.surface, {"なきゃ", "なけりゃ"}) || naru.surface != "なら" ||
        negative.surface != "ない") {
      continue;
    }
    naru.pos = core::PartOfSpeech::Verb;
    naru.extended_pos = core::ExtendedPOS::VerbMizenkei;
    naru.lemma = "なる";
    naru.conj_type = dictionary::ConjugationType::GodanRa;
    naru.conj_form = grammar::ConjForm::Mizenkei;
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
    na.pos = core::PartOfSpeech::Adjective;
    na.extended_pos = core::ExtendedPOS::AdjStem;
    na.lemma = "ない";
    na.conj_type = dictionary::ConjugationType::IAdjective;
    na.conj_form = grammar::ConjForm::Renyokei;
    sa.pos = core::PartOfSpeech::Suffix;
    sa.extended_pos = core::ExtendedPOS::Suffix;
    sa.lemma = "さ";
    sa.conj_type = dictionary::ConjugationType::None;
    sa.conj_form = grammar::ConjForm::Base;
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
    negative.pos = core::PartOfSpeech::Adjective;
    negative.extended_pos = core::ExtendedPOS::AdjRenyokei;
    negative.lemma = "ない";
    negative.conj_type = dictionary::ConjugationType::IAdjective;
    negative.conj_form = grammar::ConjForm::Renyokei;
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

// Formal よう is auxiliary-like in similitude/purpose constructions
// (読む+よう+だ, 次+の+よう+に, この+よう+な), but remains a formal noun in
// the productive renyokei noun 読み+よう+がない/による.  Resolve that
// contextual category after the lattice has preserved the shared boundary.
void resolveSimilitudeYou(std::vector<core::Morpheme>& result) {
  for (size_t idx = 0; idx + 1 < result.size(); ++idx) {
    auto& similitude = result[idx];
    const auto& next = result[idx + 1];
    if (similitude.surface != "よう" || similitude.extended_pos != core::ExtendedPOS::NounFormal) {
      continue;
    }
    const bool copula_follows =
        next.extended_pos == core::ExtendedPOS::AuxCopulaDa || next.extended_pos == core::ExtendedPOS::AuxCopulaDesu;
    const bool ni_follows = next.extended_pos == core::ExtendedPOS::ParticleCase && next.surface == "に";
    const bool renyokei_noun = idx > 0 && result[idx - 1].extended_pos == core::ExtendedPOS::VerbRenyokei;
    if (!copula_follows && (!ni_follows || renyokei_noun)) {
      continue;
    }
    similitude.pos = core::PartOfSpeech::Auxiliary;
    similitude.extended_pos = core::ExtendedPOS::AuxSimilitudeYou;
    similitude.lemma = "よう";
    similitude.conj_type = dictionary::ConjugationType::None;
    similitude.conj_form = grammar::ConjForm::Base;
  }
}

}  // namespace suzume::postprocess::resolver
