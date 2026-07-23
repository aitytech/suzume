#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>

#include "normalize/utf8.h"
#include "suzume.h"

namespace suzume {
namespace {

class SuzumeApiTest : public ::testing::Test {
 protected:
  SuzumeOptions makeTestOptions() {
    SuzumeOptions opts;
    opts.skip_user_dictionary = true;
    return opts;
  }
};

TEST_F(SuzumeApiTest, DefaultConstructorCreatesInstance) {
  Suzume instance;
  // Should not crash, instance is valid
  EXPECT_FALSE(Suzume::version().empty());
}

TEST_F(SuzumeApiTest, ConstructWithOptions) {
  SuzumeOptions opts = makeTestOptions();
  opts.lemmatize = true;
  opts.merge_compounds = false;
  Suzume instance(opts);
  // Instance created successfully with custom options
  EXPECT_EQ(instance.mode(), core::AnalysisMode::Normal);
}

TEST_F(SuzumeApiTest, ConstructWithSkipUserDictionary) {
  SuzumeOptions opts;
  opts.skip_user_dictionary = true;
  Suzume instance(opts);
  auto results = instance.analyze("\xe6\x9d\xb1\xe4\xba\xac");  // Tokyo
  EXPECT_FALSE(results.empty());
}

TEST_F(SuzumeApiTest, VersionReturnsNonEmptyString) {
  std::string ver = Suzume::version();
  EXPECT_FALSE(ver.empty());
}

TEST_F(SuzumeApiTest, ModeDefaultsToNormal) {
  Suzume instance(makeTestOptions());
  EXPECT_EQ(instance.mode(), core::AnalysisMode::Normal);
}

TEST_F(SuzumeApiTest, SetModeRoundtrip) {
  Suzume instance(makeTestOptions());

  instance.setMode(core::AnalysisMode::Search);
  EXPECT_EQ(instance.mode(), core::AnalysisMode::Search);

  instance.setMode(core::AnalysisMode::Split);
  EXPECT_EQ(instance.mode(), core::AnalysisMode::Split);

  instance.setMode(core::AnalysisMode::Normal);
  EXPECT_EQ(instance.mode(), core::AnalysisMode::Normal);
}

TEST_F(SuzumeApiTest, SplitModeDisablesMixedScriptJoinCandidates) {
  SuzumeOptions normal_opts = makeTestOptions();
  normal_opts.mode = core::AnalysisMode::Normal;
  Suzume normal(normal_opts);

  SuzumeOptions split_opts = makeTestOptions();
  split_opts.mode = core::AnalysisMode::Split;
  Suzume split(split_opts);

  auto normal_results = normal.analyze("API開発");
  auto split_results = split.analyze("API開発");

  ASSERT_FALSE(normal_results.empty());
  ASSERT_FALSE(split_results.empty());
  EXPECT_EQ(normal_results.front().surface, "API開発");
  EXPECT_GT(split_results.size(), normal_results.size());
}

TEST_F(SuzumeApiTest, SetModeUpdatesTokenizerAndPostprocessor) {
  Suzume instance(makeTestOptions());

  auto normal_results = instance.analyze("API開発");
  instance.setMode(core::AnalysisMode::Split);
  auto split_results = instance.analyze("API開発");

  ASSERT_FALSE(normal_results.empty());
  ASSERT_FALSE(split_results.empty());
  EXPECT_EQ(normal_results.front().surface, "API開発");
  EXPECT_GT(split_results.size(), normal_results.size());
}

TEST_F(SuzumeApiTest, SuruVerbSplitsWithoutCoreDictionary) {
  // Vanilla (core.dic disabled) must still split 管理する会社 grammatically as
  // 管理 / する / 会社. The base form する is a closed-class irregular verb held
  // in L1, so this does not depend on the L2 dictionary. Without a standalone
  // する token the analyzer mis-splits into 管 / 理する / 会社.
  SuzumeOptions opts;
  opts.skip_user_dictionary = true;
  opts.skip_core_dictionary = true;
  Suzume instance(opts);

  auto results = instance.analyze("管理する会社");
  ASSERT_EQ(results.size(), 3u);
  EXPECT_EQ(results[0].surface, "管理");
  EXPECT_EQ(results[1].surface, "する");
  EXPECT_EQ(results[1].pos, core::PartOfSpeech::Verb);
  EXPECT_EQ(results[2].surface, "会社");
}

TEST_F(SuzumeApiTest, AnalyzeSimpleText) {
  Suzume instance(makeTestOptions());
  // "Tokyo is beautiful"
  auto results = instance.analyze("\xe6\x9d\xb1\xe4\xba\xac\xe3\x81\xaf\xe7\xbe\x8e\xe3\x81\x97\xe3\x81\x84");
  EXPECT_FALSE(results.empty());

  // Check that surfaces are non-empty
  for (const auto& morpheme : results) {
    EXPECT_FALSE(morpheme.surface.empty());
  }
}

TEST_F(SuzumeApiTest, AnalyzeReturnsNonEmptyForJapanese) {
  Suzume instance(makeTestOptions());
  // "eat" (taberu)
  auto results = instance.analyze("\xe9\xa3\x9f\xe3\x81\xb9\xe3\x82\x8b");
  EXPECT_FALSE(results.empty());
}

TEST_F(SuzumeApiTest, ProlongedSoundMergeKeepsSurfaceAndOffsetsConsistent) {
  Suzume instance(makeTestOptions());
  auto results = instance.analyze("すごーーい");

  ASSERT_FALSE(results.empty());
  bool found = false;
  for (const auto& morpheme : results) {
    if (morpheme.surface.find("ーー") != std::string::npos) {
      found = true;
      EXPECT_EQ(morpheme.end_pos - morpheme.start_pos, normalize::utf8Length(morpheme.surface));
    }
  }
  EXPECT_TRUE(found);
}

TEST_F(SuzumeApiTest, AnalyzeEmptyTextReturnsEmpty) {
  Suzume instance(makeTestOptions());
  auto results = instance.analyze("");
  EXPECT_TRUE(results.empty());
}

TEST_F(SuzumeApiTest, AnalyzeInvalidUtf8ReturnsEmpty) {
  Suzume instance(makeTestOptions());
  auto results = instance.analyze(std::string_view("\xE3\x81", 2));
  EXPECT_TRUE(results.empty());
}

TEST_F(SuzumeApiTest, NumericKatakanaMergesAsQuantity) {
  Suzume instance(makeTestOptions());

  // MeCab treats a numeral + any katakana noun as a single quantity token, so
  // this is a general rule rather than a curated unit list — both measurement
  // units and arbitrary katakana nouns merge.
  auto meter = instance.analyze("5メートル");
  ASSERT_FALSE(meter.empty());
  EXPECT_EQ(meter.front().surface, "5メートル");

  auto cut = instance.analyze("3カット");
  ASSERT_FALSE(cut.empty());
  EXPECT_EQ(cut.front().surface, "3カット");

  auto pattern = instance.analyze("10パターン");
  ASSERT_FALSE(pattern.empty());
  EXPECT_EQ(pattern.front().surface, "10パターン");
}

TEST_F(SuzumeApiTest, AnalyzeSingleCharacter) {
  Suzume instance(makeTestOptions());
  // Single kanji "mountain"
  auto results = instance.analyze("\xe5\xb1\xb1");
  EXPECT_FALSE(results.empty());
}

TEST_F(SuzumeApiTest, PretokenizedMorphemesHaveExtendedPos) {
  Suzume instance(makeTestOptions());
  auto results = instance.analyze("https://example.com");
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].pos, core::PartOfSpeech::Noun);
  EXPECT_EQ(results[0].extended_pos, core::ExtendedPOS::Noun);
}

TEST_F(SuzumeApiTest, SimilitudeYouRemainsAFormalNoun) {
  Suzume instance(makeTestOptions());

  for (const std::string_view text :
       {"夢のようだ", "読むようにする", "このような方法", "お待ちくださいますようお願いします"}) {
    auto results = instance.analyze(text);
    auto iter = std::find_if(results.begin(), results.end(),
                             [](const core::Morpheme& morpheme) { return morpheme.surface == "よう"; });
    ASSERT_NE(iter, results.end()) << text;
    EXPECT_EQ(iter->pos, core::PartOfSpeech::Noun) << text;
    EXPECT_EQ(iter->extended_pos, core::ExtendedPOS::NounFormal) << text;
    EXPECT_TRUE(iter->features.is_formal_noun) << text;
  }

  auto volitional = instance.analyze("見よう");
  auto iter = std::find_if(volitional.begin(), volitional.end(),
                           [](const core::Morpheme& morpheme) { return morpheme.surface == "う"; });
  ASSERT_NE(iter, volitional.end());
  EXPECT_EQ(iter->pos, core::PartOfSpeech::Auxiliary);
  EXPECT_EQ(iter->extended_pos, core::ExtendedPOS::AuxVolitional);
  EXPECT_FALSE(iter->features.is_formal_noun);
}

TEST_F(SuzumeApiTest, FinalParticleQuotationUsesQuotativeExtendedPos) {
  Suzume instance(makeTestOptions());

  auto quotation = instance.analyze("行くかと尋ねた");
  auto quote = std::find_if(quotation.begin(), quotation.end(),
                            [](const core::Morpheme& morpheme) { return morpheme.surface == "と"; });
  ASSERT_NE(quote, quotation.end());
  EXPECT_EQ(quote->extended_pos, core::ExtendedPOS::ParticleQuote);

  auto companion = instance.analyze("誰かと話す");
  auto case_particle = std::find_if(companion.begin(), companion.end(),
                                    [](const core::Morpheme& morpheme) { return morpheme.surface == "と"; });
  ASSERT_NE(case_particle, companion.end());
  EXPECT_EQ(case_particle->extended_pos, core::ExtendedPOS::ParticleCase);
}

TEST_F(SuzumeApiTest, AttributivePredicateKeepsTemporalMaAsNoun) {
  Suzume instance(makeTestOptions());

  for (const std::string_view text : {"長い間続いた", "短い間休む", "待つ間休む", "休む間もなく働いた"}) {
    auto results = instance.analyze(text);
    auto interval = std::find_if(results.begin(), results.end(),
                                 [](const core::Morpheme& morpheme) { return morpheme.surface == "間"; });
    ASSERT_NE(interval, results.end()) << text;
    EXPECT_EQ(interval->pos, core::PartOfSpeech::Noun) << text;
  }

  auto lexical_compound = instance.analyze("期間を確認する");
  ASSERT_FALSE(lexical_compound.empty());
  EXPECT_EQ(lexical_compound.front().surface, "期間");
}

TEST_F(SuzumeApiTest, LexicalMamonakuRemainsAnAdverb) {
  Suzume instance(makeTestOptions());

  for (const std::string_view text : {"間もなく到着する", "終了間もなく報告する"}) {
    auto results = instance.analyze(text);
    auto temporal_adverb = std::find_if(results.begin(), results.end(),
                                        [](const core::Morpheme& morpheme) { return morpheme.surface == "間もなく"; });
    ASSERT_NE(temporal_adverb, results.end()) << text;
    EXPECT_EQ(temporal_adverb->pos, core::PartOfSpeech::Adverb) << text;
    EXPECT_EQ(temporal_adverb->lemma, "間もなく") << text;
  }

  auto duration = instance.analyze("時間もなく終わった");
  ASSERT_GE(duration.size(), 2U);
  EXPECT_EQ(duration[0].surface, "時間");
  EXPECT_EQ(duration[1].surface, "も");
}

TEST_F(SuzumeApiTest, AdverbHomographsRespectNominalFrames) {
  Suzume instance(makeTestOptions());

  for (const std::string_view text : {"一切を任せる", "一切合切を確認する", "むしろを使う"}) {
    auto results = instance.analyze(text);
    ASSERT_FALSE(results.empty()) << text;
    EXPECT_EQ(results.front().pos, core::PartOfSpeech::Noun) << text;
  }

  auto adverbial_genitive = instance.analyze("まったくの偶然");
  ASSERT_FALSE(adverbial_genitive.empty());
  EXPECT_EQ(adverbial_genitive.front().pos, core::PartOfSpeech::Adverb);

  for (const std::string_view text : {"一切確認する", "一切合切確認する", "このほど確認した", "むしろ必要だ"}) {
    auto results = instance.analyze(text);
    ASSERT_FALSE(results.empty()) << text;
    EXPECT_EQ(results.front().pos, core::PartOfSpeech::Adverb) << text;
  }
}

TEST_F(SuzumeApiTest, BareNominalNegativeUsesIndependentAdjective) {
  Suzume instance(makeTestOptions());

  for (const std::string_view text : {"問題ない", "関係ない", "本にちがいない"}) {
    auto results = instance.analyze(text);
    ASSERT_GE(results.size(), 2U) << text;
    const auto& negative = results.back();
    EXPECT_EQ(negative.surface, "ない") << text;
    EXPECT_EQ(negative.pos, core::PartOfSpeech::Adjective) << text;
    EXPECT_EQ(negative.lemma, "ない") << text;
  }

  auto attributive = instance.analyze("頼りない返事");
  ASSERT_GE(attributive.size(), 3U);
  EXPECT_EQ(attributive[0].surface, "頼り");
  EXPECT_EQ(attributive[0].pos, core::PartOfSpeech::Noun);
  EXPECT_EQ(attributive[1].surface, "ない");
  EXPECT_EQ(attributive[1].pos, core::PartOfSpeech::Adjective);
}

TEST_F(SuzumeApiTest, TemporalNaoIsAdverbial) {
  Suzume instance(makeTestOptions());

  auto temporal = instance.analyze("いまなお確認できる");
  ASSERT_GE(temporal.size(), 2U);
  EXPECT_EQ(temporal[0].pos, core::PartOfSpeech::Adverb);
  EXPECT_EQ(temporal[1].surface, "なお");
  EXPECT_EQ(temporal[1].pos, core::PartOfSpeech::Adverb);

  auto connective = instance.analyze("なお確認する");
  ASSERT_FALSE(connective.empty());
  EXPECT_EQ(connective.front().surface, "なお");
  EXPECT_EQ(connective.front().pos, core::PartOfSpeech::Conjunction);
}

TEST_F(SuzumeApiTest, DemonstrativeIdentificationEndsInNoun) {
  Suzume instance(makeTestOptions());

  for (const std::string_view text : {"これが答え。", "それが答え。"}) {
    auto results = instance.analyze(text);
    ASSERT_FALSE(results.empty()) << text;
    const auto& answer = results.back();
    EXPECT_EQ(answer.surface, "答え") << text;
    EXPECT_EQ(answer.pos, core::PartOfSpeech::Noun) << text;
    EXPECT_EQ(answer.lemma, "答え") << text;
  }

  auto imperative = instance.analyze("君が急げ。");
  ASSERT_FALSE(imperative.empty());
  const auto& hurry = imperative.back();
  EXPECT_EQ(hurry.surface, "急げ");
  EXPECT_EQ(hurry.pos, core::PartOfSpeech::Verb);
  EXPECT_EQ(hurry.lemma, "急ぐ");
}

TEST_F(SuzumeApiTest, GenerateTagsReturnsResults) {
  Suzume instance(makeTestOptions());
  // "Tokyo is beautiful"
  auto tags = instance.generateTags("\xe6\x9d\xb1\xe4\xba\xac\xe3\x81\xaf\xe7\xbe\x8e\xe3\x81\x97\xe3\x81\x84");
  EXPECT_FALSE(tags.empty());

  // Tags should have non-empty tag strings
  for (const auto& entry : tags) {
    EXPECT_FALSE(entry.tag.empty());
  }
}

TEST_F(SuzumeApiTest, GenerateTagsWithCustomOptions) {
  Suzume instance(makeTestOptions());
  postprocess::TagGeneratorOptions tag_opts;
  tag_opts.exclude_particles = true;
  tag_opts.min_tag_length = 1;

  auto tags =
      instance.generateTags("\xe6\x9d\xb1\xe4\xba\xac\xe3\x81\xaf\xe7\xbe\x8e\xe3\x81\x97\xe3\x81\x84", tag_opts);
  EXPECT_FALSE(tags.empty());
}

TEST_F(SuzumeApiTest, GenerateTagsEmptyText) {
  Suzume instance(makeTestOptions());
  auto tags = instance.generateTags("");
  EXPECT_TRUE(tags.empty());
}

TEST_F(SuzumeApiTest, MoveConstruct) {
  Suzume src(makeTestOptions());
  Suzume dst = std::move(src);

  // Moved-to instance should work
  auto results = dst.analyze("\xe3\x83\x86\xe3\x82\xb9\xe3\x83\x88");  // test
  EXPECT_FALSE(results.empty());
}

TEST_F(SuzumeApiTest, MoveAssign) {
  Suzume src(makeTestOptions());
  Suzume dst(makeTestOptions());
  dst = std::move(src);

  // Moved-to instance should work
  auto results = dst.analyze("\xe3\x82\x8a\xe3\x82\x93\xe3\x81\x94");  // ringo
  EXPECT_FALSE(results.empty());
}

TEST_F(SuzumeApiTest, AnalyzeWithLemmatizeOption) {
  SuzumeOptions opts = makeTestOptions();
  opts.lemmatize = true;
  Suzume instance(opts);

  // "ate" (tabeta) - past tense of taberu
  auto results = instance.analyze("\xe9\xa3\x9f\xe3\x81\xb9\xe3\x81\x9f");
  EXPECT_FALSE(results.empty());
}

TEST_F(SuzumeApiTest, AnalyzeWithMergeCompoundsOption) {
  SuzumeOptions opts = makeTestOptions();
  opts.merge_compounds = true;
  Suzume instance(opts);

  auto results = instance.analyze("\xe6\x9d\xb1\xe4\xba\xac\xe3\x81\xaf\xe7\xbe\x8e\xe3\x81\x97\xe3\x81\x84");
  EXPECT_FALSE(results.empty());
}

TEST_F(SuzumeApiTest, LoadUserDictionaryFromInvalidPath) {
  Suzume instance(makeTestOptions());
  bool result = instance.loadUserDictionary("/nonexistent/path/dict.csv");
  EXPECT_FALSE(result);
}

TEST_F(SuzumeApiTest, LoadUserDictionaryResultReportsInvalidPath) {
  Suzume instance(makeTestOptions());
  auto result = instance.loadUserDictionaryResult("/nonexistent/path/dict.csv");
  EXPECT_FALSE(result.hasValue());
  EXPECT_NE(result.error().message.find("Failed to open dictionary file"), std::string::npos);
}

TEST_F(SuzumeApiTest, LoadUserDictionaryFromNullMemory) {
  Suzume instance(makeTestOptions());
  bool result = instance.loadUserDictionaryFromMemory(nullptr, 0);
  EXPECT_FALSE(result);
}

TEST_F(SuzumeApiTest, LoadUserDictionaryFromMemoryResultReportsParseError) {
  Suzume instance(makeTestOptions());
  const char* csv_data = "\"東京,NOUN,0.5\n";
  auto result = instance.loadUserDictionaryFromMemoryResult(csv_data, std::strlen(csv_data));
  EXPECT_FALSE(result.hasValue());
  EXPECT_NE(result.error().message.find("unterminated quoted field"), std::string::npos);
}

TEST_F(SuzumeApiTest, LoadUserDictionaryFromMemoryResultReportsEntryCount) {
  Suzume instance(makeTestOptions());
  const char* csv_data =
      "東京,NOUN,0.5\n"
      "大阪,NOUN,0.5\n";
  auto result = instance.loadUserDictionaryFromMemoryResult(csv_data, std::strlen(csv_data));
  ASSERT_TRUE(result.hasValue());
  EXPECT_EQ(result.value(), 2u);
}

TEST_F(SuzumeApiTest, LoadBinaryDictionaryFromInvalidMemory) {
  Suzume instance(makeTestOptions());
  const uint8_t bad_data[] = {0x00, 0x01, 0x02, 0x03};
  bool result = instance.loadBinaryDictionary(bad_data, sizeof(bad_data));
  EXPECT_FALSE(result);
}

TEST_F(SuzumeApiTest, LoadBinaryDictionaryResultReportsParseError) {
  Suzume instance(makeTestOptions());
  const uint8_t bad_data[] = {0x00, 0x01, 0x02, 0x03};
  auto result = instance.loadBinaryDictionaryResult(bad_data, sizeof(bad_data));
  EXPECT_FALSE(result.hasValue());
  EXPECT_NE(result.error().message.find("Dictionary file too small"), std::string::npos);
}

TEST_F(SuzumeApiTest, AutoDictionaryLoadWarningsAreRecorded) {
#ifndef __EMSCRIPTEN__
  namespace fs = std::filesystem;

  const char* old_data_dir = std::getenv("SUZUME_DATA_DIR");
  std::string old_value = old_data_dir != nullptr ? old_data_dir : "";

  fs::path dir = fs::temp_directory_path() / "suzume_bad_dict_test";
  fs::create_directories(dir);
  {
    std::ofstream file(dir / "core.dic", std::ios::binary);
    const char bad_data[] = {0x00, 0x01, 0x02, 0x03};
    file.write(bad_data, sizeof(bad_data));
  }

  setenv("SUZUME_DATA_DIR", dir.string().c_str(), 1);
  SuzumeOptions opts = makeTestOptions();
  Suzume instance(opts);
  if (old_data_dir != nullptr) {
    setenv("SUZUME_DATA_DIR", old_value.c_str(), 1);
  } else {
    unsetenv("SUZUME_DATA_DIR");
  }
  fs::remove_all(dir);

  auto warnings = instance.dictionaryWarnings();
  ASSERT_FALSE(warnings.empty());
  EXPECT_NE(warnings.front().find("Failed to auto-load dictionary"), std::string::npos);
  EXPECT_NE(warnings.front().find("Dictionary file too small"), std::string::npos);
#endif
}

TEST_F(SuzumeApiTest, ScorerEnvWarningsAreSilentByDefault) {
#ifndef __EMSCRIPTEN__
  setenv("SUZUME_SCORER_UNARY_noun_prior", "not-a-number", 1);
  testing::internal::CaptureStderr();
  { Suzume instance(makeTestOptions()); }
  std::string stderr_output = testing::internal::GetCapturedStderr();
  unsetenv("SUZUME_SCORER_UNARY_noun_prior");

  EXPECT_TRUE(stderr_output.empty());
#endif
}

TEST_F(SuzumeApiTest, ScorerEnvWarningsCanBeReported) {
#ifndef __EMSCRIPTEN__
  setenv("SUZUME_SCORER_UNARY_noun_prior", "not-a-number", 1);
  testing::internal::CaptureStderr();
  {
    SuzumeOptions opts = makeTestOptions();
    opts.report_scorer_config = true;
    Suzume instance(opts);
  }
  std::string stderr_output = testing::internal::GetCapturedStderr();
  unsetenv("SUZUME_SCORER_UNARY_noun_prior");

  EXPECT_NE(stderr_output.find("Invalid value"), std::string::npos);
#endif
}

}  // namespace
}  // namespace suzume
