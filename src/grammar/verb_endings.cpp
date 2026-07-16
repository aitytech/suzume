/**
 * @file verb_endings.cpp
 * @brief Verb ending patterns for reverse inflection analysis
 *
 * Godan patterns are generated from Conjugation::getGodanRows() for consistency.
 * Irregular verb patterns (Ichidan, Suru, Kuru, IAdjective) are manually defined.
 */

#include "verb_endings.h"

#include <utility>

namespace suzume::grammar {

namespace {

// Fixed Godan-type iteration order for deterministic ending generation.
//
// Keep the reverse-lookup preference explicit: it differs from the canonical
// table's verb-type order for shared onbin forms and is part of the cross-binding
// output contract.
//
// The order is chosen to agree with Conjugation::getGodanTypesByOnbin()'s declared
// onbin preference, so that when no dictionary entry breaks the tie the same lemma
// wins everywhere:
//   - い-onbin: Ka before Ga
//   - っ-onbin: Ka before Ra before Ta before Wa
//   - ん-onbin: Ma before Ba before Na
constexpr VerbType kGodanTypeOrder[] = {
    VerbType::GodanKa, VerbType::GodanGa, VerbType::GodanSa, VerbType::GodanMa, VerbType::GodanBa,
    VerbType::GodanNa, VerbType::GodanRa, VerbType::GodanTa, VerbType::GodanWa,
};

// Generate all Godan verb endings from Conjugation::getGodanRow()
std::vector<VerbEnding> generateGodanEndings() {
  std::vector<VerbEnding> endings;
  endings.reserve(80);  // Approx 9 types * 9 forms

  for (VerbType type : kGodanTypeOrder) {
    const auto* row_ptr = Conjugation::getGodanRow(type);
    if (row_ptr == nullptr) {
      continue;
    }
    const auto& row = *row_ptr;
    const GodanVowels vowels = encodeGodanVowels(row);
    const std::string& base = vowels.base;
    const std::string& a_row = vowels.a;
    const std::string& i_row = vowels.i;
    const std::string& e_row = vowels.e;
    const std::string& o_row = vowels.o;

    // Onbinkei (音便形): explicit onbin (い/っ/ん) or, for サ行, the い段 form.
    endings.push_back({onbinFormOf(row), base, type, conn::kVerbOnbinkei, true});

    // Special case: GodanKa also has っ-onbin for いく (irregular)
    if (type == VerbType::GodanKa) {
      endings.push_back({"っ", base, type, conn::kVerbOnbinkei, true});
    }

    // Renyokei (連用形)
    endings.push_back({i_row, base, type, conn::kVerbRenyokei, false});

    // Mizenkei (未然形)
    endings.push_back({a_row, base, type, conn::kVerbMizenkei, false});

    // Potential (可能形) - skip for GodanRa (conflicts with Ichidan stems)
    if (type != VerbType::GodanRa) {
      endings.push_back({e_row, base, type, conn::kVerbPotential, false});
    }

    // Kateikei (仮定形)
    endings.push_back({e_row, base, type, conn::kVerbKatei, false});

    // Meireikei (命令形)
    endings.push_back({e_row, base, type, conn::kVerbMeireikei, false});

    // Volitional (意志形)
    endings.push_back({o_row, base, type, conn::kVerbVolitional, false});

    // Base/dictionary form (終止形)
    endings.push_back({base, base, type, conn::kVerbBase, false});
  }

  return endings;
}

// Manually defined irregular verb patterns
const std::vector<VerbEnding> kIrregularEndings = {
    // 一段 (食べる)
    {"", "る", VerbType::Ichidan, conn::kVerbOnbinkei, true},
    {"", "る", VerbType::Ichidan, conn::kVerbRenyokei, false},
    {"", "る", VerbType::Ichidan, conn::kVerbMizenkei, false},
    {"れ", "る", VerbType::Ichidan, conn::kVerbKatei, false},       // Hypothetical: 食べれ(ば)
    {"ろ", "る", VerbType::Ichidan, conn::kVerbMeireikei, false},   // Imperative: 食べろ
    {"よ", "る", VerbType::Ichidan, conn::kVerbVolitional, false},  // Volitional stem
    {"る", "る", VerbType::Ichidan, conn::kVerbBase, false},        // Base/dictionary form

    // サ変 (する)
    {"し", "する", VerbType::Suru, conn::kVerbOnbinkei, true},
    {"し", "する", VerbType::Suru, conn::kVerbRenyokei, false},
    {"し", "する", VerbType::Suru, conn::kVerbMizenkei, false},  // しない
    {"さ", "する", VerbType::Suru, conn::kVerbMizenkei, false},  // させる/される
    {"せ", "する", VerbType::Suru, conn::kVerbMizenkei, false},  // せず/せぬ
    // Empty suffix for suru-verb + passive/causative (開催+された → 開催する)
    {"", "する", VerbType::Suru, conn::kVerbMizenkei, false},
    // Empty suffix for suru-verb + してる/してた contraction
    {"", "する", VerbType::Suru, conn::kVerbOnbinkei, true},
    {"すれ", "する", VerbType::Suru, conn::kVerbKatei, false},       // すれば
    {"しろ", "する", VerbType::Suru, conn::kVerbMeireikei, false},   // Imperative: しろ
    {"せよ", "する", VerbType::Suru, conn::kVerbMeireikei, false},   // Imperative (classical): せよ
    {"しよ", "する", VerbType::Suru, conn::kVerbVolitional, false},  // しよう
    {"する", "する", VerbType::Suru, conn::kVerbBase, false},        // Base/dictionary form
    {"す", "する", VerbType::Suru, conn::kVerbBase, false},          // すべき special

    // カ変 (来る)
    {"き", "くる", VerbType::Kuru, conn::kVerbOnbinkei, true},
    {"き", "くる", VerbType::Kuru, conn::kVerbRenyokei, false},
    {"こ", "くる", VerbType::Kuru, conn::kVerbMizenkei, false},
    {"くれ", "くる", VerbType::Kuru, conn::kVerbKatei, false},       // くれば
    {"こい", "くる", VerbType::Kuru, conn::kVerbMeireikei, false},   // Imperative: こい
    {"こよ", "くる", VerbType::Kuru, conn::kVerbVolitional, false},  // こよう
    {"くる", "くる", VerbType::Kuru, conn::kVerbBase, false},        // Base/dictionary form

    // い形容詞 (美しい)
    {"", "い", VerbType::IAdjective, conn::kIAdjStem, false},
};

}  // namespace

const std::vector<VerbEnding>& getVerbEndings() {
  // Lazy initialization: generate Godan patterns and combine with irregulars
  static const std::vector<VerbEnding> kEndings = []() {
    std::vector<VerbEnding> all;

    // Add generated Godan endings
    auto godan = generateGodanEndings();
    all.reserve(godan.size() + kIrregularEndings.size());
    for (auto& ending : godan) {
      all.push_back(std::move(ending));
    }

    // Add irregular verb endings
    all.insert(all.end(), kIrregularEndings.begin(), kIrregularEndings.end());

    return all;
  }();

  return kEndings;
}

const std::unordered_map<uint16_t, std::vector<VerbEnding>>& getVerbEndingsByConn() {
  static const auto kGrouped = []() {
    std::unordered_map<uint16_t, std::vector<VerbEnding>> grouped;
    for (const auto& ending : getVerbEndings()) {
      grouped[ending.provides_conn].push_back(ending);
    }
    return grouped;
  }();
  return kGrouped;
}

}  // namespace suzume::grammar
