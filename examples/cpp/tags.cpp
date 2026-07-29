/**
 * Tag generation example - extract content word tags for indexing/search
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

  // Generate tags (content words suitable for search indexing)
  auto tags = analyzer.generateTags("東京都渋谷区で開催されたイベントに参加しました");
  if (tags.empty()) {
    std::cerr << "tag generation returned no tags\n";
    return 1;
  }

  std::cout << "Tags:\n";
  for (const auto& tag : tags) {
    std::cout << "  " << tag.tag << " (" << tag.pos << ")\n";
  }

  // With custom options: nouns only, exclude basic words
  suzume::TagOptions opts;
  opts.pos_filter = SUZUME_TAG_POS_NOUN;
  opts.exclude_basic = true;

  auto noun_tags = analyzer.generateTags("東京都渋谷区で開催されたイベントに参加しました", opts);
  if (noun_tags.empty()) {
    std::cerr << "noun tag generation returned no tags\n";
    return 1;
  }

  std::cout << "\nNoun tags (excluding basic):\n";
  for (const auto& tag : noun_tags) {
    std::cout << "  " << tag.tag << "\n";
  }

  return 0;
}
