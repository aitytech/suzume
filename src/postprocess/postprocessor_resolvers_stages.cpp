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

namespace suzume::postprocess {

// These stages deliberately preserve the resolver order in process().  The
// second stage follows PREFIX+VERB nominalization because its rules inspect
// the resulting noun/verb category.
void resolvePrePrefixMorphemeRoles(std::vector<core::Morpheme>& result) {
  // A small closed class of kanji+i surfaces is an Ichidan continuative stem
  // (強いる, 用いる, 率いる, ...), while some members are also i-adjectives.
  // The passive auxiliary makes the verbal reading unambiguous.
  for (size_t idx = 0; idx + 1 < result.size(); ++idx) {
    auto& morpheme = result[idx];
    if (morpheme.pos == core::PartOfSpeech::Adjective &&
        grammar::inflection::isValidKanjiIStemException(morpheme.surface) &&
        result[idx + 1].extended_pos == core::ExtendedPOS::AuxPassive) {
      morpheme.pos = core::PartOfSpeech::Verb;
      morpheme.extended_pos = core::ExtendedPOS::VerbRenyokei;
      morpheme.lemma = morpheme.surface + "る";
      morpheme.conj_type = dictionary::ConjugationType::Ichidan;
      morpheme.conj_form = grammar::ConjForm::Renyokei;
    }
  }

  resolver::resolveNegativeAppearanceChain(result);
  resolver::resolveAdjectiveNominalizerSa(result);
  resolver::resolveAppearanceSouPredicate(result);
  resolver::resolveProgressiveContractionNominalizer(result);
  resolver::resolveNominalPredicateNai(result);
  resolver::resolveCertaintyChigaiNai(result);
  resolver::mergeSplitCopularNegative(result);
  resolver::mergeSplitFormalNounNegativeRenyokei(result);
  resolver::resolveInitialNegativeAdjective(result);
  resolver::resolveObligationNaranai(result);
  resolver::resolveHonorificVerbInflection(result);
  resolver::resolvePreparatoryVolitional(result);
  resolver::resolveCopularForms(result);
  resolver::resolveGozaruPoliteAuxiliary(result);
  resolver::resolveNominalCaseDe(result);
  resolver::resolveCopularForms(result);
  resolver::resolveTendencySuffixCopula(result);
  resolver::resolveCopularAro(result);
  resolver::resolveNominalDeAru(result);
  resolver::resolveComparisonNoun(result);
  resolver::resolveNegativeRenyokei(result);
  resolver::resolveVerbTeParticle(result);
  resolver::resolveTearuAuxiliary(result);
  resolver::resolveKuruwaPoliteAru(result);

  // At sentence start, なり+ける is the continuative form of lexical なる
  // followed by the classical past auxiliary.  The copular なり reading is
  // retained when it follows a nominal predicate (春なりける).
  if (result.size() >= 2 && utf8::equalsAny(result[0].surface, {"なり"}) &&
      result[0].extended_pos == core::ExtendedPOS::AuxClassicalNari &&
      result[1].extended_pos == core::ExtendedPOS::AuxClassicalKeri && utf8::equalsAny(result[1].surface, {"ける"})) {
    result[0].pos = core::PartOfSpeech::Verb;
    result[0].extended_pos = core::ExtendedPOS::VerbRenyokei;
    result[0].lemma = "なる";
    result[0].conj_type = dictionary::ConjugationType::GodanRa;
    result[0].conj_form = grammar::ConjForm::Renyokei;
  }

  resolver::resolveParticleAruOnbin(result);
  resolver::resolveDemonstrativeMiseru(result);
  resolver::resolveBenefactivePotential(result);
  resolver::resolveInitialInabilityVerb(result);
  resolver::resolveTeBenefactiveNegativePotential(result);
  resolver::resolveProgressiveIru(result);
  resolver::resolveSahenRenyokei(result);
  resolver::resolveDemonstrativeQuotativeOnbin(result);
  resolver::resolvePoliteSuruRenyokei(result);
  resolver::resolveIndefiniteCaseDe(result);
  resolver::resolveIndefiniteExistentialIru(result);
  resolver::resolveNominalizedRenyokeiPredicate(result);
}

void resolvePostPrefixMorphemeRoles(std::vector<core::Morpheme>& result) {
  resolver::resolveCompoundAdjectiveRenyokei(result);
  resolver::resolveAdverbExplanatoryCopula(result);
  resolver::resolveSimilitudeYou(result);
  resolver::resolveNominalConditionalNara(result);

  for (size_t i = 0; i + 1 < result.size(); ++i) {
    if (result[i].surface == "付け" && result[i].pos == core::PartOfSpeech::Verb && result[i + 1].surface == "で" &&
        result[i + 1].pos == core::PartOfSpeech::Particle) {
      result[i].pos = core::PartOfSpeech::Noun;
      result[i].extended_pos = core::ExtendedPOS::Noun;
      result[i].lemma = result[i].surface;
      result[i].conj_type = dictionary::ConjugationType::None;
      result[i].conj_form = grammar::ConjForm::Base;
    }
  }
  for (size_t i = 1; i + 1 < result.size(); ++i) {
    if (result[i - 1].surface == "あり" && result[i].surface == "ん" && result[i + 1].surface == "す") {
      auto& suru = result[i + 1];
      suru.pos = core::PartOfSpeech::Verb;
      suru.extended_pos = core::ExtendedPOS::VerbShuushikei;
      suru.lemma = "する";
      suru.conj_type = dictionary::ConjugationType::Suru;
      suru.conj_form = grammar::ConjForm::Base;
    }
  }

  // Temporal 後 after a counter/duration quantity is a suffix, not a standalone
  // noun (2時間+後, 10日+後, 5分+後 → 名詞,接尾). Runs after numeric merging so the
  // quantity token is final. 前 is never a suffix in MeCab, so this is 後-only.
  for (size_t i = 1; i < result.size(); ++i) {
    if (result[i].surface == "後" && result[i].pos == core::PartOfSpeech::Noun &&
        result[i - 1].pos == core::PartOfSpeech::Noun && resolver::isCounterDurationNoun(result[i - 1].surface)) {
      result[i].pos = core::PartOfSpeech::Suffix;
      result[i].extended_pos = core::ExtendedPOS::Suffix;
      result[i].lemma = "後";
    }
  }

  // Kanji 過ぎ/過ぎる directly after a verb 連用形 is the excessive subsidiary すぎる
  // (食べ過ぎ, 使い過ぎ, 働き過ぎ, 走り過ぎる → 過ぎ = 動詞,非自立 → Auxiliary, lemma 過ぎる),
  // not a standalone verb 過ぎる (時間が過ぎる stays Verb after a case particle). The
  // lexicalized compounds 通り過ぎる/過ぎ去る/行き過ぎる are single L2 tokens, so no standalone
  // 過ぎ follows a V1 here. Also repairs the bare-renyokei fake godan reading 過ぐ → 過ぎる.
  // Hiragana すぎ keeps POS Verb (MeCab orthography split), so this is kanji-過ぎ only.
  for (size_t i = 1; i < result.size(); ++i) {
    if ((result[i].surface == "過ぎ" || result[i].surface == "過ぎる") && result[i].pos == core::PartOfSpeech::Verb &&
        result[i - 1].pos == core::PartOfSpeech::Verb &&
        result[i - 1].extended_pos == core::ExtendedPOS::VerbRenyokei) {
      result[i].pos = core::PartOfSpeech::Auxiliary;
      result[i].extended_pos = core::ExtendedPOS::AuxExcessive;
      result[i].lemma = "過ぎる";
    }
  }

  // A verb continuative directly followed by the negative form ない requires
  // the auxiliary reading. Keep the selected verb lemma: a compound
  // continuative such as 差し支え must retain its dictionary form 差し支える.
  // The irrealis-plus-negative construction already selects the auxiliary and
  // therefore does not reach this recovery step.
  for (size_t i = 1; i < result.size(); ++i) {
    if (result[i].surface == "ない" && result[i].pos == core::PartOfSpeech::Adjective &&
        result[i - 1].pos == core::PartOfSpeech::Verb &&
        result[i - 1].extended_pos == core::ExtendedPOS::VerbRenyokei &&
        result[i - 1].conj_type != dictionary::ConjugationType::Suru) {
      result[i].pos = core::PartOfSpeech::Auxiliary;
      result[i].extended_pos = core::ExtendedPOS::AuxNegativeNai;
      result[i].lemma = "ない";
    }
  }

  // A compound-verb 連用形 (積み重ね, 組み立て, 話し合い) used as the head of a nominal phrase
  // is a 連用形転成名詞, not a verb: 努力の積み重ね**が**, 組み立て**が**得意, 経験を積み重ね
  // (EOS). Retag to NounVerbal with lemma = surface when such a compound-shape VerbRenyokei
  // is at the end of the sentence or is directly marked by a case/topic particle. Verbal
  // continuations (た/て/ながら, 連用中止 before 、, a following V2, quotative と) keep VERB.
  // Runs after lemmatizeAll, so overwriting lemma = surface is final; the retag only fires
  // when the right neighbor is a particle/EOS, so it never creates NOUN+NOUN adjacency that
  // would perturb the later noun-compound merge.
  for (size_t i = 0; i < result.size(); ++i) {
    if (result[i].pos == core::PartOfSpeech::Verb && result[i].extended_pos == core::ExtendedPOS::VerbRenyokei &&
        resolver::isCompoundRenyokeiShape(result[i].surface) &&
        (i + 1 == result.size() || resolver::isNominalForcingParticle(result[i + 1]))) {
      result[i].pos = core::PartOfSpeech::Noun;
      result[i].extended_pos = core::ExtendedPOS::NounVerbal;
      result[i].lemma = result[i].surface;
      result[i].conj_type = dictionary::ConjugationType::None;
      result[i].conj_form = grammar::ConjForm::Base;
    }
  }
}

// These repairs run only after merges and filtering have finalized the local
// token context.  Moving them earlier would change the grammatical evidence
// each resolver observes.
void resolveFinalMorphemeRoles(std::vector<core::Morpheme>& result) {
  resolver::resolveDurationPredicateKakaru(result);

  // Resolve the sentence-initial demonstrative after all compound candidates
  // have been filtered, so retagging cannot alter a copular boundary.
  if (result.size() >= 2 && result[0].surface == "そう" && result[0].pos == core::PartOfSpeech::Adverb &&
      result[1].extended_pos == core::ExtendedPOS::AuxCopulaDa &&
      (result.size() < 3 || result[2].surface != "ござい")) {
    resolver::retagNaAdjectivalSou(result[0]);
  }
  if (result.size() >= 2 && result[0].surface == "どう" && result[0].pos == core::PartOfSpeech::Adverb &&
      result[1].extended_pos == core::ExtendedPOS::ParticleCase && result[1].surface == "に") {
    result[0].pos = core::PartOfSpeech::Adjective;
    result[0].extended_pos = core::ExtendedPOS::AdjNaAdj;
    result[0].lemma = "どう";
    result[0].conj_type = dictionary::ConjugationType::None;
    result[0].conj_form = grammar::ConjForm::Base;
  }
  for (size_t idx = 0; idx + 3 < result.size(); ++idx) {
    const auto& stem = result[idx];
    auto& sou = result[idx + 1];
    auto& na = result[idx + 2];
    auto& noun = result[idx + 3];
    if (stem.extended_pos != core::ExtendedPOS::AdjStem || sou.surface != "そう" || na.surface != "な" ||
        noun.pos != core::PartOfSpeech::Adjective || !grammar::containsKanji(noun.surface)) {
      continue;
    }
    resolver::retagAppearanceSou(sou);
    resolver::retagCopulaDa(na);
    noun.pos = core::PartOfSpeech::Noun;
    noun.extended_pos = core::ExtendedPOS::Noun;
    noun.lemma = noun.surface;
    noun.conj_type = dictionary::ConjugationType::None;
    noun.conj_form = grammar::ConjForm::Base;
  }

  // A one-kanji Ichidan stem has the same visible form before the negative
  // auxiliary. Resolve its lemma from the final token sequence, after all
  // ambiguity-specific retagging has completed.
  for (size_t idx = 0; idx + 1 < result.size(); ++idx) {
    auto& stem = result[idx];
    const auto& negative = result[idx + 1];
    if (stem.pos == core::PartOfSpeech::Verb && stem.surface.size() == core::kJapaneseCharBytes &&
        grammar::isAllKanji(stem.surface) && negative.surface == "ない") {
      stem.lemma = stem.surface + "る";
    }
  }

  // A kanji-plus-て continuative before the past auxiliary or connective
  // particle is an Ichidan stem (立て+て, 棄て+た). The lattice also has a
  // homographic analysis that drops the e-row mora from the lemma; restore
  // the productive Ichidan dictionary form after contextual disambiguation.
  for (size_t idx = 0; idx + 1 < result.size(); ++idx) {
    auto& stem = result[idx];
    const auto& next = result[idx + 1];
    const auto codepoints = normalize::toCodepoints(stem.surface);
    const bool is_ichidan_te_stem = stem.pos == core::PartOfSpeech::Verb &&
                                    stem.extended_pos == core::ExtendedPOS::VerbRenyokei && codepoints.size() == 2 &&
                                    normalize::isKanjiCodepoint(codepoints.front()) && codepoints.back() == U'て';
    const bool inflection_follows =
        next.extended_pos == core::ExtendedPOS::AuxTenseTa || next.extended_pos == core::ExtendedPOS::ParticleConj;
    if (is_ichidan_te_stem && inflection_follows) {
      stem.lemma = stem.surface + "る";
      stem.conj_type = dictionary::ConjugationType::Ichidan;
      stem.conj_form = grammar::ConjForm::Renyokei;
    }
  }

  // A suffix between the genitive particle and a case particle heads its own
  // nominal phrase (穴の中から, 月の末に). Retag it as a noun; a true bound
  // suffix instead remains directly attached to its nominal stem.
  for (size_t idx = 1; idx + 1 < result.size(); ++idx) {
    auto& candidate = result[idx];
    const auto& previous = result[idx - 1];
    const auto& next = result[idx + 1];
    if (candidate.pos == core::PartOfSpeech::Suffix && previous.extended_pos == core::ExtendedPOS::ParticleNo &&
        next.extended_pos == core::ExtendedPOS::ParticleCase) {
      candidate.pos = core::PartOfSpeech::Noun;
      candidate.extended_pos = core::ExtendedPOS::Noun;
      candidate.conj_type = dictionary::ConjugationType::None;
      candidate.conj_form = grammar::ConjForm::Base;
    }
  }
}

}  // namespace suzume::postprocess
