#include "entries_internal.h"

namespace suzume::dictionary::entries {

// =============================================================================
EntrySpecRange getParticleEntries() {
  static constexpr EntrySpec kEntries[] = {
      // Case particles (格助詞)
      particle("が", EPOS::ParticleCase),
      particle("を", EPOS::ParticleCase),
      particle("に", EPOS::ParticleCase),
      particle("で", EPOS::ParticleCase),  // low cost for te-form split
      particle("と", EPOS::ParticleCase),
      particle("から", EPOS::ParticleCase),
      particle("まで", EPOS::ParticleCase),
      particle("より", EPOS::ParticleCase),
      particle("へ", EPOS::ParticleCase),

      // Topic/Binding particles (係助詞)
      particle("は", EPOS::ParticleTopic),
      particle("も", EPOS::ParticleTopic),
      particle("こそ", EPOS::ParticleBinding),
      particle("さえ", EPOS::ParticleBinding),
      particle("すら", EPOS::ParticleBinding),
      particle("しか", EPOS::ParticleBinding),
      particle("でも", EPOS::ParticleAdverbial),

      // Conjunctive particles (接続助詞)
      particle("て", EPOS::ParticleConj),  // low cost for te-form split
      particle("で", EPOS::ParticleConj),  // te-form for hatsuonbin verbs (読んで, 飛んで)
      particle("ば", EPOS::ParticleConj),
      particle("たら", EPOS::ParticleConj),
      particle("なら", EPOS::ParticleConj),
      // Contracted conditional ちゃ (= ては): なく+ちゃ+いけない.
      particle("ちゃ", EPOS::ParticleConj),
      // Note: ら removed - たら handles conditional, ら suffix is in L2 as SUFFIX
      particle("ながら", EPOS::ParticleConj),
      particle("つつ", EPOS::ParticleConj),  // 反復・並行の接続助詞 (連用形接続): 重ね+つつ, 増加し+つつ+ある
      particle("とともに", EPOS::ParticleConj),  // 並行・同時: 読むとともに書く
      particle("とも", EPOS::ParticleConj),      // 譲歩: 読まずとも, 食べずとも
      particle("ど", EPOS::ParticleConj),        // 文語的譲歩: といえど
      particle("ども", EPOS::ParticleConj),      // 譲歩: といえども, いかに…ども
      particle("のに", EPOS::ParticleConj),
      particle("ので", EPOS::ParticleConj),
      // Causal premise: 書いたからには, 高いからには.
      particle("からには", EPOS::ParticleConj),
      particle("けれど", EPOS::ParticleConj),
      particle("けど", EPOS::ParticleConj),
      particle("けども", EPOS::ParticleConj),
      particle("けれども", EPOS::ParticleConj),
      particle("し", EPOS::ParticleConj),    // 列挙・理由 (接続助詞)
      particle("たり", EPOS::ParticleConj),  // 並立助詞 (食べたり飲んだり)
      particle("だり", EPOS::ParticleConj),  // 並立助詞 (voiced: 飲んだり)
      particle("なり", EPOS::ParticleConj),  // 動作直後: 鳴るなり
      particle("や", EPOS::ParticleConj),    // 並立助詞 (AやB)

      // Quotation particles (引用助詞)
      particle("って", EPOS::ParticleQuote),

      // Final particles (終助詞)
      particle("か", EPOS::ParticleFinal),
      particle("け", EPOS::ParticleFinal),  // colloquial variant (こんだけ → こん+だ+け)
      particle("な", EPOS::ParticleFinal),
      particle("ね", EPOS::ParticleFinal),
      particle("よ", EPOS::ParticleFinal),
      particle("わ", EPOS::ParticleFinal),
      particle("ぞ", EPOS::ParticleFinal),
      particle("ぜ", EPOS::ParticleFinal),
      particle("の", EPOS::ParticleNo),               // nominalizer
      {"ん", POS::Particle, EPOS::ParticleNo, "の"},  // colloquial の
      particle("じゃん", EPOS::ParticleFinal),
      particle("っけ", EPOS::ParticleFinal),
      particle("かしら", EPOS::ParticleFinal),

      // Adverbial particles (副助詞)
      particle("かも", EPOS::ParticleAdverbial),  // prevent か+も split in かもしれない
      particle("ばかり", EPOS::ParticleAdverbial),
      particle("だけ", EPOS::ParticleAdverbial),
      particle("のみ", EPOS::ParticleAdverbial),
      particle("ほど", EPOS::ParticleAdverbial),
      particle("くらい", EPOS::ParticleAdverbial),
      particle("ぐらい", EPOS::ParticleAdverbial),
      particle("など", EPOS::ParticleAdverbial),
      particle("とか", EPOS::ParticleAdverbial),  // 並立 (AとかBとか)
      particle("なんて", EPOS::ParticleAdverbial),
      particle("ずつ", EPOS::ParticleAdverbial),  // distributive 副助詞 - prevent ず(打消)+つ split after a quantity
      particle("ってば", EPOS::ParticleFinal),
      particle("ったら", EPOS::ParticleFinal),

  };
  return makeEntrySpecRange(kEntries);
}

}  // namespace suzume::dictionary::entries
