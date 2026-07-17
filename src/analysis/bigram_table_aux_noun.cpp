#include "bigram_table_internal.h"

namespace suzume::analysis::bigram_rules {

using EPOS = core::ExtendedPOS;
namespace cost = bigram_cost;
constexpr float kDeterminerNounBonus = -2.5F;

void setAuxiliaryAndNounCosts(BigramMatrix& table) {
  static constexpr BigramRule kRules[] = {
      // =========================================================================
      // Auxiliary → Auxiliary Chains
      // =========================================================================

      // AuxTenseMasu → AuxTenseTa (まし+た) - strong bonus
      {EPOS::AuxTenseMasu, EPOS::AuxTenseTa, cost::kStrongBonus},

      // AuxTenseMasu → AuxNegativeNu (ませ+ん for polite negative) - strong bonus
      // Ensures ません → ませ+ん (aux) over ませ+ん (particle の)
      {EPOS::AuxTenseMasu, EPOS::AuxNegativeNu, cost::kStrongBonus},

      // AuxTenseMasu → AuxVolitional (ましょ+う) - strong bonus for the volitional boundary
      {EPOS::AuxTenseMasu, EPOS::AuxVolitional, cost::kStrongBonus},

      // AuxCopulaDesu → AuxVolitional (でしょ+う) - strong bonus for the volitional boundary
      {EPOS::AuxCopulaDesu, EPOS::AuxVolitional, cost::kStrongBonus},

      // AuxCopulaDa → AuxVolitional (だろ+う) - strong bonus for the volitional boundary
      {EPOS::AuxCopulaDa, EPOS::AuxVolitional, cost::kStrongBonus},

      // AuxCausative → AuxPassive (せ+られ in causative-passive) - strong bonus
      // Ensures 聞かせられた → 聞か+せ+られ+た over 聞か+せられた
      {EPOS::AuxCausative, EPOS::AuxPassive, cost::kStrongBonus},

      // AuxCausative → AuxNegativeNai (せ+ない, させ+ない in causative negative) - moderate bonus
      // Ensures 読ませない → 読ま+せ+ない with せ as causative せる, not せ=する (サ変未然).
      // せ+ない is always causative negation; する negation is しない, so no ambiguity.
      // Parallel to AuxPassive → AuxNegativeNai; without this the VerbMizenkei→AuxNegativeNai
      // bonus on the せ=する reading wins asymmetrically only when ない follows.
      {EPOS::AuxCausative, EPOS::AuxNegativeNai, cost::kModerateBonus},

      // AuxPassive → AuxTenseMasu (れ+ます in passive polite) - strong bonus
      // Ensures 言われます → 言わ+れ+ます over 言われ+ます
      {EPOS::AuxPassive, EPOS::AuxTenseMasu, cost::kStrongBonus},

      // AuxPassive → AuxNegativeNai (れ+ない in passive negative) - strong bonus
      // Ensures 言われない → 言わ+れ+ない over 言われ+ない
      {EPOS::AuxPassive, EPOS::AuxNegativeNai, cost::kStrongBonus},

      // AuxPassive → AuxTenseTa (れ+た in passive past) - strong bonus
      // Ensures 言われた → 言わ+れ+た over 言われ+た
      {EPOS::AuxPassive, EPOS::AuxTenseTa, cost::kStrongBonus},

      // AuxPassive → AuxDesireTai (れ+たい in passive desiderative) - strong bonus
      // Ensures 見られたい → 見+られ+たい over 見+られ+た+い
      {EPOS::AuxPassive, EPOS::AuxDesireTai, cost::kStrongBonus},

      // AuxPassive → AuxVolitional (れる+べき in passive obligation) - strong bonus
      // Ensures 書かれるべき → 書か+れる+べき(dict) over char_speech べき(AUX_過去) path
      {EPOS::AuxPassive, EPOS::AuxVolitional, cost::kStrongBonus},

      // AuxPassive → ParticleConj (れ+ながら, れ+ば in passive+conjunctive) - moderate bonus
      // Ensures 揉まれながら → 揉ま+れ+ながら over 揉まれ+ながら
      {EPOS::AuxPassive, EPOS::ParticleConj, cost::kModerateBonus},

      // AuxNegativeNai → AuxTenseTa (なかっ+た) - strong bonus
      {EPOS::AuxNegativeNai, EPOS::AuxTenseTa, cost::kStrongBonus},

      // AuxNegativeNai → AuxVolitional (なかろ+う) - strong bonus
      {EPOS::AuxNegativeNai, EPOS::AuxVolitional, cost::kStrongBonus},

      // AuxNegativeNu → AuxTenseTa (んかっ+た for contracted negative past)
      // Ensures くだらんかった → くだら+ん+かっ+た over くだ+らんかっ+た
      {EPOS::AuxNegativeNu, EPOS::AuxTenseTa, cost::kStrongBonus},

      // AuxNegativeNai → ParticleNo (ない+ん for のだ/んだ) - strong bonus
      // Ensures ないんだ → ない+ん+だ over な+いん+だ
      {EPOS::AuxNegativeNai, EPOS::ParticleNo, cost::kStrongBonus},

      // AuxNegativeNai → ParticleConj (ない+のに, ない+ので) - strong bonus
      // Ensures ないのに → ない+のに over ない+の+にこ+れ
      // Without this, AUX_否定→PART_準体(-0.8) makes の path win over のに
      {EPOS::AuxNegativeNai, EPOS::ParticleConj, cost::kStrongBonus},

      // ParticleNo → AuxCopulaDesu (ん+です/でし for んです/んでした) - strong bonus
      // Ensures んでした → ん+でし+た over ん+で+し+た
      {EPOS::ParticleNo, EPOS::AuxCopulaDesu, cost::kStrongBonus},

      // AuxNegativeNu → AuxCopulaDesu (ん+でし for ませんでした) - strong bonus
      // Ensures ませんでした → ませ+ん+でし+た (negative aux ん)
      {EPOS::AuxNegativeNu, EPOS::AuxCopulaDesu, cost::kStrongBonus},

      // AuxNegativeNu → ParticleTopic (ずに+は for ずにはいられない) - strong bonus
      // Ensures ずには → ずに+は(topic) over ずに+はいられ(verb)
      {EPOS::AuxNegativeNu, EPOS::ParticleTopic, cost::kStrongBonus},

      // ParticleNo → AuxCopulaDa (ん+だ for んだ) - strong bonus
      // Ensures んだ → ん+だ over ん+だ(VERB)
      {EPOS::ParticleNo, EPOS::AuxCopulaDa, cost::kStrongBonus},

      // ParticleNo → Noun (の+学生, の+画像) - strong bonus
      // Genitive の + noun is fundamental Japanese grammar
      {EPOS::ParticleNo, EPOS::Noun, cost::kStrongBonus},

      // AuxDesireTai → AuxTenseTa (たかっ+た) - strong bonus
      {EPOS::AuxDesireTai, EPOS::AuxTenseTa, cost::kStrongBonus},

      // AuxDesireTai → AuxNegativeNai (たく+ない/なかっ) - moderate bonus
      // 走り出したくなかった → 走り出し+たく+なかっ+た (not 走り+出したく+なかっ+た)
      {EPOS::AuxDesireTai, EPOS::AuxNegativeNai, cost::kModerateBonus},

      // AuxTenseTa → verb forms - prohibit. A completed predicate cannot take a
      // second bare verb without a connective boundary. Cover every verb form;
      // limiting this to onbin/past shapes permits fragments such as た+だく.
      {EPOS::AuxTenseTa, EPOS::VerbShuushikei, cost::kAlmostNever},
      {EPOS::AuxTenseTa, EPOS::VerbRenyokei, cost::kAlmostNever},
      {EPOS::AuxTenseTa, EPOS::VerbMizenkei, cost::kAlmostNever},
      {EPOS::AuxTenseTa, EPOS::VerbOnbinkei, cost::kAlmostNever},
      {EPOS::AuxTenseTa, EPOS::VerbTeForm, cost::kAlmostNever},
      {EPOS::AuxTenseTa, EPOS::VerbKateikei, cost::kAlmostNever},
      {EPOS::AuxTenseTa, EPOS::VerbMeireikei, cost::kAlmostNever},
      {EPOS::AuxTenseTa, EPOS::VerbRentaikei, cost::kAlmostNever},
      {EPOS::AuxTenseTa, EPOS::VerbTaForm, cost::kAlmostNever},
      {EPOS::AuxTenseTa, EPOS::VerbTaraForm, cost::kAlmostNever},

      // ParticleTopic → AuxTenseTa - prohibit
      // The past auxiliary attaches to a verb/adjective renyokei, never to a topic
      // particle, so は+た is not a real boundary. Prevents an isolated hiragana noun
      // from splitting into は(係助詞)+た(過去)+… (はたけ → は+た+け).
      {EPOS::ParticleTopic, EPOS::AuxTenseTa, cost::kSevere},

      // AuxAspectIru → AuxTenseTa (い+た) - moderate bonus
      {EPOS::AuxAspectIru, EPOS::AuxTenseTa, cost::kModerateBonus},

      // AuxAspectOku → AuxTenseTa (とい+た, どい+た) - strong bonus
      // Contracted ~ておく form + past tense: 見とい+た, 読んどい+た
      {EPOS::AuxAspectOku, EPOS::AuxTenseTa, cost::kStrongBonus},

      // Both contracted and uncontracted renyokei forms accept polite ます.
      // The stronger connection resolves their lexical homographs (おき/とき).
      {EPOS::AuxAspectOku, EPOS::AuxTenseMasu, cost::kVeryStrongBonus},

      // The progressive auxiliary conjugates as an Ichidan verb. Its stem い
      // therefore takes the negative auxiliary directly (覚えて+い+なかった).
      // This also distinguishes subsidiary い from the independent verb いる.
      {EPOS::AuxAspectIru, EPOS::AuxNegativeNai, cost::kStrongBonus},

      // AuxAspectIru → AuxTenseMasu (い+ます) - strong bonus for aspect plus politeness
      // Ensures 学んで+い+ます uses AuxAspectIru (auxiliary) not VerbRenyokei
      {EPOS::AuxAspectIru, EPOS::AuxTenseMasu, cost::kStrongBonus},

      // The trial subsidiary みる conjugates as an Ichidan auxiliary. Its stem
      // therefore accepts the same independent tense, negation, and desiderative
      // auxiliaries as a lexical Ichidan renyokei (試してみ+ます/ない/たい).
      {EPOS::AuxAspectMiru, EPOS::AuxTenseMasu, cost::kStrongBonus},
      {EPOS::AuxAspectMiru, EPOS::AuxNegativeNai, cost::kStrongBonus},
      {EPOS::AuxAspectMiru, EPOS::AuxDesireTai, cost::kStrongBonus},
      {EPOS::AuxAspectMiru, EPOS::AuxTenseTa, cost::kModerateBonus},
      {EPOS::AuxAspectMiru, EPOS::AuxVolitional, cost::kModerateBonus},

      // AuxAspectIru → AuxPassive (い+られ in potential/passive) - moderate bonus
      // いられる = いる + られる (potential: can stay/be)
      // E.g., はいられない → は + い + られ + ない
      {EPOS::AuxAspectIru, EPOS::AuxPassive, cost::kModerateBonus},

      // AuxAspectIru → VerbShuushikei (い+ける) - penalty
      // Progressive auxiliary い cannot be followed by a new verb
      // Prevents て+い+ける from beating て+いける (potential of いく)
      {EPOS::AuxAspectIru, EPOS::VerbShuushikei, cost::kRare},

      // AuxCopulaDa → AuxTenseTa (だっ+た) - strong bonus
      {EPOS::AuxCopulaDa, EPOS::AuxTenseTa, cost::kStrongBonus},

      // AuxCopulaDa → AuxTenseMasu (あり+ます in であります) - strong bonus
      // Ensures で+あり+ます uses AuxCopulaDa for both で and あり
      {EPOS::AuxCopulaDa, EPOS::AuxTenseMasu, cost::kStrongBonus},

      // AuxCopulaDesu → AuxTenseTa (でし+た) - strong bonus for polite past copula
      // Ensures 本でした → 本+でし+た over 本+で+し+た
      {EPOS::AuxCopulaDesu, EPOS::AuxTenseTa, cost::kStrongBonus},

      // AuxTenseTa → AuxCopulaDesu (た+です) - moderate bonus for polite past
      // e.g., 長かっ+た+です, 美しかっ+た+です (adjective past polite)
      {EPOS::AuxTenseTa, EPOS::AuxCopulaDesu, cost::kModerateBonus},

      // =========================================================================
      // Auxiliary → Particle
      // =========================================================================

      // AuxCopulaDa → ParticleConj (な+ので, な+のに) - strong bonus
      // Ensures なので → な+ので over な+の+で
      // Without this, PART_準体→AUX_断定 bonus makes の+で(AUX) path win
      {EPOS::AuxCopulaDa, EPOS::ParticleConj, cost::kStrongBonus},

      // AuxTenseTa → Noun/Pronoun (食べた+人, 来た+彼) - moderate bonus for past+noun
      // POS-level AUX→NOUN=0.5 penalty is too harsh for this natural connection
      {EPOS::AuxTenseTa, EPOS::Noun, cost::kModerateBonus},
      {EPOS::AuxTenseTa, EPOS::Pronoun, cost::kModerateBonus},

      // AuxTenseTa → ParticleFinal (た+ね/よ) - minor bonus
      {EPOS::AuxTenseTa, EPOS::ParticleFinal, cost::kMinorBonus},

      // AuxTenseMasu → ParticleFinal (ます+ね/よ) - minor bonus
      {EPOS::AuxTenseMasu, EPOS::ParticleFinal, cost::kMinorBonus},

      // AuxCopulaDesu → ParticleFinal (です+ね/よ) - minor bonus
      {EPOS::AuxCopulaDesu, EPOS::ParticleFinal, cost::kMinorBonus},

      // AuxCopulaDa → ParticleFinal (だ+ね/よ) - minor bonus
      {EPOS::AuxCopulaDa, EPOS::ParticleFinal, cost::kMinorBonus},

      // =========================================================================
      // Noun → Particles
      // =========================================================================

      // Noun → ParticleCase (机+が/を/に) - neutral (very common)
      {EPOS::Noun, EPOS::ParticleCase, cost::kNeutral},

      // Noun → ParticleTopic (机+は/も) - minor bonus
      // Helps サイズ+は+あり win over サイズ+はあり (particle-starting verb)
      {EPOS::Noun, EPOS::ParticleTopic, cost::kMinorBonus},

      // Noun → ParticleAdverbial (そん+だけ, あん+だけ) - strong bonus
      // Ensures そんだけ → そん+だけ over そん+だ+け
      {EPOS::Noun, EPOS::ParticleAdverbial, cost::kStrongBonus},

      // Noun → ParticleBinding (時間+さえ, 水+すら) - strong bonus
      // Binding particles attach to nouns just like adverbial ones; without this
      // 時間さえ loses to 時間+さ(する未然, suru-passive surface bonus)+え and
      // 水すら is absorbed into a fabricated verb blob
      {EPOS::Noun, EPOS::ParticleBinding, cost::kStrongBonus},

      // ParticleBinding → AdjBasic (さえ+ない, すら+ない) - moderate bonus
      // Existence-negation ない after a binding particle (時間さえない);
      // mirrors NounFormal→AdjBasic. Without this the fragment path
      // さ(する未然)+え(AUX可能)+ない outruns さえ+ない via the AUX chain bonus
      {EPOS::ParticleBinding, EPOS::AdjBasic, cost::kModerateBonus},

      // Noun → AdjNaAdj (一番+獰猛, とても+大切) - strong bonus
      // Prevents long kanji noun from absorbing na-adjective stem
      // (e.g., 一番獰猛+な → 一番+獰猛+な)
      {EPOS::Noun, EPOS::AdjNaAdj, cost::kStrongBonus},

      // NounFormal → ParticleConj (こと+が) - neutral
      {EPOS::NounFormal, EPOS::ParticleCase, cost::kNeutral},

      // NounFormal → AuxCopulaDa (はず+だ, つもり+だ, ところ+だ) - moderate bonus
      // Ensures formal noun + だ split over verb candidate (e.g., はずだ as VERB)
      {EPOS::NounFormal, EPOS::AuxCopulaDa, cost::kModerateBonus},

      // NounFormal → AuxCopulaDesu (はず+です, つもり+です) - strong bonus
      // Ensures はずです → はず+です over はず+で+す
      {EPOS::NounFormal, EPOS::AuxCopulaDesu, cost::kStrongBonus},

      // NounFormal → AuxNegativeNai (こと+ない) - moderate bonus
      // Ensures こと+ない over こと+な+い (な=AuxCopulaDa連用形)
      {EPOS::NounFormal, EPOS::AuxNegativeNai, cost::kModerateBonus},

      // NounFormal → AdjBasic (こと+ない adjective) - very strong bonus
      // Ensures そんなこと+ない(ADJ) wins over そんなこと+ない(AUX)
      // When ない follows a formal noun, it's the existence-negation adjective
      // Needs to overcome: AUX word cost (0.3) + AuxNegativeNai bonus (-0.5) = -0.2
      // vs ADJ word cost (0.5) + this bonus = must be < -0.2
      {EPOS::NounFormal, EPOS::AdjBasic, cost::kVeryStrongBonus},

      // NounFormal → ParticleAdverbial (こと+だけ, はず+だけ) - strong bonus
      // Prevents ことだけ → こと+だ+け split
      {EPOS::NounFormal, EPOS::ParticleAdverbial, cost::kStrongBonus},

      // =========================================================================
      // Pronoun → Particles
      // =========================================================================

      // Pronoun → ParticleCase (あれ+が, これ+を, それ+に) - moderate bonus
      // Pronouns naturally take case particles; beats VERB_連用 interpretation
      // E.g., あれが欲しい → あれ(PRON)+が, not あれ(VERB ある)+が
      {EPOS::Pronoun, EPOS::ParticleCase, cost::kModerateBonus},

      // Pronoun → AuxCopulaDesu (何+です, これ+です) - moderate bonus
      // Pronouns naturally take polite copula; matches Noun→AuxCopulaDesu bonus
      {EPOS::Pronoun, EPOS::AuxCopulaDesu, cost::kModerateBonus},

      // Pronoun → ParticleAdverbial (あれ+だけ, それ+だけ)
      {EPOS::Pronoun, EPOS::ParticleAdverbial, cost::kStrongBonus},

      // Pronoun → Adverb penalty (何+もし should be 何+も+し, not 何+もし(ADV))
      // Pronouns are followed by particles, not adverbs. PRON→ADV is rarely valid.
      {EPOS::Pronoun, EPOS::Adverb, cost::kRare},

      // =========================================================================
      // Determiner → Noun (連体詞は名詞を修飾)
      // =========================================================================

      // Determiner → Noun (そんな+こと, こんな+話) - very strong bonus
      // Ensures そんなことない → そんな+こと+ない over そん+な+こと+ない
      // Ensures あんな+人 over あん+な+人 (NOUN→AUX_断定→NOUN chain has -2.5 total)
      {EPOS::Determiner, EPOS::Noun, kDeterminerNounBonus},
      {EPOS::Determiner, EPOS::NounFormal, kDeterminerNounBonus},
      {EPOS::Determiner, EPOS::NounProper, kDeterminerNounBonus},

      // Determiner → ParticleNo (という+の, こんな+の)
      // 準体助詞の follows determiners naturally (same grammatical slot as nouns)
      // Use same bonus as DET→NOUN so the の+は split path can compete
      {EPOS::Determiner, EPOS::ParticleNo, kDeterminerNounBonus},

      // Determiner → Adjective (その+薄暗い+部屋, この+大きい+建物)
      // Determiners modify adjective+noun combinations in Japanese
      // Uses same bonus as DET→NOUN to allow adjective path to compete
      {EPOS::Determiner, EPOS::AdjBasic, kDeterminerNounBonus},
      {EPOS::Determiner, EPOS::AdjRenyokei, kDeterminerNounBonus},

      // Determiner → Determiner (そんな+大きな, こんな+小さな) - strong bonus
      // Demonstrative determiners stack with descriptive ones; without this the
      // high POS-level DET→DET default (0.8) makes the fragment path win
      // (そんな大きな → そん(NOUN)+な(AUX_断定)+大きな).
      {EPOS::Determiner, EPOS::Determiner, cost::kStrongBonus},

      // ParticleCase → Determiner (rare; 連体詞 rarely follows case particles)
      // Determiners introduce a new modifier clause and don't follow が/を/に/と/から/etc.
      // Counteracts overly strong DET→NOUN bonus for verb-ambiguous hiragana DET like かかる
      // (e.g., 壁にかかる絵 should be VERB, not DET).
      {EPOS::ParticleCase, EPOS::Determiner, cost::kStrong},

      // AuxTenseTa → Determiner (past tense should not be followed by determiner)
      // Prevents over-greedy match of L1 DET like かの in `た+か+の` (e.g., 覚めたかのような).
      // The correct parse is た(past) + か(question particle) + の(particle).
      // Needs kSevere to outweigh the DET→NounFormal bonus (-2.5 for かの→よう).
      {EPOS::AuxTenseTa, EPOS::Determiner, cost::kSevere},

      // Pronoun → Determiner (pronoun does not directly take a determiner)
      // Prevents over-greedy match of L1 DET like かの in `いくつ+か+の` (e.g., いくつかの限界).
      // The correct parse is いくつ(pronoun) + か(particle) + の(particle).
      {EPOS::Pronoun, EPOS::Determiner, cost::kStrong},

      // =========================================================================
      // Noun → Verb (サ変動詞パターン)
      // =========================================================================

      // Noun → VerbRenyokei (得+し for サ変動詞 得する) - moderate bonus
      // This favors 名詞+し split over 名詞し as single token
      {EPOS::Noun, EPOS::VerbRenyokei, cost::kModerateBonus},

      // =========================================================================
      // Noun → Copula/Negative
      // =========================================================================

      // Noun → AuxCopulaDa (学生+だ) - moderate bonus
      {EPOS::Noun, EPOS::AuxCopulaDa, cost::kModerateBonus},

      // Noun → AuxCopulaDesu (学生+です) - moderate bonus
      {EPOS::Noun, EPOS::AuxCopulaDesu, cost::kModerateBonus},

      // Noun → AuxNegativeNai (間違い+ない, 違い+ない) - moderate bonus
      // For idiomatic patterns meaning "certain" or "no doubt"
      {EPOS::Noun, EPOS::AuxNegativeNai, cost::kModerateBonus},

      // Noun → AuxCausative (色褪+せる) - strong penalty
      // Causative auxiliary only follows verb mizenkei, never nouns
      {EPOS::Noun, EPOS::AuxCausative, cost::kStrong},

      // Noun → AuxPassive (色褪+れる) - strong penalty
      // Passive auxiliary only follows verb mizenkei, never nouns
      {EPOS::Noun, EPOS::AuxPassive, cost::kStrong},

      // Noun → aspect auxiliary いる/くる (驚+い, 先生+き): aspect attaches only to a
      // te-form, never a bare noun (食べて+いた, 走って+きた). Prevents 間続+い+た and
      // overcomes the DET→NOUN bonus on prefix compounds like 先生.
      {EPOS::Noun, EPOS::AuxAspectIru, cost::kSevere},
      {EPOS::Noun, EPOS::AuxAspectKuru, cost::kProhibitive},

      // Binding particle (は/も) → aspect auxiliary: aspect attaches only to a
      // te-form, so は/も before き/いく/いる is a mis-parse (ではきもの → で+は+きもの).
      {EPOS::ParticleTopic, EPOS::AuxAspectKuru, cost::kProhibitive},
      {EPOS::ParticleTopic, EPOS::AuxAspectIku, cost::kProhibitive},
      {EPOS::ParticleTopic, EPOS::AuxAspectIru, cost::kSevere},

      // NaAdj → AuxCopulaDa (静か+だ) - strong bonus
      {EPOS::AdjNaAdj, EPOS::AuxCopulaDa, cost::kStrongBonus},

      // NaAdj → AuxCopulaDesu (静か+です) - strong bonus
      {EPOS::AdjNaAdj, EPOS::AuxCopulaDesu, cost::kStrongBonus},

      // Adverb → AuxCopulaDa/Desu - penalty: adverbs modify verbs/adjectives, they
      // don't directly take copula (そうです: そう should be na-adjective, not adverb).
      {EPOS::Adverb, EPOS::AuxCopulaDa, cost::kRare},
      {EPOS::Adverb, EPOS::AuxCopulaDesu, cost::kRare},

      // AuxCopulaDa → Noun (さすがな+人, 静かな+部屋) - strong bonus
      // Copula な(連体形 of だ) + Noun is the na-adjective attributive pattern
      {EPOS::AuxCopulaDa, EPOS::Noun, cost::kStrongBonus},

      // AuxCopulaDa → NounFormal (な+もの, な+こと) - strong bonus
      // Ensures 妙なもの → 妙+な+もの over 妙+な+も+の
      // Without this, AUX_断定→PART_係(-0.5) makes も path win over もの
      {EPOS::AuxCopulaDa, EPOS::NounFormal, cost::kStrongBonus},

      // AdjStem → Suffix (な+さ in なさそう) - strong bonus for nominalization
      // This favors な(ADJ stem of ない) + さ(nominalization suffix) over さ(する mizenkei)
      {EPOS::AdjStem, EPOS::Suffix, cost::kStrongBonus},

      // VerbRenyokei → Suffix (遅れ+がち, 忘れ+がち) - strong bonus
      // This favors verb renyokei + suffix pattern over merged tokens
      {EPOS::VerbRenyokei, EPOS::Suffix, cost::kStrongBonus},
  };
  applyRules(table, kRules);
}  // namespace suzume::analysis::bigram_rules

}  // namespace suzume::analysis::bigram_rules
