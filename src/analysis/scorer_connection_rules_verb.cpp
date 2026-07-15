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

}  // namespace suzume::analysis::connection_rules
