#include <cmath>

#include "analysis/bigram_table.h"
#include "analysis/category_cost.h"
#include "analysis/scorer.h"
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

namespace suzume::analysis {

namespace {

// Surface-based connection rules, extracted from connectionCost for readability.
// Each helper accumulates the `surface_bonus +=` contributions of a thematically
// related group of rules and returns their sum. Helpers are self-contained: they
// recompute any needed locals from prev/next and never read caller state. Because
// every contribution is additive, the order among these helpers does not affect the
// total; call sites are kept at their original positions for readability.

// VerbRenyokei attachment to adjectives, auxiliaries, and subsidiary verbs:
// すぎ/AdjBasic, し-conjunction, causative さ, compound-particle→topic, て→い,
// し→てる, and ゆく/いく subsidiary verbs.
float computeVerbRenyokeiEarlyBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float bonus{};  // value-init to 0

  // VerbRenyokei → AdjBasic bonus for kanji-containing verb + adjective
  // E.g., 尽くし+難い, 食べ+やすい — verb renyoukei + compound adjective
  // Only when verb contains kanji to prevent false splits in hiragana sequences
  // (e.g., おこがましい → おこ+がましい would be wrong)
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && next.extended_pos == core::ExtendedPOS::AdjBasic &&
      grammar::containsKanji(prev.surface)) {
    bonus += cost::kExtraStrongBonus;
  }

  // Penalty for VerbRenyokei + し(conjunction) with kanji verb
  // In modern Japanese, conjunction し follows shuushikei (行く+し), not renyoukei (行き+し).
  // VerbRenyokei + し is usually a false split of godan-sa renyoukei (尽く+し → 尽くし).
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && next.surface == "し" &&
      next.extended_pos == core::ExtendedPOS::ParticleConj && grammar::containsKanji(prev.surface)) {
    bonus += cost::kMinor;  // Penalty to discourage false split
  }

  // VerbRenyokei (A-row ending) → VerbMizenkei(さ) causative pattern
  // E.g., やら+さ+れ+た — hiragana verb mizenkei + causative さ
  // VerbRenyokei is used for short hiragana verbs (EPOS can't distinguish mizenkei)
  // Need strong bonus to overcome VerbRenyokei→VerbMizenkei bigram penalty (1.8)
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && next.extended_pos == core::ExtendedPOS::VerbMizenkei &&
      next.surface == "さ" && grammar::endsWithARow(prev.surface)) {
    bonus += sc::kBonusVerbCausativePattern;
  }

  // Compound particle (≥3 chars) → topic/binding particle (は, も, が)
  // E.g., にとって+も, について+は, において+も, として+は
  // Only applies to long compound particles to avoid boosting て+も, し+は
  // Needs to overcome ADV→NOUN bonus advantage in competing paths
  if (prev.extended_pos == core::ExtendedPOS::ParticleConj && next.extended_pos == core::ExtendedPOS::ParticleTopic &&
      prev.surface.size() >= core::kThreeJapaneseCharBytes) {
    bonus += sc::kBonusCompoundParticleToTopic;
  }

  // Bonus for て → い (Auxiliary) pattern
  // E.g., して+い+ます, 食べて+い+た (MeCab-compatible: い is auxiliary, not verb)
  // The auxiliary い (from いる) should win over verb renyokei い
  if (prev.surface == "て" && prev.extended_pos == core::ExtendedPOS::ParticleConj && next.surface == "い" &&
      next.extended_pos == core::ExtendedPOS::AuxAspectIru) {
    bonus += cost::kStrongBonus;
  }

  // Penalty for し (PART_接続) → てる (AuxAspectIru) pattern
  // E.g., 何してる should be 何+し(VERB)+てる, not 何+し(PART)+てる
  // The reasoning conjunction し should not be directly followed by progressive てる
  // This cancels the ParticleConj→AuxAspectIru bonus (-0.8) for this specific case
  if (prev.surface == "し" && prev.extended_pos == core::ExtendedPOS::ParticleConj && next.surface == "てる" &&
      next.extended_pos == core::ExtendedPOS::AuxAspectIru) {
    bonus += cost::kRare;  // Cancel the -0.8 bonus
  }

  // Bonus for VerbRenyokei → subsidiary verb ゆく/いく (補助動詞)
  // V1 連用形 + ゆく forms the literary compound-verb construction (散り+ゆく, 消え+ゆく, 暮れ+ゆく).
  // The generic VerbRenyokei→VerbShuushikei penalty (0.8) guards against false
  // splits, but ゆく/いく after 連用形 is grammatical and beats the NOUN fallback.
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && next.pos == core::PartOfSpeech::Verb &&
      (next.surface == "ゆく" || next.surface == "いく")) {
    bonus += cost::kStrongBonus;
  }

  return bonus;
}

// Passive/humble attachment, youon boundary, plural ら, らし→い, and suru-verb
// causative/mizenkei/imperative (させ, さ, せよ/しろ) rules.
float computePassiveCausativeBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float bonus{};  // value-init to 0

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

  // Bonus for て → いただき (humble auxiliary verb) pattern
  // E.g., 食べて+いただき+ます, して+いただけ+ます (MeCab-compatible split)
  // The て→い(AUX)→ただき path incorrectly splits いただき
  // いただく is a humble auxiliary verb that should not be split after て
  if (prev.surface == "て" && prev.extended_pos == core::ExtendedPOS::ParticleConj &&
      utf8::startsWith(next.surface, "いただ") && next.pos == core::PartOfSpeech::Verb) {
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
  // E.g., 食べ+させ+られ+た (not 食べ+さ+せ+られ+た)
  // Apply bonus when connecting to AuxCausative with surface starting with させ
  if ((prev.extended_pos == core::ExtendedPOS::VerbRenyokei || prev.extended_pos == core::ExtendedPOS::VerbMizenkei) &&
      next.extended_pos == core::ExtendedPOS::AuxCausative && utf8::startsWith(next.surface, "させ")) {
    bonus += cost::kVeryStrongBonus;
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

  // Surface-based bonus for VerbRenyokei → た (ichidan/irregular た-form)
  // E.g., 食べ+た, 来+た (MeCab-compatible split)
  // Must be surface == "た" to distinguish from て (particle)
  // Guard: require kanji or dict origin to prevent false verbs like まし(ましる)
  // from stealing た bonus over AUX_丁寧 path (参加してきました)
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && next.surface == "た" &&
      next.extended_pos == core::ExtendedPOS::AuxTenseTa &&
      (grammar::containsKanji(prev.surface) || prev.fromDictionary())) {
    bonus += cost::kVeryStrongBonus;
  }

  // Bonus for VerbRenyokei/VerbOnbinkei → たり/だり (parallel listing particle)
  // E.g., 食べ+たり+する, 飲ん+だり+食べ+たり+する
  // Without this, た(AuxTenseTa) wins over たり(ParticleConj) due to strong た bonus
  // Exclude pure hiragana onbin forms (ぴっ, ばっ) which are onomatopoeia, not verbs
  if ((prev.extended_pos == core::ExtendedPOS::VerbRenyokei || prev.extended_pos == core::ExtendedPOS::VerbOnbinkei) &&
      (next.surface == "たり" || next.surface == "だり") && next.extended_pos == core::ExtendedPOS::ParticleConj &&
      grammar::containsKanji(prev.surface)) {
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
    // Codepoint immediately before the trailing れ (3 bytes)
    std::string_view before(prev.surface.data(), prev.surface.size() - 3);
    auto before_cps = normalize::toCodepoints(before);
    if (!before_cps.empty() && grammar::verbTypeFromARowCodepoint(before_cps.back()) != grammar::VerbType::Unknown) {
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

  // Surface-based bonus for しよ → う (suru verb volitional)
  // MeCab: 勉強しよう → 勉強|しよ|う (しよ=verb volitional base, う=volitional aux)
  // This bonus ensures the split path (しよ|う) beats the merged path (し|よう)
  if (prev.surface == "しよ" && prev.pos == core::PartOfSpeech::Verb && next.surface == "う" &&
      next.extended_pos == core::ExtendedPOS::AuxVolitional) {
    bonus += cost::kVeryStrongBonus;
  }

  // The volitional auxiliary is realized as bare う only after an o-row mizenkei
  // (書こ+う, 泳ご+う, しよ+う; the ichidan form is the 2-mora 食べ+よう). A bare
  // う after any non-o-row verb ending is impossible Japanese — an a-row mizenkei
  // (つか of つく) or a u-row shuushikei (す of する) never takes the volitional う
  // — yet the spurious split would otherwise beat the real godan-wa verb
  // (つかう/使う, あらう/洗う, すう/吸う). Penalize so the whole-verb reading wins.
  // A single-mora AuxVolitional surface is necessarily bare う (the ichidan form
  // is the 2-mora よう), so the byte-length gate identifies it without a surface
  // string compare.
  if (next.extended_pos == core::ExtendedPOS::AuxVolitional && next.surface.size() == core::kJapaneseCharBytes &&
      prev.pos == core::PartOfSpeech::Verb && !grammar::endsWithORow(prev.surface)) {
    bonus += cost::kSevere;
  }

  return bonus;
}

// Negative auxiliaries (ない/ず/ね) and noun↔short-verb-renyokei disambiguation.
float computeNegativeAndNounVerbBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float bonus{};  // value-init to 0

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
    bonus += cost::kStrongBonus;
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
    bonus += cost::kModerateBonus;  // -0.5 to match PART_格→VERB cost
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

  // Penalty for single-char case particle → very short pure-hiragana verb pattern
  // E.g., が+おさ is likely mis-segmentation (should be がお+さん)
  // Valid patterns usually have longer verbs (3+ chars) or kanji stems
  // Single-char particles: が, を, に, へ, と, で, から, etc.
  // Only penalize very short verbs (2 chars or less) to avoid affecting なくし, etc.
  // Exception: "い" (いる renyokei) has specific bonus rule below for PART_格→い pattern
  if (prev.extended_pos == core::ExtendedPOS::ParticleCase &&
      prev.surface.size() <= core::kJapaneseCharBytes &&  // Single hiragana char (3 bytes in UTF-8)
      next.pos == core::PartOfSpeech::Verb && !next.fromDictionary() && grammar::isPureHiragana(next.surface) &&
      next.surface.size() <= 6 &&  // 2 chars or less (6 bytes in UTF-8)
      next.surface != "い") {      // Exclude い - has specific rule
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
  if (prev.pos == core::PartOfSpeech::Noun && next.surface == "かかる" &&
      next.extended_pos == core::ExtendedPOS::Determiner) {
    bonus += cost::kStrong;
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
      // Check if surface ends with exactly 1 hiragana (nominalized pattern)
      auto codepoints = normalize::toCodepoints(next.surface);
      if (!codepoints.empty() && kana::isHiraganaCodepoint(codepoints.back()) && codepoints.size() >= 2 &&
          normalize::isKanjiCodepoint(codepoints[codepoints.size() - 2])) {
        bonus += cost::kAlmostNever;
      }
    }
  }

  // Penalty for case particle → final particle pattern
  // E.g., を+な in をなくした should not split as を+な+くし+た
  // Final particles (な, ね, よ) don't follow case particles directly
  if (prev.extended_pos == core::ExtendedPOS::ParticleCase && next.extended_pos == core::ExtendedPOS::ParticleFinal) {
    bonus += cost::kAlmostNever;
  }

  return bonus;
}

// Prefix/adverb→short-verb, symbol→particle/aux/furigana, and で+も copula rules.
float computePrefixSymbolBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float bonus{};  // value-init to 0

  // Penalty for PREFIX → short pure-hiragana verb pattern
  // E.g., お+い in において should not happen (お is prefix, い is not a verb here)
  // Valid お+verb patterns: お待ち, お願い (longer, often with kanji)
  // Note: 「い」 is in L1 dictionary as verb renyokei, so don't check fromDictionary
  if (prev.pos == core::PartOfSpeech::Prefix && next.pos == core::PartOfSpeech::Verb &&
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

  // Penalty for SYMBOL → PARTICLE pattern (furigana in parentheses)
  // E.g., 東京（とうきょう） should not split と+う+きょう
  // Particles don't normally follow opening parentheses directly
  if (prev.pos == core::PartOfSpeech::Symbol && next.pos == core::PartOfSpeech::Particle) {
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
  // 彼女だけど → 彼女+だ+けど, 静かだから → 静か+だ+から, 危なそうだから → 危な+そう+だ+から.
  // Keyed on the だ onset rather than each surface so the rule generalizes across the set.
  if (next.extended_pos == core::ExtendedPOS::Conjunction && utf8::startsWith(next.surface, "だ") &&
      (prev.pos == core::PartOfSpeech::Noun || prev.pos == core::PartOfSpeech::Pronoun ||
       prev.pos == core::PartOfSpeech::Adverb || prev.pos == core::PartOfSpeech::Adjective ||
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

  // Penalty for て/で (ParticleConj) → single-char VerbRenyokei (い)
  // Progressive pattern: 食べて+い+ます should use い(AuxAspectIru), not い(VerbRenyokei)
  // This ensures て+いる patterns use the auxiliary form
  // Exception: たり/だり → し is valid (食べたり+し+てる)
  // Exception: み (みる auxiliary = "try") after て is valid (食べて+み+たい)
  if (prev.extended_pos == core::ExtendedPOS::ParticleConj && next.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      next.surface.size() <= 3 &&  // Single hiragana (3 bytes)
      grammar::isPureHiragana(next.surface) && prev.surface != "たり" && prev.surface != "だり" &&
      next.surface != "み") {
    bonus += cost::kAlmostNever;  // Strongly discourage
  }

  return bonus;
}

// Progressive/contracted て, dialectal やで, 付け-で formal noun, honorific
// renyokei (いたし/いただき), and い/た/だ auxiliary attachment rules.
float computeProgressiveHonorificBonus(const core::LatticeEdge& prev, const core::LatticeEdge& next) {
  float bonus{};  // value-init to 0

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
  // E.g., 高+すぎる, 美味し+すぎた (MeCab-compatible split)
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

}  // namespace

float Scorer::connectionCost(const core::LatticeEdge& prev, const core::LatticeEdge& next) const {
  float base_cost = bigramCost(prev.pos, next.pos);

  // ExtendedPOS bigram cost (replaces all check functions)
  float extended_cost = BigramTable::getCost(prev.extended_pos, next.extended_pos);

  // Surface-based bonus for VerbRenyokei → すぎ pattern
  // E.g., 読み+すぎる, 書き+すぎた, 食べ+すぎ (MeCab-compatible split)
  // The default VERB→VERB penalty should not apply to auxiliary verbs
  float surface_bonus = 0.0F;
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && utf8::startsWith(next.surface, "すぎ")) {
    surface_bonus = cost::kStrongBonus;
  }

  surface_bonus += computeVerbRenyokeiEarlyBonus(prev, next);
  surface_bonus += sc::compoundVerbSplitBonus(prev.extended_pos, prev.surface, next.extended_pos, next.surface);

  surface_bonus += computePassiveCausativeBonus(prev, next);

  surface_bonus += computeTaFormVolitionalBonus(prev, next);

  surface_bonus += computeNegativeAndNounVerbBonus(prev, next);

  surface_bonus += computeParticleDeterminerBonus(prev, next);

  surface_bonus += computePrefixSymbolBonus(prev, next);

  // Note: Removed penalty for Pronoun + でも patterns
  // MeCab behavior is context-dependent:
  // - "何でもいい" → keeps でも together (副助詞)
  // - "何でもあり" (standalone) → keeps でも together
  // - "何でもありだな" → splits で+も
  // This context-sensitivity can't be captured in bigram scorer.
  // Let other scoring mechanisms handle the distinction.

  surface_bonus += computeSuffixShortVerbBonus(prev, next);

  surface_bonus += computeProgressiveHonorificBonus(prev, next);

  surface_bonus += computeSugiFinalParticleBonus(prev, next);

  surface_bonus += computeBarePotentialRenyokeiPenalty(prev, next);

  // Note: "かも" is kept as single token per SuzumeUtils.pm normalization
  // (か+も → かも merge rule). No penalty for AUX → かも.

  // Penalty for ParticleFinal(か) → ADV(もし) in かもしれない pattern
  // "もし" is a valid adverb, but not in "かもしれない" context
  // This prevents か+もし+れ split, favoring か+も+しれ
  if (prev.extended_pos == core::ExtendedPOS::ParticleFinal && prev.surface == "か" &&
      next.pos == core::PartOfSpeech::Adverb && next.surface == "もし") {
    surface_bonus += cost::kVeryRare;
  }

  // Penalty for short hiragana verb mizenkei + ん pattern
  // E.g., が+おさ+ん should be がお+さん (name + honorific suffix)
  // Short hiragana verbs followed by ん are often mis-segmented names
  // Valid patterns like 押さ+ん (kanji verb) have non-hiragana stems
  // ん can be AUX_否定古 or PART_準体, both should be penalized
  if (prev.extended_pos == core::ExtendedPOS::VerbMizenkei && grammar::isPureHiragana(prev.surface) &&
      prev.surface.size() <= 6 &&  // 2 chars or less (6 bytes in UTF-8)
      next.surface == "ん") {
    surface_bonus += cost::kAlmostNever;
  }

  // Penalty for short pure-hiragana dict verb + ず (classical negative)
  // E.g., おか+ず should be おかず (noun), not おか (dict verb おく) + ず (aux)
  // Short pure-hiragana verbs + ず are likely false parses of nouns/adverbs
  // Long verbs (かかわら+ず) and kanji verbs (表さ+ず) are productive grammar
  // Lexicalized forms like 思わず have their own dict entries (ADV) that win anyway
  // Note: ん, ぬ, ざる, ざれ, ね excluded — common productive patterns
  // (ね is 已然形: せねば from する; まい carries its own AuxNegativeMai EPOS and
  // never matches this rule)
  if (prev.pos == core::PartOfSpeech::Verb && prev.fromDictionary() && grammar::isPureHiragana(prev.surface) &&
      prev.surface.size() <= 9 &&  // ≤3 hiragana chars (9 bytes)
      next.extended_pos == core::ExtendedPOS::AuxNegativeNu &&
      !utf8::equalsAny(next.surface, {"ん", "ぬ", "ざる", "ざれ", "ね"})) {
    surface_bonus += cost::kAlmostNever;
  }

  // Penalty for single-kanji noun + hiragana verb renyokei/onbinkei
  // E.g., 勘+違い should be 勘違い (compound noun), not 勘 (noun) + 違い (dict verb)
  // Single-kanji nouns rarely form valid noun+verb compounds with hiragana verbs
  // Exception: し (suru renyokei) is valid for サ変 pattern (得+し, 得する)
  // Exception: Katakana verbs (バズっ, ググっ) are valid after nouns (超バズった)
  // Exception: Kanji-initial verbs (本+買っ) are valid noun+verb (dropped を)
  // Exception: NounNumber quantity tokens (半 split off a duration-counter run)
  //            legitimately precede verbs directly (三時間|半|かかった)
  if (prev.pos == core::PartOfSpeech::Noun && prev.extended_pos != core::ExtendedPOS::NounNumber &&
      normalize::utf8Length(prev.surface) == 1 &&  // Single char
      next.pos == core::PartOfSpeech::Verb &&
      (next.extended_pos == core::ExtendedPOS::VerbRenyokei || next.extended_pos == core::ExtendedPOS::VerbOnbinkei) &&
      next.surface != "し" &&  // Exclude suru renyokei (サ変動詞パターン)
      !kana::isKatakanaCodepoint(utf8::decodeFirstChar(next.surface)) &&            // Exclude katakana verbs
      !suzume::normalize::isKanjiCodepoint(utf8::decodeFirstChar(next.surface))) {  // Exclude kanji verbs
    surface_bonus += cost::kVeryRare;
  }

  // Penalty for single-kanji NOUN → verbal auxiliary patterns
  // E.g., 合+う should be 合う (verb), not 合 (noun) + う (volitional)
  // E.g., 揺+れる should be 揺れる (verb), not 揺 (noun) + れる (passive)
  // Single-kanji nouns rarely take verbal auxiliaries directly
  // Exception: Single-kanji ichidan verb stems + causative させ (見+させる, etc.)
  if (prev.extended_pos == core::ExtendedPOS::Noun && prev.surface.size() == 3 &&  // Single kanji (3 bytes in UTF-8)
      (next.extended_pos == core::ExtendedPOS::AuxVolitional || next.extended_pos == core::ExtendedPOS::AuxPassive ||
       next.extended_pos == core::ExtendedPOS::AuxPotential || next.extended_pos == core::ExtendedPOS::AuxCausative ||
       next.extended_pos == core::ExtendedPOS::AuxClassicalBeshi)) {
    // Check if this is single-kanji ichidan + causative させ (should be allowed)
    bool is_ichidan_causative = false;
    if (next.extended_pos == core::ExtendedPOS::AuxCausative && utf8::startsWith(next.surface, "させ")) {
      is_ichidan_causative = verb_helpers::isSingleKanjiIchidanSurface(prev.surface);
    }
    if (!is_ichidan_causative) {
      surface_bonus += cost::kVeryRare;
    }
  }

  // Penalty for PARTICLE → もあり/もありだ verb candidates
  // "もあり" is mis-recognized as godan verb (もある) or suru verb (もありする)
  // In "何でもありだな", "もありだ" should be も+あり+だ, not もありする+た
  // This pattern only appears after particles (で in でもあり)
  if (prev.pos == core::PartOfSpeech::Particle && next.pos == core::PartOfSpeech::Verb &&
      utf8::equalsAny(next.surface, {"もあり", "もありだ", "もある", "もあっ"})) {
    surface_bonus += cost::kAlmostNever;
  }

  // Bonus for PART_格 → い (VerbRenyokei of いる)
  // E.g., 家にいた → 家+に+い+た (not 家+にいた)
  // "にいた" is mis-recognized as godan verb (にく) past tense
  // い is the renyokei of いる (to exist/be), very common after particles
  // Exclude と→い because という is a common determiner that should stay as one token
  if (prev.extended_pos == core::ExtendedPOS::ParticleCase && next.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      next.surface == "い" && prev.surface != "と") {  // Exclude と to protect という determiner
    surface_bonus += cost::kStrongBonus;
  }

  // Penalty for と → いう pattern to protect という determiner
  // E.g., という名前 → という+名前 (not と+いう+名前)
  // という is a common quotative determiner that should stay as one token
  if (prev.extended_pos == core::ExtendedPOS::ParticleCase && prev.surface == "と" &&
      next.extended_pos == core::ExtendedPOS::VerbRenyokei && (next.surface == "いう" || next.surface == "いっ")) {
    surface_bonus += cost::kUncommon;
  }

  // Bonus for VerbRenyokei → し (サ変 する renyokei)
  // E.g., お願いします → お+願い+し+ます (not お+願いし+ます)
  // "願いし" is mis-recognized as godan-sa verb (願いす)
  // This pattern is common for サ変複合動詞: 願い+する, 案内+する
  // Exclude single-char "い" which is いる renyokei (interferes with 上手い+し)
  // Exclude "で" which is でる renyokei (interferes with んでした → ん+でし+た)
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && next.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      next.surface == "し" && prev.fromDictionary() && prev.surface != "い" && prev.surface != "で") {
    surface_bonus += cost::kVeryStrongBonus;
  }

  // Penalty for kanji+sokuon+kanji NOUN → し(VerbRenyokei) pattern
  // E.g., 引っ越+し should be 引っ越し (compound verb), not NOUN + suru renyokei
  // These patterns are usually compound verbs registered in dictionary
  // The pattern: 漢字+っ+漢字 (kanji + sokuon + kanji) as NOUN → し(する連用形)
  if (prev.pos == core::PartOfSpeech::Noun && !prev.fromDictionary() &&
      next.extended_pos == core::ExtendedPOS::VerbRenyokei && next.surface == "し" &&
      prev.surface.size() >= 9 &&  // At least 3 chars (2 kanji + っ)
      utf8::contains(prev.surface, "っ")) {
    // Check if it's kanji+っ+kanji pattern
    bool has_sokuon_between_kanji = false;
    auto codepoints = normalize::toCodepoints(prev.surface);
    for (size_t i = 1; i + 1 < codepoints.size(); ++i) {
      if (codepoints[i] == U'っ' && suzume::normalize::isKanjiCodepoint(codepoints[i - 1]) &&
          suzume::normalize::isKanjiCodepoint(codepoints[i + 1])) {
        has_sokuon_between_kanji = true;
        break;
      }
    }
    if (has_sokuon_between_kanji) {
      surface_bonus += cost::kVeryRare;
    }
  }

  // Penalty for pure-hiragana dict NOUN → し(VerbRenyokei) pattern
  // E.g., はな+し should be はなし (verb), not はな(NOUN) + し(する連用形)
  // はな is a dict NOUN but not a suru-noun, so はな+し is not a valid suru compound
  // This does not affect kanji nouns (勉強+し is valid suru compound)
  // Use small penalty (0.08) to tip balance: はなし gap=0.013, なんし gap=0.102
  if (prev.pos == core::PartOfSpeech::Noun && prev.fromDictionary() &&
      next.extended_pos == core::ExtendedPOS::VerbRenyokei && next.surface == "し" &&
      grammar::isPureHiragana(prev.surface)) {
    surface_bonus += sc::kPenaltyHiraganaNounToSuruTip;
  }

  // Bonus for multi-kanji NOUN → せ(VerbMizenkei) sahen pattern
  // 認識+せ, 期待+せ: favors split over merged 認識せ/期待せ verb candidate
  // Only for 2+ kanji nouns (sahen-compatible), not single-kanji like 下+さ
  if (prev.pos == core::PartOfSpeech::Noun && next.extended_pos == core::ExtendedPOS::VerbMizenkei &&
      next.surface == "せ" && prev.surface.size() >= 6) {  // 2+ kanji = 6+ bytes in UTF-8
    surface_bonus += cost::kStrongBonus;
  }

  // Penalty for PART_準体(の) → で to prevent ので splitting
  // High penalty keeps ので merged in conjunctive use (寒いので出かけた)
  // For のではない pattern, the downstream で→は surface bonus overcomes this
  if (prev.extended_pos == core::ExtendedPOS::ParticleNo && prev.surface == "の" && next.surface == "で") {
    surface_bonus += cost::kVeryRare;  // 1.8
  }

  // Penalty for AuxCopulaDa(な) → し(PART_接続) pattern
  // な is the adnominal form of copula だ, normally followed by a noun (きれいな人)
  // な+し as copula+conjunction is invalid - this prevents はなし → は+な+し
  // The bigram AuxCopulaDa→ParticleConj bonus (-0.8) is for だ+し, not な+し
  // BUT: な+のに and な+ので are valid (嫌なのに, 嫌なので) - only penalize し
  if (prev.extended_pos == core::ExtendedPOS::AuxCopulaDa && prev.surface == "な" &&
      next.extended_pos == core::ExtendedPOS::ParticleConj && next.surface == "し") {
    surface_bonus += cost::kVeryRare;  // Cancel the -0.8 bonus and add penalty
  }

  // Penalty for AuxCopulaDa(で) → し pattern (VerbRenyokei or ParticleConj)
  // 本でした should be 本+でし+た, not 本+で+し+た
  // で as copula te-form followed by し is grammatically unusual
  // し can be recognized as VerbRenyokei (suru) or PARTICLE_接続 (parallel particle)
  // Neither is correct in this context - the でし is the renyokei of です copula
  // This ensures でし (AuxCopulaDesu renyokei) wins over で+し split
  if (prev.extended_pos == core::ExtendedPOS::AuxCopulaDa && prev.surface == "で" && next.surface == "し" &&
      (next.extended_pos == core::ExtendedPOS::VerbRenyokei || next.extended_pos == core::ExtendedPOS::ParticleConj)) {
    surface_bonus += cost::kAlmostNever;
  }

  // Penalty for PART_接続(し) → て pattern (PART_接続 or AUX_継続)
  // E.g., をなくして should be を+なくし+て, not を+なく+し+て
  // "し" as conjunctive particle (reason-listing) rarely followed by "て" directly
  // This prevents adjective renyokei (なく) + し (particle) + て from winning
  // over verb renyokei (なくし) + て pattern
  // Note: "て" can be either ParticleConj or AuxAspectIru (ている/てる aspect)
  if (prev.extended_pos == core::ExtendedPOS::ParticleConj && prev.surface == "し" && next.surface == "て" &&
      (next.extended_pos == core::ExtendedPOS::ParticleConj || next.extended_pos == core::ExtendedPOS::AuxAspectIru)) {
    surface_bonus += cost::kStrong;
  }

  // Penalty for ADJ_連用(なく) → VERB_連用(し) pattern
  // E.g., なくした should be なくし+た, not なく+し+た
  // "なくす" (to lose) is a distinct verb from "なく+する" (to make not exist)
  // The AdjRenyokei→VerbRenyokei bonus (-0.8) for 美しく+なり pattern
  // incorrectly applies to なく+し, causing over-split of なくす verb
  // This penalty cancels the bonus specifically for ない形容詞 + する pattern
  if (prev.extended_pos == core::ExtendedPOS::AdjRenyokei && prev.surface == "なく" &&
      next.extended_pos == core::ExtendedPOS::VerbRenyokei && next.surface == "し") {
    surface_bonus += cost::kRare;  // Cancel the -0.8 bonus
  }

  // Penalty for short hiragana VERB_連用 → し/き (single-char verb renyokei)
  // E.g., おかしを → おかし+を, not おか+し+を
  // Short verb renyokei (2-3 chars) followed by し or き often indicates
  // over-segmentation of a noun or longer verb
  // Exclude ば (valid conditional: よれ+ば), て (te-form), etc.
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && prev.surface.size() >= 6 &&
      prev.surface.size() <= 9 &&  // 2-3 hiragana
      (next.surface == "し" || next.surface == "き")) {
    // Check prev is all hiragana
    if (grammar::isPureHiragana(prev.surface) && (next.extended_pos == core::ExtendedPOS::VerbRenyokei ||
                                                  next.extended_pos == core::ExtendedPOS::ParticleConj)) {
      surface_bonus += cost::kStrong;
    }
  }

  // Penalty for negation PREFIX (非/不/無/未) → single-kanji NOUN
  // E.g., 非常 → 非|常 is wrong (非常 is a single word, not prefix+noun)
  // E.g., 不可能 → 不|可能 is wrong (不可能 is a single word)
  // But お|茶, ご|報告 are valid (honorific prefix + noun)
  // Only penalize negation prefixes followed by single kanji
  if (prev.extended_pos == core::ExtendedPOS::Prefix && sc::isNegationPrefix(prev.surface) &&
      next.extended_pos == core::ExtendedPOS::Noun && next.surface.size() == 3) {  // Single kanji (3 bytes in UTF-8)
    surface_bonus += cost::kAlmostNever;
  }

  // Penalty for AuxCopulaDa(で) → Symbol/EOS pattern
  // E.g., あとで。 should be NOUN+PART_格+。, not NOUN+AUX_断定+。
  // Copula 「で」 at sentence end is unusual; 格助詞「で」 is more natural
  // Note: 「だ」+Symbol is valid (学生だ。), so only penalize 「で」
  if (prev.extended_pos == core::ExtendedPOS::AuxCopulaDa && prev.surface == "で" &&
      next.extended_pos == core::ExtendedPOS::Symbol) {
    surface_bonus += cost::kStrong;
  }

  // Penalty for NOUN → で(AUX_断定): copula で after noun is uncommon
  // Most NOUN+copula uses だ directly; で is mainly in で+ある/で+は/で+も
  // This counteracts the Noun→AuxCopulaDa bigram bonus (-0.5) for で
  // E.g., あとで, 爆速で, きっかけで, 電車で → NOUN+PART_格 preferred
  // Note: NOUN→だ(AUX_断定) is NOT affected (学生だ is correct)
  if ((prev.pos == core::PartOfSpeech::Noun || prev.pos == core::PartOfSpeech::Pronoun) &&
      next.extended_pos == core::ExtendedPOS::AuxCopulaDa && next.surface == "で") {
    surface_bonus += cost::kRare;  // 1.0: must exceed kModerateBonus (-0.5)
  }

  // Bonus for で(AuxCopulaDa) → ある/あっ/あろ (formal copula pattern)
  // のである, ではある, であった are standard literary/formal expressions
  // AuxCopulaDa→VerbShuushikei has kMinor (0.5) bigram + kVeryRare (1.8) の→で surface
  // Total penalty to overcome: ~2.3
  if (prev.extended_pos == core::ExtendedPOS::AuxCopulaDa && prev.surface == "で" &&
      next.pos == core::PartOfSpeech::Verb && utf8::equalsAny(next.surface, {"ある", "あっ", "あろ", "あり"})) {
    surface_bonus += sc::kBonusDoubleVeryStrong;  // -3.2 to overcome ~2.3 total penalty
  }

  // Bonus for で(AuxCopulaDa) → は(ParticleTopic) surface connection
  // Helps のではない/のではなく pattern split の+で+は despite の→で penalty (1.8)
  // Only fires when で is split from の (not when ので stays as single token)
  // Safe: で(AuxCopulaDa)+は is always correct when it occurs (ではない, ではなく)
  if (prev.extended_pos == core::ExtendedPOS::AuxCopulaDa && prev.surface == "で" &&
      next.extended_pos == core::ExtendedPOS::ParticleTopic && next.surface == "は") {
    surface_bonus += cost::kVeryStrongBonus;  // -1.6
  }

  // Penalty for で(VERB_連用 of 出る) → ない(AUX_否定): copula でない is more common
  // でない = "is not" (copula) vs "doesn't come out" (verb 出る)
  // Without context (を/から before で), copula interpretation should win
  // E.g., 正式でない, 必要でない → で(AUX_断定) + ない(AUX_否定)
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && prev.surface == "で" &&
      next.extended_pos == core::ExtendedPOS::AuxNegativeNai) {
    surface_bonus += cost::kStrong;
  }

  // Bonus for ParticleTopic → なかろ (AuxNegativeNai volitional stem)
  // Helps は+なかろ+う split beat fake godan-ra verb candidate なかろう
  // Only targets なかろ — other forms (ない, なかっ, なけれ) can be ADJ or AUX
  if (prev.extended_pos == core::ExtendedPOS::ParticleTopic && next.extended_pos == core::ExtendedPOS::AuxNegativeNai &&
      next.surface == "なかろ") {
    surface_bonus += cost::kModerateBonus;
  }

  // Penalty for NOUN/PRON → で(VERB_連用 of 出る): verb で after noun is rare
  // Most NOUN+で patterns use particle or copula, not verb 出る
  // E.g., あとで, 爆速で → NOUN+PART_格, not NOUN+VERB(出る)
  if ((prev.pos == core::PartOfSpeech::Noun || prev.pos == core::PartOfSpeech::Pronoun) &&
      next.extended_pos == core::ExtendedPOS::VerbRenyokei && next.surface == "で") {
    surface_bonus += cost::kRare;  // 1.0: must exceed NOUN→VERB_連用 bonus
  }

  // Penalty for VerbRenyokei → で (any interpretation)
  // Ichidan te-form only uses て (食べ+て, 見+て), NOT で
  // Godan te-form with で uses onbinkei (飲ん+で, 読ん+で), not renyokei
  // AUX_断定(で) only attaches to nouns/na-adj (静かで, 学生で), not verbs
  // Without this, kanji+り nouns like 夏祭り get falsely parsed as VERB_連用
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && utf8::startsWith(next.surface, "で")) {
    if (next.extended_pos == core::ExtendedPOS::ParticleConj) {
      surface_bonus += cost::kMinor;  // +0.5 to cancel the -0.5 bigram bonus
    } else if (next.extended_pos == core::ExtendedPOS::AuxCopulaDa) {
      surface_bonus += cost::kMinor;  // Penalize invalid VERB_連用→断定
    }
  }

  // Penalty for single-kanji ADJ_語幹 → AuxGaru
  // E.g., 挙+がっ should be 挙がっ (verb onbin), not 挙(adj stem)+がっ(がる suffix)
  // Multi-char adj stems + がる are valid (可愛+がる, 怖+がる via dict)
  // But single-kanji adj stems are usually false positives from the candidate generator
  if (prev.extended_pos == core::ExtendedPOS::AdjStem && suzume::normalize::utf8Length(prev.surface) == 1 &&
      next.extended_pos == core::ExtendedPOS::AuxGaru) {
    surface_bonus += cost::kStrong;
  }

  // Penalty for single-hiragana VERB_連用 → を(PART_格)
  // E.g., おかしを → おかし+を, not おか+し+を
  // Single hiragana verb renyokei (し, き, み, etc.) rarely takes を directly
  // Nominalized verb renyokei like 読み, 書き take を (読みを深める) but those
  // are multi-char and should be recognized as NOUN, not single-char VERB_連用
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      prev.surface.size() == 3 &&  // Single char (3 bytes = hiragana/katakana)
      next.extended_pos == core::ExtendedPOS::ParticleCase && next.surface == "を") {
    // Check if single char is hiragana
    auto decoded = normalize::utf8::decode(prev.surface);
    auto it = decoded.begin();
    if (it != decoded.end() && kana::isHiraganaCodepoint(*it)) {
      surface_bonus += cost::kStrong;
    }
  }

  // Penalty for で(VerbRenyokei of 出る) → Particle (except て)
  // で as 出る renyokei should only be followed by auxiliaries (たい, ます) or て
  // 彼女でも → 彼女+で(PART)+も, not 彼女+で(VERB 出る)+も
  // But でて → で+て is valid (出る renyokei + te-form)
  // The verb interpretation is only valid before auxiliaries like たい/ます or て
  if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && prev.surface == "で" &&
      next.pos == core::PartOfSpeech::Particle && next.surface != "て") {  // Exclude て to allow でて → で+て split
    surface_bonus += cost::kStrong;
  }

  // Bonus for dictionary NOUN → dictionary NOUN connection
  // E.g., 明日+雨, 毎日+電車 should beat 明日雨, 毎日電車 (kanji_seq)
  // When both nouns are in dictionary, the split path is more accurate
  // This helps time nouns (明日, 今日, 毎日) + common nouns (雨, 電車)
  if (prev.pos == core::PartOfSpeech::Noun && prev.fromDictionary() && next.pos == core::PartOfSpeech::Noun &&
      next.fromDictionary() && !prev.isFormalNoun() && !next.isFormalNoun()) {
    surface_bonus += cost::kModerateBonus;  // Prefer dict+dict split over kanji_seq
  }

  // Penalty for identical hiragana NOUN → NOUN sequence
  // E.g., もも|もも is less likely than もも|も|もも (particle between)
  // This prevents すもももも... from being split as もも|もも|もの
  if (prev.pos == core::PartOfSpeech::Noun && next.pos == core::PartOfSpeech::Noun && prev.surface == next.surface &&
      grammar::isPureHiragana(prev.surface)) {
    surface_bonus += cost::kVeryRare;
  }

  // Bonus for dictionary hiragana NOUN → single-char particle も/の pattern
  // E.g., すもも|も|もも should beat すもも|もも (particle interpretation)
  // E.g., もも|の|うち should beat もの|うち (particle interpretation)
  // This helps famous test sentence: すもももももももものうち
  if (prev.pos == core::PartOfSpeech::Noun && prev.fromDictionary() && grammar::isPureHiragana(prev.surface) &&
      next.pos == core::PartOfSpeech::Particle && (next.surface == "も" || next.surface == "の")) {
    surface_bonus += cost::kModerateBonus;
  }

  // Penalty for NOUN/PRON → pure-hiragana VERB_た形 (non-dict) pattern
  // E.g., 家にいた should be 家+に+い+た, not 家+にいた
  // E.g., ここにいた should be ここ+に+い+た, not ここ+にいた
  // "にいた" is mis-recognized as godan verb (にく) past tense
  // Pure-hiragana た-form verbs after NOUN/PRON are typically particle+aux sequences
  // Valid kanji+hiragana た-forms like 食べた are not affected (not pure hiragana)
  if ((prev.pos == core::PartOfSpeech::Noun || prev.pos == core::PartOfSpeech::Pronoun) &&
      next.extended_pos == core::ExtendedPOS::VerbTaForm && !next.fromDictionary() &&
      grammar::isPureHiragana(next.surface) &&
      next.surface.size() <= core::kFourJapaneseCharBytes) {  // 4 chars or less (12 bytes in UTF-8)
    surface_bonus += cost::kVeryRare;
  }

  // Bonus for か (particle) → dictionary verb mizenkei
  // In quotative patterns like かどうか分からない, か is followed by verb directly
  // Override the PART_終→VERB_未然 penalty for dictionary verbs
  if (prev.surface == "か" && prev.extended_pos == core::ExtendedPOS::ParticleFinal &&
      next.extended_pos == core::ExtendedPOS::VerbMizenkei && next.fromDictionary()) {
    surface_bonus += cost::kStrongBonus;  // -0.8 to reduce the 1.8 penalty
  }

  // Penalty for single-kanji NOUN → pure-hiragana VERB_未然 (non-dict)
  // E.g., 分+から should be 分から (single verb), not 分(NOUN) + から(VERB かる)
  // When a dictionary entry exists for combined form, penalize the split
  if (prev.pos == core::PartOfSpeech::Noun && normalize::utf8Length(prev.surface) == 1 &&  // Single char
      grammar::isAllKanji(prev.surface) && next.extended_pos == core::ExtendedPOS::VerbMizenkei &&
      !next.fromDictionary() && grammar::isPureHiragana(next.surface)) {
    surface_bonus += cost::kVeryRare;  // Penalize split to favor combined dict verb
  }

  // Penalty for demonstrative-based CONJ → kanji VERB pattern
  // E.g., それで帰った should be それ+で+帰っ+た, not それで(CONJ)+帰った
  // Demonstrative-origin conjunctions (それで, そこで, ここで) can be split
  // when followed directly by kanji verb (no comma/pause)
  // "それで" as pure conjunction prefers comma/pause before verb
  // But "それでございます" (それで+ござっ) is valid - ござっ is honorific
  // Apply to both VerbOnbinkei (帰っ) and VerbTaForm (帰った)
  if (prev.pos == core::PartOfSpeech::Conjunction &&
      (prev.surface == "それで" || prev.surface == "そこで" || prev.surface == "ここで") &&
      (next.extended_pos == core::ExtendedPOS::VerbOnbinkei || next.extended_pos == core::ExtendedPOS::VerbTaForm) &&
      next.surface.size() >= 3 &&  // At least 1 kanji (3 bytes)
      grammar::isAllKanji(next.surface.substr(0, 3)) &&
      !utf8::startsWith(next.surface, "ござ")) {  // Exclude honorific ござる
    surface_bonus += cost::kAlmostNever;
  }

  // Bonus for proper name sequence: Family → Given (姓→名)
  // E.g., 優木(FAMILY) + せつ菜(GIVEN) should strongly prefer staying together
  if (prev.extended_pos == core::ExtendedPOS::NounProperFamily &&
      next.extended_pos == core::ExtendedPOS::NounProperGiven) {
    surface_bonus += cost::kStrongBonus;  // -2.5 bonus
  }

  // Penalty for single-kana verb renyokei after adverb
  // Single-kana renyokei (で=出る, し=する) are ambiguous with copula/particles.
  // After adverbs, copula/particle interpretation dominates (それほどで+も+ない)
  // Exception: dict verbs (し=する) are valid after onomatopoeia ADV (じめじめ+し+た)
  // Exception: kanji verbs (見, 寝, 出) are unambiguous and valid after adverbs (初めて+見+た)
  if (prev.pos == core::PartOfSpeech::Adverb && next.extended_pos == core::ExtendedPOS::VerbRenyokei &&
      next.surface.size() <= 3 &&               // Single kana (3 bytes)
      grammar::isPureHiragana(next.surface) &&  // Only hiragana (で, し), not kanji (見, 出)
      !core::hasFlag(next.flags, core::EdgeFlags::FromDictionary)) {
    surface_bonus += cost::kVeryRare;
  }

  // Penalty for non-て/で particle/verb before い/いる auxiliary (AuxAspectIru)
  // AuxAspectIru (い/いる) requires て-form as prerequisite: V連用+て+いる
  // VERB_連用+い directly (し+い) or PART_接続(し)+い are grammatically invalid
  // Note: て/で themselves also have AuxAspectIru EPOS, so exclude them as next
  // Fixes: 一番美+し+い → 一番+美しい (wrongly split adjective 美しい)
  if (next.extended_pos == core::ExtendedPOS::AuxAspectIru && next.surface != "て" && next.surface != "で") {
    if (prev.extended_pos == core::ExtendedPOS::ParticleConj && prev.surface != "て" && prev.surface != "で") {
      surface_bonus += cost::kAlmostNever;
    } else if (prev.extended_pos == core::ExtendedPOS::VerbRenyokei && prev.surface != "て" && prev.surface != "で") {
      surface_bonus += cost::kAlmostNever;
    }
  }

  // Bonus for Noun → dict i-adjective (AdjBasic)
  // Dict adjectives are verified words — favor NOUN+ADJ split over false verb paths
  // e.g., 一番+美しい(dict ADJ) should beat 一番+美しい(false VERB)
  // Only for dict adjectives to avoid boosting false adj candidates (e.g., 払い)
  if (prev.pos == core::PartOfSpeech::Noun && next.extended_pos == core::ExtendedPOS::AdjBasic &&
      next.fromDictionary()) {
    surface_bonus += cost::kModerateBonus;
  }

  // Penalty for conjunction directly after a bare single-char token (keeps verb stems
  // 醒/覚/冷 from splitting before まして). Symbol prev excluded so punctuation may
  // precede a conjunction (雨、しかし…); codepoint count stays correct for 4-byte kanji.
  if (next.pos == core::PartOfSpeech::Conjunction && prev.pos != core::PartOfSpeech::Symbol &&
      normalize::utf8Length(prev.surface) == 1) {
    surface_bonus += cost::kAlmostNever;
  }

  float total = base_cost + extended_cost + surface_bonus;

  SUZUME_DEBUG_VERBOSE_BLOCK {
    SUZUME_DEBUG_STREAM << "[CONN] \"" << prev.surface << "\" (" << core::posToString(prev.pos) << "/"
                        << core::extendedPosToString(prev.extended_pos) << ") → \"" << next.surface << "\" ("
                        << core::posToString(next.pos) << "/" << core::extendedPosToString(next.extended_pos) << "): "
                        << "bigram=" << base_cost << " epos_adj=" << extended_cost;
    if (surface_bonus != 0.0F) {
      SUZUME_DEBUG_STREAM << " surface_bonus=" << surface_bonus;
    }
    if (extended_cost != 0.0F) {
      // Show rule name: PrevEPOS→NextEPOS
      SUZUME_DEBUG_STREAM << " (rule=" << core::extendedPosToString(prev.extended_pos) << "→"
                          << core::extendedPosToString(next.extended_pos) << ")";
    } else if (surface_bonus == 0.0F) {
      SUZUME_DEBUG_STREAM << " (default)";
    }
    SUZUME_DEBUG_STREAM << " total=" << total << "\n";
  }

  return total;
}

}  // namespace suzume::analysis
