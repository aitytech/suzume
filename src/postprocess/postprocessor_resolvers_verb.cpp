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

// A selected honorific auxiliary before the negative or te-form is an
// inflected lexical honorific verb (いらっしゃら+ない, いらっしゃっ+て,
// なさら+ない). Keeping its dictionary candidate auxiliary-shaped preserves
// the boundary after a preceding te-form; resolve the public grammatical
// category once its own continuation is known.
void resolveHonorificVerbInflection(std::vector<core::Morpheme>& result) {
  for (size_t idx = 0; idx + 1 < result.size(); ++idx) {
    auto& honorific = result[idx];
    const auto& continuation = result[idx + 1];
    const bool is_subsidiary_nasaru = honorific.lemma == "なさる";
    const bool negative_follows =
        continuation.surface == "ない" && continuation.extended_pos == core::ExtendedPOS::AuxNegativeNai;
    const bool te_follows =
        continuation.surface == "て" && continuation.extended_pos == core::ExtendedPOS::ParticleConj;
    if (honorific.extended_pos != core::ExtendedPOS::AuxHonorific || is_subsidiary_nasaru ||
        (!negative_follows && !te_follows)) {
      continue;
    }
    honorific.pos = core::PartOfSpeech::Verb;
    honorific.conj_type = dictionary::ConjugationType::GodanRa;
    honorific.extended_pos = negative_follows ? core::ExtendedPOS::VerbMizenkei : core::ExtendedPOS::VerbOnbinkei;
    honorific.conj_form = negative_follows ? grammar::ConjForm::Mizenkei : grammar::ConjForm::Onbinkei;
  }
}

// In a te-form followed by a volitional, おこ is the mizenkei of the
// preparatory auxiliary おく (して+おこ+う), not an independent lexical verb.
void resolvePreparatoryVolitional(std::vector<core::Morpheme>& result) {
  for (size_t idx = 1; idx + 1 < result.size(); ++idx) {
    const auto& te = result[idx - 1];
    auto& oku = result[idx];
    const auto& volitional = result[idx + 1];
    if (te.surface != "て" || te.extended_pos != core::ExtendedPOS::ParticleConj || oku.surface != "おこ" ||
        volitional.extended_pos != core::ExtendedPOS::AuxVolitional) {
      continue;
    }
    oku.pos = core::PartOfSpeech::Auxiliary;
    oku.extended_pos = core::ExtendedPOS::AuxAspectOku;
    oku.lemma = "おく";
    oku.conj_type = dictionary::ConjugationType::GodanKa;
    oku.conj_form = grammar::ConjForm::Mizenkei;
  }
}

// The potential humble receiving verb is a benefactive auxiliary after a
// te-form or honorific renyokei (読んで+いただける, お待ち+いただける).
// Its continuative form before polite ます remains a verb (ご覧+いただけ+ます),
// which is a distinct finite inflection rather than the base-form auxiliary.
void resolveBenefactivePotential(std::vector<core::Morpheme>& result) {
  for (size_t idx = 1; idx < result.size(); ++idx) {
    const auto& predecessor = result[idx - 1];
    auto& benefactive = result[idx];
    const bool dependent_predecessor =
        predecessor.extended_pos == core::ExtendedPOS::ParticleConj ||
        predecessor.extended_pos == core::ExtendedPOS::VerbRenyokei ||
        (idx >= 2 && result[idx - 2].pos == core::PartOfSpeech::Prefix && predecessor.pos == core::PartOfSpeech::Noun);
    const bool followed_by_polite =
        idx + 1 < result.size() && result[idx + 1].extended_pos == core::ExtendedPOS::AuxTenseMasu;
    const bool conditional_form = benefactive.extended_pos == core::ExtendedPOS::VerbKateikei;
    if (!dependent_predecessor || followed_by_polite || conditional_form ||
        !grammar::isPotentialBenefactiveLemma(benefactive.lemma)) {
      continue;
    }
    benefactive.pos = core::PartOfSpeech::Auxiliary;
    benefactive.extended_pos = core::ExtendedPOS::AuxBenefactive;
    benefactive.conj_type = dictionary::ConjugationType::Ichidan;
    benefactive.conj_form = grammar::ConjForm::Base;
  }
}

// The closed subsidiary みせる expresses resolve after a te-form
// (確認し+て+みせる). Candidate generation already marks only that contextual
// path as AuxAspectMiru, so expose its auxiliary POS without changing the
// homographic lexical verb in 絵を見せる.
void resolveDemonstrativeMiseru(std::vector<core::Morpheme>& result) {
  for (size_t idx = 1; idx < result.size(); ++idx) {
    const auto& connective = result[idx - 1];
    auto& subsidiary = result[idx];
    if (connective.extended_pos != core::ExtendedPOS::ParticleConj || subsidiary.lemma != "みせる") {
      continue;
    }
    subsidiary.pos = core::PartOfSpeech::Auxiliary;
    subsidiary.extended_pos = core::ExtendedPOS::AuxAspectMiru;
  }
}

// A clause-initial inability form has no predicate to attach to, so it is the
// lexical verb rather than a subsidiary auxiliary (かね+ない).  The lattice
// retains the closed-class candidate because the continuation alone is also
// valid for an auxiliary; the missing predecessor supplies the distinction.
void resolveInitialInabilityVerb(std::vector<core::Morpheme>& result) {
  for (size_t idx = 0; idx + 1 < result.size(); ++idx) {
    const bool starts_clause = idx == 0 || result[idx - 1].pos == core::PartOfSpeech::Symbol;
    auto& inability = result[idx];
    const auto& negative = result[idx + 1];
    if (!starts_clause || inability.extended_pos != core::ExtendedPOS::AuxInability ||
        negative.extended_pos != core::ExtendedPOS::AuxNegativeNai) {
      continue;
    }
    inability.pos = core::PartOfSpeech::Verb;
    inability.extended_pos = core::ExtendedPOS::VerbMizenkei;
    inability.conj_form = grammar::ConjForm::Mizenkei;
  }
}

// After an unvoiced te-form, the negative e-row receiving form is a finite
// potential verb (書いて+もらえ+ない), while the voiced de-form retains the
// benefactive auxiliary reading (読んで+もらえ+ない).  The e-row gate excludes
// Godan benefactives such as やら+ない. Resolve this only after the complete
// three-token context is available.
void resolveTeBenefactiveNegativePotential(std::vector<core::Morpheme>& result) {
  for (size_t idx = 1; idx + 1 < result.size(); ++idx) {
    const auto& connective = result[idx - 1];
    auto& benefactive = result[idx];
    const auto& negative = result[idx + 1];
    if (connective.surface != "て" || connective.extended_pos != core::ExtendedPOS::ParticleConj ||
        benefactive.extended_pos != core::ExtendedPOS::AuxBenefactive || !grammar::endsWithERow(benefactive.surface) ||
        negative.extended_pos != core::ExtendedPOS::AuxNegativeNai) {
      continue;
    }
    benefactive.pos = core::PartOfSpeech::Verb;
    benefactive.extended_pos = core::ExtendedPOS::VerbMizenkei;
    benefactive.conj_type = dictionary::ConjugationType::Ichidan;
    benefactive.conj_form = grammar::ConjForm::Mizenkei;
  }
}

// A finite いる directly after the connective te/de particle is the
// progressive auxiliary. The lattice preserves the lexical verb candidate so
// existential uses remain available, then this complete local context assigns
// the dependent reading (遅れて+いる, 読んで+いる).
void resolveProgressiveIru(std::vector<core::Morpheme>& result) {
  for (size_t idx = 1; idx < result.size(); ++idx) {
    const auto& connective = result[idx - 1];
    auto& iru = result[idx];
    if (connective.extended_pos != core::ExtendedPOS::ParticleConj || !grammar::isTeDeSurface(connective.surface) ||
        iru.surface != "いる" || iru.lemma != "いる" || iru.extended_pos != core::ExtendedPOS::VerbShuushikei) {
      continue;
    }
    iru.pos = core::PartOfSpeech::Auxiliary;
    iru.extended_pos = core::ExtendedPOS::AuxAspectIru;
    iru.conj_type = dictionary::ConjugationType::Ichidan;
    iru.conj_form = grammar::ConjForm::Base;
  }
}

// A te-form followed by the continuative of ある is the resultative auxiliary
// construction (確認+し+て+あり+ます), not an existential verb.  The lattice
// cannot decide the homograph until the connective particle is selected.
void resolveTearuAuxiliary(std::vector<core::Morpheme>& result) {
  for (size_t idx = 1; idx < result.size(); ++idx) {
    const auto& connective = result[idx - 1];
    auto& aru = result[idx];
    if (connective.extended_pos != core::ExtendedPOS::ParticleConj ||
        (connective.surface != "て" && connective.surface != "で") || aru.lemma != "ある" ||
        aru.extended_pos != core::ExtendedPOS::VerbRenyokei) {
      continue;
    }
    aru.pos = core::PartOfSpeech::Auxiliary;
    aru.extended_pos = core::ExtendedPOS::AuxCopulaDa;
    aru.conj_type = dictionary::ConjugationType::GodanRa;
    aru.conj_form = grammar::ConjForm::Renyokei;
  }
}

// The Kuruwa-kotoba polite ending ありんす contains the continuative lexical
// verb あり followed by the archaic auxiliary ん and the verb す. When it
// follows copular で, the lattice can otherwise reinterpret あり as a copula
// and ん as a nominalizer.
void resolveKuruwaPoliteAru(std::vector<core::Morpheme>& result) {
  for (size_t idx = 1; idx + 2 < result.size(); ++idx) {
    const auto& de = result[idx - 1];
    auto& aru = result[idx];
    auto& n = result[idx + 1];
    const auto& su = result[idx + 2];
    if (de.surface != "で" || de.extended_pos != core::ExtendedPOS::AuxCopulaDa || aru.surface != "あり" ||
        n.surface != "ん" || su.surface != "す" || su.lemma != "する") {
      continue;
    }
    aru.pos = core::PartOfSpeech::Verb;
    aru.extended_pos = core::ExtendedPOS::VerbRenyokei;
    aru.lemma = "ある";
    aru.conj_type = dictionary::ConjugationType::GodanRa;
    aru.conj_form = grammar::ConjForm::Renyokei;
    n.pos = core::PartOfSpeech::Auxiliary;
    n.extended_pos = core::ExtendedPOS::AuxNegativeNu;
    n.lemma = "ん";
    n.conj_type = dictionary::ConjugationType::None;
    n.conj_form = grammar::ConjForm::Base;
  }
}

// A limiting particle followed by あって is the lexical verb ある in its
// sokuonbin form (読むだけあって).  The copular あっ plus auxiliary てる
// alternative is not a valid auxiliary chain.
void resolveParticleAruOnbin(std::vector<core::Morpheme>& result) {
  for (size_t idx = 1; idx + 1 < result.size(); ++idx) {
    const auto& particle = result[idx - 1];
    auto& aru = result[idx];
    auto& te = result[idx + 1];
    if (particle.extended_pos != core::ExtendedPOS::ParticleAdverbial || aru.surface != "あっ" ||
        aru.extended_pos != core::ExtendedPOS::AuxCopulaDa || te.surface != "て" ||
        te.extended_pos != core::ExtendedPOS::AuxAspectIru) {
      continue;
    }
    aru.pos = core::PartOfSpeech::Verb;
    aru.extended_pos = core::ExtendedPOS::VerbOnbinkei;
    aru.lemma = "ある";
    aru.conj_type = dictionary::ConjugationType::GodanRa;
    aru.conj_form = grammar::ConjForm::Onbinkei;
    te.pos = core::PartOfSpeech::Particle;
    te.extended_pos = core::ExtendedPOS::ParticleConj;
    te.lemma = "て";
    te.conj_type = dictionary::ConjugationType::None;
    te.conj_form = grammar::ConjForm::Base;
  }
}

// A verb's continuative or onbin form followed by て/で is the connective
// particle.  Its surface overlaps with several auxiliary entries, whose POS
// is only disambiguated once the preceding selected verb form is available.
void resolveVerbTeParticle(std::vector<core::Morpheme>& result) {
  for (size_t idx = 1; idx < result.size(); ++idx) {
    const auto& verb = result[idx - 1];
    auto& te = result[idx];
    const bool is_connective_verb_form =
        verb.extended_pos == core::ExtendedPOS::VerbOnbinkei || verb.extended_pos == core::ExtendedPOS::VerbRenyokei ||
        verb.extended_pos == core::ExtendedPOS::VerbTeForm || verb.extended_pos == core::ExtendedPOS::AuxAspectOku;
    if (!is_connective_verb_form || (te.surface != "て" && te.surface != "で")) {
      continue;
    }
    const bool contracted_progressive_before_past = te.extended_pos == core::ExtendedPOS::AuxAspectIru &&
                                                    idx + 1 < result.size() &&
                                                    result[idx + 1].extended_pos == core::ExtendedPOS::AuxTenseTa;
    if (contracted_progressive_before_past) {
      continue;
    }
    te.pos = core::PartOfSpeech::Particle;
    te.extended_pos = core::ExtendedPOS::ParticleConj;
    te.lemma = te.surface;
    te.conj_type = dictionary::ConjugationType::None;
    te.conj_form = grammar::ConjForm::Base;
  }
}

// Productive compound adjectives attach to a verb renyokei (読み+やすい、
// 使い+にくい).  The stem is also a deverbal noun, so recover its verbal
// category from the closed adjective suffix after the boundary is selected.
void resolveCompoundAdjectiveRenyokei(std::vector<core::Morpheme>& result) {
  for (size_t idx = 0; idx + 1 < result.size(); ++idx) {
    auto& stem = result[idx];
    const auto& adjective = result[idx + 1];
    const bool has_renyokei = stem.extended_pos == core::ExtendedPOS::VerbRenyokei;
    if ((stem.pos != core::PartOfSpeech::Noun && !has_renyokei) || adjective.pos != core::PartOfSpeech::Adjective ||
        adjective.extended_pos == core::ExtendedPOS::AdjStem ||
        !utf8::equalsAny(adjective.lemma, {"やすい", "にくい", "がたい"})) {
      continue;
    }
    if (!has_renyokei) {
      retagGodanRenyokeiFromIRow(stem, true);
    }
  }

  // The same suffixes can occur in their stem forms before appearance そう.
  // Their lexical readings (やすい adjective / にく verb) are then unavailable:
  // the preceding i-row stem fixes the productive compound construction.
  for (size_t idx = 0; idx + 2 < result.size(); ++idx) {
    auto& stem = result[idx];
    auto& suffix = result[idx + 1];
    auto& sou = result[idx + 2];
    const bool has_renyokei = stem.extended_pos == core::ExtendedPOS::VerbRenyokei;
    if ((stem.pos != core::PartOfSpeech::Noun && !has_renyokei) || sou.surface != "そう" ||
        !utf8::equalsAny(suffix.surface, {"やす", "にく"}) ||
        (!has_renyokei && !retagGodanRenyokeiFromIRow(stem, true))) {
      continue;
    }

    if (suffix.surface == "やす") {
      suffix.pos = core::PartOfSpeech::Auxiliary;
      suffix.extended_pos = core::ExtendedPOS::Unknown;
      suffix.lemma = "やす";
      suffix.conj_type = dictionary::ConjugationType::None;
      suffix.conj_form = grammar::ConjForm::Base;
      if (idx + 3 < result.size() && isPredicativeCopula(result[idx + 3])) {
        sou.pos = core::PartOfSpeech::Adjective;
        sou.extended_pos = core::ExtendedPOS::AdjNaAdj;
      }
      continue;
    }

    suffix.pos = core::PartOfSpeech::Noun;
    suffix.extended_pos = core::ExtendedPOS::Noun;
    suffix.lemma = "にく";
    suffix.conj_type = dictionary::ConjugationType::None;
    suffix.conj_form = grammar::ConjForm::Base;
    if (idx + 3 < result.size() && result[idx + 3].surface == "な") {
      retagAppearanceSou(sou);
      auto& na = result[idx + 3];
      retagCopulaDa(na);
    }
  }
}

// A sahen noun followed by the ambiguous し takes the productive する
// reading before an independent verb or negative auxiliary. The lattice may
// retain either a connective-particle or lexical-verb candidate, so resolve
// the complete grammatical context after boundary selection.
void resolveSahenRenyokei(std::vector<core::Morpheme>& result) {
  for (size_t idx = 1; idx + 1 < result.size(); ++idx) {
    const auto& noun = result[idx - 1];
    auto& suru = result[idx];
    auto& following = result[idx + 1];
    const bool can_be_suru =
        suru.extended_pos == core::ExtendedPOS::ParticleConj || suru.extended_pos == core::ExtendedPOS::VerbRenyokei;
    const bool negative_follows = following.extended_pos == core::ExtendedPOS::AuxNegativeNai ||
                                  (following.pos == core::PartOfSpeech::Adjective && following.surface == "ない");
    const bool completes_sahen = following.pos == core::PartOfSpeech::Verb || negative_follows;
    if (noun.pos != core::PartOfSpeech::Noun || !grammar::isAllKanji(noun.surface) ||
        normalize::utf8Length(noun.surface) < 2 || !grammar::isSuruRenyokeiSurface(suru.surface) || !can_be_suru ||
        !completes_sahen) {
      continue;
    }
    suru.pos = core::PartOfSpeech::Verb;
    suru.extended_pos = core::ExtendedPOS::VerbRenyokei;
    suru.lemma = "する";
    suru.conj_type = dictionary::ConjugationType::Suru;
    suru.conj_form = grammar::ConjForm::Renyokei;
    if (negative_follows && following.surface == "ない") {
      retagNegativeNai(following);
    }
  }
}

// A demonstrative adverb followed by いっ and a te/past continuation is the
// quotative verb 言う in euphonic form. The same bare surface remains
// ambiguous with 行く, so the deictic quotation context supplies the evidence.
void resolveDemonstrativeQuotativeOnbin(std::vector<core::Morpheme>& result) {
  for (size_t idx = 1; idx + 1 < result.size(); ++idx) {
    const auto& demonstrative = result[idx - 1];
    auto& verb = result[idx];
    const auto& continuation = result[idx + 1];
    const bool is_te_or_past = continuation.extended_pos == core::ExtendedPOS::ParticleConj ||
                               continuation.extended_pos == core::ExtendedPOS::AuxTenseTa;
    if (demonstrative.extended_pos != core::ExtendedPOS::Adverb ||
        !grammar::isDemonstrativeUAdverb(demonstrative.surface) || verb.surface != "いっ" || !is_te_or_past) {
      continue;
    }
    verb.pos = core::PartOfSpeech::Verb;
    verb.extended_pos = core::ExtendedPOS::VerbOnbinkei;
    verb.lemma = "いう";
    verb.conj_type = dictionary::ConjugationType::GodanWa;
    verb.conj_form = grammar::ConjForm::Onbinkei;
  }
}

// The causative homograph し before polite ます is the renyokei of する
// after an honorific verb stem or a nominalized stem (お+願い+し+ます).
// A real causative uses a mizenkei before せ/させ, so this finite polite
// continuation resolves the grammatical role without changing boundaries.
void resolvePoliteSuruRenyokei(std::vector<core::Morpheme>& result) {
  for (size_t idx = 1; idx + 1 < result.size(); ++idx) {
    const auto& stem = result[idx - 1];
    auto& suru = result[idx];
    const auto& masu = result[idx + 1];
    if ((stem.pos != core::PartOfSpeech::Noun && stem.extended_pos != core::ExtendedPOS::VerbRenyokei) ||
        !grammar::isSuruRenyokeiSurface(suru.surface) || suru.extended_pos != core::ExtendedPOS::AuxCausative ||
        masu.extended_pos != core::ExtendedPOS::AuxTenseMasu) {
      continue;
    }
    suru.pos = core::PartOfSpeech::Verb;
    suru.extended_pos = core::ExtendedPOS::VerbRenyokei;
    suru.lemma = "する";
    suru.conj_type = dictionary::ConjugationType::Suru;
    suru.conj_form = grammar::ConjForm::Renyokei;
  }
}

// An interrogative plus final particle forms an indefinite subject in
// 誰かいますか. The following いる is the existential main verb before ます,
// not the progressive auxiliary that appears after a verb te-form.
void resolveIndefiniteExistentialIru(std::vector<core::Morpheme>& result) {
  for (size_t idx = 2; idx + 1 < result.size(); ++idx) {
    const auto& interrogative = result[idx - 2];
    const auto& final_particle = result[idx - 1];
    auto& iru = result[idx];
    const auto& masu = result[idx + 1];
    if (interrogative.extended_pos != core::ExtendedPOS::PronounInterrogative ||
        final_particle.extended_pos != core::ExtendedPOS::ParticleFinal ||
        iru.extended_pos != core::ExtendedPOS::AuxAspectIru || masu.extended_pos != core::ExtendedPOS::AuxTenseMasu) {
      continue;
    }
    iru.pos = core::PartOfSpeech::Verb;
    iru.extended_pos = core::ExtendedPOS::VerbRenyokei;
    iru.lemma = "いる";
    iru.conj_type = dictionary::ConjugationType::Ichidan;
    iru.conj_form = grammar::ConjForm::Renyokei;
  }
}

}  // namespace suzume::postprocess::resolver

namespace suzume::postprocess {

void Postprocessor::convertPrefixVerbToNoun(std::vector<core::Morpheme>& morphemes) {
  if (morphemes.size() < 2) {
    return;
  }

  for (size_t i = 0; i < morphemes.size(); ++i) {
    core::Morpheme& morpheme = morphemes[i];

    // Check if previous morpheme was PREFIX (お or ご)
    if (i > 0 && morphemes[i - 1].pos == core::PartOfSpeech::Prefix) {
      const std::string& prefix_surface = morphemes[i - 1].surface;
      // Only for honorific prefixes お and ご
      if (utf8::equalsAny(prefix_surface, {"お", "ご", "御"})) {
        // Restore a nominally homographic stem when a following humble or
        // honorific subsidiary supplies decisive verbal context:
        // お+知らせ+いたす, お+使い+いただく. This mirrors the preservation
        // rule below for stems that already reached the lattice as verbs.
        if (morpheme.pos == core::PartOfSpeech::Noun && i + 1 < morphemes.size() &&
            grammar::isHumbleHonorificLemma(morphemes[i + 1].lemma)) {
          const char32_t morpheme_last = utf8::decodeLastChar(morpheme.surface);
          if (grammar::isIRowCodepoint(morpheme_last)) {
            resolver::retagGodanRenyokeiFromIRow(morpheme, false);
          } else if (grammar::isERowCodepoint(morpheme_last)) {
            morpheme.lemma = morpheme.surface + "る";
            morpheme.conj_type = dictionary::ConjugationType::Ichidan;
            morpheme.pos = core::PartOfSpeech::Verb;
            morpheme.extended_pos = core::ExtendedPOS::VerbRenyokei;
          }
        }
        if (morpheme.pos == core::PartOfSpeech::Noun && i + 2 < morphemes.size() &&
            morphemes[i + 1].extended_pos == core::ExtendedPOS::VerbRenyokei && morphemes[i + 1].surface == "し" &&
            morphemes[i + 2].extended_pos == core::ExtendedPOS::AuxTenseMasu && i >= 2 &&
            morphemes[i - 2].extended_pos == core::ExtendedPOS::ParticleCase && morphemes[i - 2].surface == "を") {
          if (grammar::isIRowCodepoint(utf8::decodeLastChar(morpheme.surface))) {
            resolver::retagGodanRenyokeiFromIRow(morpheme, true);
          }
        }
        // Convert VERB to NOUN (renyoukei nominalization)
        // e.g., 願い(VERB) → 願い(NOUN) after お
        // Exception: when followed by causative auxiliary (せ/させ),
        // the verb is part of a causative construction (お聞かせ, お知らせ)
        // and should remain as VERB
        // Nominalization after an honorific prefix applies to a continuative
        // stem, not to a finite verb.  Keeping this distinction preserves
        // literary terminal forms such as お+はす as verbs.
        if (morpheme.pos == core::PartOfSpeech::Verb && morpheme.extended_pos == core::ExtendedPOS::VerbRenyokei) {
          bool preserves_verbal_reading = false;
          if (i + 1 < morphemes.size()) {
            const auto& next = morphemes[i + 1];
            preserves_verbal_reading =
                grammar::isHumbleHonorificLemma(next.lemma) || next.extended_pos == core::ExtendedPOS::AuxCausative;

            // In the productive service construction object+お+連用形+する,
            // the stem remains a verb (荷物をお預かりします). The direct
            // object distinguishes this from nominalized forms such as
            // お待ちします, where 待ち remains a noun before する.
            bool follows_suru = next.extended_pos == core::ExtendedPOS::VerbRenyokei && next.surface == "し" &&
                                i + 2 < morphemes.size() &&
                                morphemes[i + 2].extended_pos == core::ExtendedPOS::AuxTenseMasu;
            bool has_direct_object = i >= 2 && morphemes[i - 2].extended_pos == core::ExtendedPOS::ParticleCase &&
                                     morphemes[i - 2].surface == "を";
            preserves_verbal_reading = preserves_verbal_reading || (has_direct_object && follows_suru);

            // An e-row continuative before する is an ichidan verb in the
            // productive honorific construction (お見せする), not a nominal
            // renyokei such as お待ちする.
            if (grammar::isERowCodepoint(utf8::decodeLastChar(morpheme.surface)) &&
                next.pos == core::PartOfSpeech::Verb && next.lemma == "する") {
              preserves_verbal_reading = true;
            }

            // In the productive honorific お/ご+連用形+に+なる
            // construction, the stem remains verbal even when it is
            // homographic with a nominalized renyokei (お聞きになる).
            if (next.extended_pos == core::ExtendedPOS::ParticleCase && next.surface == "に" &&
                i + 2 < morphemes.size()) {
              const auto& following = morphemes[i + 2];
              preserves_verbal_reading =
                  preserves_verbal_reading || (following.pos == core::PartOfSpeech::Verb && following.lemma == "なる");
            }
          }
          if (!preserves_verbal_reading) {
            morpheme.pos = core::PartOfSpeech::Noun;
            morpheme.extended_pos = core::ExtendedPOS::Noun;
            // Keep surface as lemma for nominalized form
            morpheme.lemma = morpheme.surface;
            SUZUME_DEBUG_LOG_VERBOSE("[POSTPROC] Nominalized: " << morpheme.surface << " (VERB → NOUN after "
                                                                << prefix_surface << ")\n");
          }
        }
      }
    }
  }
}

}  // namespace suzume::postprocess
