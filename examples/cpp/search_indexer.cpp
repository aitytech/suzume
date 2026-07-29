/**
 * Search indexer - build inverted index from Japanese text
 *
 * Demonstrates: tag generation, POS filtering, deduplication,
 * and batch processing of multiple documents.
 */
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "suzume/suzume.hpp"

struct Document {
  int id;
  std::string text;
};

int main() {
  suzume::Tokenizer analyzer;
  if (!analyzer) {
    std::cerr << "failed to create tokenizer: " << suzume::Tokenizer::lastError() << "\n";
    return 1;
  }

  // Sample documents
  std::vector<Document> docs = {
      {1, "東京都渋谷区で新しいカフェがオープンしました"},
      {2, "渋谷のカフェで美味しいコーヒーを飲みました"},
      {3, "新宿駅の近くにある図書館で本を読んでいます"},
  };

  // Build inverted index: tag -> [doc_ids]
  std::unordered_map<std::string, std::vector<int>> index;

  suzume::TagOptions tag_opts;
  tag_opts.pos_filter = static_cast<std::uint8_t>(SUZUME_TAG_POS_NOUN | SUZUME_TAG_POS_VERB);
  tag_opts.exclude_basic = true;
  tag_opts.use_lemma = true;
  tag_opts.min_length = 2;

  for (const auto& doc : docs) {
    auto tags = analyzer.generateTags(doc.text, tag_opts);
    if (tags.empty()) {
      std::cerr << "tag generation returned no tags for document " << doc.id << "\n";
      return 1;
    }
    for (const auto& tag : tags) {
      index[tag.tag].push_back(doc.id);
    }
  }

  // Print index
  std::cout << "Inverted index:\n";
  for (const auto& [tag, doc_ids] : index) {
    std::cout << "  " << tag << " -> [";
    for (size_t i = 0; i < doc_ids.size(); ++i) {
      if (i > 0) {
        std::cout << ", ";
      }
      std::cout << doc_ids[i];
    }
    std::cout << "]\n";
  }

  return index.empty() ? 1 : 0;
}
