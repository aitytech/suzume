#include "postprocess/postprocessor.h"

#include <algorithm>
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

namespace suzume::postprocess {

namespace {

void retag(core::Morpheme& morpheme, core::PartOfSpeech pos, core::ExtendedPOS extended_pos, std::string_view lemma,
           dictionary::ConjugationType conj_type, grammar::ConjForm conj_form) {
  morpheme.pos = pos;
  morpheme.extended_pos = extended_pos;
  morpheme.lemma = lemma;
  morpheme.conj_type = conj_type;
  morpheme.conj_form = conj_form;
}

void retagAppearanceSou(core::Morpheme& sou) {
  retag(sou, core::PartOfSpeech::Auxiliary, core::ExtendedPOS::AuxAppearanceSou, "そう",
        dictionary::ConjugationType::None, grammar::ConjForm::Base);
}

void retagNaAdjectivalSou(core::Morpheme& sou) {
  retag(sou, core::PartOfSpeech::Adjective, core::ExtendedPOS::AdjNaAdj, "そう", dictionary::ConjugationType::None,
        grammar::ConjForm::Base);
}

void retagAdverbialSou(core::Morpheme& sou) {
  retag(sou, core::PartOfSpeech::Adverb, core::ExtendedPOS::Adverb, "そう", dictionary::ConjugationType::None,
        grammar::ConjForm::Base);
}

void retagCopulaDa(core::Morpheme& copula) {
  retag(copula, core::PartOfSpeech::Auxiliary, core::ExtendedPOS::AuxCopulaDa, "だ", dictionary::ConjugationType::None,
        grammar::ConjForm::Base);
}

void retagNegativeNai(core::Morpheme& negative) {
  retag(negative, core::PartOfSpeech::Auxiliary, core::ExtendedPOS::AuxNegativeNai, "ない",
        dictionary::ConjugationType::None, grammar::ConjForm::Base);
}

void retagBasicNegativeAdjective(core::Morpheme& negative) {
  retag(negative, core::PartOfSpeech::Adjective, core::ExtendedPOS::AdjBasic, "ない",
        dictionary::ConjugationType::IAdjective, grammar::ConjForm::Base);
}

// Recover a Godan dictionary form from an i-row continuative stem.  Some
// callers intentionally preserve the lattice-provided conjugation form while
// others require an explicit renyokei form, so that policy stays at the call
// site.
bool retagGodanRenyokeiFromIRow(core::Morpheme& stem, bool set_conj_form) {
  const auto codepoints = normalize::toCodepoints(stem.surface);
  if (codepoints.empty() || !grammar::isIRowCodepoint(codepoints.back())) {
    return false;
  }
  const std::string_view base_suffix = grammar::godanBaseSuffixFromIRow(codepoints.back());
  const grammar::VerbType verb_type = grammar::verbTypeFromIRowCodepoint(codepoints.back());
  if (base_suffix.empty() || verb_type == grammar::VerbType::Unknown) {
    return false;
  }
  stem.pos = core::PartOfSpeech::Verb;
  stem.extended_pos = core::ExtendedPOS::VerbRenyokei;
  stem.lemma = std::string(utf8::dropLastChar(stem.surface)) + std::string(base_suffix);
  stem.conj_type = grammar::verbTypeToConjType(verb_type);
  if (set_conj_form) {
    stem.conj_form = grammar::ConjForm::Renyokei;
  }
  return true;
}

// Check if a surface is a counter/duration quantity that a temporal 後 attaches to
// as a suffix (2時間, 10日, 5分, 数日, 半年): first codepoint is a numeral or inexact
// quantity prefix (数/半/何) and the last is a counter kanji. MeCab tags 後 after such
// a quantity as 名詞,接尾 (Suffix), whereas 後 after an ordinary noun (食事の後) stays a
// plain noun.
bool isCounterDurationNoun(const std::string& surface) {
  auto codepoints = normalize::toCodepoints(surface);
  if (codepoints.empty()) {
    return false;
  }
  return (normalize::isNumeralCodepoint(codepoints.front()) || normalize::isQuantityPrefixKanji(codepoints.front())) &&
         normalize::isCounterKanji(codepoints.back());
}

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
    if (!dependent_predecessor || followed_by_polite || benefactive.lemma != "いただける") {
      continue;
    }
    benefactive.pos = core::PartOfSpeech::Auxiliary;
    benefactive.extended_pos = core::ExtendedPOS::AuxBenefactive;
    benefactive.conj_type = dictionary::ConjugationType::Ichidan;
    benefactive.conj_form = grammar::ConjForm::Base;
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

  resolveNegativeAppearanceChain(result);
  resolveAdjectiveNominalizerSa(result);
  resolveAppearanceSouPredicate(result);
  resolveProgressiveContractionNominalizer(result);
  resolveNominalPredicateNai(result);
  resolveCertaintyChigaiNai(result);
  mergeSplitCopularNegative(result);
  resolveInitialNegativeAdjective(result);
  resolveObligationNaranai(result);
  resolveHonorificVerbInflection(result);
  resolvePreparatoryVolitional(result);
  resolveCopularForms(result);
  resolveGozaruPoliteAuxiliary(result);
  resolveNominalCaseDe(result);
  resolveCopularForms(result);
  resolveTendencySuffixCopula(result);
  resolveCopularAro(result);
  resolveNominalDeAru(result);
  resolveComparisonNoun(result);
  resolveNegativeRenyokei(result);
  resolveVerbTeParticle(result);
  resolveTearuAuxiliary(result);
  resolveKuruwaPoliteAru(result);

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

  resolveParticleAruOnbin(result);
  resolveBenefactivePotential(result);
  resolveInitialInabilityVerb(result);
  resolveTeBenefactiveNegativePotential(result);
  resolveProgressiveIru(result);
  resolveSahenRenyokei(result);
  resolveDemonstrativeQuotativeOnbin(result);
  resolvePoliteSuruRenyokei(result);
  resolveIndefiniteExistentialIru(result);
  resolveNominalizedRenyokeiPredicate(result);
  convertPrefixVerbToNoun(result);
  resolveCompoundAdjectiveRenyokei(result);
  resolveAdverbExplanatoryCopula(result);
  resolveSimilitudeYou(result);
  resolveNominalConditionalNara(result);

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
        result[i - 1].pos == core::PartOfSpeech::Noun && isCounterDurationNoun(result[i - 1].surface)) {
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
        isCompoundRenyokeiShape(result[i].surface) &&
        (i + 1 == result.size() || isNominalForcingParticle(result[i + 1]))) {
      result[i].pos = core::PartOfSpeech::Noun;
      result[i].extended_pos = core::ExtendedPOS::NounVerbal;
      result[i].lemma = result[i].surface;
      result[i].conj_type = dictionary::ConjugationType::None;
      result[i].conj_form = grammar::ConjForm::Base;
    }
  }

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

  resolveDurationPredicateKakaru(result);

  // Resolve the sentence-initial demonstrative after all compound candidates
  // have been filtered, so retagging cannot alter a copular boundary.
  if (result.size() >= 2 && result[0].surface == "そう" && result[0].pos == core::PartOfSpeech::Adverb &&
      result[1].extended_pos == core::ExtendedPOS::AuxCopulaDa &&
      (result.size() < 3 || result[2].surface != "ござい")) {
    retagNaAdjectivalSou(result[0]);
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
    retagAppearanceSou(sou);
    retagCopulaDa(na);
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
          auto codepoints = normalize::toCodepoints(morpheme.surface);
          if (!codepoints.empty() && grammar::isIRowCodepoint(codepoints.back())) {
            retagGodanRenyokeiFromIRow(morpheme, false);
          } else if (!codepoints.empty() && grammar::isERowCodepoint(codepoints.back())) {
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
          auto codepoints = normalize::toCodepoints(morpheme.surface);
          if (!codepoints.empty() && grammar::isIRowCodepoint(codepoints.back())) {
            retagGodanRenyokeiFromIRow(morpheme, true);
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
            const auto stem_codepoints = normalize::toCodepoints(morpheme.surface);
            if (!stem_codepoints.empty() && grammar::isERowCodepoint(stem_codepoints.back()) &&
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

namespace {

// Check if a character is a digit (ASCII or fullwidth)
bool isDigitChar(char32_t ch) {
  return (ch >= U'0' && ch <= U'9') || (ch >= U'０' && ch <= U'９');
}

// Check if surface is a numeric expression (starts with digit or contains units)
bool isNumericExpression(const std::string& surface) {
  if (surface.empty())
    return false;

  size_t pos = 0;
  char32_t first_ch = suzume::normalize::decodeUtf8(surface, pos);
  return isDigitChar(first_ch);
}

// Check if surface ends with a digit
bool endsWithDigit(const std::string& surface) {
  if (surface.empty())
    return false;

  auto codepoints = suzume::normalize::toCodepoints(surface);
  if (codepoints.empty())
    return false;

  return isDigitChar(codepoints.back());
}

using normalize::isAllKatakana;
using normalize::isCounterKanji;

// Check if surface looks like a unit (noun that can follow numbers)
// For kanji: must start with a counter kanji (円, 分, 時間, etc.)
// For katakana: any katakana noun merges (MeCab treats number + katakana as one
// quantity token, e.g. 3キロ, 100メダル), so no curated unit list is needed.
bool looksLikeUnit(const std::string& surface) {
  if (surface.empty())
    return false;

  // Only the first codepoint drives the kanji-unit branch; kanji/katakana are
  // 3-byte, so decoding just the leading char avoids allocating a codepoint
  // vector. Non-3-byte leads decode to 0 and fall through to the katakana check.
  char32_t first = utf8::decodeFirstChar(surface);

  // Kanji units: first char must be a counter kanji
  // CJK Unified Ideographs: U+4E00-U+9FFF
  if (first >= 0x4E00 && first <= 0x9FFF) {
    return isCounterKanji(first);
  }

  // Katakana nouns: any all-katakana surface merges with a preceding numeral
  if (isAllKatakana(surface)) {
    return true;
  }

  return false;
}

// Check if surface ends with a numeric unit that can be followed by more numbers
bool endsWithContinuableUnit(const std::string& surface) {
  if (surface.empty())
    return false;

  // Targets 兆/億/万/千/百 are all 3-byte kanji; decode only the trailing char.
  char32_t last_ch = utf8::decodeLastChar(surface);
  // Units that can be followed by more numbers (兆, 億, 万, 千, 百)
  return last_ch == U'兆' || last_ch == U'億' || last_ch == U'万' || last_ch == U'千' || last_ch == U'百';
}

}  // namespace

std::vector<core::Morpheme> Postprocessor::mergeNumericExpressions(std::vector<core::Morpheme> morphemes) {
  if (morphemes.empty()) {
    return morphemes;
  }

  std::vector<core::Morpheme> result;
  result.reserve(morphemes.size());

  size_t idx = 0;
  while (idx < morphemes.size()) {
    const auto& current = morphemes[idx];

    // Pattern 0: Ordinal prefix + numeric expression (第 + 3回 → 第3回).
    // The prefix scopes the complete quantity, including an optional ordinal
    // suffix (第3回目), so retain it as one search unit.
    if (current.pos == core::PartOfSpeech::Noun && utf8::equalsAny(current.surface, {"第"}) &&
        idx + 1 < morphemes.size()) {
      const auto& next = morphemes[idx + 1];
      if (next.pos == core::PartOfSpeech::Noun && isNumericExpression(next.surface)) {
        core::Morpheme merged = current;
        merged.surface += next.surface;
        merged.lemma = merged.surface;
        merged.end = next.end;
        merged.end_pos = next.end_pos;
        size_t merge_end = idx + 2;

        if (merge_end < morphemes.size() && morphemes[merge_end].pos == core::PartOfSpeech::Noun &&
            utf8::equalsAny(morphemes[merge_end].surface, {"目"})) {
          merged.surface += morphemes[merge_end].surface;
          merged.lemma = merged.surface;
          merged.end = morphemes[merge_end].end;
          merged.end_pos = morphemes[merge_end].end_pos;
          ++merge_end;
        }

        result.push_back(merged);
        idx = merge_end;
        continue;
      }
    }

    // Pattern 1: Merge large numbers (3億 + 5000万円)
    if (current.pos == core::PartOfSpeech::Noun && isNumericExpression(current.surface) &&
        endsWithContinuableUnit(current.surface)) {
      core::Morpheme merged = current;
      size_t merge_end = idx + 1;

      // Collect consecutive numeric expressions
      while (merge_end < morphemes.size()) {
        const auto& next = morphemes[merge_end];
        if (next.pos == core::PartOfSpeech::Noun && isNumericExpression(next.surface)) {
          merged.surface += next.surface;
          merged.lemma = merged.surface;
          merged.end = next.end;
          merged.end_pos = next.end_pos;
          ++merge_end;

          // Continue if this also ends with a continuable unit
          if (!endsWithContinuableUnit(next.surface)) {
            break;
          }
        } else {
          break;
        }
      }

      SUZUME_DEBUG_IF(merge_end > idx + 1) {
        SUZUME_DEBUG_STREAM << "[POSTPROC] Merged numeric: ";
        for (size_t i = idx; i < merge_end; ++i) {
          if (i > idx)
            SUZUME_DEBUG_STREAM << " + ";
          SUZUME_DEBUG_STREAM << "\"" << morphemes[i].surface << "\"";
        }
        SUZUME_DEBUG_STREAM << " → \"" << merged.surface << "\"\n";
      }

      result.push_back(merged);
      idx = merge_end;
      continue;
    }

    // Pattern 2: Merge number + unit (3 + 時間, 100 + ゴールド, 3時 + 間)
    // Exception: 対 (versus) should not merge - 2対1 should be 2|対|1
    if (current.pos == core::PartOfSpeech::Noun && isNumericExpression(current.surface) &&
        endsWithDigit(current.surface) && idx + 1 < morphemes.size()) {
      const auto& next = morphemes[idx + 1];
      bool is_versus = (next.surface == "対");
      if (next.pos == core::PartOfSpeech::Noun && looksLikeUnit(next.surface) && !is_versus) {
        core::Morpheme merged = current;
        merged.surface += next.surface;
        merged.lemma = merged.surface;
        merged.end = next.end;
        merged.end_pos = next.end_pos;

        SUZUME_DEBUG_LOG_VERBOSE("[POSTPROC] Merged number+unit: \"" << current.surface << "\" + \"" << next.surface
                                                                     << "\" → \"" << merged.surface << "\"\n");

        result.push_back(merged);
        idx += 2;
        continue;
      }
    }

    // Pattern 3: Merge numeric with unit suffix (3時 + 間 → 3時間)
    if (current.pos == core::PartOfSpeech::Noun && isNumericExpression(current.surface) && idx + 1 < morphemes.size()) {
      const auto& next = morphemes[idx + 1];
      // Check for common time/counter suffixes that get split
      if (next.pos == core::PartOfSpeech::Noun && utf8::equalsAny(next.surface, {"間", "半", "目"})) {
        core::Morpheme merged = current;
        merged.surface += next.surface;
        merged.lemma = merged.surface;
        merged.end = next.end;
        merged.end_pos = next.end_pos;

        SUZUME_DEBUG_LOG_VERBOSE("[POSTPROC] Merged numeric+suffix: \"" << current.surface << "\" + \"" << next.surface
                                                                        << "\" → \"" << merged.surface << "\"\n");

        result.push_back(merged);
        idx += 2;
        continue;
      }
    }

    // Pattern 4: Merge indefinite numeral + counter suffix (数 + ヶ月 → 数ヶ月)
    // Indefinite numerals: 数 (suu = some/several), 幾 (iku = how many)
    if ((current.pos == core::PartOfSpeech::Noun || current.pos == core::PartOfSpeech::Pronoun) &&
        utf8::equalsAny(current.surface, {"数", "幾", "何"}) && idx + 1 < morphemes.size()) {
      const auto& next = morphemes[idx + 1];
      if (next.pos == core::PartOfSpeech::Suffix) {
        core::Morpheme merged = current;
        merged.pos = core::PartOfSpeech::Noun;  // Merged result is always NOUN
        merged.surface += next.surface;
        merged.lemma = merged.surface;
        merged.end = next.end;
        merged.end_pos = next.end_pos;

        SUZUME_DEBUG_LOG_VERBOSE("[POSTPROC] Merged indefinite+suffix: \""
                                 << current.surface << "\" + \"" << next.surface << "\" → \"" << merged.surface
                                 << "\"\n");

        result.push_back(merged);
        idx += 2;
        continue;
      }
    }

    result.push_back(std::move(morphemes[idx]));
    ++idx;
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
    // Check if next morpheme is ー (or consecutive ーs)
    if (i + 1 < morphemes.size()) {
      const auto& next = morphemes[i + 1];
      const auto next_cps = normalize::toCodepoints(next.surface);
      const bool next_is_prolonged = std::all_of(next_cps.begin(), next_cps.end(), normalize::isProlongedSoundMark);

      if (next_is_prolonged && !next.surface.empty()) {
        const auto& current = morphemes[i];
        // Only merge if preceding token is not a symbol
        if (current.pos != core::PartOfSpeech::Symbol) {
          core::Morpheme merged = current;
          merged.surface += next.surface;
          merged.end = next.end;
          merged.end_pos = next.end_pos;
          // Update lemma
          if (!merged.lemma.empty()) {
            merged.lemma += next.surface;
          }

          // Skip any additional ー tokens
          size_t skip = i + 2;
          while (skip < morphemes.size()) {
            const auto skip_cps = normalize::toCodepoints(morphemes[skip].surface);
            const bool is_prolonged = std::all_of(skip_cps.begin(), skip_cps.end(), normalize::isProlongedSoundMark);
            if (!is_prolonged)
              break;
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
          i = skip - 1;  // Will be incremented by loop
          continue;
        }
      }
    }
    result.push_back(std::move(morphemes[i]));
  }

  return result;
}

}  // namespace suzume::postprocess
