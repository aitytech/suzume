#include "bigram_table_internal.h"

namespace suzume::analysis::bigram_rules {

using EPOS = core::ExtendedPOS;
namespace cost = bigram_cost;

void setParticleAndLexicalPenaltyCosts(BigramMatrix& table) {
  static constexpr BigramRule kRules[] = {
      // =========================================================================
      // Particle → Particle penalties (unnatural adjacent particle chains)
      // =========================================================================
      // These particle combinations never occur adjacent in valid Japanese.
      // Penalizing them helps hiragana words (はし, もも, かし) compete against
      // false particle-chain interpretations (は+し, も+も, か+し).

      // PART_係 → PART_接続 (は+し, も+て): topic particle directly followed by
      // conjunctive particle is grammatically invalid (need content between them)
      {EPOS::ParticleTopic, EPOS::ParticleConj, cost::kRare},

      // PART_係 → PART_格 (は+が, は+を, も+に): topic+case markers never stack
      // adjacent on the same phrase (は...が with content between is fine)
      {EPOS::ParticleTopic, EPOS::ParticleCase, cost::kRare},

      // PART_係 → PART_係 (は+も, も+は): double topic marking never adjacent
      {EPOS::ParticleTopic, EPOS::ParticleTopic, cost::kVeryRare},

      // PART_格 → PART_格 (が+を, を+に, に+で): case particles never stack
      {EPOS::ParticleCase, EPOS::ParticleCase, cost::kVeryRare},

      // Note: PART_格 → PART_係 (に+は, で+は, と+は) is valid Japanese,
      // so preserve the stacked-particle boundary. This also covers に+も,
      // whose focus-particle reading is productive before a predicate.
      {EPOS::ParticleCase, EPOS::ParticleTopic, cost::kVeryStrongBonus},

      // Note: PART_接続 → PART_係 bonus is NOT set here because short particles
      // like て, し also have PART_接続 and would incorrectly bond with は, も.
      // Instead, compound particle (≥3 chars) + topic particle bonus is handled
      // in scorer.cpp with surface length check.

      // =========================================================================
      // Particle → Other penalties (prevents over-segmentation of hiragana words)
      // =========================================================================
      // Patterns like も+ちろん, と+にかく are not valid Japanese morphology.
      // Single-char particles followed by unknown hiragana are usually misanalyses.

      {EPOS::ParticleTopic, EPOS::Other, cost::kRare},
      {EPOS::ParticleCase, EPOS::Other, cost::kRare},
      {EPOS::ParticleFinal, EPOS::Other, cost::kRare},
      {EPOS::ParticleConj, EPOS::Other, cost::kUncommon},

      // =========================================================================
      // Conjunction → auxiliary and predicate rules
      // =========================================================================
      // Conjunctions like でも/だって do not directly precede auxiliaries.
      // 彼女でもない is 彼女|で|も|ない, not 彼女|でも(CONJ)|ない.
      {EPOS::Conjunction, EPOS::AuxNegativeNai, cost::kVeryRare},
      {EPOS::Conjunction, EPOS::ParticleFinal, cost::kRare},
      {EPOS::Conjunction, EPOS::VerbShuushikei, cost::kStrongBonus},
      {EPOS::Conjunction, EPOS::VerbRenyokei, cost::kStrongBonus},
      {EPOS::Conjunction, EPOS::AdjBasic, cost::kStrongBonus},
      {EPOS::Conjunction, EPOS::AdjStem, cost::kStrongBonus},
      {EPOS::Conjunction, EPOS::AdjRenyokei, cost::kStrongBonus},
      {EPOS::Conjunction, EPOS::AdjNaAdj, cost::kStrongBonus},

      // =========================================================================
      // Interjection and adverbial lexical boundaries
      // =========================================================================
      {EPOS::Adverb, EPOS::Interjection, cost::kStrongBonus},
      {EPOS::Interjection, EPOS::AuxGozaru, cost::kStrongBonus},
      {EPOS::Interjection, EPOS::AuxCopulaDesu, cost::kDoubleVeryStrongBonus},
      {EPOS::Adverb, EPOS::ParticleTopic, cost::kMinorBonus},
      {EPOS::Adverb, EPOS::ParticleCase, cost::kStrongBonus},
      {EPOS::Adverb, EPOS::ParticleFinal, cost::kVeryStrongBonus},
      {EPOS::Adverb, EPOS::Noun, cost::kModerateBonus},
      {EPOS::Adverb, EPOS::AdjBasic, cost::kStrongBonus},
      {EPOS::Adverb, EPOS::AdjRenyokei, cost::kStrongBonus},
      {EPOS::Adverb, EPOS::AdjNaAdj, cost::kStrongBonus},
      {EPOS::Adverb, EPOS::AdjKatt, cost::kStrongBonus},
      {EPOS::Adverb, EPOS::AdjStem, cost::kVeryRare},
      {EPOS::Adverb, EPOS::VerbRenyokei, cost::kModerateBonus},
      {EPOS::Adverb, EPOS::VerbShuushikei, cost::kModerateBonus},
      {EPOS::Adverb, EPOS::VerbOnbinkei, cost::kModerateBonus},
      {EPOS::Adverb, EPOS::VerbTaForm, cost::kModerateBonus},
      {EPOS::Prefix, EPOS::Noun, cost::kStrongBonus},
      {EPOS::Prefix, EPOS::VerbRenyokei, cost::kStrongBonus},

      // Particles do not introduce interjections within a running phrase.
      {EPOS::ParticleCase, EPOS::Interjection, cost::kAlmostNever},
      {EPOS::ParticleTopic, EPOS::Interjection, cost::kAlmostNever},
      {EPOS::ParticleNo, EPOS::Interjection, cost::kAlmostNever},
      {EPOS::ParticleAdverbial, EPOS::Interjection, cost::kAlmostNever},
      {EPOS::ParticleConj, EPOS::Interjection, cost::kAlmostNever},
      {EPOS::ParticleQuote, EPOS::Interjection, cost::kAlmostNever},
      {EPOS::ParticleFinal, EPOS::Interjection, cost::kAlmostNever},
  };
  applyRules(table, kRules, sizeof(kRules) / sizeof(kRules[0]));
}

}  // namespace suzume::analysis::bigram_rules
