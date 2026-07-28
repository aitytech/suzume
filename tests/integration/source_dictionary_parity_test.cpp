#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "dict_compiler.h"
#include "dictionary/binary_dict.h"
#include "dictionary/source_parser.h"
#include "dictionary/user_dict.h"
#include "grammar/dictionary_expansion.h"
#include "suzume.h"

namespace suzume {
namespace {

const dictionary::DictionaryEntry* findExact(const dictionary::BinaryDictionary& dictionary, std::string_view surface) {
  for (const auto& result : dictionary.lookup(surface, 0)) {
    if (result.entry != nullptr && result.entry->surface == surface) {
      return result.entry;
    }
  }
  return nullptr;
}

TEST(SourceDictionaryParityTest, CompilerAndRuntimeUseIdenticalExpandedEntries) {
  const std::string source =
      "\xEF\xBB\xBFテストする\tVERB\tSURU\n"
      "新しい\tADJ\tI_ADJ\n"
      "東京\tPROPER_NOUN\tFAMILY\n";
  const auto unique_id = reinterpret_cast<uintptr_t>(&source);
  const auto source_path =
      std::filesystem::temp_directory_path() / ("suzume_source_parity_" + std::to_string(unique_id) + ".tsv");
  const auto binary_path =
      std::filesystem::temp_directory_path() / ("suzume_source_parity_" + std::to_string(unique_id) + ".dic");
  {
    std::ofstream file(source_path);
    file << source;
  }

  cli::DictCompiler compiler;
  auto compile_result = compiler.compile(source_path.string(), binary_path.string());
  ASSERT_TRUE(compile_result.hasValue()) << compile_result.error().message;

  auto parsed = dictionary::parseDictionarySource(source);
  ASSERT_TRUE(parsed.hasValue()) << parsed.error().message;
  auto expanded = grammar::expandDictionarySourceEntries(parsed.value().entries);

  dictionary::BinaryDictionary compiled_dictionary;
  auto binary_load = compiled_dictionary.loadFromFile(binary_path.string());
  ASSERT_TRUE(binary_load.hasValue()) << binary_load.error().message;

  dictionary::UserDictionary runtime_dictionary;
  for (const auto& entry : expanded.entries) {
    runtime_dictionary.addEntry(entry);
  }
  for (const auto& expected : expanded.entries) {
    const auto* compiled = findExact(compiled_dictionary, expected.surface);
    ASSERT_NE(compiled, nullptr) << expected.surface;
    EXPECT_EQ(compiled->pos, expected.pos) << expected.surface;
    EXPECT_EQ(compiled->extended_pos, expected.extended_pos) << expected.surface;
    EXPECT_EQ(compiled->lemma, expected.lemma) << expected.surface;

    const auto* runtime = runtime_dictionary.lookupExact(expected.surface, expected.pos);
    ASSERT_NE(runtime, nullptr) << expected.surface;
    EXPECT_EQ(runtime->extended_pos, expected.extended_pos) << expected.surface;
    EXPECT_EQ(runtime->lemma, expected.lemma) << expected.surface;
  }

  SuzumeOptions options;
  options.skip_core_dictionary = true;
  options.skip_user_dictionary = true;
  Suzume analyzer(options);
  auto source_load = analyzer.loadUserDictionaryFromMemoryResult(source.data(), source.size());
  ASSERT_TRUE(source_load.hasValue()) << source_load.error().message;
  EXPECT_EQ(source_load.value(), expanded.entries.size());

  auto analyzed = analyzer.analyze("テストすれば");
  auto runtime_form = std::find_if(analyzed.begin(), analyzed.end(),
                                   [](const auto& morpheme) { return morpheme.surface == "テストすれば"; });
  ASSERT_NE(runtime_form, analyzed.end());
  EXPECT_EQ(runtime_form->lemma, "テストする");
  EXPECT_EQ(runtime_form->extended_pos, core::ExtendedPOS::VerbKateikei);

  Suzume file_analyzer(options);
  auto file_load = file_analyzer.loadUserDictionaryResult(source_path.string());
  ASSERT_TRUE(file_load.hasValue()) << file_load.error().message;
  EXPECT_EQ(file_load.value(), expanded.entries.size());
  auto file_analyzed = file_analyzer.analyze("テストすれば");
  auto file_form = std::find_if(file_analyzed.begin(), file_analyzed.end(),
                                [](const auto& morpheme) { return morpheme.surface == "テストすれば"; });
  ASSERT_NE(file_form, file_analyzed.end());
  EXPECT_EQ(file_form->lemma, runtime_form->lemma);
  EXPECT_EQ(file_form->extended_pos, runtime_form->extended_pos);

  std::filesystem::remove(source_path);
  std::filesystem::remove(binary_path);
}

}  // namespace
}  // namespace suzume
