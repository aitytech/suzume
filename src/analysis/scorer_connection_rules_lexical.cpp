#include "analysis/bigram_table.h"
#include "analysis/category_cost.h"
#include "analysis/scorer.h"
#include "analysis/scorer_connection_rules.h"
#include "analysis/scorer_constants.h"
#include "analysis/verb_candidates_helpers.h"
#include "core/debug.h"
#include "core/kana_constants.h"
#include "core/types.h"
#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "grammar/honorific_verbs.h"
#include "normalize/char_type.h"
#include "normalize/utf8.h"

namespace cost = suzume::analysis::bigram_cost;
namespace sc = suzume::analysis::scorer;

// Surface-based adjustments use cost:: namespace directly from bigram_cost.
// See bigram_table.h and scorer_constants.h for constant values.

namespace suzume::analysis::connection_rules {

namespace {

bool isGodanRenyokeiOfLemma(std::string_view surface, std::string_view lemma) {
  const auto codepoints = normalize::toCodepoints(surface);
  if (codepoints.empty()) {
    return false;
  }

  const std::string_view base_suffix = grammar::godanBaseSuffixFromIRow(codepoints.back());
  if (base_suffix.empty()) {
    return false;
  }

  const std::string_view surface_last = utf8::lastChar(surface);
  const std::string_view lemma_last = utf8::lastChar(lemma);
  return lemma_last == base_suffix &&
         surface.substr(0, surface.size() - surface_last.size()) == lemma.substr(0, lemma.size() - lemma_last.size());
}

}  // namespace

float computeSuffixShortVerbBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float bonus{};  // value-init to 0

  // The temporal suffix 中 cannot govern an accusative object.  In a noun
  // compound before を, retain the complete lexical noun (背中を) rather than
  // treating its final character as a duration suffix.
  if (prev.extended_pos == core::ExtendedPOS::Suffix && grammar::isStateDurationSuffix(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::ParticleCase && grammar::isAccusativeParticleWoSurface(next.surface)) {
    bonus += cost::kStrong;
  }

  // The literary formal noun いかん forms a case-marked conditional phrase
  // before a predicate (いかん+に+かかわらず). This closed nominal pattern
  // outranks the homographic いか + ん negative-verb analysis.
  if (prev.extended_pos == core::ExtendedPOS::NounFormal && prev.lemma == "いかん" &&
      next.extended_pos == core::ExtendedPOS::ParticleCase) {
    bonus += cost::kDoubleVeryStrongBonus;
  }

  // The explanatory copula sequence のです must not be resegmented as the
  // conjunctive particle ので followed by the lexical verb す.  The latter
  // has no grammatical continuation, whereas の + です is productive after
  // any predicate.
  if (prev.extended_pos == core::ExtendedPOS::ParticleConj && next.extended_pos == core::ExtendedPOS::VerbShuushikei &&
      utf8::equalsAny(prev.surface, {"ので"}) && utf8::equalsAny(next.surface, {"す"})) {
    bonus += cost::kAlmostNever;
  }

  // The sentence-final particle ったら attaches to a completed predicate
  // (困ったら), never directly to a noun. This blocks a high-scoring false
  // parse such as 行 + ったら while leaving ordinary noun-final questions
  // (本か) untouched.
  if (prev.pos == core::PartOfSpeech::Noun && next.extended_pos == core::ExtendedPOS::ParticleFinal &&
      utf8::equalsAny(next.surface, {"ったら"})) {
    bonus += cost::kAlmostNever;
  }

  // The fixed negative predicate ほかならない starts after a case-marked
  // nominal complement (原因に+ほかなら+ない). Keep its lexical verb stem
  // together instead of reopening the formal noun ほか before ならない.
  if (prev.extended_pos == core::ExtendedPOS::ParticleCase && next.extended_pos == core::ExtendedPOS::VerbMizenkei &&
      next.lemma == "ほかなる") {
    bonus += cost::kStrongBonus;
  }

  // The conditional allomorph たら is an auxiliary after a completed verb.
  // When another predicate follows, retain that analysis rather than the
  // homographic conjunctive-particle entry.
  if (prev.extended_pos == core::ExtendedPOS::AuxTenseTa && utf8::equalsAny(prev.surface, {"たら"}) &&
      next.pos == core::PartOfSpeech::Verb) {
    bonus += cost::kTripleVeryStrongBonus;
  }

  // The enumerative particle だの follows a noun directly. Keep its
  // closed-class analysis ahead of the unrelated copula-plus-nominalizer
  // sequence in noun lists.
  if (prev.pos == core::PartOfSpeech::Noun && next.extended_pos == core::ExtendedPOS::ParticleConj &&
      utf8::equalsAny(next.surface, {"だの"})) {
    bonus += cost::kStrongBonus;
  }

  // Classical focus なむ can precede a quotative と. The fused particle
  // provides a grammatical boundary that must outrank な + む auxiliaries.
  if (prev.extended_pos == core::ExtendedPOS::ParticleBinding && next.extended_pos == core::ExtendedPOS::ParticleCase &&
      utf8::equalsAny(prev.surface, {"なむ"}) && utf8::equalsAny(next.surface, {"と"})) {
    bonus += cost::kTripleVeryStrongBonus;
  }

  // The adjacent copula and nominalizer spelling だの is the enumerative
  // particle, not a productive copula phrase.
  if (prev.extended_pos == core::ExtendedPOS::AuxCopulaDa && next.extended_pos == core::ExtendedPOS::ParticleNo &&
      utf8::equalsAny(prev.surface, {"だ"}) && utf8::equalsAny(next.surface, {"の"})) {
    bonus += cost::kAlmostNever;
  }

  // A bare noun followed by the adverbial negative なく is the formal
  // "without [noun]" construction (資金なくして). Prefer it over the
  // transitive verb なくす, whose object requires a case-marked noun (物を
  // なくして). The lemma gate keeps ordinary adjective renyokei attachments
  // untouched.
  if (prev.pos == core::PartOfSpeech::Noun && next.extended_pos == core::ExtendedPOS::AdjRenyokei &&
      next.lemma == "ない") {
    bonus += cost::kVeryStrongBonus;
  }

  // A deverbal noun ending in 違い before ない forms the lexicalized
  // negative-adjective predicate (間違い+ない). The suffix identifies the
  // productive noun family without changing ordinary nominal negatives.
  if (prev.extended_pos == core::ExtendedPOS::Noun && utf8::endsWith(prev.surface, "違い") &&
      next.extended_pos == core::ExtendedPOS::AdjBasic && next.lemma == "ない") {
    bonus += cost::kStrongBonus;
  }

  // A personal or demonstrative pronoun cannot directly precede the
  // continuative し of する. This keeps それ+らしい distinct from the
  // unrelated plural-pronoun-plus-verb path while leaving interrogative
  // 何+し constructions to their dedicated rule.
  if (prev.extended_pos == core::ExtendedPOS::Pronoun && grammar::isConjunctiveParticleShi(next.surface)) {
    bonus += cost::kAlmostNever;
  }

  // A dictionary noun followed by a registered adverbial particle is a stable
  // nominal phrase (あん+だけ, そん+だけ).  Keep that lexical boundary ahead
  // of a spurious copula plus sentence-final particle, but do not promote
  // generated noun readings such as the adjective ない before ほど.
  if (prev.pos == core::PartOfSpeech::Noun && prev.fromDictionary() &&
      next.extended_pos == core::ExtendedPOS::ParticleAdverbial) {
    bonus += cost::kMinorBonus;
  }

  // Determiners directly modify formal nouns (このこと, どういうこと).
  // Preserve that productive adnominal boundary over a split into an
  // adverb/adjective and the independent verb いう.
  if (prev.extended_pos == core::ExtendedPOS::Determiner && next.extended_pos == core::ExtendedPOS::NounFormal) {
    bonus += cost::kStrongBonus;
  }

  // The productive honorific construction お/ご + verb (お聞きになる,
  // おはす) must retain the dictionary-verified verbal reading ahead of a
  // homographic noun candidate. This covers renyokei requests and finite
  // literary honorific verbs; an irrealis stem cannot directly follow the
  // prefix and must instead take its auxiliary before the construction is
  // complete.
  if (prev.extended_pos == core::ExtendedPOS::Prefix && grammar::isHonorificPrefix(prev.surface) &&
      next.pos == core::PartOfSpeech::Verb && next.extended_pos != core::ExtendedPOS::VerbMizenkei &&
      next.fromDictionary()) {
    bonus += cost::kStrongBonus;
  }

  // The parallel compound particle とともに must remain whole instead of
  // being reanalyzed as a quotative particle followed by the adverb ともに.
  if (prev.extended_pos == core::ExtendedPOS::ParticleCase && next.pos == core::PartOfSpeech::Adverb &&
      grammar::isParallelTogetherAdverb(next.surface)) {
    bonus += cost::kProhibitive;
  }

  // A non-quotative case particle can introduce a multi-mora renyokei in an
  // ordinary predicate. Keep this complete verb ahead of a fabricated
  // one-mora causative plus polite-auxiliary chain. The quotative と is
  // excluded because its following いう boundary is intentionally ambiguous.
  if (prev.extended_pos == core::ExtendedPOS::ParticleCase && !utf8::equalsAny(prev.surface, {"と"}) &&
      next.extended_pos == core::ExtendedPOS::VerbRenyokei && next.surface.size() > core::kJapaneseCharBytes) {
    bonus += cost::kStrongBonus;
  }

  // A finite verb directly follows the accusative particle を (しびれを切らす),
  // but not every case particle permits that boundary.  In particular, で
  // must leave the polite copula です intact.
  if (prev.extended_pos == core::ExtendedPOS::ParticleCase && grammar::isAccusativeParticleWoSurface(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::VerbShuushikei) {
    bonus += cost::kVeryStrongBonus;
  }

  // A nominative case particle directly introduces a finite predicate. Keep a
  // dictionary verb at this boundary ahead of a shorter homographic split.
  if (prev.extended_pos == core::ExtendedPOS::ParticleCase && utf8::equalsAny(prev.surface, {"が"}) &&
      next.extended_pos == core::ExtendedPOS::VerbShuushikei && next.fromDictionary()) {
    bonus += cost::kStrongBonus;
  }

  if (prev.extended_pos == core::ExtendedPOS::AuxTenseTa && utf8::equalsAny(prev.surface, {"た"}) &&
      next.extended_pos == core::ExtendedPOS::ParticleConj && utf8::equalsAny(next.surface, {"ば"})) {
    bonus += cost::kAlmostNever;
  }

  // A conditional ば requires a hypothetical verb form (書け+ば), not a
  // terminal form. This blocks fragment paths such as す+ば+らしい.
  if (prev.extended_pos == core::ExtendedPOS::VerbShuushikei && next.extended_pos == core::ExtendedPOS::ParticleConj &&
      utf8::equalsAny(next.surface, {"ば"})) {
    bonus += cost::kAlmostNever;
  }

  // A connective し follows a terminal predicate, never an euphonic verb
  // form. The restriction preserves whole i-adjectives such as おいしく
  // against a fabricated おい+し route.
  if (prev.extended_pos == core::ExtendedPOS::VerbOnbinkei && next.extended_pos == core::ExtendedPOS::ParticleConj &&
      grammar::isConjunctiveParticleShi(next.surface)) {
    bonus += cost::kAlmostNever;
  }

  // A connective し also cannot attach to an interjection or an adverb. Those
  // paths split i-adjective stems (おい+し, バカバカ+し) without a predicate.
  if ((prev.pos == core::PartOfSpeech::Interjection || prev.extended_pos == core::ExtendedPOS::Adverb) &&
      next.extended_pos == core::ExtendedPOS::ParticleConj && grammar::isConjunctiveParticleShi(next.surface)) {
    bonus += cost::kAlmostNever;
  }

  // An adjective stem cannot take the past-conditional particle ったら.
  // The sequence is a verb's euphonic stem plus tense auxiliary (なっ+たら).
  if (prev.extended_pos == core::ExtendedPOS::AdjStem && next.extended_pos == core::ExtendedPOS::ParticleFinal &&
      utf8::equalsAny(next.surface, {"ったら"})) {
    bonus += cost::kAlmostNever;
  }

  // The concessive particle とも attaches to an adjective renyokei or the
  // classical negative ず, then introduces a finite predicate. Keep this
  // closed particle whole instead of splitting it into quotative と plus も.
  if ((prev.extended_pos == core::ExtendedPOS::AdjRenyokei || prev.extended_pos == core::ExtendedPOS::AuxNegativeNu) &&
      next.extended_pos == core::ExtendedPOS::ParticleConj && grammar::isConcessiveParticleTomoSurface(next.surface)) {
    bonus += cost::kStrongBonus;
  }
  if (prev.extended_pos == core::ExtendedPOS::ParticleConj && grammar::isConcessiveParticleTomoSurface(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::VerbShuushikei) {
    bonus += cost::kStrongBonus;
  }
  if (prev.extended_pos == core::ExtendedPOS::ParticleConj && grammar::isConcessiveParticleTomoSurface(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::AdjBasic) {
    bonus += cost::kStrongBonus;
  }

  // A case-marked object cannot be followed by an unsplit negative verb
  // candidate. Negative inflection is represented as a predicate stem plus
  // its auxiliary (を+え+ない), including past and conditional forms.
  if (prev.extended_pos == core::ExtendedPOS::ParticleCase && grammar::isAccusativeParticleWoSurface(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      utf8::endsWithAny(next.surface, {"ない", "なかっ", "なけれ"})) {
    bonus += cost::kAlmostNever;
  }
  if (prev.extended_pos == core::ExtendedPOS::ParticleCase && grammar::isAccusativeParticleWoSurface(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::VerbOnbinkei && utf8::endsWith(next.surface, "なかっ")) {
    bonus += cost::kAlmostNever;
  }

  // A candidate ending in te is not a terminal adjective. It must be analyzed
  // as an adjective renyokei plus the connective particle before a formal
  // noun, rather than as a malformed whole adjective (嬉しくてしかたがない).
  if (prev.extended_pos == core::ExtendedPOS::AdjBasic && utf8::endsWith(prev.surface, "て") &&
      next.extended_pos == core::ExtendedPOS::NounFormal) {
    bonus += cost::kAlmostNever;
  }

  // An adverbial particle can directly modify a kanji verb in euphonic form
  // before the past auxiliary (ずつ+配っ+た). A generated candidate must also
  // have an actual euphonic ending: its ExtendedPOS alone can be spuriously
  // assigned to an unsplit negative such as 帰らない. Keep the productive past
  // split ahead of a single VerbTaForm candidate; pure-hiragana copular forms
  // stay outside this rule.
  if (prev.extended_pos == core::ExtendedPOS::ParticleAdverbial &&
      next.extended_pos == core::ExtendedPOS::VerbOnbinkei && grammar::containsKanji(next.surface) &&
      (utf8::endsWithAny(next.surface, {"っ", "ん"}) ||
       (utf8::endsWith(next.surface, "い") && !utf8::endsWith(next.surface, "ない")))) {
    bonus += cost::kExtremeBonus;
  }

  // A productive temporal compound ending in 時 (開始時, 緊急時) is a
  // complete search unit before a following case particle.  Keep that unit
  // ahead of the competing noun + formal-noun 時 path.
  if (prev.pos == core::PartOfSpeech::Noun && !prev.fromDictionary() && utf8::endsWith(prev.surface, "時") &&
      next.extended_pos == core::ExtendedPOS::ParticleCase) {
    bonus += cost::kStrongBonus;
  }

  // A one-kanji noun followed by a one-kanji formal noun is usually a lexical
  // compound (人物, 結末), not a productive formal-noun boundary.  Longer
  // nominal stems remain available for bound temporal/spatial forms such as
  // 年度+末 and 期間+内.
  if (prev.pos == core::PartOfSpeech::Noun && next.extended_pos == core::ExtendedPOS::NounFormal &&
      prev.surface.size() == core::kJapaneseCharBytes && next.surface.size() == core::kJapaneseCharBytes &&
      grammar::isAllKanji(prev.surface) && grammar::isAllKanji(next.surface)) {
    bonus += cost::kStrong;
  }

  // A multi-kanji lexical stem followed by an all-kanji formal noun is normally
  // one compound search unit (不祥事, 出来事). Temporal endpoint suffixes retain
  // their productive boundary (年度+末, 学期+末).
  if (next.extended_pos == core::ExtendedPOS::NounFormal && prev.surface.size() >= 2 * core::kJapaneseCharBytes &&
      next.surface.size() == core::kJapaneseCharBytes && grammar::isAllKanji(prev.surface) &&
      grammar::isAllKanji(next.surface) && !normalize::isTemporalSpanSuffixKanji(utf8::decodeFirstChar(next.surface))) {
    bonus += cost::kStrong;
  }

  // A numeral duration ending in 時間 can directly modify a following
  // predicate without a case particle (三時間かかる, 二時間待つ). Prefer the
  // complete NounNumber candidate to a spurious 時+間 suffix split.
  if (prev.extended_pos == core::ExtendedPOS::NounNumber && utf8::endsWith(prev.surface, "時間") &&
      next.pos == core::PartOfSpeech::Verb) {
    bonus += cost::kVeryStrongBonus;
  }

  // A quantity suffix such as 半 follows a completed numeral-counter phrase;
  // it cannot be the kanji stem of a following compound verb.
  if (prev.extended_pos == core::ExtendedPOS::NounNumber && next.pos == core::PartOfSpeech::Verb &&
      normalize::isQuantityPrefixKanji(utf8::decodeFirstChar(next.surface))) {
    bonus += cost::kAlmostNever;
  }

  // A dictionary-verified long hiragana renyokei can lexicalize as a
  // discourse connective before a nominal predicate (さておき説明する).
  // Requiring four moras keeps short auxiliary stems such as おき outside the
  // rule while retaining the productive verb-form analysis and its lemma.
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && prev.fromDictionary() &&
      prev.surface.size() >= 4 * core::kJapaneseCharBytes && grammar::isPureHiragana(prev.surface) &&
      next.pos == core::PartOfSpeech::Noun) {
    bonus += cost::kDoubleVeryStrongBonus;
  }

  // The concessive particle とも attaches to a predicate (読まずとも), not
  // directly to an adverb or a finite adjective. After either predicate-like
  // modifier, retain the productive quotation plus topic sequence instead
  // (そう+と+も言える, 恐しい+と+も思わない).
  if ((prev.pos == core::PartOfSpeech::Adverb || prev.extended_pos == core::ExtendedPOS::AdjBasic ||
       prev.extended_pos == core::ExtendedPOS::AdjNaAdj) &&
      next.extended_pos == core::ExtendedPOS::ParticleConj && grammar::isConcessiveParticleTomoSurface(next.surface)) {
    bonus += cost::kAlmostNever;
  }

  // The concessive とも can directly introduce an adjective predicate
  // (読まずともよい). Keep that closed grammatical connection ahead of the
  // unrelated quotative-particle path.
  if (prev.extended_pos == core::ExtendedPOS::ParticleConj && grammar::isConcessiveParticleTomoSurface(prev.surface) &&
      next.pos == core::PartOfSpeech::Adjective) {
    bonus += cost::kStrongBonus;
  }

  // An adverb ending in the connective mora て cannot directly introduce the
  // progressive auxiliary. Preserve the productive verb-onbin + て + いる
  // path (続い+て+いる) over a lexicalized adverb candidate (続いて).
  if (prev.pos == core::PartOfSpeech::Adverb && utf8::endsWith(prev.surface, "て") &&
      (next.extended_pos == core::ExtendedPOS::AuxAspectIru ||
       (next.pos == core::PartOfSpeech::Verb && next.lemma == "いる"))) {
    bonus += cost::kStrong;
  }

  // The directional particle sequence へ+と is productive (次へと進む,
  // 都市へと向かう). Keep the two search units instead of promoting their
  // hiragana spelling to an unknown noun.
  if (prev.extended_pos == core::ExtendedPOS::ParticleCase && next.extended_pos == core::ExtendedPOS::ParticleCase &&
      utf8::equalsAny(prev.surface, {"へ"}) && utf8::equalsAny(next.surface, {"と"})) {
    bonus += cost::kVeryStrongBonus;
  }

  // 向け is a productive audience suffix after a nominal (読者+向けに,
  // 家庭+向け). Its following case particle makes the compositional boundary
  // preferable to an unknown noun candidate spanning the entire expression.
  if (prev.pos == core::PartOfSpeech::Noun && next.pos == core::PartOfSpeech::Suffix &&
      utf8::equalsAny(next.surface, {"向け"})) {
    bonus += cost::kMinorBonus;
  }

  // Adjacent administrative units are separate search units (都道府県+市区町村).
  // Their dedicated suffix candidates retain this boundary over an arbitrary
  // long unknown kanji sequence.
  if (prev.origin == core::CandidateOrigin::SuffixPattern && next.origin == core::CandidateOrigin::SuffixPattern &&
      prev.pos == core::PartOfSpeech::Noun && next.pos == core::PartOfSpeech::Noun &&
      grammar::endsWithAdministrativeSuffix(prev.surface) && grammar::endsWithAdministrativeSuffix(next.surface)) {
    bonus += cost::kMinorBonus;
  }

  // 中 is a productive state/duration suffix after a nominal stem (作業+中,
  // 今日+中). Prefer that grammatical boundary over an unregistered compound;
  // lexicalized compounds retain their dictionary candidate.
  if (prev.pos == core::PartOfSpeech::Noun && prev.extended_pos != core::ExtendedPOS::NounNumber &&
      !normalize::isNumeralCodepoint(utf8::decodeFirstChar(prev.surface)) && next.pos == core::PartOfSpeech::Suffix &&
      grammar::isStateDurationSuffix(next.surface)) {
    bonus += cost::kMinorBonus;
  }

  // A written honorific title is a separate searchable unit after a multi-kanji
  // nominal stem. This preserves the boundary even when the title also has an
  // unknown noun candidate rather than a dictionary suffix entry.
  if (prev.pos == core::PartOfSpeech::Noun && prev.surface.size() >= 2 * core::kJapaneseCharBytes &&
      grammar::isAllKanji(prev.surface) && next.pos == core::PartOfSpeech::Noun &&
      grammar::isKanjiHonorificTitle(next.surface)) {
    bonus += cost::kMinorBonus;
  }

  // A one-kanji formal noun followed by a one-kanji kanji suffix is normally
  // a lexical compound (時間, 期間), not a productive formal-noun boundary.
  // Leave kana suffixes and multi-character forms unaffected.
  if (prev.extended_pos == core::ExtendedPOS::NounFormal && next.pos == core::PartOfSpeech::Suffix &&
      prev.surface.size() == core::kJapaneseCharBytes && next.surface.size() == core::kJapaneseCharBytes &&
      grammar::isAllKanji(prev.surface) && grammar::isAllKanji(next.surface)) {
    bonus += cost::kStrong;
  }

  // ところで is a discourse conjunction only at a clause boundary. Likewise,
  // ついで following a past form is the formal noun in an incidental-action
  // construction. After a formal noun or a past/copular auxiliary, keep the
  // compositional reading (読んだところで, 読んだついで) over a fused
  // conjunction candidate.
  if (next.extended_pos == core::ExtendedPOS::Conjunction && utf8::equalsAny(next.surface, {"ところで", "ついで"}) &&
      (prev.extended_pos == core::ExtendedPOS::NounFormal || prev.extended_pos == core::ExtendedPOS::AuxTenseTa ||
       prev.extended_pos == core::ExtendedPOS::AuxCopulaDa || prev.extended_pos == core::ExtendedPOS::VerbTaForm ||
       prev.extended_pos == core::ExtendedPOS::ParticleNo)) {
    bonus += cost::kProhibitive;
  }

  // ゆえに is a discourse conjunction at a clause boundary, but after a
  // nominal phrase it is the productive causal phrase ゆえ(形式名詞)+に.
  // A fused conjunction cannot follow a noun, pronoun, or genitive particle.
  if (next.extended_pos == core::ExtendedPOS::Conjunction && utf8::equalsAny(next.surface, {"ゆえに"}) &&
      (prev.pos == core::PartOfSpeech::Noun || prev.extended_pos == core::ExtendedPOS::Pronoun ||
       prev.extended_pos == core::ExtendedPOS::ParticleNo)) {
    bonus += cost::kProhibitive;
  }

  // The formal particle により cannot follow the copula's attributive form
  // な. Reject that accidental path so a following interrogative pronoun can
  // retain the productive comparison boundary (なに+より).
  if ((prev.extended_pos == core::ExtendedPOS::AuxCopulaDa || prev.extended_pos == core::ExtendedPOS::AdjStem) &&
      next.extended_pos == core::ExtendedPOS::ParticleCase && utf8::equalsAny(next.surface, {"により"})) {
    bonus += cost::kProhibitive;
  }

  // The benefactive expression お+かげで uses the instrumental case
  // particle, not the copula. Its formal-noun reading supplies the productive
  // prefix boundary; retain the corresponding particle analysis afterwards.
  if (prev.extended_pos == core::ExtendedPOS::NounFormal && utf8::equalsAny(prev.surface, {"かげ"}) &&
      next.extended_pos == core::ExtendedPOS::ParticleCase && utf8::equalsAny(next.surface, {"で"})) {
    bonus += cost::kStrongBonus;
  }

  // おかげさまで extends the benefactive formal noun with the honorific
  // suffix さま. Prefer that productive suffix to the unrelated sentence
  // particle さ followed by まで.
  if (prev.extended_pos == core::ExtendedPOS::NounFormal && utf8::equalsAny(prev.surface, {"かげ"}) &&
      next.pos == core::PartOfSpeech::Suffix && utf8::equalsAny(next.surface, {"さま"})) {
    bonus += cost::kStrongBonus;
  }

  // Penalty for SUFFIX(さ) + VERB starting with せ/させ pattern
  // E.g., 勉強 + さ(SUFFIX) + せられてい is wrong; should be 勉強 + さ(VERB_未然) + せ(AUX_使役)
  // This pattern indicates suru-verb causative form where さ is the verb stem, not suffix
  if (prev.pos == core::PartOfSpeech::Suffix && utf8::equalsAny(prev.surface, {"さ"}) &&
      next.pos == core::PartOfSpeech::Verb &&
      (utf8::startsWith(next.surface, "せ") || utf8::startsWith(next.surface, "させ"))) {
    bonus += cost::kAlmostNever;
  }

  // Penalty for single-kanji NOUN → single-kanji SUFFIX pattern
  // E.g., 正+式, 手+法, 結+論 are 2-kanji compound words being oversplit
  // because the second kanji is registered as SUFFIX in L1 dictionary.
  // The NOUN→SUFFIX bigram bonus (-0.8) + SUFFIX→PART_格 epos bonus
  // makes the split path cheaper than the compound path.
  // This penalty counteracts that for single-kanji-to-single-kanji transitions,
  // without affecting multi-kanji noun + suffix (e.g., 学生+たち, 科学+的).
  // Exceptions:
  // - 様/氏: handled by +4.0 kanji_seq penalty in unknown.cpp (always split)
  // - 的: removed from kanji_seq penalty; 1-char + 的 stays merged naturally
  //   (目的, 動的, 知的), 2+ char + 的 still splits via bigram bonus (論理+的)
  if (prev.pos == core::PartOfSpeech::Noun && next.pos == core::PartOfSpeech::Suffix &&
      prev.surface.size() == core::kJapaneseCharBytes && next.surface.size() == core::kJapaneseCharBytes &&
      grammar::isAllKanji(prev.surface) && next.surface != "様" && next.surface != "氏") {
    bonus += cost::kRare;  // +1.0 to counteract -0.8 bonus
  }

  // Penalty for 年+度 lexical binding pattern (年度, fiscal year)
  // A kanji noun ending in 年 followed by the lone suffix 度 forms 年度, not a
  // NOUN+degree/frequency-suffix split. Without this, the NOUN→SUFFIX bigram
  // bonus (-0.8) makes 今年|度 cheaper than the whole 今年度.
  if (prev.pos == core::PartOfSpeech::Noun && next.pos == core::PartOfSpeech::Suffix &&
      grammar::isAllKanji(prev.surface) && next.surface.size() == core::kJapaneseCharBytes &&
      grammar::isAllKanji(next.surface)) {
    auto prev_codepoints = normalize::toCodepoints(prev.surface);
    auto next_codepoints = normalize::toCodepoints(next.surface);
    if (prev_codepoints.size() >= 2 && next_codepoints.size() == 1 &&
        normalize::isFiscalYearBindingPair(prev_codepoints.back(), next_codepoints.front())) {
      bonus += cost::kRare;  // +1.0 to neutralize -0.8 bigram bonus
    }
  }

  // Penalty for 3+ char non-dict kanji NOUN → 1-char SUFFIX pattern
  // For non-dict 3+ char kanji NOUN preceding a single-kanji suffix, the 4-char
  // input is often two 2-char kango compounds (新規 + 手法) rather than
  // a 3+1 stem+suffix split (新規手 + 法). Penalize to let the whole-word
  // (or 2+2 split) compete fairly. Dict-verified 3-char NOUNs (e.g., 政治学+者 if
  // 政治学 were in dict) keep the bonus, since they represent intended compounds.
  if (prev.pos == core::PartOfSpeech::Noun && next.pos == core::PartOfSpeech::Suffix && !prev.fromDictionary() &&
      prev.surface.size() >= 3 * core::kJapaneseCharBytes && next.surface.size() == core::kJapaneseCharBytes &&
      grammar::isAllKanji(prev.surface) && grammar::isAllKanji(next.surface)) {
    bonus += cost::kRare;  // +1.0 to neutralize -0.8 bigram bonus
  }

  // Penalty for ADV → でも (CONJ or PART_副) pattern
  // After adverbs, でも should split as で(copula)+も(particle)
  // e.g., それほどでもない → それほど+で+も+ない
  if (prev.pos == core::PartOfSpeech::Adverb && next.surface == "でも" &&
      (next.pos == core::PartOfSpeech::Conjunction || next.extended_pos == core::ExtendedPOS::ParticleAdverbial)) {
    bonus += cost::kAlmostNever;
  }

  // Penalty for predicate → copula-compound conjunction (だから/だけど/だが/…) pattern
  // These conjunctions are the copula だ fused with a particle (から/けど/が). They are
  // valid at a sentence/clause boundary, but after a copula-taking predicate (noun,
  // pronoun, adverb, na-adjective stem, or 様態 そう) they must split as だ(AUX) + PART:
  // 彼女だけど → 彼女+だ+けど, 静かだから → 静か+だ+から, 危なそうだから → 危な+そう+だ+から,
  // 遅刻しがちだが → がち(SUFFIX)+だ+が (nominal suffixes take the copula too).
  // Keyed on the だ onset rather than each surface so the rule generalizes across the set.
  // Other (unknown noun-like blobs) is included so the copula still splits when a
  // nominal suffix is demoted to Other in the presence of the fused conjunction:
  // がち would otherwise drop to Other purely to dodge this penalty (遅刻しがちだが).
  if (next.extended_pos == core::ExtendedPOS::Conjunction && utf8::startsWith(next.surface, "だ") &&
      (prev.pos == core::PartOfSpeech::Noun || prev.pos == core::PartOfSpeech::Pronoun ||
       prev.pos == core::PartOfSpeech::Adverb || prev.pos == core::PartOfSpeech::Adjective ||
       prev.pos == core::PartOfSpeech::Suffix || prev.pos == core::PartOfSpeech::Other ||
       prev.extended_pos == core::ExtendedPOS::AuxAppearanceSou)) {
    bonus += cost::kProhibitive;
  }

  // Note: Removed penalty for PARTICLE と → VERB_音便 いっ pattern
  // The dictionary entry "といった" (determiner) handles that case
  // For といって pattern, we want と+いっ+て split (MeCab compatible)

  // Penalty for VerbRenyokei → single-char char_speech AUX pattern
  // E.g., 食べろ should be 食べろ (imperative), not 食べ+ろ
  // The ろ is the ichidan imperative ending, not a character speech suffix
  // Character speech suffixes like ろ are valid after だ/です (だろ, でしょ)
  // but not after verb renyokei
  // Valid patterns after VerbRenyokei: た, て, ます, etc. (multi-char or dictionary)
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && next.pos == core::PartOfSpeech::Auxiliary &&
      !next.fromDictionary() && next.surface.size() <= core::kJapaneseCharBytes) {  // Single char (3 bytes)
    bonus += cost::kUncommon;
  }

  // Penalty for single-char hiragana VerbRenyokei → AuxPassive/AuxCausative
  // Bigram table gives bonus for VerbRenyokei→AuxPassive (for 知らせ+られ)
  // But single-char hiragana like せ+られ should prefer AuxCausative+AuxPassive path
  // Valid patterns like 知らせ+られ have longer surfaces (2+ chars)
  // This prevents せ(VERB連用) from being selected over せ(AuxCausative)
  // Exception: い+られ is valid (いる potential: いられない = cannot stay)
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      (next.extended_pos == core::ExtendedPOS::AuxPassive || next.extended_pos == core::ExtendedPOS::AuxCausative) &&
      prev.surface.size() <= 3 &&                                       // Single hiragana (3 bytes)
      grammar::isPureHiragana(prev.surface) && prev.surface != "い") {  // い+られ is valid (いる potential)
    bonus += cost::kAlmostNever;                                        // Strongly discourage
  }

  // Penalty for て/で (ParticleConj) → a single-character lexical verb stem.
  // Subsidiary verbs after a te-form must use their aspect-specific ExtendedPOS
  // (い→AuxAspectIru, み→AuxAspectMiru), not a fabricated lexical verb.
  // Exception: たり/だり → し is valid (食べたり+し+てる)
  if (prev.extended_pos == core::ExtendedPOS::ParticleConj && next.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      next.surface.size() <= 3 &&  // Single hiragana (3 bytes)
      grammar::isPureHiragana(next.surface) && prev.surface != "たり" && prev.surface != "だり") {
    bonus += cost::kAlmostNever;  // Strongly discourage
  }

  return bonus;
}

float computeParticleQuoteBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float bonus{};

  // The comparative case particle follows an adverbial reference point
  // (かねて+より, 以前+より). Keep this relation ahead of its homographic
  // continuative verb without changing other adverb-to-case boundaries.
  if (prev.pos == core::PartOfSpeech::Adverb && next.extended_pos == core::ExtendedPOS::ParticleCase &&
      utf8::equalsAny(next.surface, {"より"})) {
    bonus += cost::kStrongBonus;
  }

  // A final particle can be quoted as a complete utterance (かしら+と
  // 思う, かな+と考える). This relation is specific to the quotative case
  // particle; applying it to every case particle incorrectly favors paths
  // such as ADV+わ+から over an ordinary following predicate.
  if (prev.extended_pos == core::ExtendedPOS::ParticleFinal && next.extended_pos == core::ExtendedPOS::ParticleCase &&
      utf8::equalsAny(next.surface, {"と"})) {
    bonus += cost::kStrongBonus;
  }

  // A small closed set of sentence-final particles stacks productively
  // (か+な, よ+ね, わ+ね, ぜ+よ). Restore these grammatical sequences against
  // the general final-particle-to-final-particle penalty.
  if (prev.extended_pos == core::ExtendedPOS::ParticleFinal && next.extended_pos == core::ExtendedPOS::ParticleFinal &&
      grammar::isFinalParticleStack(prev.surface, next.surface)) {
    bonus += cost::kExtremeBonus;
  }

  return bonus;
}

float computeCompoundNominalizationBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  // A verified compound in renyokei can be nominalized by の (書きかけの紙,
  // 書きたての文). Its single-word candidate must remain available against a
  // competing decomposition into a verb stem and suffix.
  if (prev.origin == core::CandidateOrigin::VerbCompound && prev.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      next.extended_pos == core::ExtendedPOS::ParticleNo && utf8::equalsAny(next.surface, {"の"})) {
    return cost::kTripleVeryStrongBonus + cost::kModerateBonus;
  }
  return cost::kNeutral;
}

// Progressive/contracted て, dialectal やで, 付け-で formal noun, honorific
// renyokei (いたし/いただき), and い/た/だ auxiliary attachment rules.
float computeProgressiveHonorificBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float bonus{};  // value-init to 0

  // The continuative construction V連用形+つつ+ある uses the existential verb,
  // not the homographic copula or determiner. The same finite verb remains
  // correct before an attributive noun (読みつつある本).
  if (prev.extended_pos == core::ExtendedPOS::ParticleConj && utf8::equalsAny(prev.surface, {"つつ"}) &&
      next.extended_pos == core::ExtendedPOS::VerbShuushikei && next.lemma == "ある") {
    bonus += cost::kVeryStrongBonus;
  }

  // A conjunctive particle cannot host the contracted negative auxiliary.
  // This preserves the productive hatsuonbin verb path (つつん+で) while
  // leaving classical negative ず after a conjunction (のみなら+ず) intact.
  if (prev.extended_pos == core::ExtendedPOS::ParticleConj && next.extended_pos == core::ExtendedPOS::AuxNegativeNu &&
      grammar::isContractedNegativeAuxiliaryLemma(next.lemma)) {
    bonus += cost::kAlmostNever;
  }

  // The conjunctive negative ずに is a single closed auxiliary form. Its
  // registered edge must outrank the otherwise valid classical-negative plus
  // case-particle sequence, while other case-marked forms (ざるを, ぬを)
  // retain their ordinary boundary.
  if (prev.extended_pos == core::ExtendedPOS::AuxNegativeNu && utf8::equalsAny(prev.surface, {"ず"}) &&
      next.extended_pos == core::ExtendedPOS::ParticleCase && utf8::equalsAny(next.surface, {"に"})) {
    bonus += cost::kStrong;
  }

  // A nominal phrase followed by の+ある uses the existential verb in
  // attributive form (意味のある文), not the homographic copula or determiner.
  // Keep the condition on the preceding nominalizer so ordinary ある本 remains
  // a determiner.
  if (prev.extended_pos == core::ExtendedPOS::ParticleNo && next.extended_pos == core::ExtendedPOS::VerbShuushikei &&
      next.lemma == "ある") {
    bonus += cost::kDoubleVeryStrongBonus;
  }

  // The nominalizer の can introduce conditional particles such as なら and
  // で, but it cannot be followed by the conjunctive particle し or the
  // renyokei of する. This guards against a fabricated copula-plus-nominalizer
  // sequence hiding a registered closed auxiliary.
  if (prev.extended_pos == core::ExtendedPOS::ParticleNo &&
      ((next.extended_pos == core::ExtendedPOS::ParticleConj && grammar::isConjunctiveParticleShi(next.surface)) ||
       (next.extended_pos == core::ExtendedPOS::VerbRenyokei && grammar::isSuruRenyokeiSurface(next.surface)))) {
    bonus += cost::kStrong;
  }

  // The honorific potential construction Noun+に+なれ+ます keeps the
  // potential form of なる intact. Without this connection, the homographic
  // adjective stem plus passive auxiliary path can win before polite ます.
  if (prev.extended_pos == core::ExtendedPOS::ParticleCase && grammar::isSingleHiragana(prev.surface, U'に') &&
      next.extended_pos == core::ExtendedPOS::VerbKateikei && next.lemma == "なる") {
    bonus += cost::kStrongBonus;
  }

  // A dictionary-backed godan renyokei of the progressive subsidiary after
  // connective て/で should beat a homographic unknown lexical verb. Restrict
  // this to a true renyokei (e.g. おり from おる) so fused inflected forms such
  // as います and いない retain their grammatical internal split.
  if (prev.extended_pos == core::ExtendedPOS::ParticleConj && grammar::isTeDeSurface(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::AuxAspectIru && next.fromDictionary() &&
      isGodanRenyokeiOfLemma(next.surface, next.lemma)) {
    bonus += cost::kVeryStrongBonus;
  }

  // Demonstrative そう is a na-adjective before the polite copula.  The
  // dedicated connection keeps そう+です intact instead of taking the
  // homographic adverb plus で+す path.
  if (prev.extended_pos == core::ExtendedPOS::AdjNaAdj && prev.lemma == "そう" &&
      next.extended_pos == core::ExtendedPOS::AuxCopulaDesu) {
    bonus += cost::kDoubleVeryStrongBonus;
  }

  // みたい immediately after a connective て/で is not the conjecture
  // auxiliary: 〜てみたい consists of the trial subsidiary み + desiderative
  // たい. This applies whether the competing connective edge was classified as a
  // particle or as a contracted aspect auxiliary.
  if (grammar::isTeDeSurface(prev.surface) && next.extended_pos == core::ExtendedPOS::AuxConjectureMitai) {
    bonus += cost::kAlmostNever;
  }

  // Progressive で+い+ます should use the auxiliary い, not the standalone verb いる.
  // The preceding で can be tagged as either a conjunction particle or a te-form
  // particle depending on the onbin path, so match by surface here.
  if (prev.surface == "で" && next.surface == "い" && next.extended_pos == core::ExtendedPOS::VerbRenyokei) {
    bonus += cost::kAlmostNever;
  }

  // Excessive-degree すぎ + て is ordinary connective て. Do not reinterpret it
  // as contracted progressive てる.
  if (prev.extended_pos == core::ExtendedPOS::AuxExcessive && next.surface == "て" &&
      next.extended_pos == core::ExtendedPOS::AuxAspectIru) {
    bonus += cost::kAlmostNever;
  }

  // The excessive subsidiary inflects before the past auxiliary. Restrict the
  // rule to the actual past surface so a homographic character-speech edge for
  // て cannot be selected as a past form.
  if (prev.extended_pos == core::ExtendedPOS::AuxExcessive && next.extended_pos == core::ExtendedPOS::AuxTenseTa &&
      next.surface == "た") {
    bonus += cost::kVeryStrongBonus;
  }

  // If contracted-progressive て is followed by ordinary particles or content
  // words, prefer the connective particle て instead.
  if (prev.surface == "て" && prev.extended_pos == core::ExtendedPOS::AuxAspectIru &&
      (next.pos == core::PartOfSpeech::Particle || next.pos == core::PartOfSpeech::Noun ||
       next.pos == core::PartOfSpeech::Pronoun || next.pos == core::PartOfSpeech::Determiner ||
       next.pos == core::PartOfSpeech::Adverb || next.pos == core::PartOfSpeech::Conjunction ||
       next.pos == core::PartOfSpeech::Verb)) {
    bonus += cost::kAlmostNever;
  }

  // The colloquial progressive contraction permits the nominalizer directly
  // after its contracted て/で form (食べ+て+ん+の, 読ん+で+ん+の). This is
  // distinct from an independent progressive auxiliary such as いる or い,
  // whose nominalization must not absorb a following clause boundary.
  if (prev.extended_pos == core::ExtendedPOS::AuxAspectIru && grammar::isTeDeSurface(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::ParticleNo) {
    bonus += cost::kDoubleVeryStrongBonus;
  }

  // The ん in a contracted progressive (〜てんだ) is the nominalizer の, not
  // the negative auxiliary. Exclude only that impossible immediate auxiliary
  // attachment, leaving genuine negative forms after mizenkei untouched.
  if (prev.extended_pos == core::ExtendedPOS::AuxAspectIru && grammar::isTeDeSurface(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::AuxNegativeNu &&
      grammar::isContractedNegativeAuxiliaryLemma(next.lemma)) {
    bonus += cost::kAlmostNever;
  }

  // The regional ておる/でおる contractions retain Godan-ra inflection, so
  // their conditional form attaches to a conjunctive particle (食べ+とれ+ば).
  // Keep this lemma-scoped to avoid broadly favoring a finite progressive
  // before compound conjunction candidates such as のに.
  const bool is_dialectal_oru_contraction = grammar::isDialectalOruContractionLemma(prev.lemma);
  if ((prev.extended_pos == core::ExtendedPOS::AuxAspectIru || prev.extended_pos == core::ExtendedPOS::VerbKateikei) &&
      is_dialectal_oru_contraction && next.extended_pos == core::ExtendedPOS::ParticleConj) {
    bonus += cost::kStrongBonus;
  }

  // The negative dialectal contraction uses the mizenkei plus ん
  // (食べ+とら+ん). Prefer the negative auxiliary over the homographic
  // nominalizer only for the contracted おる paradigm.
  if (prev.extended_pos == core::ExtendedPOS::AuxAspectIru && is_dialectal_oru_contraction &&
      next.extended_pos == core::ExtendedPOS::AuxNegativeNu) {
    bonus += cost::kVeryStrongBonus;
  }

  // Dialectal/character-speech やで is particle + particle in the regression
  // corpus, not copula で.
  if (prev.surface == "や" && next.surface == "で" && next.extended_pos == core::ExtendedPOS::AuxCopulaDa) {
    bonus += cost::kAlmostNever;
  }

  // The attributive copula な cannot introduce the progressive/aspectual いる.
  // This rules out the fabricated な+い+ん+だ chain and leaves the independent
  // adjective plus nominalizer in ないんだ.
  if (prev.extended_pos == core::ExtendedPOS::AuxCopulaDa && grammar::isAttributiveCopulaNa(prev.surface)) {
    if (next.extended_pos == core::ExtendedPOS::AuxAspectIru || next.extended_pos == core::ExtendedPOS::AuxNegativeNu ||
        next.pos == core::PartOfSpeech::Verb) {
      bonus += cost::kAlmostNever;
    }
  }

  // The independent adjective ない is commonly nominalized before a copula
  // (ない+ん+だ). Favor that complete adjective phrase over the unrelated
  // negative-auxiliary homograph.
  if (prev.extended_pos == core::ExtendedPOS::AdjBasic && grammar::isIndependentNegativeAdjective(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::ParticleNo) {
    bonus += cost::kStrongBonus;
  }

  // The complete negative ない cannot itself take another negative ん. This
  // applies regardless of the provisional POS chosen for the homograph and
  // retains the nominalizer analysis in ないんだ.
  if (grammar::isIndependentNegativeAdjective(prev.surface) && next.extended_pos == core::ExtendedPOS::AuxNegativeNu) {
    bonus += cost::kAlmostNever;
  }

  // An onbin candidate that already includes the complete negative ない is
  // not an irrealis form for another negative ん. This removes only the
  // whole-verb competitor in 知らないんだ, not ordinary onbin inflections.
  if (prev.extended_pos == core::ExtendedPOS::VerbOnbinkei && grammar::endsWithNegativeNai(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::AuxNegativeNu) {
    bonus += cost::kAlmostNever;
  }

  // である is the formal copula sequence. A case/conjunctive で followed by
  // the determiner ある is not a grammatical alternative, so preserve the
  // copula candidate before formal nouns such as 以上 and 場合.
  if (utf8::equalsAny(prev.surface, {"で"}) && utf8::equalsAny(next.surface, {"ある"}) &&
      next.extended_pos == core::ExtendedPOS::Determiner) {
    bonus += cost::kAlmostNever;
  }

  // In the copular negative でなく, なく is the adverbial adjective form of
  // ない. Keep the auxiliary reading for verbal 〜なく separate.
  if (utf8::equalsAny(prev.surface, {"で"}) && utf8::equalsAny(next.surface, {"なく"}) &&
      next.extended_pos == core::ExtendedPOS::AuxNegativeNai) {
    bonus += cost::kAlmostNever;
  }

  // Date + 付け + で is a formal-noun construction ("as of ..."), not the
  // verb 付ける in renyokei.
  if (prev.pos == core::PartOfSpeech::Noun && next.surface == "付け" &&
      next.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      (utf8::endsWith(prev.surface, "日") || utf8::endsWith(prev.surface, "月") ||
       utf8::endsWith(prev.surface, "年"))) {
    bonus += cost::kAlmostNever;
  }

  // Keep the common na-adjective 複雑 together; the split 複 + 雑い is a false
  // i-adjective path.
  if (prev.surface == "複" && next.surface == "雑" && next.pos == core::PartOfSpeech::Adjective) {
    bonus += cost::kAlmostNever;
  }

  // Penalty for VerbOnbinkei/VerbRenyokei ending in いい → AuxTenseTa pattern
  // E.g., 願いい+た should be 願い+いたし+ます, not 願いい (願いく) + た
  // Valid forms are: 書い, 泳い, etc. (single い after kanji)
  // Invalid: 願いい (連用形い + さらにい) - this suggests wrong verb base
  // Include VerbRenyokei since 願いい is sometimes assigned as renyokei of 願いう
  if ((prev.extended_pos == core::ExtendedPOS::VerbOnbinkei || prev.extended_pos == core::ExtendedPOS::VerbRenyokei) &&
      next.extended_pos == core::ExtendedPOS::AuxTenseTa && utf8::endsWith(prev.surface, "いい")) {
    bonus += cost::kAlmostNever;
  }

  // Penalty for ParticleAdverbial → single-mora hiragana VerbRenyokei.
  // A subsidiary/adverbial particle (しか, だけ, ばかり…) directly followed by a
  // one-mora hiragana renyokei is a fabricated over-split: the particle path
  // severs a real verb compound (し+かね) and reads the trailing mora as a bare
  // ichidan renyokei (ね ← 寝る). Genuine ParticleAdverbial→VerbRenyokei
  // sequences (かも+しれ, など+あり, でも+あり) always carry a ≥2-mora renyokei
  // stem, and a one-kanji renyokei (だけ+寝) keeps its self-standing stem, so
  // gating on a single hiragana character leaves those untouched while
  // countering the ParticleAdverbial→VerbRenyokei bonus that the false path
  // would otherwise receive.
  if (prev.extended_pos == core::ExtendedPOS::ParticleAdverbial &&
      next.extended_pos == core::ExtendedPOS::VerbRenyokei && next.surface.size() == core::kJapaneseCharBytes &&
      normalize::classifyChar(utf8::decodeFirstChar(next.surface)) == normalize::CharType::Hiragana) {
    bonus += cost::kStrong;
  }

  // A final particle followed immediately by a one-mora hiragana renyokei
  // is likewise an implausible no-boundary split. It fabricates chains such
  // as 扱い+か+ね+た from an adjective/question fragment, instead of preserving
  // the preceding verb-renyokei plus its subsidiary. Real sentence-final
  // questions end here; a following lexical verb begins a new clause and is
  // normally separated by punctuation or has a multi-mora stem.
  if (prev.extended_pos == core::ExtendedPOS::ParticleFinal && next.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      next.surface.size() == core::kJapaneseCharBytes &&
      normalize::classifyChar(utf8::decodeFirstChar(next.surface)) == normalize::CharType::Hiragana) {
    bonus += cost::kStrong;
  }

  // Bonus for VerbRenyokei/VerbOnbinkei → VerbRenyokei (subsidiary verb patterns)
  // E.g., 願い+いたし (お願いいたします), 報告+いたし (ご報告いたします),
  //       し+かね (賛成しかねます), 沿い+かね (ご期待に沿いかねます)
  // Include VerbOnbinkei since 願い is often recognized as onbin form of 願う
  if ((prev.extended_pos == core::ExtendedPOS::VerbRenyokei || prev.extended_pos == core::ExtendedPOS::VerbOnbinkei) &&
      next.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      (grammar::isSubsidiaryHonorificRenyokei(next.surface) || grammar::isModalSubsidiaryRenyokei(next.surface))) {
    bonus += cost::kVeryStrongBonus;
  }

  // A humble auxiliary in renyokei productively takes the connective て
  // (いたし+ております). This preserves the lexical honorific verb against a
  // coincidental auxiliary sequence at the beginning of a sentence.
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && grammar::isHumbleHonorificRenyokei(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::ParticleConj && utf8::equalsAny(next.surface, {"て"})) {
    bonus += cost::kModerateBonus;
  }

  // Bonus for honorific verb renyokei → AuxTenseMasu (ます)
  // E.g., いただき+ます (いただきます), いたし+ます (いたします)
  // This helps いただき beat い+た+だき pattern
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && next.extended_pos == core::ExtendedPOS::AuxTenseMasu &&
      grammar::isHumbleHonorificRenyokei(prev.surface)) {
    bonus += cost::kVeryStrongBonus;
  }

  // Penalty for single い → AuxTenseTa pattern (いただきます problem)
  // い+た+だき should lose to いただき+ます
  // But て+い+た is valid (食べていた)
  // We penalize い→た only when prev is OTHER (sentence start) or NOUN
  // NOT when prev comes from て-form (VerbTeForm)
  if (prev.surface == "い" && prev.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      next.extended_pos == core::ExtendedPOS::AuxTenseTa) {
    bonus += cost::kVeryRare;
  }

  // Penalty for AuxTenseTa → い pattern (たい over-split prevention)
  // 行きたい should be 行き+たい, not 行き+た+い
  // た (AuxTenseTa) should not be followed by standalone い
  // This fixes the issue where VerbRenyokei→た bonus (-1.6) beats たい (-0.8)
  if (prev.extended_pos == core::ExtendedPOS::AuxTenseTa && next.surface == "い") {
    bonus += cost::kAlmostNever;
  }

  // Penalty for だ(AuxTenseTa) after non-ん/non-い words
  // だ as past tense follows ん-onbin (読んだ, 飲んだ) or い-onbin (泳いだ, 注いだ)
  // Without this, てる+だ(past) beats てる+だけ(adverbial particle)
  // because AuxAspectIru→AuxTenseTa bonus applies to both た and だ
  if (next.surface == "だ" && next.extended_pos == core::ExtendedPOS::AuxTenseTa &&
      !utf8::endsWith(prev.surface, "ん") && !utf8::endsWith(prev.surface, "い")) {
    bonus += cost::kAlmostNever;
  }

  return bonus;
}

// Contracted negative past (かっ), progressive でる, できる, て→いた, ござい,
// すぎ intensifier attachment, and final-particle guards.
float computeSugiFinalParticleBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float bonus{};  // value-init to 0

  // A generated renyokei ending in ず is a fused classical-negative path.
  // Before an independent finite predicate, the productive analysis is
  // mizenkei + ず + predicate (種類を問わ|ず|進む), not a fabricated verb
  // connection. Dictionary words retain their lexical reading.
  if (!prev.fromDictionary() && prev.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      utf8::endsWith(prev.surface, "ず") && next.extended_pos == core::ExtendedPOS::VerbShuushikei) {
    bonus += cost::kAlmostNever;
  }

  // A dictionary noun that carries a particle-like extended POS is a surface
  // homograph, not a grammatical binding particle. Do not let it replace the
  // topic-particle boundary in nominal predicates such as 本|は|ない.
  if (prev.pos == core::PartOfSpeech::Noun && next.pos == core::PartOfSpeech::Noun &&
      next.extended_pos == core::ExtendedPOS::ParticleBinding) {
    bonus += cost::kStrong;
  }

  // A contracted negative ん cannot be followed by an independent かっ verb.
  // The colloquial past is represented by the closed auxiliary んかっ, so
  // reject the fabricated ん + かっ verb chain.
  if (prev.extended_pos == core::ExtendedPOS::AuxNegativeNu && next.extended_pos == core::ExtendedPOS::VerbOnbinkei &&
      next.surface == "かっ") {
    bonus += cost::kAlmostNever;
  }

  // A generated verb-onbin candidate whose reconstructed lemma ends in ぬ
  // is a contracted negative (読まん, 書かん), not a lexical onbin form.
  // Before connective で, keep the productive mizenkei + ん + でも chain.
  if (prev.extended_pos == core::ExtendedPOS::VerbOnbinkei && utf8::endsWith(prev.lemma, "ぬ") &&
      utf8::startsWith(next.surface, "で") &&
      (next.extended_pos == core::ExtendedPOS::ParticleConj ||
       next.extended_pos == core::ExtendedPOS::ParticleAdverbial ||
       next.extended_pos == core::ExtendedPOS::Conjunction)) {
    bonus += cost::kStrong;
  }

  // Penalty for VerbOnbinkei(ん) → Verb(でる) pattern
  // After ん音便, でる is almost always the contracted ている, not the verb 出る
  // E.g., 並んでる = 並んでいる (progressive), やんでる = 病んでいる
  // Force the で(PART_接続) + る path instead
  if (prev.extended_pos == core::ExtendedPOS::VerbOnbinkei && utf8::endsWith(prev.surface, "ん") &&
      next.pos == core::PartOfSpeech::Verb && next.surface == "でる") {
    bonus += cost::kStrong;
  }

  // The completion auxiliary after the renyokei homograph of 出る belongs
  // to the voiced te-form chain (読ん+で+しまう), not to a lexical verb.
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && utf8::equalsAny(prev.lemma, {"出る", "でる"}) &&
      next.extended_pos == core::ExtendedPOS::AuxAspectShimau) {
    bonus += cost::kStrong;
  }

  // The voiced progressive contraction follows an n-onbin: 読んでる,
  // 飲んでる. Outside this environment でる retains its lexical-verb reading.
  if (prev.extended_pos == core::ExtendedPOS::VerbOnbinkei && utf8::endsWith(prev.surface, "ん") &&
      next.extended_pos == core::ExtendedPOS::AuxAspectIru && next.surface == "でる") {
    bonus += cost::kStrongBonus;
  }

  // The irregular potential できる can follow either a verbal noun or a
  // compound adverbial particle.  Keep it ahead of an accidental で + きる
  // split while preserving the stronger particle boundary evidence.
  if (next.pos == core::PartOfSpeech::Verb && next.surface == "できる") {
    if (prev.pos == core::PartOfSpeech::Noun) {
      bonus += cost::kModerateBonus;
    } else if (prev.extended_pos == core::ExtendedPOS::ParticleAdverbial) {
      bonus += cost::kStrongBonus;
    }
  }

  // Penalty for PARTICLE て → VerbTaForm いた pattern
  // MeCab splits て+い+た, not て+いた
  // いた as verb た-form should not follow て directly
  if (prev.extended_pos == core::ExtendedPOS::ParticleConj && prev.surface == "て" &&
      next.extended_pos == core::ExtendedPOS::VerbTaForm && next.surface == "いた") {
    bonus += cost::kAlmostNever;
  }

  // Penalty for PREFIX ご → VerbRenyokei ざい pattern
  // E.g., ございます should be ござい+ます, not ご+ざい+ます
  // The prefix ご is for nouns (ご報告), not for splitting ござる
  if (prev.extended_pos == core::ExtendedPOS::Prefix && prev.surface == "ご" &&
      next.extended_pos == core::ExtendedPOS::VerbRenyokei && utf8::startsWith(next.surface, "ざい")) {
    bonus += cost::kAlmostNever;
  }

  // Surface-based bonus for AdjStem → すぎ pattern
  // E.g., 高+すぎる, 美味し+すぎた: adjective stem plus excessive auxiliary.
  // AdjStem→Verb has prohibitive penalty to prevent な+い splits
  // But AdjStem+すぎ is valid grammar (i-adjective stem + すぎる)
  // Exclude VerbTeForm (すぎて) - should split as すぎ+て
  if (prev.extended_pos == core::ExtendedPOS::AdjStem && next.extended_pos != core::ExtendedPOS::VerbTeForm &&
      utf8::startsWith(next.surface, "すぎ")) {
    // Strong bonus to overcome AdjStem→Verb prohibitive penalty
    bonus += sc::kBonusDoubleVeryStrong;
  }

  // Surface-based bonus for AdjNaAdj → すぎ pattern
  // E.g., シンプル+すぎない, 静か+すぎる (na-adjective + sugiru)
  // NOUN→VERB_連用 has bonus from bigram table, which can beat ADJ_NA path
  // This helps dictionary ADJ_NA entries beat unknown NOUN candidates
  if (prev.extended_pos == core::ExtendedPOS::AdjNaAdj && utf8::startsWith(next.surface, "すぎ")) {
    bonus += cost::kStrongBonus;
  }

  // Surface-based bonus for all-kanji NOUN → すぎ pattern
  // E.g., 最高+すぎ, 贅沢+すぎ, 美人+すぎ (kanji compound + sugiru "too much")
  // Without this, multi-kanji nouns get split: 最高→最+高(ADJ_語幹)+すぎ
  // because ADJ_語幹→すぎ has a very strong surface bonus (-3.2)
  // Only apply to all-kanji surfaces (not katakana/verb renyokei)
  if (prev.pos == core::PartOfSpeech::Noun && prev.surface.size() >= 6 &&  // 2+ chars (6+ bytes)
      grammar::isAllKanji(prev.surface) && utf8::startsWith(next.surface, "すぎ")) {
    bonus += sc::kBonusDoubleVeryStrong;
  }

  // A sokuonbin copula followed by たら uses the hypothetical form of the
  // past auxiliary: 静か+だっ+たら. The homographic conjunctive particle
  // cannot attach directly to the copula's だっ form.
  if (prev.extended_pos == core::ExtendedPOS::AuxCopulaDa && utf8::endsWith(prev.surface, "っ") &&
      next.extended_pos == core::ExtendedPOS::ParticleConj && utf8::equalsAny(next.surface, {"たら"})) {
    bonus += cost::kVeryStrongBonus;
  }

  // Penalty for AuxCopulaDa(だ/な) → ParticleFinal(ったら) pattern.
  // The final particle is valid after a noun (あなた+ったら), but after the
  // copula these surfaces belong to a different inflectional boundary:
  // だっ+たら (copula conditional) or なっ+たら (なる conditional).
  if (prev.extended_pos == core::ExtendedPOS::AuxCopulaDa && utf8::equalsAny(prev.surface, {"だ", "な"}) &&
      next.extended_pos == core::ExtendedPOS::ParticleFinal && utf8::startsWith(next.surface, "った")) {
    bonus += cost::kStrong;
  }

  // Penalty for ParticleFinal → VerbRenyokei pattern
  // E.g., いいよね should be いい+よ+ね(PART), not いい+よ+ね(VERB 寝る)
  // Final particles (よ, な, ね, わ) are rarely followed by verb renyokei
  // The short hiragana verb ね (寝る renyokei) competes with final particle ね
  // This penalty ensures particle interpretation wins in よね, なね, etc. patterns
  if (prev.extended_pos == core::ExtendedPOS::ParticleFinal && next.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      grammar::isPureHiragana(next.surface) && next.surface.size() <= 3) {  // Single hiragana (3 bytes)
    bonus += cost::kRare;
  }

  // The surface か also marks an indefinite phrase (誰か来る, 何かいる).
  // If a verb actually follows, the edge is internal and therefore cannot be
  // sentence-final. Cancel the generic final-particle-to-verb penalty for this
  // homograph while retaining it for genuine final particles よ/ね/な/わ.
  if (prev.extended_pos == core::ExtendedPOS::ParticleFinal && utf8::equalsAny(prev.surface, {"か"}) &&
      next.pos == core::PartOfSpeech::Verb && next.fromDictionary() &&
      next.extended_pos != core::ExtendedPOS::VerbMizenkei) {
    bonus += cost::kVeryStrongBonus;
  }

  // Penalty for pure-hiragana Conjunction → bare single-hiragana non-particle
  // A conjunction is a complete word; a following lone hiragana verb/aux/unknown
  // is never a natural continuation. When the conjunction surface is a proper
  // prefix of a longer i-adjective, this is the fragment path that must lose:
  // ただしい → ただし(CONJ)+い must lose to the ただしい adjective.
  // Particles are exempt: they legitimately form compound conjunctions
  // (されど+も, だけど+も).
  if (prev.pos == core::PartOfSpeech::Conjunction && grammar::isPureHiragana(prev.surface) &&
      next.pos != core::PartOfSpeech::Particle && grammar::isPureHiragana(next.surface) &&
      next.surface.size() <= 3) {  // Single hiragana (3 bytes)
    bonus += cost::kNever;
  }

  return bonus;
}

/// Penalty for a bare single-character potential auxiliary (え/得, renyokei of
/// える/得る) followed by anything other than a continuation morpheme.
/// The renyokei form only occurs in chains like あり+え+ない / 解決し+得+ない /
/// あり+え+て, so a following noun/verb/symbol means the え is a fragment of a
/// longer word (いいえ → いい+え, ねえ → ね+え). The multi-character
/// shuushikei える/うる legitimately ends a clause and is exempt.
float computeBarePotentialRenyokeiPenalty(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float penalty{};
  if (prev.extended_pos == core::ExtendedPOS::AuxPotential && prev.surface.size() <= 3 &&  // Single character (3 bytes)
      next.pos != core::PartOfSpeech::Auxiliary && next.pos != core::PartOfSpeech::Particle &&
      next.pos != core::PartOfSpeech::Suffix) {
    penalty += cost::kSevere;
  }
  return penalty;
}

float computeCopulaConditionalBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  // The literary concessive/conditional construction であれ(ば) is the
  // continuative copula followed by the hypothetical form of ある. Favor this
  // grammatical chain over the homographic case-particle + pronoun sequence.
  if (prev.extended_pos == core::ExtendedPOS::AuxCopulaDa && next.extended_pos == core::ExtendedPOS::VerbKateikei &&
      utf8::equalsAny(next.surface, {"あれ"}) && utf8::equalsAny(next.lemma, {"ある"})) {
    return cost::kVeryStrongBonus;
  }
  // A copula cannot directly take an unrelated lexical hypothetical form.
  // This also keeps the known で+あれ+ば chain split rather than selecting a
  // fabricated one-token verb candidate for あれば.
  if (prev.extended_pos == core::ExtendedPOS::AuxCopulaDa && next.extended_pos == core::ExtendedPOS::VerbKateikei) {
    return cost::kStrong;
  }
  // In であれ, the hypothetical ある can coordinate another nominal
  // predicate (本であれ水であれ) or introduce the following predicate
  // (本であれ読む). These continuations distinguish it from the pronoun あれ.
  if (prev.extended_pos == core::ExtendedPOS::VerbKateikei && prev.lemma == "ある" &&
      (next.pos == core::PartOfSpeech::Noun || next.extended_pos == core::ExtendedPOS::VerbShuushikei)) {
    return cost::kStrongBonus;
  }
  return {};
}

float computePastConditionalVerbBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  // The sahen renyokei takes the past conditional directly in 〜としたら.
  // This is a true inflectional chain, unlike a general renyokei followed by
  // a past auxiliary, and keeps たら as AUX rather than a conjunction.
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && prev.lemma == "する" &&
      next.extended_pos == core::ExtendedPOS::AuxTenseTa && utf8::equalsAny(next.surface, {"たら"})) {
    return cost::kVeryStrongBonus;
  }

  // The conditional forms of the past auxiliary introduce a following main
  // predicate (読ん+たら+進む, 読ま+せ+ん+でし+たら+進む). They are unlike a
  // completed-past た, which must not be followed by a bare verb.
  if (prev.extended_pos == core::ExtendedPOS::AuxTenseTa && utf8::equalsAny(prev.surface, {"たら", "だら"}) &&
      next.extended_pos == core::ExtendedPOS::VerbShuushikei) {
    return cost::kDoubleVeryStrongBonus;
  }
  return {};
}

float computeExistentialAruNominalPredicateBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  // A bare noun can directly predicate the existential verb in literary and
  // fixed constructions (本あってこそ, 本あれば, 本ある限り). This is distinct
  // from the copular である sequence, whose preceding morpheme is auxiliary.
  if (prev.extended_pos == core::ExtendedPOS::Noun && next.pos == core::PartOfSpeech::Verb && next.fromDictionary() &&
      next.lemma == "ある") {
    // The generic noun-to-short-verb guards intentionally resist an unmarked
    // object+verb split. A dictionary-backed existential form is the
    // grammatical exception, so cancel those guards only for this paradigm.
    return cost::kDoubleVeryStrongBonus + cost::kStrongBonus;
  }
  return {};
}

float computeCompletionAuxiliaryBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  // A lexical renyokei can take the completion subsidiary 終わる in its
  // irrealis form before negation (読み+終わら+ない). This is distinct from
  // an arbitrary renyokei-to-verb sequence, which remains discouraged.
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && next.extended_pos == core::ExtendedPOS::VerbMizenkei &&
      utf8::equalsAny(next.lemma, {"終わる"})) {
    return cost::kDoubleVeryStrongBonus;
  }
  return {};
}

float computeAdjectiveTePredicatePenalty(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  // An i-adjective candidate ending in て/で cannot directly govern a new
  // lexical predicate. The final mora is the connective particle and must be
  // separated (嬉しく|て|なら|ない, 高く|て|なる). Auxiliary continuations are
  // represented by their own extended POS and are intentionally unaffected.
  if (prev.extended_pos == core::ExtendedPOS::AdjRenyokei &&
      (utf8::endsWith(prev.surface, "て") || utf8::endsWith(prev.surface, "で")) &&
      (next.extended_pos == core::ExtendedPOS::VerbMizenkei || next.extended_pos == core::ExtendedPOS::ParticleConj)) {
    return cost::kAlmostNever;
  }
  return {};
}

float computeClassicalNegativeBoundaryPenalty(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  // A renyokei candidate ending in ぬ before a formal noun is a false fused
  // negative. The productive analysis is mizenkei + ぬ + formal noun.
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && utf8::endsWith(prev.surface, "ぬ") &&
      next.extended_pos == core::ExtendedPOS::NounFormal) {
    return cost::kStrong;
  }
  return {};
}

}  // namespace suzume::analysis::connection_rules
