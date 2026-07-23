#include "bigram_table_internal.h"

namespace suzume::analysis::bigram_rules {

using EPOS = core::ExtendedPOS;
namespace cost = bigram_cost;

void setVerbAndAdjectiveCosts(BigramMatrix& table) {
  static constexpr BigramRule kRules[] = {
      // =========================================================================
      // Verb Forms → Auxiliaries (Core Grammar)
      // =========================================================================

      // VerbRenyokei → AuxTenseMasu (食べ+ます) - strong bonus
      {EPOS::VerbRenyokei, EPOS::AuxTenseMasu, cost::kStrongBonus},

      // VerbRenyokei → AuxDesireTai (食べ+たい) - strong bonus
      {EPOS::VerbRenyokei, EPOS::AuxDesireTai, cost::kStrongBonus},

      // VerbRenyokei → AuxHonorific (書き+なさい, お読み+なさる) - the
      // subsidiary reading must outrank a homographic lexical honorific verb.
      {EPOS::VerbRenyokei, EPOS::AuxHonorific, cost::kDoubleVeryStrongBonus},

      // VerbMizenkei → AuxNegativeNai (食べ+ない, 行か+なけれ) - very
      // strong grammatical connection.  In particular, なけれ before ば
      // must remain the negative auxiliary instead of a fabricated
      // standalone conditional verb.
      {EPOS::VerbMizenkei, EPOS::AuxNegativeNai, cost::kVeryStrongBonus},

      // VerbRenyokei → AuxNegativeNai (食べ+ない/なけれ for ichidan
      // same-form mizen/renyokei).  Keep this parallel with the explicit
      // mizenkei connection because the stem label is ambiguous but the
      // following negative auxiliary is not.
      {EPOS::VerbRenyokei, EPOS::AuxNegativeNai, cost::kVeryStrongBonus},

      // VerbRenyokei → AdjStem (食べ+な, 読み+にく, 使い+やす) - strong bonus. The negative ない is stored
      // as an adjective stem (な/ない) and attaches to a verb 未然形, which shares the ichidan
      // renyokei surface. Overrides the default cross-category penalty so the negative reading
      // 食べ + な(ない) + さ + そう wins over the 断定 copula な, without over-splitting でき.
      {EPOS::VerbRenyokei, EPOS::AdjStem, cost::kStrongBonus},

      // VerbMizenkei → AuxNegativeNu (くだら+ん contracted negative) - moderate bonus
      {EPOS::VerbMizenkei, EPOS::AuxNegativeNu, cost::kModerateBonus},

      // VerbRenyokei → AuxNegativeNu (消え+ぬ classical negative)
      // Ichidan verbs have same form for mizen and renyokei (e.g., 消え from 消える)
      // This helps 消えぬ炎 → 消え+ぬ+炎 over 消えぬ+炎
      {EPOS::VerbRenyokei, EPOS::AuxNegativeNu, cost::kModerateBonus},

      // A finite i-adjective also cannot take that negative auxiliary. The
      // following ん in ないんだ is the nominalizer の.
      {EPOS::AdjBasic, EPOS::AuxNegativeNu, cost::kAlmostNever},

      // まい (negative volitional) connection grammar:
      // - Godan 終止形 + まい (行く+まい, なる+まい)
      // - Ichidan 未然形 + まい (食べ+まい; surfaces carry VerbRenyokei EPOS)
      // - する/来る 未然形 + まい (し+まい, こ+まい)
      // A terminal-form verb followed by まい is a fully licensed negative
      // intent chain.  Make this strong enough to beat a dictionary adjective
      // that happens to begin at the verb's final kana (言う+まい vs 言+うまい).
      {EPOS::VerbShuushikei, EPOS::AuxNegativeMai, cost::kVeryStrongBonus},
      // Attributive/terminal verb forms precede concessive and causal
      // conjunctive particles (いう+ものの, 読む+ので). This favors the
      // closed-class particle over a formal-noun plus nominalizer split.
      {EPOS::VerbShuushikei, EPOS::ParticleConj, cost::kStrongBonus},
      {EPOS::VerbRenyokei, EPOS::AuxNegativeMai, cost::kModerateBonus},
      {EPOS::VerbMizenkei, EPOS::AuxNegativeMai, cost::kModerateBonus},

      // VerbMizenkei → AuxPassive (食べ+られる) - moderate bonus
      {EPOS::VerbMizenkei, EPOS::AuxPassive, cost::kModerateBonus},

      // VerbRenyokei → AuxPassive (知らせ+られ) - strong bonus
      // Ensures 知らせられた → 知らせ+られ+た over 知ら+せ+られ+た
      // The ichidan causative verb (知らせる) renyokei should connect to passive
      {EPOS::VerbRenyokei, EPOS::AuxPassive, cost::kStrongBonus},

      // VerbRenyokei → AuxPotential (し+え, し+える) - strong bonus
      // Literary potential 得る: 看過しえない, 理解しえた, 想像しうる
      // Must be strong to beat single_kanji_ichidan_polite VERB path for 得
      {EPOS::VerbRenyokei, EPOS::AuxPotential, cost::kStrongBonus},

      // The connective copula joins coordinated/predicative adjectives
      // (静かで美しい). Without this, the generic copula→noun preference can make
      // a structurally complete i-adjective lose to an unknown noun homograph.
      {EPOS::AuxCopulaDa, EPOS::AdjBasic, cost::kModerateBonus},

      // AuxPotential → AuxNegativeNai (え+ない, 得+ない) - extra strong bonus
      // Literary potential + negation: 看過しえない, 解決し得ない
      // Needs extra strength to overcome base AUX→AUX category penalty (0.3)
      {EPOS::AuxPotential, EPOS::AuxNegativeNai, cost::kExtraStrongBonus},

      // VerbMizenkei → AuxCausative (読ま+せる, 食べ+させる) - strong
      // bonus. This must outrank a nominal stem followed by a homographic
      // polite auxiliary (読+ませ).
      {EPOS::VerbMizenkei, EPOS::AuxCausative, cost::kStrongBonus},

      // VerbMizenkei → VerbMizenkei (読ま+さ, やら+さ causative pattern)
      // Godan mizenkei + causative さ (する mizenkei) - moderate bonus
      {EPOS::VerbMizenkei, EPOS::VerbMizenkei, cost::kModerateBonus},

      // VerbMizenkei → AuxVolitional (食べ+よう) - moderate bonus
      {EPOS::VerbMizenkei, EPOS::AuxVolitional, cost::kModerateBonus},

      // A Godan e-row form before ない is the stem of its productive
      // potential Ichidan predicate (見つけ出せ+ない). It is represented by
      // the same lattice class as the conditional form, so license negation.
      {EPOS::VerbKateikei, EPOS::AuxNegativeNai, cost::kStrongBonus},

      // Predicates introduce quotative determiners: 読む+という,
      // 読んだ+という, 読む+っていう.
      // The contracted っていう competes with a spurious っ+て+い+う
      // auxiliary chain, so this grammatical clause boundary needs to be
      // decisive for every terminal predicate.
      {EPOS::VerbShuushikei, EPOS::DeterminerQuotative, cost::kDoubleVeryStrongBonus},
      {EPOS::VerbTaForm, EPOS::DeterminerQuotative, cost::kDoubleVeryStrongBonus},
      {EPOS::VerbTeForm, EPOS::DeterminerQuotative, cost::kDoubleVeryStrongBonus},

      // VerbMizenkei → Conjunction: very rare
      // Mizenkei connects to ない/せる/れる/よう, never to conjunctions
      // Prevents さ(mizenkei)+まして(CONJ) over さまし(renyokei)+て
      {EPOS::VerbMizenkei, EPOS::Conjunction, cost::kVeryRare},

      // An irrealis stem cannot take a sentence-final particle. The
      // prohibitive な attaches to the terminal form (読む+な), while negative
      // ない is an auxiliary (読ま+ない).
      // Mizenkei never directly connects to sentence-ending particles
      // Prevents 勉強+せ(mizenkei)+よ(final) over 勉強+せよ(imperative dict entry)
      {EPOS::VerbMizenkei, EPOS::ParticleFinal, cost::kAlmostNever},

      // Suffix → Conjunction: rare (without punctuation, suffix+conj is unusual)
      // Prevents さ(suffix)+まして(CONJ) over さまし(verb renyokei)+て
      {EPOS::Suffix, EPOS::Conjunction, cost::kRare},

      // Note: VerbRenyokei → AuxTenseTa is NOT set here because it would incorrectly
      // favor て as AUX over Particle. Ichidan た-form split (食べ+た) needs surface-based
      // handling in scorer.cpp to distinguish た from て.

      // VerbOnbinkei → AuxTenseTa (書い+た, 泳い+だ) - strong bonus
      {EPOS::VerbOnbinkei, EPOS::AuxTenseTa, cost::kStrongBonus},

      // VerbOnbinkei → contracted progressive auxiliary (行っ+て+た).
      {EPOS::VerbOnbinkei, EPOS::AuxAspectIru, cost::kVeryStrongBonus},

      // VerbOnbinkei → AuxAspectOku (書い+とく, やっ+とく, 読ん+どく) - very
      // strong bonus for the contracted ~ておく boundary over nominal and
      // adverb alternatives.
      {EPOS::VerbOnbinkei, EPOS::AuxAspectOku, cost::kVeryStrongBonus},

      // VerbOnbinkei → AuxAspectShimau (行っ+ちゃっ) - very strong bonus for contracted ~てしまう split
      {EPOS::VerbOnbinkei, EPOS::AuxAspectShimau, cost::kVeryStrongBonus},

      // VerbTeForm → AuxAspectIru (食べて+いる) - penalty to prefer 食べ+て+いる split
      // MeCab splits as 食べ+て+いる, not 食べて+いる
      {EPOS::VerbTeForm, EPOS::AuxAspectIru, cost::kUncommon},

      // VerbTeForm → AuxAspectShimau (食べて+しまう) - moderate bonus
      {EPOS::VerbTeForm, EPOS::AuxAspectShimau, cost::kModerateBonus},

      // VerbTeForm → AuxAspectOku (食べて+おく) - moderate bonus
      {EPOS::VerbTeForm, EPOS::AuxAspectOku, cost::kModerateBonus},

      // VerbTeForm → AuxAspectMiru (食べて+みる) - moderate bonus
      {EPOS::VerbTeForm, EPOS::AuxAspectMiru, cost::kModerateBonus},

      // VerbTeForm → AuxAspectIku (食べて+いく) - moderate bonus
      {EPOS::VerbTeForm, EPOS::AuxAspectIku, cost::kModerateBonus},

      // VerbTeForm → AuxAspectKuru (食べて+くる) - moderate bonus
      {EPOS::VerbTeForm, EPOS::AuxAspectKuru, cost::kModerateBonus},

      // VerbRenyokei → AuxInability (読み+かねる, 言い+かねます).
      {EPOS::VerbRenyokei, EPOS::AuxInability, cost::kExtremeBonus},

      // VerbRenyokei → AuxAspectHajimeru (読み+はじめる, 食べ+はじめる).
      {EPOS::VerbRenyokei, EPOS::AuxAspectHajimeru, cost::kVeryStrongBonus},

      // VerbRenyokei → AuxAspectOku (食べ+とく contraction of 食べておく) - strong bonus
      // This handles contracted forms where ておく → とく
      {EPOS::VerbRenyokei, EPOS::AuxAspectOku, cost::kStrongBonus},

      // VerbRenyokei → AuxAspectShimau (食べ+ちゃっ contraction of 食べてしまう) - very strong bonus
      // This handles contracted forms where てしまう → ちゃう. Dict ちゃっ is base POS
      // Verb, so the base VERB→VERB bigram (+0.8) would cancel out a mere strong
      // bonus (-0.8, net 0); use very-strong so the net (-0.8) matches the working
      // VerbOnbinkei → AuxTenseTa connection (食べ+た).
      {EPOS::VerbRenyokei, EPOS::AuxAspectShimau, cost::kVeryStrongBonus},

      // The excessive subsidiary follows a verb continuative (読み+過ぎる,
      // 通り+過ぎる). Keep this productive auxiliary boundary ahead of a
      // lexical compound that spans the same surface.
      {EPOS::VerbRenyokei, EPOS::AuxExcessive, cost::kStrongBonus},

      // =========================================================================
      // Verb Forms → Particles
      // =========================================================================

      // VerbShuushikei → ParticleFinal (食べる+ね) - minor bonus
      {EPOS::VerbShuushikei, EPOS::ParticleFinal, cost::kMinorBonus},

      // VerbShuushikei → ParticleNo (食べる+の+だ for のだ/んだ) - strong bonus
      {EPOS::VerbShuushikei, EPOS::ParticleNo, cost::kStrongBonus},

      // Verb → ParticleAdverbial (できる+だけ, 食べる+だけ, 行く+だけ) - minor bonus
      {EPOS::VerbShuushikei, EPOS::ParticleAdverbial, cost::kMinorBonus},
      {EPOS::VerbRenyokei, EPOS::ParticleAdverbial, cost::kMinorBonus},

      // VerbShuushikei → ParticleQuote (食べる+と言う) - neutral
      {EPOS::VerbShuushikei, EPOS::ParticleQuote, cost::kNeutral},

      // VerbKateikei → AdjBasic (滅びれば+いい) - strong bonus for 〜ればいい pattern
      // This helps beat the split path 滅び+れ+ば+いい where れ is misanalyzed as passive
      {EPOS::VerbKateikei, EPOS::AdjBasic, cost::kStrongBonus},

      // VerbOnbinkei → ParticleConj (書い+て, 読ん+で) - the voiced te-form
      // must stay intact even before a following topic particle (読ん+で+は).
      // The strong preference for a parallel たり/だり chain is gated by
      // dictionary attestation or a kanji stem in the scorer. A neutral base
      // keeps an unknown hiragana fragment from defeating a complete mimetic
      // adverb such as AっBり.
      {EPOS::VerbOnbinkei, EPOS::ParticleConj, cost::kNeutral},

      // VerbRenyokei → ParticleConj (食べ+て, 見+て) - moderate bonus for ichidan te-form split
      // Reduced from kStrongBonus to prevent す+ば splitting すばらしい
      {EPOS::VerbRenyokei, EPOS::ParticleConj, cost::kModerateBonus},

      // ParticleConj → AdjBasic (食べて+ほしい) - moderate bonus for te+adjective pattern
      {EPOS::ParticleConj, EPOS::AdjBasic, cost::kModerateBonus},

      // =========================================================================
      // Adjective Forms → Auxiliaries
      // =========================================================================

      // AdjRenyokei → AuxNegativeNai (美しく+ない) - very strong grammatical connection
      // This needs to beat the full-form hiragana adjective bonus (e.g., しんどくない as single token)
      {EPOS::AdjRenyokei, EPOS::AuxNegativeNai, cost::kExtremeBonus},

      // AdjRenyokei → ParticleConj (美しく+て, ウザく+て) - strong bonus for te-form split
      {EPOS::AdjRenyokei, EPOS::ParticleConj, cost::kStrongBonus},

      // AdjRenyokei → VerbRenyokei (美しく+なり) - strong bonus for adjective + become pattern
      // This is a very common Japanese pattern (美しくなる, 大きくなる, etc.)
      {EPOS::AdjRenyokei, EPOS::VerbRenyokei, cost::kStrongBonus},

      // An adverbial adjective can also precede する in its mizenkei before
      // passive or causative inflection (余儀なく+さ+れる).
      {EPOS::AdjRenyokei, EPOS::VerbMizenkei, cost::kVeryStrongBonus},

      // The same adverbial-adjective construction commonly takes a terminal
      // verb (美しく+なる, 読まれなく+なる), not only its renyokei form.
      {EPOS::AdjRenyokei, EPOS::VerbShuushikei, cost::kVeryStrongBonus},

      // AdjRenyokei → VerbOnbinkei (深く+突き刺さっ, 美しく+咲い) - moderate bonus
      // Adverb form of adjective directly modifying verb in onbin (past/te) form
      {EPOS::AdjRenyokei, EPOS::VerbOnbinkei, cost::kModerateBonus},

      // AdjKatt → AuxTenseTa (美しかっ+た) - strong bonus
      {EPOS::AdjKatt, EPOS::AuxTenseTa, cost::kStrongBonus},

      // I-adjective past stem → coordinate particle (高かっ+たりする).
      {EPOS::AdjKatt, EPOS::ParticleConj, cost::kStrongBonus},

      // AdjKeForm → ParticleConj (美しけれ+ば) - strong bonus for conditional splitting
      {EPOS::AdjKeForm, EPOS::ParticleConj, cost::kStrongBonus},

      // AdjMizenkei → AuxVolitional (高かろ+う) - strong bonus for conjectural splitting
      {EPOS::AdjMizenkei, EPOS::AuxVolitional, cost::kStrongBonus},

      // AdjMizenkei → AuxNegativeNu (高から+ず, 美しから+ず).
      {EPOS::AdjMizenkei, EPOS::AuxNegativeNu, cost::kStrongBonus},

      // AdjBasic → AuxCopulaDesu (美しい+です) - moderate bonus
      {EPOS::AdjBasic, EPOS::AuxCopulaDesu, cost::kModerateBonus},

      // AdjBasic → ParticleFinal (美しい+ね, 楽しい+よ) - moderate bonus
      // Adjective + sentence-final particle is a very common pattern
      {EPOS::AdjBasic, EPOS::ParticleFinal, cost::kModerateBonus},

      // AdjBasic → ParticleConj (美しい+し, 高い+けど) - minor bonus
      // Helps i-adjective+conjunctive particle beat ADJ_NA+い(verb)+し path
      {EPOS::AdjBasic, EPOS::ParticleConj, cost::kMinorBonus},

      // AdjBasic → Noun (美しい+猫, 大きい+家, 高い+山) - moderate bonus
      // i-adjective attributive form + noun is fundamental Japanese grammar
      // Without this, long unknown NOUN candidates (一番美しい) beat split paths
      {EPOS::AdjBasic, EPOS::Noun, cost::kModerateBonus},

      // An attributive i-adjective can modify a bound nominal suffix
      // (長い+間). Keep that grammatical suffix reading ahead of the
      // homographic unrestricted noun fallback.
      {EPOS::AdjBasic, EPOS::Suffix, cost::kStrongBonus},

      // An i-adjective directly modifies a formal noun (難い+もの,
      // 美しい+こと). Keep this productive adnominal boundary ahead of a
      // homographic verb-renyokei candidate that postprocessing would merge.
      {EPOS::AdjBasic, EPOS::NounFormal, cost::kDoubleVeryStrongBonus},

      // Verb terminal → formal noun (読む+たび, 書く+こと, 見る+たび).
      // Formal nouns are productive clause nominalizers, so favor this
      // grammatical boundary over an unknown noun that absorbs the ending.
      {EPOS::VerbShuushikei, EPOS::NounFormal, cost::kVeryStrongBonus},

      // Verb renyokei → formal noun (読み+よう, 書き+方). This productive
      // construction must outrank the homographic volitional 〜よう path.
      {EPOS::VerbRenyokei, EPOS::NounFormal, cost::kVeryStrongBonus},

      // AdjBasic → ParticleNo (少ない+の, 美味しい+の, いいの) - moderate bonus
      // Without this, NOUN+ない(AUX)+の path beats adjective+の path because
      // AuxNeg→ParticleNo has a strong bonus (-0.8). The nominalizer の after an
      // i-adjective 終止形 is extremely common, so keep it ahead of the hira_noun_seq
      // fallback that otherwise merges のか into one NOUN (いいのか → いい|の|か).
      {EPOS::AdjBasic, EPOS::ParticleNo, cost::kModerateBonus},

      // AdjStem → AuxAppearanceSou (美し+そう) - decisive bonus
      // Must beat a homographic noun + suru-renyokei analysis before そう.
      {EPOS::AdjStem, EPOS::AuxAppearanceSou, cost::kAppearanceAuxiliaryBonus},

      // AdjStem/AdjNaAdj → AuxExcessive (高+すぎる, シンプル+すぎる) - moderate bonus
      // Helps AUX_過度 beat VERB interpretation when both have same cost
      {EPOS::AdjStem, EPOS::AuxExcessive, cost::kModerateBonus},
      {EPOS::AdjNaAdj, EPOS::AuxExcessive, cost::kModerateBonus},

      // A na-adjective can take the direct negative adjective without the
      // copular な (必要+ない, 便利+ない).
      {EPOS::AdjNaAdj, EPOS::AdjBasic, cost::kVeryStrongBonus},

      // AdjStem → AuxGaru (怖+がる, 可愛+がる) - moderate bonus
      {EPOS::AdjStem, EPOS::AuxGaru, cost::kModerateBonus},

      // Suffix → AuxAppearanceSou (さ+そう in なさそう) - moderate bonus
      // This completes the な+さ+そう chain for ない nominalization + appearance
      {EPOS::Suffix, EPOS::AuxAppearanceSou, cost::kModerateBonus},

      // Suffix → AuxCopulaDa (的+な in 論理的な) - strong bonus
      // 的 suffix followed by な (copula attributive) is very common
      {EPOS::Suffix, EPOS::AuxCopulaDa, cost::kStrongBonus},

      // The tendency suffix retains ordinary suffix inflection (病気がちで、
      // 忘れがちだ) while its dedicated left-context rule selects a verb
      // continuative form when present.
      {EPOS::SuffixTendency, EPOS::AuxCopulaDa, cost::kStrongBonus},

      // Suffix → ParticleCase (的+に in 感情的になる) - moderate bonus
      // Helps split 的+に+なる instead of 的+になる (as single verb)
      {EPOS::Suffix, EPOS::ParticleCase, cost::kModerateBonus},

      // Suffix → Verb (中+働く in 一日中働く) - moderate bonus
      // Temporal suffix 中 followed by verb is natural
      {EPOS::Suffix, EPOS::VerbRenyokei, cost::kModerateBonus},
      {EPOS::Suffix, EPOS::VerbShuushikei, cost::kModerateBonus},

      // Suffix → Noun - minor bonus
      // Suffixes naturally precede nouns (e.g., honorifics before noun phrases)
      {EPOS::Suffix, EPOS::Noun, cost::kModerateBonus},
  };
  applyRules(table, kRules, sizeof(kRules) / sizeof(kRules[0]));
}

}  // namespace suzume::analysis::bigram_rules
