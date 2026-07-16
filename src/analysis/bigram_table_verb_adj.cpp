#include "bigram_table_internal.h"

namespace suzume::analysis::bigram_rules {

using EPOS = core::ExtendedPOS;
namespace cost = bigram_cost;

void setVerbAndAdjectiveCosts(BigramMatrix& t) {
  // =========================================================================
  // Verb Forms → Auxiliaries (Core Grammar)
  // =========================================================================

  // VerbRenyokei → AuxTenseMasu (食べ+ます) - strong bonus
  setCell(t, EPOS::VerbRenyokei, EPOS::AuxTenseMasu, cost::kStrongBonus);

  // VerbRenyokei → AuxDesireTai (食べ+たい) - strong bonus
  setCell(t, EPOS::VerbRenyokei, EPOS::AuxDesireTai, cost::kStrongBonus);

  // VerbRenyokei → AuxHonorific (書き+なさい) - strong bonus for polite imperative
  setCell(t, EPOS::VerbRenyokei, EPOS::AuxHonorific, cost::kStrongBonus);

  // VerbMizenkei → AuxNegativeNai (食べ+ない for ichidan) - moderate bonus
  setCell(t, EPOS::VerbMizenkei, EPOS::AuxNegativeNai, cost::kModerateBonus);

  // VerbRenyokei → AuxNegativeNai (しれ+ない for ichidan same-form mizen/renyokei)
  // Ichidan verbs have same form for mizen and renyokei (e.g., しれ from しれる)
  // This helps かもしれない → かも+しれ+ない over かも+し+れ+ない
  setCell(t, EPOS::VerbRenyokei, EPOS::AuxNegativeNai, cost::kStrongBonus);

  // VerbRenyokei → AdjStem (食べ+な, でき+な) - minor bonus. The negative ない is stored
  // as an adjective stem (な/ない) and attaches to a verb 未然形, which shares the ichidan
  // renyokei surface. Overrides the default cross-category penalty so the negative reading
  // 食べ + な(ない) + さ + そう wins over the 断定 copula な, without over-splitting でき.
  setCell(t, EPOS::VerbRenyokei, EPOS::AdjStem, cost::kMinorBonus);

  // VerbMizenkei → AuxNegativeNu (くだら+ん contracted negative) - moderate bonus
  setCell(t, EPOS::VerbMizenkei, EPOS::AuxNegativeNu, cost::kModerateBonus);

  // VerbRenyokei → AuxNegativeNu (消え+ぬ classical negative)
  // Ichidan verbs have same form for mizen and renyokei (e.g., 消え from 消える)
  // This helps 消えぬ炎 → 消え+ぬ+炎 over 消えぬ+炎
  setCell(t, EPOS::VerbRenyokei, EPOS::AuxNegativeNu, cost::kModerateBonus);

  // まい (negative volitional) connection grammar:
  // - Godan 終止形 + まい (行く+まい, なる+まい)
  // - Ichidan 未然形 + まい (食べ+まい; surfaces carry VerbRenyokei EPOS)
  // - する/来る 未然形 + まい (し+まい, こ+まい)
  setCell(t, EPOS::VerbShuushikei, EPOS::AuxNegativeMai, cost::kModerateBonus);
  setCell(t, EPOS::VerbRenyokei, EPOS::AuxNegativeMai, cost::kModerateBonus);
  setCell(t, EPOS::VerbMizenkei, EPOS::AuxNegativeMai, cost::kModerateBonus);

  // VerbMizenkei → AuxPassive (食べ+られる) - moderate bonus
  setCell(t, EPOS::VerbMizenkei, EPOS::AuxPassive, cost::kModerateBonus);

  // VerbRenyokei → AuxPassive (知らせ+られ) - strong bonus
  // Ensures 知らせられた → 知らせ+られ+た over 知ら+せ+られ+た
  // The ichidan causative verb (知らせる) renyokei should connect to passive
  setCell(t, EPOS::VerbRenyokei, EPOS::AuxPassive, cost::kStrongBonus);

  // VerbRenyokei → AuxPotential (し+え, し+える) - strong bonus
  // Literary potential 得る: 看過しえない, 理解しえた, 想像しうる
  // Must be strong to beat single_kanji_ichidan_polite VERB path for 得
  setCell(t, EPOS::VerbRenyokei, EPOS::AuxPotential, cost::kStrongBonus);

  // The connective copula joins coordinated/predicative adjectives
  // (静かで美しい). Without this, the generic copula→noun preference can make
  // a structurally complete i-adjective lose to an unknown noun homograph.
  setCell(t, EPOS::AuxCopulaDa, EPOS::AdjBasic, cost::kModerateBonus);

  // AuxPotential → AuxNegativeNai (え+ない, 得+ない) - extra strong bonus
  // Literary potential + negation: 看過しえない, 解決し得ない
  // Needs extra strength to overcome base AUX→AUX category penalty (0.3)
  setCell(t, EPOS::AuxPotential, EPOS::AuxNegativeNai, cost::kExtraStrongBonus);

  // VerbMizenkei → AuxCausative (食べ+させる) - moderate bonus
  setCell(t, EPOS::VerbMizenkei, EPOS::AuxCausative, cost::kModerateBonus);

  // VerbMizenkei → VerbMizenkei (読ま+さ, やら+さ causative pattern)
  // Godan mizenkei + causative さ (する mizenkei) - moderate bonus
  setCell(t, EPOS::VerbMizenkei, EPOS::VerbMizenkei, cost::kModerateBonus);

  // VerbMizenkei → AuxVolitional (食べ+よう) - moderate bonus
  setCell(t, EPOS::VerbMizenkei, EPOS::AuxVolitional, cost::kModerateBonus);

  // VerbMizenkei → Conjunction: very rare
  // Mizenkei connects to ない/せる/れる/よう, never to conjunctions
  // Prevents さ(mizenkei)+まして(CONJ) over さまし(renyokei)+て
  setCell(t, EPOS::VerbMizenkei, EPOS::Conjunction, cost::kVeryRare);

  // VerbMizenkei → ParticleFinal: very rare
  // Mizenkei never directly connects to sentence-ending particles
  // Prevents 勉強+せ(mizenkei)+よ(final) over 勉強+せよ(imperative dict entry)
  setCell(t, EPOS::VerbMizenkei, EPOS::ParticleFinal, cost::kVeryRare);

  // Suffix → Conjunction: rare (without punctuation, suffix+conj is unusual)
  // Prevents さ(suffix)+まして(CONJ) over さまし(verb renyokei)+て
  setCell(t, EPOS::Suffix, EPOS::Conjunction, cost::kRare);

  // Note: VerbRenyokei → AuxTenseTa is NOT set here because it would incorrectly
  // favor て as AUX over Particle. Ichidan た-form split (食べ+た) needs surface-based
  // handling in scorer.cpp to distinguish た from て.

  // VerbOnbinkei → AuxTenseTa (書い+た, 泳い+だ) - strong bonus
  setCell(t, EPOS::VerbOnbinkei, EPOS::AuxTenseTa, cost::kStrongBonus);

  // VerbOnbinkei → AuxAspectOku (読ん+どい) - moderate bonus for contracted ~ておく split
  setCell(t, EPOS::VerbOnbinkei, EPOS::AuxAspectOku, cost::kModerateBonus);

  // VerbOnbinkei → AuxAspectShimau (行っ+ちゃっ) - strong bonus for contracted ~てしまう split
  setCell(t, EPOS::VerbOnbinkei, EPOS::AuxAspectShimau, cost::kStrongBonus);

  // VerbTeForm → AuxAspectIru (食べて+いる) - penalty to prefer 食べ+て+いる split
  // MeCab splits as 食べ+て+いる, not 食べて+いる
  setCell(t, EPOS::VerbTeForm, EPOS::AuxAspectIru, cost::kUncommon);

  // VerbTeForm → AuxAspectShimau (食べて+しまう) - moderate bonus
  setCell(t, EPOS::VerbTeForm, EPOS::AuxAspectShimau, cost::kModerateBonus);

  // VerbTeForm → AuxAspectOku (食べて+おく) - moderate bonus
  setCell(t, EPOS::VerbTeForm, EPOS::AuxAspectOku, cost::kModerateBonus);

  // VerbTeForm → AuxAspectMiru (食べて+みる) - moderate bonus
  setCell(t, EPOS::VerbTeForm, EPOS::AuxAspectMiru, cost::kModerateBonus);

  // VerbTeForm → AuxAspectIku (食べて+いく) - moderate bonus
  setCell(t, EPOS::VerbTeForm, EPOS::AuxAspectIku, cost::kModerateBonus);

  // VerbTeForm → AuxAspectKuru (食べて+くる) - moderate bonus
  setCell(t, EPOS::VerbTeForm, EPOS::AuxAspectKuru, cost::kModerateBonus);

  // VerbRenyokei → AuxAspectOku (食べ+とく contraction of 食べておく) - strong bonus
  // This handles contracted forms where ておく → とく
  setCell(t, EPOS::VerbRenyokei, EPOS::AuxAspectOku, cost::kStrongBonus);

  // VerbRenyokei → AuxAspectShimau (食べ+ちゃっ contraction of 食べてしまう) - very strong bonus
  // This handles contracted forms where てしまう → ちゃう. Dict ちゃっ is base POS
  // Verb, so the base VERB→VERB bigram (+0.8) would cancel out a mere strong
  // bonus (-0.8, net 0); use very-strong so the net (-0.8) matches the working
  // VerbOnbinkei → AuxTenseTa connection (食べ+た).
  setCell(t, EPOS::VerbRenyokei, EPOS::AuxAspectShimau, cost::kVeryStrongBonus);

  // =========================================================================
  // Verb Forms → Particles
  // =========================================================================

  // VerbShuushikei → ParticleFinal (食べる+ね) - minor bonus
  setCell(t, EPOS::VerbShuushikei, EPOS::ParticleFinal, cost::kMinorBonus);

  // VerbShuushikei → ParticleNo (食べる+の+だ for のだ/んだ) - strong bonus
  setCell(t, EPOS::VerbShuushikei, EPOS::ParticleNo, cost::kStrongBonus);

  // Verb → ParticleAdverbial (できる+だけ, 食べる+だけ, 行く+だけ) - minor bonus
  setCell(t, EPOS::VerbShuushikei, EPOS::ParticleAdverbial, cost::kMinorBonus);
  setCell(t, EPOS::VerbRenyokei, EPOS::ParticleAdverbial, cost::kMinorBonus);

  // VerbShuushikei → ParticleQuote (食べる+と言う) - neutral
  setCell(t, EPOS::VerbShuushikei, EPOS::ParticleQuote, cost::kNeutral);

  // VerbKateikei → ParticleConj (食べれ+ば) - strong bonus
  setCell(t, EPOS::VerbKateikei, EPOS::ParticleConj, cost::kStrongBonus);

  // VerbKateikei → AdjBasic (滅びれば+いい) - strong bonus for 〜ればいい pattern
  // This helps beat the split path 滅び+れ+ば+いい where れ is misanalyzed as passive
  setCell(t, EPOS::VerbKateikei, EPOS::AdjBasic, cost::kStrongBonus);

  // VerbOnbinkei → ParticleConj (書い+て, 読ん+で) - strong bonus for te-form split
  setCell(t, EPOS::VerbOnbinkei, EPOS::ParticleConj, cost::kStrongBonus);

  // VerbRenyokei → ParticleConj (食べ+て, 見+て) - moderate bonus for ichidan te-form split
  // Reduced from kStrongBonus to prevent す+ば splitting すばらしい
  setCell(t, EPOS::VerbRenyokei, EPOS::ParticleConj, cost::kModerateBonus);

  // ParticleConj → AdjBasic (食べて+ほしい) - moderate bonus for te+adjective pattern
  setCell(t, EPOS::ParticleConj, EPOS::AdjBasic, cost::kModerateBonus);

  // =========================================================================
  // Adjective Forms → Auxiliaries
  // =========================================================================

  // AdjRenyokei → AuxNegativeNai (美しく+ない) - very strong grammatical connection
  // This needs to beat the full-form hiragana adjective bonus (e.g., しんどくない as single token)
  setCell(t, EPOS::AdjRenyokei, EPOS::AuxNegativeNai, cost::kExtremeBonus);

  // AdjRenyokei → ParticleConj (美しく+て, ウザく+て) - strong bonus for te-form split
  setCell(t, EPOS::AdjRenyokei, EPOS::ParticleConj, cost::kStrongBonus);

  // AdjRenyokei → VerbRenyokei (美しく+なり) - strong bonus for adjective + become pattern
  // This is a very common Japanese pattern (美しくなる, 大きくなる, etc.)
  setCell(t, EPOS::AdjRenyokei, EPOS::VerbRenyokei, cost::kStrongBonus);

  // AdjRenyokei → VerbOnbinkei (深く+突き刺さっ, 美しく+咲い) - moderate bonus
  // Adverb form of adjective directly modifying verb in onbin (past/te) form
  setCell(t, EPOS::AdjRenyokei, EPOS::VerbOnbinkei, cost::kModerateBonus);

  // AdjKatt → AuxTenseTa (美しかっ+た) - strong bonus
  setCell(t, EPOS::AdjKatt, EPOS::AuxTenseTa, cost::kStrongBonus);

  // AdjKeForm → ParticleConj (美しけれ+ば) - strong bonus for conditional splitting
  setCell(t, EPOS::AdjKeForm, EPOS::ParticleConj, cost::kStrongBonus);

  // AdjMizenkei → AuxVolitional (高かろ+う) - strong bonus for conjectural splitting
  setCell(t, EPOS::AdjMizenkei, EPOS::AuxVolitional, cost::kStrongBonus);

  // AdjBasic → AuxCopulaDesu (美しい+です) - moderate bonus
  setCell(t, EPOS::AdjBasic, EPOS::AuxCopulaDesu, cost::kModerateBonus);

  // AdjBasic → ParticleFinal (美しい+ね, エロい+よ) - moderate bonus
  // Adjective + sentence-final particle is a very common pattern
  setCell(t, EPOS::AdjBasic, EPOS::ParticleFinal, cost::kModerateBonus);

  // AdjBasic → ParticleConj (美しい+し, 高い+けど) - minor bonus
  // Helps i-adjective+conjunctive particle beat ADJ_NA+い(verb)+し path
  setCell(t, EPOS::AdjBasic, EPOS::ParticleConj, cost::kMinorBonus);

  // AdjBasic → Noun (美しい+猫, 大きい+家, 高い+山) - moderate bonus
  // i-adjective attributive form + noun is fundamental Japanese grammar
  // Without this, long unknown NOUN candidates (一番美しい) beat split paths
  setCell(t, EPOS::AdjBasic, EPOS::Noun, cost::kModerateBonus);
  setCell(t, EPOS::AdjBasic, EPOS::NounFormal, cost::kModerateBonus);

  // AdjBasic → ParticleNo (少ない+の, 美味しい+の, いいの) - moderate bonus
  // Without this, NOUN+ない(AUX)+の path beats adjective+の path because
  // AuxNeg→ParticleNo has a strong bonus (-0.8). The nominalizer の after an
  // i-adjective 終止形 is extremely common, so keep it ahead of the hira_noun_seq
  // fallback that otherwise merges のか into one NOUN (いいのか → いい|の|か).
  setCell(t, EPOS::AdjBasic, EPOS::ParticleNo, cost::kModerateBonus);

  // AdjStem → AuxAppearanceSou (美し+そう) - very strong bonus
  // Must beat adverb bonus (-1.0 for 2-char hiragana) to prefer auxiliary
  setCell(t, EPOS::AdjStem, EPOS::AuxAppearanceSou, cost::kVeryStrongBonus);

  // AdjStem/AdjNaAdj → AuxExcessive (高+すぎる, シンプル+すぎる) - moderate bonus
  // Helps AUX_過度 beat VERB interpretation when both have same cost
  setCell(t, EPOS::AdjStem, EPOS::AuxExcessive, cost::kModerateBonus);
  setCell(t, EPOS::AdjNaAdj, EPOS::AuxExcessive, cost::kModerateBonus);

  // AdjStem → AuxGaru (怖+がる, 可愛+がる) - moderate bonus
  setCell(t, EPOS::AdjStem, EPOS::AuxGaru, cost::kModerateBonus);

  // Suffix → AuxAppearanceSou (さ+そう in なさそう) - moderate bonus
  // This completes the な+さ+そう chain for ない nominalization + appearance
  setCell(t, EPOS::Suffix, EPOS::AuxAppearanceSou, cost::kModerateBonus);

  // Suffix → AuxCopulaDa (的+な in 論理的な) - strong bonus
  // 的 suffix followed by な (copula attributive) is very common
  setCell(t, EPOS::Suffix, EPOS::AuxCopulaDa, cost::kStrongBonus);

  // Suffix → ParticleCase (的+に in 感情的になる) - moderate bonus
  // Helps split 的+に+なる instead of 的+になる (as single verb)
  setCell(t, EPOS::Suffix, EPOS::ParticleCase, cost::kModerateBonus);

  // Suffix → Verb (中+働く in 一日中働く) - moderate bonus
  // Temporal suffix 中 followed by verb is natural
  setCell(t, EPOS::Suffix, EPOS::VerbRenyokei, cost::kModerateBonus);
  setCell(t, EPOS::Suffix, EPOS::VerbShuushikei, cost::kModerateBonus);

  // Suffix → Noun - minor bonus
  // Suffixes naturally precede nouns (e.g., honorifics before noun phrases)
  setCell(t, EPOS::Suffix, EPOS::Noun, cost::kModerateBonus);
}

}  // namespace suzume::analysis::bigram_rules
