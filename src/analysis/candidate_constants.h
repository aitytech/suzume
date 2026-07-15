#ifndef SUZUME_ANALYSIS_CANDIDATE_CONSTANTS_H_
#define SUZUME_ANALYSIS_CANDIDATE_CONSTANTS_H_

#include <cstddef>

// =============================================================================
// Candidate Generation Constants
// =============================================================================
// This file centralizes all penalty and bonus values used in candidate
// generation (join_candidates.cpp and split_candidates.cpp).
//
// Naming convention:
//   kPenalty* - increases cost (discourages pattern)
//   kBonus*   - decreases cost (encourages pattern, note: negative values)
//
// These constants determine which candidate splits/joins are preferred
// during lattice construction, before Viterbi path selection.
// =============================================================================

namespace suzume::analysis::candidate {

// =============================================================================
// Join Candidate Constants (join_candidates.cpp)
// =============================================================================

// Neutral origin-confidence for lattice edges that carry no inflection confidence
// (debug-only field). Named so callers needing an explicit ExtendedPOS argument can
// reach it positionally without a raw score literal.
constexpr float kNoOriginConfidence = 0.0F;

// Last-resort single-character edge used to keep the lattice connected.
constexpr float kFallbackCandidateCost = 5.0F;

// Colloquial emphatic suffixes (やばいっ, きたあああ).
constexpr size_t kEmphaticMinRepeatedVowels = 2;
constexpr float kEmphaticRepeatedVowelBonus = -0.5F;
constexpr float kEmphaticRepeatedVowelLengthPenalty = 0.05F;
constexpr float kEmphaticCharacterPenalty = 0.3F;

// High origin-confidence for a rule-derived candidate whose surface context makes
// the analysis effectively unambiguous (e.g. the gated 来る mizenkei こ before a
// ない-family negative).
constexpr float kHighOriginConfidence = 0.9F;

// Compound verb bonus (連用形 + 補助動詞)
// E.g., 読み+終わる, 書き+始める
constexpr float kCompoundVerbBonus = -0.8F;

// Verified Ichidan verb bonus
// Applied when join creates a valid ichidan verb pattern
constexpr float kVerifiedV1Bonus = -0.3F;

// Verified noun in compound bonus
// Applied when noun component is verified in dictionary
constexpr float kVerifiedNounBonus = -0.3F;

// Te-form + auxiliary bonus
// E.g., 食べて+いる, 走って+しまう
constexpr float kTeFormAuxBonus = -0.8F;

// =============================================================================
// Split Candidate Constants (split_candidates.cpp)
// =============================================================================

// Alpha + Kanji split bonus
// E.g., Web開発, AI研究
constexpr float kAlphaKanjiBonus = -0.3F;

// Alpha + Katakana split bonus
// E.g., APIリクエスト
constexpr float kAlphaKatakanaBonus = -0.3F;

// Digit + Kanji split bonuses
// E.g., 5分, 3月, 100人
constexpr float kDigitKanji1Bonus = -0.2F;   // 1-kanji counter
constexpr float kDigitKanji2Bonus = -0.2F;   // 2-kanji counter
constexpr float kDigitKanji3Penalty = 0.5F;  // 3+ kanji (rare, likely wrong)

// Dictionary word split bonus
// Applied when split creates a dictionary-verified word
constexpr float kDictSplitBonus = -0.5F;

// Base cost for split candidates
// Added to all split candidates as baseline cost
constexpr float kSplitBaseCost = 1.0F;

// Quantified-time + relational-suffix split bonus (三日|後, 十年|前, 数日|後, 半年|後)
// A temporal counter (日/年/分…) followed by 後/前 is always compositional; the
// whole run is otherwise emitted as one kanji_seq unknown word (三日後), so this
// discounts the left counter token enough for the split path to win over the
// merged token while the standalone 後/前 comes from the single-kanji candidate.
// Strong enough to also beat a productive-prefix join (半 in 半年後 → 半+年後 at 0.4);
// only ever applied when 後/前 follows a temporal counter, so it cannot over-split.
constexpr float kCounterRelationSplitBonus = -1.2F;

// Counter-quantity 半 suffix token (三時間|半, 五分|半). Zero defers to the
// NounNumber category cost; the discount that lets the split beat the merged
// kanji_seq run lives on the left counter token (kCounterRelationSplitBonus).
// The candidate exists to carry the NounNumber EPOS, marking 半 as a quantity
// noun so connection scoring can distinguish it from an ordinary single-kanji
// noun in front of a hiragana verb (三時間|半|かかった).
constexpr float kCounterHalfSuffixCost = 0.0F;

// Noun + Verb split bonus
// E.g., 勉強+する, 説明+する
constexpr float kNounVerbSplitBonus = -1.0F;

// Post-particle noun promotion penalty (は|たばこ|を: たばこ as a Noun)
// A non-particle-initial hiragana run bracketed by genuine particles (私は…を) is
// far more likely a content noun than the merged particle-blob (はたばこ). Emit a
// parallel Noun candidate exempt from the exceeds_dict_length penalty; this small
// positive cost keeps it just below a real dictionary/verb reading so it wins only
// when nothing better spans the bracket, never shattering (た|ば|こ).
constexpr float kPostParticleNounPenalty = 0.4F;

// Verified verb in split bonus
// Applied when verb component is verified in dictionary
constexpr float kVerifiedVerbBonus = -0.8F;

// =============================================================================
// Adjective Candidate Constants (adjective_candidates.cpp)
// =============================================================================

// I-adjective conjugation form split bonuses
// Applied when generating split candidates for MeCab-compatible output.
// E.g., 美味しくない → 美味しく + ない

// く形 split bonus (kanji i-adjectives: 美味しく, 高く)
constexpr float kAdjKuSplitBonus = -0.5F;
// く形 split bonus (hiragana i-adjectives: しんどく, うまく)
constexpr float kAdjKuSplitBonusWeak = -0.3F;
// かっ形 split bonus (past tense: 美味しかっ + た)
constexpr float kAdjKattSplitBonus = -0.1F;
// け形 split bonus (conditional: 美味しけれ + ば)
constexpr float kAdjKeSplitBonus = -0.1F;
// 語幹 split bonus (stem + そう: 美味し + そう)
constexpr float kAdjStemSplitBonus = -0.5F;

// Base costs for confidence-based adjective cost formulas
// Formula: base + (1.0 - confidence) * scale

// Kanji i-adjective candidates (美味しい, 恐ろしい)
constexpr float kKanjiAdjBaseCost = 0.2F;
constexpr float kKanjiAdjConfScale = 0.3F;
// Hiragana i-adjective candidates (すごい, うまい)
constexpr float kHiraganaAdjBaseCost = 0.25F;
constexpr float kHiraganaAdjConfScale = 0.5F;
// Adjective stem candidates (美味し + そう, 高 + さ)
constexpr float kAdjStemBaseCost = -0.8F;
constexpr float kAdjStemConfScale = 0.2F;

// Single-kanji i-adjective candidate costs
// Moderate costs so these beat competing verb candidates.
constexpr float kSingleKanjiICost = 0.35F;   // 高い, 辛い (in-context 甘いもの)
constexpr float kSingleKanjiKuCost = 0.52F;  // 甘く, 辛く renyokei

// Minimum inflection confidence to accept an i-adjective candidate
// (kanji/katakana paths and しそう stem validation)
constexpr float kIAdjConfMin = 0.5F;

// Debug confidence recorded on the generated 未然形 (かろ) conjectural candidate
constexpr float kIAdjKaroConfidence = 0.8F;

// Minimum verb hypothesis confidence to treat a ゆく/いく prefix as 連用形
constexpr float kV1PrefixMinConfidence = 0.3F;

// Minimum inflection confidence for a prefix-compound's second kanji to count as
// a verb stem and thus suppress the compound (今食べてる → 今|食べ|てる)
constexpr float kPrefixCompoundVerbStemConf = 0.5F;

// Compound adjective (2-kanji stem: 薄暗い, 物悲しく)
constexpr float kCompoundAdjConfMin = 0.3F;   // minimum inflection confidence
constexpr float kCompoundAdjBaseCost = 0.5F;  // base cost for generated candidate

// Na-adjective candidate costs
constexpr float kNaAdjYakaCost = 0.2F;  // やか/らか/か + な (華やかな, 静かな)
constexpr float kNaAdjTekiCost = 1.5F;  // 的 suffix (論理的) — high to prefer NOUN+的+な
constexpr float kNaAdjStemCost = 0.5F;  // kanji compound + な (獰猛な)

// Hiragana i-adjective confidence thresholds
constexpr float kHiraAdjConfMin = 0.55F;        // default hiragana-only
constexpr float kHiraAdjConfParticle = 0.50F;   // particle-starting sequences
constexpr float kHiraAdjConfProlonged = 0.40F;  // prolonged sound mark (すごーい)

// Hiragana i-adjective cost adjustments
constexpr float kProlongedSoundBonus = -0.1F;   // colloquial すごーい, やばーい
constexpr float kLongParticleAdjBonus = -0.5F;  // 5+ char particle-starting (はなはだしい)

// Full-reduplication 〜しい adjective bonus (馬鹿馬鹿しい, バカバカしい, ばかばかしい)
// A spelled-out doubled stem is otherwise pre-empted by onomatopoeia ADV candidates
// (aa_doubled -1.0 / abab_pattern 0.1) plus a split-off しい tail, so the whole-word
// adjective needs a strong bonus to win. Applied only when the surface matches the
// reduplicated head (see verb_helpers::isReduplicatedShiiAdjectiveHead), where the
// adjective reading is decisively correct across all three scripts.
constexpr float kReduplicatedShiiAdjBonus = -0.8F;

// Garu-connection adjective stem (高すぎる, 怖がる)
constexpr float kGaruAdjConfMin = 0.35F;            // minimum stem+い validity confidence
constexpr float kDictFallbackAdjConfidence = 0.5F;  // assumed confidence for dict fallback (可愛い)

// Minimum adj-over-verb confidence margin for non-dict しい stem split (話し vs 難し)
constexpr float kAdjVerbConfDiffMin = 0.15F;

// =============================================================================
// Verb Candidate Constants (verb_candidates_kanji.cpp, verb_candidates_hiragana.cpp)
// =============================================================================

// Shared cost values for verb candidate generation
namespace verb_cost {
// Standard bonus for verb candidates (mizenkei, passive, etc.)
constexpr float kStandardBonus = -0.5F;
// Moderate bonus for verb candidates (extended/te-aux sokuonbin)
constexpr float kModerateBonus = -0.3F;
// Strong bonus for verb candidates (ichidan renyokei, te/ta forms)
constexpr float kStrongBonus = -0.8F;
// Weak penalty for uncertain verb patterns (passive, causative, zu-form)
constexpr float kWeakPenalty = 0.1F;
// Bonus for a kanji dict-verb imperative/kateikei standing sentence-final (書け, 読め, 止まれ).
// A bare え-row form terminating a clause is the imperative (命令形) of the base verb (読め→読む);
// the potential-verb reading (読める) is a distinct word that needs the full surface or a
// continuation (読めます/読めば). Applied only when no auxiliary/ば continuation follows, so
// 読める/走れます/止まれる are untouched. Sized to beat both the spurious potential-verb renyokei
// candidate (~-0.17) and the 未然+受身れ split (止ま+れ) that otherwise win over the single token.
constexpr float kImperativeFinalBonus = -0.8F;
}  // namespace verb_cost

// =============================================================================
// Adjective Cost Adjustment Constants (adjective_candidates.cpp)
// =============================================================================

// Extended cost for adjective stem candidates (dict and non-dict)
// Used for stem+そう, stem splits where confidence is high
constexpr float kAdjStemExtCost = -1.2F;

// Strong penalty to force MeCab-compatible adjective splits
// Applied to compound adj, く+なる, という, まい patterns
constexpr float kAdjSplitForcePenalty = 2.0F;

// Moderate penalty for uncertain adjective patterns
// Applied to unconfirmed さ nominalization and らしい conjecture
constexpr float kAdjModeratePenalty = 1.5F;

}  // namespace suzume::analysis::candidate

#endif  // SUZUME_ANALYSIS_CANDIDATE_CONSTANTS_H_
