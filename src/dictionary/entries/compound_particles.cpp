#include "entries_internal.h"

namespace suzume::dictionary::entries {

EntrySpecRange getCompoundParticleEntries() {
  static constexpr EntrySpec kEntries[] = {
      // Relation (関連)
      particle("について", EPOS::ParticleCase),  // beat false positive adjective candidates

      // Cause/Means (原因・手段)
      particle("によって", EPOS::ParticleCase),  // beat によっ(verb)+て split
      particle("により", EPOS::ParticleCase),
      particle("につれて", EPOS::ParticleCase),  // beat に+つれ(verb)+て split
      particle("にかけて", EPOS::ParticleCase),  // beat にか(noun)+けて(verb) split
      // Note: による removed - grammatically に+よる (格助詞+動詞連体形)
      // Note: によると removed - MeCab splits as に+よる+と (引用表現)
      // Note: によれば removed - grammatically に+よれ+ば
      // These compound particles are better split for grammatical accuracy

      // Place/Situation (場所・状況)
      particle("において", EPOS::ParticleCase),  // prevent に+おい(verb)+て split
      particle("における", EPOS::ParticleCase),  // prevent に+おける split
      particle("にて", EPOS::ParticleCase),

      // Capacity/Viewpoint (資格・観点)
      particle("として", EPOS::ParticleCase),  // prevent と+し(VERB)+て split
      particle("にとって", EPOS::ParticleCase),
      // にとっても removed — MeCab splits as にとって+も
      particle("に関して", EPOS::ParticleCase),
      particle("に関する", EPOS::ParticleCase),
      particle("に際して", EPOS::ParticleCase),
      particle("に対して", EPOS::ParticleCase),
      particle("に対する", EPOS::ParticleCase),

      // Duration/Scope (範囲・期間)
      particle("にわたって", EPOS::ParticleCase),
      particle("にわたり", EPOS::ParticleCase),
      particle("にあたって", EPOS::ParticleCase),
      particle("にあたり", EPOS::ParticleCase),

      // Topic/Means (話題・手段)
      particle("をめぐって", EPOS::ParticleCase),
      particle("をめぐり", EPOS::ParticleCase),
      particle("をもって", EPOS::ParticleCase),
      particle("を通じて", EPOS::ParticleCase),
      particle("を通して", EPOS::ParticleCase),
  };
  return makeEntrySpecRange(kEntries);
}

}  // namespace suzume::dictionary::entries
