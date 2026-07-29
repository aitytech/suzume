/**
 * Basic morphological analysis example
 *
 * Build from the project root with `make examples`, or as an installed
 * consumer through `examples/consumer`.
 */
#include <iostream>

#include "suzume/suzume.hpp"

int main() {
  suzume::Tokenizer analyzer;
  if (!analyzer) {
    std::cerr << "failed to create tokenizer: " << suzume::Tokenizer::lastError() << "\n";
    return 1;
  }

  // Analyze Japanese text
  auto morphemes = analyzer.analyze("すもももももももものうち");
  if (morphemes.empty()) {
    std::cerr << "analysis returned no morphemes\n";
    return 1;
  }

  for (const auto& m : morphemes) {
    std::cout << m.surface << "\t" << m.pos << "\t" << m.base_form << "\n";
  }

  return morphemes.empty() ? 1 : 0;
}
