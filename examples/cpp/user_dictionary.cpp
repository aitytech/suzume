/**
 * User dictionary - load custom vocabulary for domain-specific text
 *
 * Demonstrates: user dictionary loading from file and from memory,
 * before/after comparison of analysis results.
 */
#include <algorithm>
#include <iostream>

#include "suzume.h"

void printMorphemes(const std::vector<suzume::core::Morpheme>& morphemes) {
  for (const auto& m : morphemes) {
    std::cout << "  " << m.surface << " [" << suzume::core::posToString(m.pos) << "]";
    if (!m.lemma.empty() && m.lemma != m.surface) {
      std::cout << " (lemma: " << m.lemma << ")";
    }
    std::cout << "\n";
  }
}

int main() {
  suzume::SuzumeOptions opts;
  opts.skip_user_dictionary = true;  // Start clean
  suzume::Suzume analyzer(opts);

  std::string text = "青空りんご園の案内図を作りました";

  // Without user dictionary
  std::cout << "Without user dictionary:\n";
  printMorphemes(analyzer.analyze(text));

  // TSV: surface<TAB>POS[<TAB>conj_type][<TAB>lemma]
  std::string dict_data = "青空りんご園\tNOUN\n";
  if (!analyzer.loadUserDictionaryFromMemory(dict_data.data(), dict_data.size())) {
    std::cerr << "failed to load in-memory user dictionary\n";
    return 1;
  }
  std::cout << "\nWith user dictionary:\n";
  const auto morphemes = analyzer.analyze(text);
  printMorphemes(morphemes);
  const bool found_user_entry = std::any_of(morphemes.begin(), morphemes.end(),
                                            [](const auto& morpheme) { return morpheme.features.is_user_dict; });
  if (!found_user_entry) {
    std::cerr << "loaded user dictionary entry was not used\n";
    return 1;
  }

  // Or load from file:
  // analyzer.loadUserDictionary("/path/to/user.tsv");

  return 0;
}
