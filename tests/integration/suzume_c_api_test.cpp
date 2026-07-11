#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>

#include "suzume/suzume_c.h"

namespace {

TEST(SuzumeCApiTest, LastErrorReportsInvalidArguments) {
  EXPECT_EQ(suzume_analyze(nullptr, "test"), nullptr);

  std::string error = suzume_last_error();
  EXPECT_NE(error.find("null handle"), std::string::npos);
}

TEST(SuzumeCApiTest, LastErrorClearsAfterSuccess) {
  EXPECT_EQ(suzume_analyze(nullptr, "test"), nullptr);
  ASSERT_STRNE(suzume_last_error(), "");

  suzume_t handle = suzume_create();
  ASSERT_NE(handle, nullptr);

  suzume_result_t* result = suzume_analyze(handle, "東京");
  ASSERT_NE(result, nullptr);
  EXPECT_STREQ(suzume_last_error(), "");

  suzume_result_free(result);
  suzume_destroy(handle);
}

TEST(SuzumeCApiTest, LoadUserDictReportsParseDetails) {
  suzume_t handle = suzume_create();
  ASSERT_NE(handle, nullptr);

  const char* csv_data = "\"東京,NOUN,0.5\n";
  EXPECT_EQ(suzume_load_user_dict(handle, csv_data, std::strlen(csv_data)), 0);

  std::string error = suzume_last_error();
  EXPECT_NE(error.find("Invalid CSV quoting"), std::string::npos);
  EXPECT_NE(error.find("unterminated quoted field"), std::string::npos);

  suzume_destroy(handle);
}

TEST(SuzumeCApiTest, LoadBinaryDictReportsParseDetails) {
  suzume_t handle = suzume_create();
  ASSERT_NE(handle, nullptr);

  const uint8_t bad_data[] = {0x00, 0x01, 0x02, 0x03};
  EXPECT_EQ(suzume_load_binary_dict(handle, bad_data, sizeof(bad_data)), 0);

  std::string error = suzume_last_error();
  EXPECT_NE(error.find("Dictionary file too small"), std::string::npos);

  suzume_destroy(handle);
}

TEST(SuzumeCApiTest, CreateWithExtendedOptionsAcceptsModeAndPostprocessOptions) {
  suzume_extended_options_t options{};
  suzume_init_extended_options(&options);
  options.mode = 2;  // split

  suzume_t handle = suzume_create_with_extended_options(&options);
  ASSERT_NE(handle, nullptr);

  suzume_result_t* result = suzume_analyze(handle, "API開発");
  ASSERT_NE(result, nullptr);
  EXPECT_GT(result->count, 1u);

  suzume_result_free(result);
  suzume_destroy(handle);
}

TEST(SuzumeCApiTest, InitExtendedOptionsPreservesDefaultTrueFields) {
  suzume_extended_options_t options{};
  suzume_init_extended_options(&options);

  EXPECT_EQ(options.size, sizeof(options));
  EXPECT_EQ(options.preserve_vu, 1);
  EXPECT_EQ(options.preserve_case, 1);
  EXPECT_EQ(options.preserve_symbols, 0);
  EXPECT_EQ(options.mode, 0);
  EXPECT_EQ(options.lemmatize, 1);
  EXPECT_EQ(options.merge_compounds, 0);
}

TEST(SuzumeCApiTest, CreateWithExtendedOptionsRejectsInvalidMode) {
  suzume_extended_options_t options{};
  suzume_init_extended_options(&options);
  options.mode = 99;

  suzume_t handle = suzume_create_with_extended_options(&options);
  EXPECT_EQ(handle, nullptr);

  std::string error = suzume_last_error();
  EXPECT_NE(error.find("invalid mode"), std::string::npos);
}

TEST(SuzumeCApiTest, AnalyzeReturnsOffsetsAndDiagnosticFields) {
  suzume_t handle = suzume_create();
  ASSERT_NE(handle, nullptr);

  suzume_result_t* result = suzume_analyze(handle, "東京");
  ASSERT_NE(result, nullptr);
  ASSERT_GT(result->count, 0u);

  const auto& morpheme = result->morphemes[0];
  EXPECT_STREQ(morpheme.surface, "東京");
  EXPECT_EQ(morpheme.start, 0u);
  EXPECT_GE(morpheme.end, morpheme.start + 1u);
  EXPECT_GE(morpheme.score, 0.0F);
  EXPECT_TRUE(morpheme.is_from_dictionary == 0 || morpheme.is_from_dictionary == 1);
  EXPECT_TRUE(morpheme.is_user_dict == 0 || morpheme.is_user_dict == 1);
  EXPECT_TRUE(morpheme.is_formal_noun == 0 || morpheme.is_formal_noun == 1);
  EXPECT_TRUE(morpheme.is_low_info == 0 || morpheme.is_low_info == 1);
  EXPECT_TRUE(morpheme.is_unknown == 0 || morpheme.is_unknown == 1);

  suzume_result_free(result);
  suzume_destroy(handle);
}

TEST(SuzumeCApiTest, TagOptionsExposeAllGeneratorFilters) {
  suzume_t handle = suzume_create();
  ASSERT_NE(handle, nullptr);

  suzume_tag_options_t options{};
  options.use_lemma = 1;
  options.min_length = 1;
  options.max_tags = 0;
  options.exclude_particles = 0;
  options.exclude_auxiliaries = 1;
  options.exclude_formal_nouns = 1;
  options.exclude_low_info = 1;
  options.remove_duplicates = 1;

  suzume_tags_t* tags = suzume_generate_tags_with_options(handle, "猫が走る", &options);
  ASSERT_NE(tags, nullptr);

  bool found_particle = false;
  for (size_t idx = 0; idx < tags->count; ++idx) {
    if (std::strcmp(tags->tags[idx], "が") == 0) {
      found_particle = true;
    }
  }
  EXPECT_TRUE(found_particle);

  suzume_tags_free(tags);
  suzume_destroy(handle);
}

TEST(SuzumeCApiTest, DictionaryWarningAccessorsHandleEmptyAndInvalidIndex) {
  suzume_t handle = suzume_create();
  ASSERT_NE(handle, nullptr);

  EXPECT_EQ(suzume_dictionary_warning_count(nullptr), 0u);
  EXPECT_EQ(suzume_dictionary_warning(handle, suzume_dictionary_warning_count(handle)), nullptr);

  std::string error = suzume_last_error();
  EXPECT_NE(error.find("index out of range"), std::string::npos);

  suzume_destroy(handle);
}

TEST(SuzumeCApiTest, FreeNullPointersAreNoOps) {
  suzume_destroy(nullptr);
  suzume_result_free(nullptr);
  suzume_tags_free(nullptr);
  suzume_free(nullptr);
}

TEST(SuzumeCApiTest, LayoutFunctionsMatchNativeStructs) {
  EXPECT_EQ(suzume_sizeof_result(), sizeof(suzume_result_t));
  EXPECT_EQ(suzume_sizeof_morpheme(), sizeof(suzume_morpheme_t));
  EXPECT_EQ(suzume_sizeof_tags(), sizeof(suzume_tags_t));
  EXPECT_EQ(suzume_sizeof_tag_options(), sizeof(suzume_tag_options_t));
  EXPECT_EQ(suzume_sizeof_extended_options(), sizeof(suzume_extended_options_t));

  EXPECT_EQ(suzume_offsetof_result(0), offsetof(suzume_result_t, morphemes));
  EXPECT_EQ(suzume_offsetof_result(1), offsetof(suzume_result_t, count));
  EXPECT_EQ(suzume_offsetof_morpheme(6), offsetof(suzume_morpheme_t, extended_pos));
  EXPECT_EQ(suzume_offsetof_morpheme(7), offsetof(suzume_morpheme_t, start));
  EXPECT_EQ(suzume_offsetof_morpheme(14), offsetof(suzume_morpheme_t, score));
  EXPECT_EQ(suzume_offsetof_tags(2), offsetof(suzume_tags_t, count));
  EXPECT_EQ(suzume_offsetof_tag_options(4), offsetof(suzume_tag_options_t, max_tags));
  EXPECT_EQ(suzume_offsetof_tag_options(5), offsetof(suzume_tag_options_t, exclude_particles));
  EXPECT_EQ(suzume_offsetof_tag_options(9), offsetof(suzume_tag_options_t, remove_duplicates));
  EXPECT_EQ(suzume_offsetof_extended_options(0), offsetof(suzume_extended_options_t, size));
  EXPECT_EQ(suzume_offsetof_extended_options(4), offsetof(suzume_extended_options_t, mode));
  EXPECT_EQ(suzume_offsetof_extended_options(6), offsetof(suzume_extended_options_t, merge_compounds));
  EXPECT_EQ(suzume_offsetof_result(99), static_cast<size_t>(-1));
  EXPECT_EQ(suzume_offsetof_extended_options(99), static_cast<size_t>(-1));
}

}  // namespace
