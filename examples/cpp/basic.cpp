/**
 * Basic morphological analysis example
 *
 * Build from the project root with `make examples`; the CMake target links the
 * complete `suzume` library.
 */
#include <iostream>

#include "core/types.h"
#include "suzume.h"

int main() {
  suzume::Suzume analyzer;

  // Analyze Japanese text
  auto morphemes = analyzer.analyze("すもももももももものうち");
  if (morphemes.empty()) {
    std::cerr << "analysis returned no morphemes\n";
    return 1;
  }

  for (const auto& m : morphemes) {
    std::cout << m.surface << "\t" << suzume::core::posToString(m.pos) << "\t" << m.getLemma() << "\n";
  }

  return morphemes.empty() ? 1 : 0;
}
