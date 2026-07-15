#include "entries_internal.h"

namespace suzume::dictionary::entries {

std::vector<DictionaryEntry> getCompoundParticleEntries() {
  return {
      // Relation (関連)
      particle("について", EPOS::ParticleCase),  // beat false positive adjective candidates

      // Cause/Means (原因・手段)
      particle("によって", EPOS::ParticleConj),  // beat によっ(verb)+て split
      particle("により", EPOS::ParticleConj),
      // Note: による removed - grammatically に+よる (格助詞+動詞連体形)
      // Note: によると removed - MeCab splits as に+よる+と (引用表現)
      // Note: によれば removed - grammatically に+よれ+ば
      // These compound particles are better split for grammatical accuracy

      // Place/Situation (場所・状況)
      particle("において", EPOS::ParticleConj),  // prevent に+おい(verb)+て split
      particle("における", EPOS::ParticleCase),  // prevent に+おける split
      particle("にて", EPOS::ParticleCase),

      // Capacity/Viewpoint (資格・観点)
      particle("として", EPOS::ParticleConj),  // prevent と+し(VERB)+て split
      particle("にとって", EPOS::ParticleConj),
      // にとっても removed — MeCab splits as にとって+も
      particle("に関して", EPOS::ParticleConj),  // MeCab compatible
      particle("に際して", EPOS::ParticleConj),  // MeCab compatible
      particle("に対して", EPOS::ParticleConj),

      // Duration/Scope (範囲・期間)
      particle("にわたって", EPOS::ParticleConj),
      particle("にわたり", EPOS::ParticleConj),
      particle("にあたって", EPOS::ParticleConj),
      particle("にあたり", EPOS::ParticleConj),

      // Topic/Means (話題・手段)
      particle("をめぐって", EPOS::ParticleConj),
      particle("をめぐり", EPOS::ParticleConj),
      particle("をもって", EPOS::ParticleConj),
  };
}

}  // namespace suzume::dictionary::entries
