#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string_view>

// Include the header directly since we add the CLI source dir to includes
#include "dict_compiler.h"
#include "dictionary/binary_dict.h"

namespace suzume::cli {
namespace {

class DictCompilerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    temp_dir_ = std::filesystem::temp_directory_path() /
                ("suzume_dict_compiler_test_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
    std::filesystem::create_directories(temp_dir_);
  }

  void TearDown() override { std::filesystem::remove_all(temp_dir_); }

  std::filesystem::path writeFile(const std::string& name, const std::string& content) const {
    auto path = temp_dir_ / name;
    std::ofstream file(path);
    file << content;
    return path;
  }

  std::filesystem::path temp_dir_;
};

// Pure kanji 3+ chars: trivial (can be segmented by rules)
TEST(IsTrivialEntryTest, PureKanjiThreeChars) {
  EXPECT_TRUE(isTrivialEntry("経済成長"));
  EXPECT_TRUE(isTrivialEntry("東京都"));
  EXPECT_TRUE(isTrivialEntry("形態素解析"));
}

// Pure katakana 3+ chars: trivial
TEST(IsTrivialEntryTest, PureKatakanaThreeChars) {
  EXPECT_TRUE(isTrivialEntry("テスト"));
  EXPECT_TRUE(isTrivialEntry("コンピュータ"));
  EXPECT_TRUE(isTrivialEntry("プログラム"));
}

// Pure hiragana: non-trivial. Hiragana runs carry lexical value and are not
// reconstructable from character type alone, so they are kept.
TEST(IsTrivialEntryTest, PureHiraganaThreeChars) {
  EXPECT_FALSE(isTrivialEntry("ありがとう"));
  EXPECT_FALSE(isTrivialEntry("こんにちは"));
}

// 2-char entries: always non-trivial (kept)
TEST(IsTrivialEntryTest, TwoCharEntries) {
  EXPECT_FALSE(isTrivialEntry("東京"));
  EXPECT_FALSE(isTrivialEntry("りん"));
  EXPECT_FALSE(isTrivialEntry("AB"));
}

// 1-char entries: non-trivial (kept)
TEST(IsTrivialEntryTest, SingleCharEntries) {
  EXPECT_FALSE(isTrivialEntry("東"));
  EXPECT_FALSE(isTrivialEntry("A"));
}

// Mixed kanji+katakana: non-trivial (kept)
TEST(IsTrivialEntryTest, MixedKanjiKatakana) {
  EXPECT_FALSE(isTrivialEntry("二次エロ"));
  EXPECT_FALSE(isTrivialEntry("東京タワー"));
}

// Mixed kanji+hiragana: non-trivial (kept)
TEST(IsTrivialEntryTest, MixedKanjiHiragana) {
  EXPECT_FALSE(isTrivialEntry("掘り出し物"));
  EXPECT_FALSE(isTrivialEntry("食べ物"));
}

// Entries with spaces: always non-trivial (kept)
TEST(IsTrivialEntryTest, EntriesWithSpaces) {
  EXPECT_FALSE(isTrivialEntry("東京 都"));
  EXPECT_FALSE(isTrivialEntry("テスト ケース"));
}

// Empty string: non-trivial (0 codepoints <= 2)
TEST(IsTrivialEntryTest, EmptyString) {
  EXPECT_FALSE(isTrivialEntry(""));
}

// Pure alphabet: non-trivial (only katakana/kanji runs are reconstructable).
TEST(IsTrivialEntryTest, PureAlphabetThreeChars) {
  EXPECT_FALSE(isTrivialEntry("ABC"));
  EXPECT_FALSE(isTrivialEntry("test"));
}

// Mixed alphabet+digit: non-trivial (kept)
TEST(IsTrivialEntryTest, MixedAlphabetDigit) {
  EXPECT_FALSE(isTrivialEntry("part3"));
  EXPECT_FALSE(isTrivialEntry("ABC123"));
}

TEST_F(DictCompilerTest, CompileMultipleRejectsDuplicateSurfaceAndPosAcrossFiles) {
  auto first = writeFile("first.tsv", "東京\tNOUN\n");
  auto second = writeFile("second.tsv", "東京\tNOUN\n");
  auto output = temp_dir_ / "out.dic";

  DictCompiler compiler;
  auto result = compiler.compileMultiple({first.string(), second.string()}, output.string());

  EXPECT_FALSE(result.hasValue());
  EXPECT_NE(result.error().message.find("Validation failed"), std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(output));
}

TEST_F(DictCompilerTest, KuruExpansionGeneratesRealKanjiSurfaces) {
  auto input = writeFile("kuru.tsv", "来る\tVERB\tKURU\n");
  auto output = temp_dir_ / "kuru.dic";

  DictCompiler compiler;
  auto compile_result = compiler.compile(input.string(), output.string());
  ASSERT_TRUE(compile_result.hasValue()) << compile_result.error().message;

  dictionary::BinaryDictionary dict;
  auto load_result = dict.loadFromFile(output.string());
  ASSERT_TRUE(load_result.hasValue()) << load_result.error().message;

  std::set<std::string> surfaces;
  for (std::string_view text : {"来る", "来", "来れ", "来よ", "来い", "来られる", "来れる"}) {
    for (const auto& result : dict.lookup(text, 0)) {
      ASSERT_NE(result.entry, nullptr);
      surfaces.insert(result.entry->surface);
      EXPECT_EQ(result.entry->lemma, "来る");
    }
  }

  const auto has_surface = [&surfaces](std::string_view surface) {
    return surfaces.find(std::string(surface)) != surfaces.end();
  };

  EXPECT_TRUE(has_surface("来る"));
  EXPECT_TRUE(has_surface("来"));
  EXPECT_TRUE(has_surface("来れ"));
  EXPECT_TRUE(has_surface("来よ"));
  EXPECT_TRUE(has_surface("来い"));
  EXPECT_TRUE(has_surface("来られる"));
  EXPECT_TRUE(has_surface("来れる"));
  EXPECT_FALSE(has_surface("来くる"));
  EXPECT_FALSE(has_surface("来き"));
  EXPECT_FALSE(has_surface("来こ"));
}

}  // namespace
}  // namespace suzume::cli
