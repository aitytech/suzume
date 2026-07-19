#include <cmath>

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

// Surface-based connection rules, extracted from connectionCost for readability.
// Each helper accumulates the `surface_bonus +=` contributions of a thematically
// related group of rules and returns their sum. Helpers are self-contained: they
// recompute any needed locals from prev/next and never read caller state. Because
// every contribution is additive, the order among these helpers does not affect the
// total; call sites are kept at their original positions for readability.

// Passive/humble attachment, youon boundary, plural ら, らし→い, and suru-verb
// causative/mizenkei/imperative (させ, さ, せよ/しろ) rules.
float computePassiveCausativeBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float bonus{};  // value-init to 0

  // A quotative と can introduce the continuative stem of an Ichidan verb
  // immediately before passive/potential られる (本と+み+られる). This
  // origin is emitted only by the productive Ichidan-plus-passive candidate
  // generator, so it does not weaken the protected と+いう determiner path.
  if (prev.extended_pos == core::ExtendedPOS::ParticleCase &&
      grammar::isSingleHiragana(prev.surface, core::hiragana::kTo) &&
      next.origin == core::CandidateOrigin::VerbHiraganaPassiveRenyokei) {
    bonus += cost::kDoubleVeryStrongBonus + cost::kVeryStrongBonus;
  }

  // The same generated stem must connect to passive られる rather than be
  // treated as a standalone verb after the quotation.
  if (prev.origin == core::CandidateOrigin::VerbHiraganaPassiveRenyokei &&
      next.extended_pos == core::ExtendedPOS::AuxPassive) {
    bonus += cost::kDoubleVeryStrongBonus + cost::kVeryStrongBonus;
  }

  // The カ変未然形 来ら takes the potential/passive auxiliary directly
  // (来ら+れる). Its full lexical form competes with this decomposition, so
  // preserve the inflectional boundary once the irregular lemma is known.
  if (prev.extended_pos == core::ExtendedPOS::VerbMizenkei && prev.lemma == "来る" &&
      next.extended_pos == core::ExtendedPOS::AuxPassive && utf8::equalsAny(next.surface, {"れる"})) {
    bonus += cost::kDoubleVeryStrongBonus;
  }

  // A verb mizenkei connects to inflectional auxiliaries (ない, れる, せる,
  // よう), not directly to an adjective. Without this grammatical guard,
  // passive + desire chains can fabricate an i-adjective spanning both
  // auxiliaries: 行か+れたく instead of 行か+れ+たく.
  if (prev.extended_pos == core::ExtendedPOS::VerbMizenkei && next.pos == core::PartOfSpeech::Adjective) {
    bonus += cost::kAlmostNever;
  }

  // The irrealis of する is さ only before a voice auxiliary (さ+せる), not
  // before a connective particle. This rejects fabricated さ+て paths while
  // leaving other irrealis + ば patterns and productive causatives intact.
  if (prev.extended_pos == core::ExtendedPOS::VerbMizenkei && prev.lemma == "する" &&
      (next.extended_pos == core::ExtendedPOS::ParticleConj || next.extended_pos == core::ExtendedPOS::AuxAspectIru)) {
    bonus += cost::kAlmostNever;
  }

  // The classical causative す attaches to an a-row irrealis stem
  // (いら+し+て). A non-a-row homograph cannot supply that inflectional
  // context, so it must not create a fabricated voice chain such as
  // かも+し+れ+ない.
  if (prev.extended_pos == core::ExtendedPOS::VerbMizenkei && next.extended_pos == core::ExtendedPOS::AuxCausative &&
      grammar::isClassicalCausativeAuxiliaryLemma(next.lemma) && !grammar::endsWithARow(prev.surface)) {
    bonus += cost::kAlmostNever;
  }

  // A causative auxiliary may be followed by another auxiliary (notably the
  // passive in 書か+せ+られる), but it does not connect directly to a lexical
  // verb. Penalize the homographic unknown-verb path without naming a surface.
  if (prev.extended_pos == core::ExtendedPOS::AuxCausative && next.pos == core::PartOfSpeech::Verb &&
      !grammar::isHumbleHonorificLemma(next.lemma)) {
    bonus += cost::kRare;
  }

  // The conditional form of a causative auxiliary attaches directly to ば:
  // 読ま+せれ+ば, 食べ+させれ+ば. Prefer the inflected auxiliary over a
  // spurious causative-plus-passive chain (せ+れ+ば).
  if (prev.extended_pos == core::ExtendedPOS::AuxCausative && utf8::endsWith(prev.surface, "れ") &&
      next.extended_pos == core::ExtendedPOS::ParticleConj && utf8::endsWith(next.surface, "ば")) {
    bonus += cost::kVeryStrongBonus + cost::kModerateBonus;
  }

  // A causative auxiliary retains its boundary before the actual past marker
  // (読ま+せ+た). Restrict the bonus to た/だ so homographic colloquial
  // auxiliaries tagged AuxTenseTa do not turn し+てる into a causative chain.
  if (prev.extended_pos == core::ExtendedPOS::AuxCausative && next.extended_pos == core::ExtendedPOS::AuxTenseTa &&
      grammar::isPastMarkerTaDaSurface(next.surface)) {
    bonus += cost::kStrongBonus;
  }

  // The conditional form of a passive auxiliary also attaches directly to
  // ば (書か+れれ+ば, 食べ+られれ+ば). Its stem ends in れれ, which
  // distinguishes this inflection from the ordinary passive continuative.
  if (prev.extended_pos == core::ExtendedPOS::AuxPassive && utf8::endsWith(prev.surface, "れれ") &&
      next.extended_pos == core::ExtendedPOS::ParticleConj && utf8::endsWith(next.surface, "ば")) {
    bonus += cost::kStrongBonus + cost::kModerateBonus + cost::kMinorBonus;
  }

  // Penalty for VerbRenyokei → れ (AuxPassive) pattern
  // The passive auxiliary れる attaches to godan 未然形 (VerbMizenkei), never to
  // 連用形; the VerbRenyokei→AuxPassive strong bonus exists for られ (ichidan/
  // kuru passive: 食べ+られ, 来+られ). A bare れ after 連用形 is a false split
  // (来れば → 来+れ+ば instead of 来れ(仮定形)+ば). Hiragana a-row endings are
  // exempt: short hiragana 未然形 carries VerbRenyokei EPOS (やら+れ+た).
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && next.extended_pos == core::ExtendedPOS::AuxPassive &&
      next.surface == "れ" && !grammar::endsWithARow(prev.surface)) {
    bonus += cost::kRare;  // Cancel the -0.8 bonus
  }

  // A humble/honorific subsidiary may follow a connective particle, another
  // verb's renyokei/onbinkei, or a verbal noun. Favor its dictionary-confirmed
  // lemma over paths that split the subsidiary into short homographs. This
  // covers both ordinary te-form attachment and honorific-prefix constructions.
  const bool can_precede_humble_subsidiary =
      prev.pos == core::PartOfSpeech::Particle || prev.extended_pos == core::ExtendedPOS::VerbRenyokei ||
      prev.extended_pos == core::ExtendedPOS::VerbOnbinkei || prev.extended_pos == core::ExtendedPOS::AuxCausative ||
      prev.pos == core::PartOfSpeech::Noun;
  if (can_precede_humble_subsidiary && next.pos == core::PartOfSpeech::Verb &&
      grammar::isHumbleHonorificLemma(next.lemma)) {
    bonus += cost::kVeryStrongBonus;
  }

  // Penalty for splitting unknown words after youon (拗音: ょ, ゃ, ゅ)
  // Youon are always part of the preceding mora (きょ, しゃ, ちゅ)
  // Splitting after them produces invalid word boundaries
  // E.g., くしょん should stay as one token, not くしょ+ん
  // Only apply to non-dictionary tokens (dict entries like でしょ are valid boundaries)
  if (!prev.fromDictionary() && prev.pos == core::PartOfSpeech::Other) {
    std::string_view last_char = utf8::lastChar(prev.surface);
    if (grammar::isSmallKana(last_char)) {
      bonus += cost::kStrong;
    }
  }

  // Penalty for non-pronoun → ら(SUFFIX)
  // Plural suffix ら only naturally follows pronouns (彼ら, 僕ら)
  // Without this, NOUN→SUFFIX bonus (-0.8) causes false splits (かし+ら, 自+ら)
  if (prev.pos != core::PartOfSpeech::Pronoun && next.pos == core::PartOfSpeech::Suffix && next.surface == "ら") {
    bonus += cost::kStrong;
  }

  // Penalty for VerbRenyokei ending in らし → い (AuxAspectIru) pattern
  // 春らしい should be 春 + らしい, not 春らし (verb) + い (auxiliary)
  // The らし ending is typically from らしい (conjecture aux), not a verb renyokei
  // Verbs ending in らし are rare (探らし from 探る is the main exception)
  // Single-kanji noun + らしい is a common pattern that should be protected
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      prev.surface.size() >= core::kTwoJapaneseCharBytes &&  // At least 2 chars (kanji + らし)
      utf8::endsWith(prev.surface, "らし") && next.extended_pos == core::ExtendedPOS::AuxAspectIru) {
    bonus += cost::kAlmostNever;
  }

  // Bonus for longer causative forms (させ over さ+せ, させられ over さ+せ+られ)
  // MeCab treats させる as a single causative auxiliary for ichidan verbs
  // E.g., 食べ+させ+られ+た (not 食べ+さ+せ+られ+た).  The irregular Kuru
  // form has its own L1 mizenkei connection (来さ+せ), so it is excluded.
  if ((prev.extended_pos == core::ExtendedPOS::VerbRenyokei || prev.extended_pos == core::ExtendedPOS::VerbMizenkei) &&
      prev.lemma != "来る" && next.extended_pos == core::ExtendedPOS::AuxCausative &&
      utf8::startsWith(next.surface, "させ")) {
    bonus += cost::kDoubleVeryStrongBonus;
  }

  // Penalty for Noun → AuxCausative starting with させ (サ変動詞は さ+せ に分割)
  // E.g., 勉強させる should be 勉強+さ+せる, not 勉強+させる
  // MeCab treats サ変動詞 causative as Noun + さ(suru_mizen) + せる(causative)
  // Exception: Single-kanji ichidan verb stems should connect directly to させ
  // E.g., 見させる = 見+させる (ichidan 見る), not 見+さ+せる
  if (prev.pos == core::PartOfSpeech::Noun && next.extended_pos == core::ExtendedPOS::AuxCausative &&
      utf8::startsWith(next.surface, "させ")) {
    // Check if prev is a single-kanji ichidan verb stem (見, 寝, 着, etc.)
    bool is_single_kanji_ichidan = verb_helpers::isSingleKanjiIchidanSurface(prev.surface);
    if (is_single_kanji_ichidan) {
      // Bonus for single-kanji ichidan verb → させ (見+させる, 寝+させる)
      bonus += cost::kVeryStrongBonus;
    } else {
      // Penalty for multi-kanji noun → させ (サ変動詞は さ+せ に分割)
      bonus += cost::kVeryRare;
    }
  }

  // Bonus for Noun → VerbMizenkei "さ" (サ変動詞の未然形)
  // E.g., 反映される should be 反映+さ+れる (not 反映+される)
  // MeCab treats サ変動詞 passive as Noun + さ(suru_mizen) + れる(passive)
  // This enables the split: 反映+さ+れ+ます
  if (prev.pos == core::PartOfSpeech::Noun && next.extended_pos == core::ExtendedPOS::VerbMizenkei &&
      next.surface == "さ") {
    bonus += cost::kStrongBonus;
  }

  // Bonus for Noun → VerbMeireikei "せよ"/"しろ" (サ変動詞の命令形)
  // E.g., 勉強せよ → 勉強+せよ, 運動しろ → 運動+しろ
  // Without this, default-AUX char_speech candidates for せよ/しろ can beat
  // the legitimate dict VERB imperative entry. Restricted to the suru-imperative
  // surfaces so godan imperative forms (柿+食え) are not falsely boosted.
  if (prev.pos == core::PartOfSpeech::Noun && next.extended_pos == core::ExtendedPOS::VerbMeireikei &&
      (next.surface == "せよ" || next.surface == "しろ")) {
    bonus += cost::kStrongBonus;
  }

  return bonus;
}

// Past-tense た/たり, でした copula, た→ら, and volitional う rules.
float computeTaFormVolitionalBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float bonus{};  // value-init to 0

  // Surface-based bonus for VerbRenyokei → た/たら (ichidan/irregular
  // past and conditional-past forms). E.g., 食べ+た, 見+たら.
  // Guard: require kanji, dictionary, or a context-validated ひらがな一段
  // candidate.  The latter is created only for its immediately following
  // て/た, so it can retain a real split (混雑を+さけ+た) without allowing
  // unbounded fragments such as まし(ましる) to steal the past auxiliary.
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && utf8::equalsAny(next.surface, {"た", "たら"}) &&
      next.extended_pos == core::ExtendedPOS::AuxTenseTa &&
      (grammar::containsKanji(prev.surface) || prev.fromDictionary() ||
       prev.origin == core::CandidateOrigin::VerbHiraganaInflectedRenyokei)) {
    bonus += cost::kVeryStrongBonus;
  }

  // Bonus for VerbRenyokei/VerbOnbinkei → たり/だり (parallel listing particle)
  // E.g., 食べ+たり+する, 飲ん+だり+食べ+たり+する
  // Without this, た(AuxTenseTa) wins over たり(ParticleConj) due to strong た bonus
  // Accept dictionary-backed hiragana forms too (なっ+たり), while excluding
  // unknown pure-hiragana sound-symbolic forms (まっ+たり).
  if ((prev.extended_pos == core::ExtendedPOS::VerbRenyokei || prev.extended_pos == core::ExtendedPOS::VerbOnbinkei) &&
      (next.surface == "たり" || next.surface == "だり") && next.extended_pos == core::ExtendedPOS::ParticleConj &&
      (grammar::containsKanji(prev.surface) || prev.fromDictionary())) {
    bonus += cost::kVeryStrongBonus;
  }

  // Penalty for godan passive/causative-passive renyokei (～Aれ for A-row) → た
  // MeCab splits these as 言わ+れ+た, not 言われ+た
  // E.g., 言われた → 言わ+れ+た, 売られた → 売ら+れ+た, 書かれた → 書か+れ+た
  // A godan passive stem is a godan mizenkei a-row mora (か/が/さ/た/な/ば/ま/ら/わ)
  // followed by れ; verbTypeFromARowCodepoint recognizes exactly that set (and
  // rejects non-godan a-row like は so 晴れた stays 晴れ+た). This cancels the
  // VerbRenyokei→た bonus for godan passive forms.
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      prev.surface.size() >= core::kTwoJapaneseCharBytes &&  // At least 2 chars (Aれ, e.g. かれ)
      utf8::endsWith(prev.surface, "れ") && next.surface == "た" &&
      next.extended_pos == core::ExtendedPOS::AuxTenseTa) {
    const std::string_view before_re = utf8::dropLastChar(prev.surface);
    if (grammar::verbTypeFromARowCodepoint(utf8::decodeLastChar(before_re)) != grammar::VerbType::Unknown) {
      bonus += cost::kSevere;  // Cancel VerbRenyokei→た bonus
    }
  }

  // Surface-based bonus for でし → た/たら (polite past copula / conditional)
  // 本でした should be 本+でし+た, not 本+で+し+た
  // でしたら should be でし+たら (conditional), not でし+た+ら
  // The competing path is Noun→で(PARTICLE)→し(VERB)→た with VerbRenyokei→た bonus
  if (prev.surface == "でし" && prev.extended_pos == core::ExtendedPOS::AuxCopulaDesu &&
      (next.surface == "た" || next.surface == "たら") && next.extended_pos == core::ExtendedPOS::AuxTenseTa) {
    bonus += cost::kVeryStrongBonus;
  }

  // Penalty for た(AuxTenseTa) → ら(Suffix) pattern
  // This discourages splitting たら into た+ら
  // たら is a conditional form of た and should stay together
  if (prev.surface == "た" && prev.extended_pos == core::ExtendedPOS::AuxTenseTa && next.surface == "ら" &&
      next.extended_pos == core::ExtendedPOS::Suffix) {
    bonus += cost::kSevere;  // Penalty to discourage た+ら split
  }

  // The volitional auxiliary is realized as bare う only after an o-row mizenkei
  // (書こ+う, 泳ご+う, しよ+う; the ichidan form is the 2-mora 食べ+よう). A bare
  // う after any non-o-row verb ending is impossible Japanese — an a-row mizenkei
  // (つか of つく) or a u-row shuushikei (す of する) never takes the volitional う
  // — yet the spurious split would otherwise beat the real godan-wa verb
  // (つかう/使う, あらう/洗う, すう/吸う). Penalize so the whole-verb reading wins.
  // This applies only to the bare う surface. The literary volitional ん is
  // likewise one mora, but follows an a-row irrealis stem in 読まんとする.
  if (next.extended_pos == core::ExtendedPOS::AuxVolitional &&
      grammar::isSingleHiragana(next.surface, core::hiragana::kU) && prev.pos == core::PartOfSpeech::Verb &&
      !grammar::endsWithORow(prev.surface)) {
    bonus += cost::kSevere;
  }

  // An o-row irrealis form followed by bare う is the productive modern
  // volitional (書こ+う, 泳ご+う, しよ+う). Prefer it over a homographic
  // continuative-form plus formal-noun path.
  if (next.extended_pos == core::ExtendedPOS::AuxVolitional &&
      grammar::isSingleHiragana(next.surface, core::hiragana::kU) &&
      prev.extended_pos == core::ExtendedPOS::VerbMizenkei && grammar::endsWithORow(prev.surface) &&
      prev.lemma != "いく" &&
      (grammar::isGodanVerbType(grammar::conjTypeToVerbType(prev.conj_type)) ||
       prev.conj_type == dictionary::ConjugationType::Suru || prev.lemma == "する")) {
    bonus += cost::kVeryStrongBonus + cost::kStrongBonus;
  }

  // The directional subsidiary retains the same volitional boundary after a
  // connective form (読んで+いこ+う), rather than yielding to the
  // demonstrative adverb こう.
  if (prev.extended_pos == core::ExtendedPOS::AuxAspectIku && next.extended_pos == core::ExtendedPOS::AuxVolitional &&
      grammar::isSingleHiragana(next.surface, core::hiragana::kU)) {
    bonus += cost::kVeryStrongBonus;
  }

  return bonus;
}

// Negative auxiliaries (ない/ず/ね) and noun↔short-verb-renyokei disambiguation.
float computeNegativeAndNounVerbBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float bonus{};  // value-init to 0

  // A multi-character nominal compound ending in 中 can modify a predicate or
  // take a case particle as one searchable unit. Keep that reading ahead of
  // a stem-plus-suffix path; short two-character temporal forms remain split.
  if (prev.extended_pos == core::ExtendedPOS::Noun && utf8::endsWith(prev.surface, "中") &&
      prev.surface.size() >= core::kThreeJapaneseCharBytes &&
      (next.extended_pos == core::ExtendedPOS::ParticleCase ||
       next.extended_pos == core::ExtendedPOS::VerbShuushikei)) {
    bonus += cost::kStrongBonus + cost::kModerateBonus;
  }

  // A topic particle cannot directly select the classical negative auxiliary.
  // In sequences such as 〜たはずだ, the apparent は+ず boundary is the formal
  // noun はず, not a predicate followed by negation.
  // Ichidan stems are both continuative and irrealis.  After the suru
  // continuative, their irrealis reading is licensed before a negative
  // auxiliary (確認し+終え+ない) rather than an unrelated unknown noun.
  const bool classical_negative_suru = prev.extended_pos == core::ExtendedPOS::AuxNegativeNu &&
                                       next.extended_pos == core::ExtendedPOS::VerbRenyokei && next.lemma == "する";
  const bool topic_before_classical_negative =
      prev.extended_pos == core::ExtendedPOS::ParticleTopic && next.extended_pos == core::ExtendedPOS::AuxNegativeNu;
  const bool sahen_ichidan_irrealis = prev.extended_pos == core::ExtendedPOS::VerbRenyokei && prev.lemma == "する" &&
                                      next.extended_pos == core::ExtendedPOS::VerbMizenkei &&
                                      next.conj_type == dictionary::ConjugationType::Ichidan;
  if (classical_negative_suru || topic_before_classical_negative || sahen_ichidan_irrealis) {
    bonus += (classical_negative_suru ? cost::kVeryStrongBonus : cost::kNeutral) +
             (topic_before_classical_negative ? cost::kProhibitive : cost::kNeutral) +
             (sahen_ichidan_irrealis ? cost::kVeryStrongBonus : cost::kNeutral);
  }

  // The copula's conjunctive form is で; an emphatic small-tsu variant cannot
  // take connective て/で. In なっ+て the onbin belongs to lexical なる, so
  // prevent a generated emphatic AuxCopulaDa edge from replacing that verb.
  if (prev.extended_pos == core::ExtendedPOS::AuxCopulaDa && utf8::endsWith(prev.surface, "っ") &&
      next.extended_pos == core::ExtendedPOS::ParticleConj && grammar::isTeDeSurface(next.surface)) {
    bonus += cost::kAlmostNever;
  }

  // Bonus for dict VERB_連用 → ない/なく/なかっ/なけれ (negative auxiliary)
  // VERB→ADJ bigram (0.8) is high, making split path lose to merged candidates
  // E.g., でき+なく should beat できなく, し+なく should beat しなく
  // Restrict to dictionary verbs (間違い+ない uses 間違い(NOUN), not 違い(VERB))
  // Exclude で (ambiguous: 出る VERB vs だ copula AUX → でない misanalysis)
  // Exclude godan mizenkei (a-dan ending): 走ら, 書か are mislabeled as VERB_連用
  // but are actually 未然形 — bonus would incorrectly boost 走ら+ない split
  if (prev.pos == core::PartOfSpeech::Verb && prev.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      prev.fromDictionary() && prev.surface != "で" && !grammar::endsWithARow(prev.surface) &&
      (next.pos == core::PartOfSpeech::Adjective || next.pos == core::PartOfSpeech::Auxiliary) &&
      utf8::equalsAny(next.surface, {"なく", "ない", "なかっ", "なけれ"})) {
    bonus += cost::kStrongBonus;
  }

  // Bonus for ば(PART_接続) → なら/なり/なる/なれ(VERB) in -なければならない pattern
  // Prevents spurious ばなら verb candidate (ばなる godan-ra) from winning
  // over correct split ば(conditional) + なら(なる mizenkei)
  if (prev.extended_pos == core::ExtendedPOS::ParticleConj && prev.surface == "ば" &&
      next.pos == core::PartOfSpeech::Verb && utf8::equalsAny(next.surface, {"なら", "なり", "なる", "なれ", "なっ"})) {
    bonus += cost::kStrongBonus;
  }

  // Contracted obligation chains retain their grammatical boundaries:
  // mizenkei + なく + ちゃ + いけ + ない, and
  // mizenkei + なきゃ/なけりゃ + なら + ない.
  if (prev.extended_pos == core::ExtendedPOS::AuxNegativeNai && utf8::equalsAny(prev.surface, {"なく"}) &&
      next.extended_pos == core::ExtendedPOS::ParticleConj && utf8::equalsAny(next.surface, {"ちゃ"})) {
    bonus += cost::kStrongBonus;
  }

  // The change-of-state construction 〜なくなる retains なく as a negative
  // auxiliary; elsewhere before a connective, the competing adjective form
  // remains appropriate (読まれなくて困る).
  if (prev.extended_pos == core::ExtendedPOS::AuxNegativeNai && utf8::equalsAny(prev.surface, {"なく"}) &&
      next.extended_pos == core::ExtendedPOS::VerbShuushikei && utf8::equalsAny(next.surface, {"なる"})) {
    bonus += cost::kExtremeBonus + cost::kMinorBonus;
  }

  // The case particle まで attaches directly to a terminal predicate
  // (食べるまで, 調べるまで). Keep this boundary before the particle's
  // leading mora can be absorbed into an unknown nominal candidate.
  if (prev.extended_pos == core::ExtendedPOS::VerbShuushikei && next.extended_pos == core::ExtendedPOS::ParticleCase &&
      utf8::equalsAny(next.surface, {"まで"})) {
    bonus += cost::kVeryStrongBonus;
  }
  if (prev.extended_pos == core::ExtendedPOS::AuxNegativeNai &&
      grammar::isColloquialConditionalNegativeSurface(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::VerbMizenkei && utf8::equalsAny(next.surface, {"なら"})) {
    bonus += cost::kStrongBonus;
  }
  if (prev.extended_pos == core::ExtendedPOS::AuxNegativeNai &&
      grammar::isColloquialConditionalNegativeSurface(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::AuxPotential && utf8::equalsAny(next.surface, {"いけ"})) {
    // The lexical いける renyokei has a low candidate cost before ない. Give
    // this closed obligation connection a small additional preference so its
    // auxiliary analysis wins only after the contracted negative conditional.
    bonus += cost::kVeryStrongBonus + cost::kMinorBonus;
  }
  // The same obligation predicate follows the formal negative conditional
  // (なけれ+ば+いけ+ない) and the te-form topic construction
  // (なく+て+は+いけ+ない).  In either case the preceding closed-class
  // particle identifies the auxiliary reading over lexical いける.
  if ((prev.extended_pos == core::ExtendedPOS::ParticleConj && utf8::equalsAny(prev.surface, {"ば"})) ||
      (prev.extended_pos == core::ExtendedPOS::ParticleTopic && utf8::equalsAny(prev.surface, {"は"}))) {
    if (next.extended_pos == core::ExtendedPOS::AuxPotential && utf8::equalsAny(next.surface, {"いけ"})) {
      bonus += cost::kDoubleVeryStrongBonus;
    }
  }
  if (prev.extended_pos == core::ExtendedPOS::ParticleConj && utf8::equalsAny(prev.surface, {"ちゃ"}) &&
      ((next.extended_pos == core::ExtendedPOS::AuxPotential && utf8::equalsAny(next.surface, {"いけ"})) ||
       (next.extended_pos == core::ExtendedPOS::VerbMizenkei && utf8::equalsAny(next.surface, {"なら"})))) {
    bonus += cost::kDoubleVeryStrongBonus;
  }

  // Bonus for VERB_未然 → AUX_否定古(ず/ずに/ね) connection
  // Godan mizenkei + classical negative: 書かず, 抜かず, 行かず, 行かねば
  // The split path needs help to beat merged verb candidates (書かずに as single VERB)
  // because AUX_否定古 → next token connections have default (high) cost.
  // ね is the 已然形 of the same classical negative (行かねば, 死なねば) and competes
  // with the dict VERB reading of ね (連用形 of ねる=寝る) and the sentence-final
  // particle ね; this bonus is what lets the AUX reading win after a verb mizenkei.
  // Note: lexicalized forms like 思わず(ADV) are handled by the candidate generator
  // which skips mizenkei_zu generation when verb+ず is in the dictionary.
  if (prev.extended_pos == core::ExtendedPOS::VerbMizenkei && next.extended_pos == core::ExtendedPOS::AuxNegativeNu &&
      utf8::equalsAny(next.surface, {"ず", "ずに", "ざる", "ざれ", "ね"})) {
    bonus += utf8::endsWith(next.surface, "に") ? cost::kDoubleVeryStrongBonus : cost::kStrongBonus;
  }

  // Cancel the ichidan-oriented VerbRenyokei → AuxNegativeNu(ね) bonus (消えぬ pattern)
  // when prev is not a genuine renyokei/mizenkei form (i.e., doesn't end in an
  // i-row/e-row hiragana). Some godan verbs get a spurious VerbRenyokei-tagged
  // candidate for their dictionary shuushikei form (e.g., 行く), which would
  // otherwise hijack ね away from the sentence-final particle reading (行くね).
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && next.extended_pos == core::ExtendedPOS::AuxNegativeNu &&
      next.surface == "ね" && !grammar::endsWithRenyokeiMarker(prev.surface)) {
    bonus += cost::kRare;
  }

  // Bonus for AUX_否定古(ずに) → VERB connection
  // ずに+帰る, ずに+済む etc. are natural patterns
  // Without this, split path ず+に+帰る wins due to PART_格→VERB having lower default cost
  if (prev.extended_pos == core::ExtendedPOS::AuxNegativeNu && prev.surface == "ずに" &&
      (next.pos == core::PartOfSpeech::Verb || next.pos == core::PartOfSpeech::Adjective)) {
    bonus += cost::kVeryStrongBonus;
  }

  // A nominalized predicate can attach to the continuative form of する.
  // This includes productive honorific-prefix constructions and ordinary
  // verbal-noun predicates, so prefer it over an unrelated lexical verb chain.
  if (prev.extended_pos == core::ExtendedPOS::Noun && next.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      grammar::isSuruRenyokeiSurface(next.surface)) {
    bonus += cost::kStrongBonus + cost::kMinorBonus;
  }

  // A derivational suffix can form the nominal base of する (重要+視+する,
  // 安定+化+する). Preserve the productive suffix boundary over a fused
  // unknown noun before the regular suru continuative form.
  if (prev.pos == core::PartOfSpeech::Suffix && next.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      grammar::isSuruRenyokeiSurface(next.surface)) {
    bonus += cost::kStrongBonus;
  }

  // An interrogative pronoun directly preceding the continuative し forms a
  // productive question predicate (何+し+てる). This is an exception to the
  // general ban on interrogative-pronoun-to-verb attachment.
  if (prev.extended_pos == core::ExtendedPOS::PronounInterrogative &&
      next.extended_pos == core::ExtendedPOS::VerbRenyokei && grammar::isSuruRenyokeiSurface(next.surface)) {
    bonus += cost::kDoubleVeryStrongBonus;
  }

  // Interrogative pronouns take the comparative case particle directly
  // (なに+より), rather than allowing a longer formal particle to absorb its
  // final syllable.
  if (prev.extended_pos == core::ExtendedPOS::PronounInterrogative && utf8::equalsAny(prev.surface, {"なに"}) &&
      next.extended_pos == core::ExtendedPOS::ParticleCase && utf8::equalsAny(next.surface, {"より"})) {
    bonus += cost::kDoubleVeryStrongBonus;
  }

  // Surface-based penalty for Noun → short VerbRenyokei (compound verb protection)
  // Bigram table gives bonus for Noun→VerbRenyokei (for サ変動詞: 得+し, 損+し)
  // But this should NOT apply to compound verbs like 見+つけ→見つけ
  // E.g., 勘違い should be single token, not 勘違+い
  // E.g., 見つけた should be 見つけ+た, not 見+つけ+た
  // Exception: multi-kanji noun + でき should split (外出+でき+ない)
  // Single kanji NOUN often forms compound verbs with following verb stems
  if (prev.extended_pos == core::ExtendedPOS::Noun && next.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      next.surface != "し" && next.surface != "せ" && next.surface.size() <= 6 &&
      prev.surface.size() == core::kJapaneseCharBytes) {
    bonus += cost::kRare;  // Cancel the bigram bonus
  }

  // Penalty for Noun/ナ形容詞 → い (VerbRenyokei of いる); mirrors the
  // Noun→AuxAspectIru bigram severity so both readings of a bare-noun-plus-い
  // are rejected (彼が+いる needs a particle; 間続+い beaten by 間+続い).
  // E.g., 上手いし should be 上手い+し, not 上手+い+し. Must NOT block サ変+でき (外出+でき).
  if ((prev.extended_pos == core::ExtendedPOS::AdjNaAdj || prev.extended_pos == core::ExtendedPOS::Noun) &&
      next.extended_pos == core::ExtendedPOS::VerbRenyokei && next.surface == "い") {
    bonus += cost::kSevere;
  }

  // Partial cancel for single-kanji NOUN + し pattern
  // E.g., 寒し (archaic adjective) should not split as 寒+し
  // But 得+し (suru-verb renyokei) should still split
  // Single kanji = 3 bytes in UTF-8
  if (prev.extended_pos == core::ExtendedPOS::Noun && next.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      next.surface == "し" && prev.surface.size() == 3) {
    bonus += cost::kUncommon;
  }

  return bonus;
}

// Short pure-hiragana verb false-split penalties after particles/OTHER, plus
// determiner→noun and case-particle→final-particle guards.
float computeParticleDeterminerBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float bonus{};  // value-init to 0

  // An object marker followed by a continuative verb strongly licenses a
  // predicate (本を買いに行く, 本を読み始める). This left-context evidence
  // offsets the general renyokei-before-case-particle nominalization bias while
  // leaving standalone nominal forms such as 読みを/香りを untouched.
  if (prev.extended_pos == core::ExtendedPOS::ParticleCase && grammar::isAccusativeParticleWoSurface(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::VerbRenyokei && next.fromDictionary()) {
    bonus += cost::kExtraStrongBonus;
  }

  // Penalty for single-char case particle → very short inflected
  // pure-hiragana verb pattern. Two-kana base forms are deliberately exempt:
  // object + きく/やく/けす is a basic feature-derived verb boundary.
  // The risky false splits here are short stems such as が+おさ.
  // Single-char particles: が, を, に, へ, と, で, から, etc.
  // Only penalize very short verbs (2 chars or less) to avoid affecting なくし, etc.
  // Exception: "い" (いる renyokei) has specific bonus rule below for PART_格→い pattern
  // Generated ichidan stems are emitted only after a following inflection has
  // validated the reconstruction. They are therefore not the short
  // unconstrained stems this guard targets (混雑を+さけ+ない/て/た), even
  // though their surfaces are two morae.
  const bool is_validated_ichidan_inflection = (next.origin == core::CandidateOrigin::VerbHiraganaNegativeRenyokei &&
                                                next.extended_pos == core::ExtendedPOS::VerbMizenkei) ||
                                               (next.origin == core::CandidateOrigin::VerbHiraganaInflectedRenyokei &&
                                                next.extended_pos == core::ExtendedPOS::VerbRenyokei);
  if (prev.extended_pos == core::ExtendedPOS::ParticleCase &&
      prev.surface.size() <= core::kJapaneseCharBytes &&  // Single hiragana char (3 bytes in UTF-8)
      next.pos == core::PartOfSpeech::Verb && !next.fromDictionary() && grammar::isPureHiragana(next.surface) &&
      next.surface.size() <= 6 &&  // 2 chars or less (6 bytes in UTF-8)
      next.extended_pos != core::ExtendedPOS::VerbShuushikei &&
      next.surface != "い" &&  // Exclude い - has specific rule
      !is_validated_ichidan_inflection) {
    bonus += cost::kAlmostNever;
  }

  // Penalty for は (topic) → short pure-hiragana verb pattern
  // E.g., は+し in はしがかかる should be はし (noun), not は+し (topic + する連用形)
  // Only applies to は — other topic particles (も, こそ) naturally precede し
  // (何もしない, 誰もしない are common patterns)
  // Exception: い (renyokei of いる) - valid in ずにはいられない pattern
  // Exception: し (renyokei of する) - valid in emphatic negation ありはしない pattern
  if (prev.extended_pos == core::ExtendedPOS::ParticleTopic && prev.surface == "は" &&
      next.pos == core::PartOfSpeech::Verb && grammar::isPureHiragana(next.surface) &&
      next.surface.size() <= 3 &&  // 1 char only (3 bytes in UTF-8)
      next.surface != "い" &&      // い+られ is valid (いる potential)
      next.surface != "し") {      // し+ない is valid (emphatic negation)
    bonus += cost::kVeryRare;
  }

  // Penalty for pure-hiragana OTHER → single-char VerbRenyokei
  // E.g., ふんど+し should be ふんどし (one word), not noun+する連用形
  // Pure hiragana unknown sequences split before し/き/etc. are usually wrong
  // Does not apply when prev is a known particle/aux (those have specific EPOS)
  if (prev.pos == core::PartOfSpeech::Other && grammar::isPureHiragana(prev.surface) &&
      prev.surface.size() >= 6 &&                                                          // 2+ hiragana chars
      next.extended_pos == core::ExtendedPOS::VerbRenyokei && next.surface.size() <= 3) {  // Single char (し, き, etc.)
    bonus += cost::kUncommon;
  }

  // An unknown hiragana fragment cannot directly introduce an onbin verb.
  // Such a path is an over-segmentation of one inflected word (よろこんで),
  // whereas ordinary adverbial modifiers have their own lexical categories.
  if (prev.pos == core::PartOfSpeech::Other && grammar::isPureHiragana(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::VerbOnbinkei) {
    bonus += cost::kStrong;
  }

  // Penalty for NOUN → single-hiragana OTHER
  // A single hiragana character classified as OTHER after a kanji NOUN is almost
  // always a misparse: the hiragana should be part of a verb (先+生きのこる) or
  // okurigana (読み+残す), not a standalone unknown token
  // E.g., 先生+き(OTHER) should lose to 先+生きのこる
  // Needs a very high penalty to overcome prefix compound bonus advantages
  if (prev.pos == core::PartOfSpeech::Noun && grammar::containsKanji(prev.surface) &&
      next.pos == core::PartOfSpeech::Other && next.surface.size() == 3 &&  // Single char = 3 bytes UTF-8
      grammar::isPureHiragana(next.surface)) {
    bonus += cost::kAlmostNever;
  }

  // Penalty for Noun → かかる(Determiner) with no intervening particle
  // The classical determiner 斯かる always needs a preceding particle, topic
  // marker, or clause boundary (は+かかる, として+かかる, かかる事態 at clause
  // start); it never directly follows a bare noun. Direct noun adjacency
  // (3週間かかる, 5分かかる) is the intransitive verb 掛かる/罹る taking a
  // duration/quantity noun without a particle. The ParticleCase→Determiner
  // bigram penalty above only covers noun+particle+かかる (壁にかかる); this
  // covers noun+かかる directly (3週間かかる).
  if (prev.pos == core::PartOfSpeech::Noun && grammar::isDurationPredicateKakaru(next.surface) &&
      next.extended_pos == core::ExtendedPOS::Determiner) {
    bonus += cost::kAlmostNever;
  }

  // A duration closing suffix and a degree particle both introduce a
  // predicate in a duration expression (三時間かかる、三時間ほどかかる).
  // The homographic determiner cannot fill that predicate slot.
  if ((prev.extended_pos == core::ExtendedPOS::Suffix && prev.surface == "間") ||
      prev.extended_pos == core::ExtendedPOS::ParticleAdverbial) {
    if (next.surface == "かかる" && next.extended_pos == core::ExtendedPOS::Determiner) {
      bonus += cost::kAlmostNever;
    }
  }

  // The quotative determiner cannot introduce the comparative particle. When
  // the particle is also emitted as an unknown fallback, keep the productive
  // quotation sequence と+いう+より available.
  if (prev.extended_pos == core::ExtendedPOS::DeterminerQuotative && next.surface == "より" &&
      next.pos == core::PartOfSpeech::Other) {
    bonus += cost::kAlmostNever;
  }

  // Penalty for DET → non-dict single-kanji NOUN
  // The DET→NOUN bigram bonus (-2.5) is too strong for unknown single-kanji tokens,
  // causing splits like こんな+伸+びる instead of こんな+伸びる
  // Valid DET+NOUN patterns (こんな+事, あんな+人) use dict nouns or multi-char nouns
  if (prev.pos == core::PartOfSpeech::Determiner && next.pos == core::PartOfSpeech::Noun && !next.fromDictionary() &&
      grammar::containsKanji(next.surface) && suzume::normalize::utf8Length(next.surface) == 1) {
    bonus += cost::kStrong;
  }

  // Penalty for DET → non-dict kanji+hiragana NOUN (nominalized verb pattern)
  // The DET→NOUN bonus (-2.5) makes heuristic candidates like "先生き" (NOUN)
  // too attractive, preventing correct splits like 先+生きのこる
  // Valid DET+NOUN uses dict nouns or pure-kanji nouns; nominalized forms
  // (kanji + 1 trailing hiragana, e.g., 先生き, 出来事み) are rare after DET
  if (prev.pos == core::PartOfSpeech::Determiner && next.pos == core::PartOfSpeech::Noun && !next.fromDictionary()) {
    size_t char_len = suzume::normalize::utf8Length(next.surface);
    if (char_len >= 3 && grammar::containsKanji(next.surface) && !grammar::isAllKanji(next.surface)) {
      // Check whether the final two characters are kanji + hiragana.
      const std::string_view before_last = utf8::dropLastChar(next.surface);
      if (kana::isHiraganaCodepoint(utf8::decodeLastChar(next.surface)) && !before_last.empty() &&
          normalize::isKanjiCodepoint(utf8::decodeLastChar(before_last))) {
        bonus += cost::kAlmostNever;
      }
    }
  }

  return bonus;
}

// Prefix/adverb→short-verb, symbol→particle/aux/furigana, and で+も copula rules.
float computePrefixSymbolBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float bonus{};  // value-init to 0

  // Penalty for PREFIX → short pure-hiragana verb pattern
  // E.g., お+い in において should not happen (お is prefix, い is not a verb here)
  // Valid お+verb patterns: お待ち, お願い (longer, often with kanji)
  // A closed-class honorific verb is the only dictionary-confirmed exception
  // to this short-prefix guard (お+はす). Other one- or two-mora hypotheses,
  // including the L1 い stem, remain too ambiguous to license directly.
  const bool is_honorific_prefix_verb = prev.extended_pos == core::ExtendedPOS::Prefix &&
                                        grammar::isHonorificPrefix(prev.surface) && next.fromDictionary() &&
                                        grammar::isHumbleHonorificLemma(next.lemma);
  if (prev.pos == core::PartOfSpeech::Prefix && next.pos == core::PartOfSpeech::Verb && !is_honorific_prefix_verb &&
      grammar::isPureHiragana(next.surface) && next.surface.size() <= 6) {  // 2 chars or less
    bonus += cost::kAlmostNever;
  }

  // Penalty for PREFIX → non-dictionary pure-hiragana verb pattern (3 chars)
  // E.g., お+はよう in おはよう - はよう is not a real verb
  // Valid patterns like お+待ち have kanji, お+召し would be in dictionary
  if (prev.pos == core::PartOfSpeech::Prefix && next.pos == core::PartOfSpeech::Verb && !next.fromDictionary() &&
      grammar::isPureHiragana(next.surface) && next.surface.size() == 9) {  // Exactly 3 chars (9 bytes)
    bonus += cost::kAlmostNever;
  }

  // Penalty for ADV → short pure-hiragana verb renyokei pattern
  // E.g., はなはだ+し should not happen (はなはだしい is an adjective)
  // Valid ADV+verb patterns: ゆっくり+歩く (verb is longer/has kanji)
  // This prevents split like はなはだ+し+い when はなはだしい exists in dict
  // Exception: dictionary verbs like ね(寝る), み(見る), で(出る) are valid
  bool is_dict_verb_renyokei = core::hasFlag(next.flags, core::EdgeFlags::FromDictionary);
  if (prev.pos == core::PartOfSpeech::Adverb && next.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      grammar::isPureHiragana(next.surface) && next.surface.size() <= 3 &&  // 1 char only (し, み, etc.)
      !is_dict_verb_renyokei) {
    bonus += cost::kVeryRare;
  }

  // Penalty for opening bracket → PARTICLE pattern (furigana in parentheses).
  // E.g., 東京（とうきょう） should not split と+う+きょう. Punctuation
  // and closing brackets are clause boundaries and may legitimately be
  // followed by a particle, so they must not receive this penalty.
  if (prev.pos == core::PartOfSpeech::Symbol && next.pos == core::PartOfSpeech::Particle &&
      normalize::isOpeningBracket(utf8::decodeFirstChar(prev.surface))) {
    bonus += cost::kAlmostNever;
  }

  // Bonus for SYMBOL → long pure-hiragana OTHER (furigana pattern)
  // E.g., 東京（とうきょう） - the hiragana in parentheses is reading/furigana
  // Long hiragana sequences after symbols should stay as single tokens
  if (prev.pos == core::PartOfSpeech::Symbol && next.pos == core::PartOfSpeech::Other &&
      grammar::isPureHiragana(next.surface) && next.surface.size() >= 12) {  // 4+ chars (12 bytes in UTF-8)
    bonus += cost::kVeryStrongBonus;
  }

  // Penalty for SYMBOL → short hiragana → AUX pattern (furigana), gated to an
  // opening bracket only. Emoji, closing brackets, and other symbols are a soft
  // boundary that still license a following copula: 天気😀です, 犬🐕でした,
  // 本(重要)です, 評価◎です must keep です/でした whole rather than splitting で|す.
  if (prev.pos == core::PartOfSpeech::Symbol && next.pos == core::PartOfSpeech::Auxiliary &&
      normalize::isOpeningBracket(utf8::decodeFirstChar(prev.surface))) {
    bonus += cost::kVeryRare;
  }

  // Penalty for AuxCopulaDa(で) + ParticleTopic(も) pattern
  // This prevents 雨+で+も split when 雨+でも (副助詞) is correct
  // But allows 何+で+も split (で=copula連用形, も=係助詞)
  // The difference: 何(Pronoun) vs 雨(Noun) - Pronoun should split
  if (prev.extended_pos == core::ExtendedPOS::AuxCopulaDa && prev.surface == "で" &&
      next.extended_pos == core::ExtendedPOS::ParticleTopic && next.surface == "も") {
    bonus += cost::kVeryRare;
  }

  return bonus;
}

// Suffix (さ/kanji-suffix) splits, ADV→でも/だけど, and short verb-renyokei→aux rules.

}  // namespace suzume::analysis::connection_rules
