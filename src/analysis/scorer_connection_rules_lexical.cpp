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

#ifdef SUZUME_DEBUG_INFO
using suzume::core::CandidateOrigin;
#endif
namespace cost = suzume::analysis::bigram_cost;
namespace sc = suzume::analysis::scorer;

// Surface-based adjustments use cost:: namespace directly from bigram_cost.
// See bigram_table.h and scorer_constants.h for constant values.

namespace suzume::analysis::connection_rules {

float computeSuffixShortVerbBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float bonus{};  // value-init to 0

  // Penalty for SUFFIX(さ) + VERB starting with せ/させ pattern
  // E.g., 勉強 + さ(SUFFIX) + せられてい is wrong; should be 勉強 + さ(VERB_未然) + せ(AUX_使役)
  // This pattern indicates suru-verb causative form where さ is the verb stem, not suffix
  if (prev.pos == core::PartOfSpeech::Suffix && prev.surface == "さ" && next.pos == core::PartOfSpeech::Verb &&
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
    bonus += cost::kAlmostNever;
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

// Progressive/contracted て, dialectal やで, 付け-で formal noun, honorific
// renyokei (いたし/いただき), and い/た/だ auxiliary attachment rules.
float computeProgressiveHonorificBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float bonus{};  // value-init to 0

  // みたい immediately after a connective て/で is not the conjecture
  // auxiliary: 〜てみたい consists of the trial subsidiary み + desiderative
  // たい. This applies whether the competing connective edge was classified as a
  // particle or as a contracted aspect auxiliary.
  if (utf8::equalsAny(prev.surface, {"て", "で"}) && next.extended_pos == core::ExtendedPOS::AuxConjectureMitai) {
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

  // If contracted-progressive て is followed by ordinary particles or content
  // words, prefer the connective particle て instead.
  if (prev.surface == "て" && prev.extended_pos == core::ExtendedPOS::AuxAspectIru &&
      (next.pos == core::PartOfSpeech::Particle || next.pos == core::PartOfSpeech::Noun ||
       next.pos == core::PartOfSpeech::Pronoun || next.pos == core::PartOfSpeech::Determiner ||
       next.pos == core::PartOfSpeech::Adverb || next.pos == core::PartOfSpeech::Conjunction)) {
    bonus += cost::kAlmostNever;
  }

  // Dialectal/character-speech やで is particle + particle in the regression
  // corpus, not copula で.
  if (prev.surface == "や" && next.surface == "で" && next.extended_pos == core::ExtendedPOS::AuxCopulaDa) {
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

  // Bonus for VerbRenyokei/VerbOnbinkei → VerbRenyokei (subsidiary verb patterns)
  // E.g., 願い+いたし (お願いいたします), 報告+いたし (ご報告いたします),
  //       し+かね (賛成しかねます), 沿い+かね (ご期待に沿いかねます)
  // Include VerbOnbinkei since 願い is often recognized as onbin form of 願う
  if ((prev.extended_pos == core::ExtendedPOS::VerbRenyokei || prev.extended_pos == core::ExtendedPOS::VerbOnbinkei) &&
      next.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      (grammar::isSubsidiaryHonorificRenyokei(next.surface) || grammar::isModalSubsidiaryRenyokei(next.surface))) {
    bonus += cost::kVeryStrongBonus;
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

  // Bonus for AuxNegativeNu(ん) → VerbOnbinkei(かっ) pattern
  // くだらん+かっ+た = contracted negative past (くだらなかった)
  // Without this, the adjective path (分からんかっ+た) beats the verb path
  // The かっ verb form (from かる) is specific to this contracted negative past pattern
  // Need very strong bonus because the かっ unknown verb has high cost (~2.7)
  if (prev.extended_pos == core::ExtendedPOS::AuxNegativeNu && next.extended_pos == core::ExtendedPOS::VerbOnbinkei &&
      next.surface == "かっ") {
    bonus += sc::kBonusContractedNegPast;  // -3.45
  }

  // Penalty for VerbOnbinkei(ん) → Verb(でる) pattern
  // After ん音便, でる is almost always the contracted ている, not the verb 出る
  // E.g., 並んでる = 並んでいる (progressive), やんでる = 病んでいる
  // Force the で(PART_接続) + る path instead
  if (prev.extended_pos == core::ExtendedPOS::VerbOnbinkei && utf8::endsWith(prev.surface, "ん") &&
      next.pos == core::PartOfSpeech::Verb && next.surface == "でる") {
    bonus += cost::kStrong;
  }

  // Bonus for NOUN → できる(VERB) pattern
  // 堪能できる, 表現できる, 想像できる etc.
  // できる (potential of する) commonly follows サ変 nouns
  // Without this, で(PART)+きる(VERB) path narrowly beats できる(VERB) path
  if (prev.pos == core::PartOfSpeech::Noun && next.pos == core::PartOfSpeech::Verb && next.surface == "できる") {
    bonus += cost::kMinorBonus;
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

  // Penalty for AuxCopulaDa(な) → ParticleFinal(ったら) pattern
  // な is attributive form of copula — cannot be followed by ったら particle
  // Valid: だ+ね, だ+よ. Invalid: な+ったら (should be なっ+たら from なる)
  if (prev.extended_pos == core::ExtendedPOS::AuxCopulaDa && prev.surface == "な" &&
      next.extended_pos == core::ExtendedPOS::ParticleFinal && utf8::startsWith(next.surface, "った")) {
    bonus += cost::kRare;
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

}  // namespace suzume::analysis::connection_rules
