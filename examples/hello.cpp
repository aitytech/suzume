// Minimal C++ consumer using the header-only wrapper (suzume/suzume.hpp).
//
// Build in-tree with -DSUZUME_BUILD_EXAMPLES=ON, or standalone against an
// installed package via examples/consumer/CMakeLists.txt.
#include <cstdio>
#include <string>

#include "suzume/suzume.hpp"

int main(int argc, char** argv) {
  const std::string text = argc > 1 ? argv[1] : "東京都に住んでいます";

  suzume::Tokenizer tokenizer;
  if (!tokenizer) {
    std::fprintf(stderr, "failed to create tokenizer: %s\n", suzume::Tokenizer::lastError().c_str());
    return 1;
  }

  const std::vector<suzume::Morpheme> morphemes = tokenizer.analyze(text);
  std::printf("suzume %s: %zu morpheme(s)\n", suzume::Tokenizer::version().c_str(), morphemes.size());
  for (const suzume::Morpheme& morph : morphemes) {
    std::printf("  %s\t%s\t%s\n", morph.surface.c_str(), morph.pos.c_str(), morph.base_form.c_str());
  }

  const std::vector<suzume::Tag> tags = tokenizer.generateTags(text);
  std::printf("tags:");
  for (const suzume::Tag& entry : tags) {
    std::printf(" %s", entry.tag.c_str());
  }
  std::printf("\n");

  return morphemes.empty() ? 1 : 0;
}
