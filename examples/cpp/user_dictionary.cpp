/**
 * User dictionary - load custom vocabulary for domain-specific text
 *
 * Demonstrates: user dictionary loading from file and from memory,
 * before/after comparison of analysis results.
 */
#include <algorithm>
#include <iostream>

#include "suzume/suzume.hpp"

void printMorphemes(const std::vector<suzume::Morpheme>& morphemes) {
  for (const auto& m : morphemes) {
    std::cout << "  " << m.surface << " [" << m.pos << "]";
    if (!m.base_form.empty() && m.base_form != m.surface) {
      std::cout << " (lemma: " << m.base_form << ")";
    }
    std::cout << "\n";
  }
}

int main() {
  suzume::Options opts;
  opts.skip_user_dictionary = true;  // Start clean
  suzume::Tokenizer analyzer(opts);
  if (!analyzer) {
    std::cerr << "failed to create tokenizer: " << suzume::Tokenizer::lastError() << "\n";
    return 1;
  }

  std::string text = "青空りんご園の案内図を作りました";

  // Without user dictionary
  std::cout << "Without user dictionary:\n";
  printMorphemes(analyzer.analyze(text));

  // TSV: surface<TAB>POS[<TAB>conj_type][<TAB>lemma]
  std::string dict_data = "青空りんご園\tNOUN\n";
  if (!analyzer.loadUserDictionary(dict_data)) {
    std::cerr << "failed to load in-memory user dictionary: " << suzume::Tokenizer::lastError() << "\n";
    return 1;
  }
  std::cout << "\nWith user dictionary:\n";
  const auto morphemes = analyzer.analyze(text);
  printMorphemes(morphemes);
  const bool found_user_entry =
      std::any_of(morphemes.begin(), morphemes.end(), [](const auto& morpheme) { return morpheme.is_user_dict; });
  if (!found_user_entry) {
    std::cerr << "loaded user dictionary entry was not used\n";
    return 1;
  }

  // Or load from file:
  // analyzer.loadUserDictionary(std::string_view{"surface\tNOUN\n"});

  return 0;
}
