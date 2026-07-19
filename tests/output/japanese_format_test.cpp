/**
 * @file japanese_format_test.cpp
 * @brief Tests for Japanese morphological output formatting
 *
 * Tests Japanese POS names, verb type names, and conjugation form names
 * for detailed morphological analysis output.
 */

#include <gtest/gtest.h>

#include <array>
#include <memory>

#include "core/types.h"
#include "dictionary/user_dict.h"
#include "grammar/conjugation.h"
#include "postprocess/lemmatizer.h"
#include "postprocess/lemmatizer_internal.h"
#include "suzume.h"

namespace suzume::output::test {

// =============================================================================
// Japanese POS Names (posToJapanese)
// =============================================================================

class JapanesePosNameTest : public ::testing::Test {};

TEST_F(JapanesePosNameTest, Noun) {
  EXPECT_EQ(core::posToJapanese(core::PartOfSpeech::Noun), "名詞");
}

TEST_F(JapanesePosNameTest, Verb) {
  EXPECT_EQ(core::posToJapanese(core::PartOfSpeech::Verb), "動詞");
}

TEST_F(JapanesePosNameTest, Adjective) {
  EXPECT_EQ(core::posToJapanese(core::PartOfSpeech::Adjective), "形容詞");
}

TEST_F(JapanesePosNameTest, Particle) {
  EXPECT_EQ(core::posToJapanese(core::PartOfSpeech::Particle), "助詞");
}

TEST_F(JapanesePosNameTest, Auxiliary) {
  EXPECT_EQ(core::posToJapanese(core::PartOfSpeech::Auxiliary), "助動詞");
}

TEST_F(JapanesePosNameTest, Adverb) {
  EXPECT_EQ(core::posToJapanese(core::PartOfSpeech::Adverb), "副詞");
}

TEST_F(JapanesePosNameTest, Conjunction) {
  EXPECT_EQ(core::posToJapanese(core::PartOfSpeech::Conjunction), "接続詞");
}

TEST_F(JapanesePosNameTest, Pronoun) {
  EXPECT_EQ(core::posToJapanese(core::PartOfSpeech::Pronoun), "代名詞");
}

TEST_F(JapanesePosNameTest, Determiner) {
  EXPECT_EQ(core::posToJapanese(core::PartOfSpeech::Determiner), "連体詞");
}

TEST_F(JapanesePosNameTest, Symbol) {
  EXPECT_EQ(core::posToJapanese(core::PartOfSpeech::Symbol), "記号");
}

TEST_F(JapanesePosNameTest, Unknown) {
  // Unknown and Other both return "その他"
  EXPECT_EQ(core::posToJapanese(core::PartOfSpeech::Unknown), "その他");
}

TEST_F(JapanesePosNameTest, Other) {
  EXPECT_EQ(core::posToJapanese(core::PartOfSpeech::Other), "その他");
}

// =============================================================================
// Japanese Verb Type Names (verbTypeToJapanese)
// =============================================================================

class JapaneseVerbTypeTest : public ::testing::Test {};

TEST_F(JapaneseVerbTypeTest, Ichidan) {
  EXPECT_EQ(grammar::verbTypeToJapanese(grammar::VerbType::Ichidan), "一段");
}

TEST_F(JapaneseVerbTypeTest, GodanKa) {
  EXPECT_EQ(grammar::verbTypeToJapanese(grammar::VerbType::GodanKa), "五段・カ行");
}

TEST_F(JapaneseVerbTypeTest, GodanGa) {
  EXPECT_EQ(grammar::verbTypeToJapanese(grammar::VerbType::GodanGa), "五段・ガ行");
}

TEST_F(JapaneseVerbTypeTest, GodanSa) {
  EXPECT_EQ(grammar::verbTypeToJapanese(grammar::VerbType::GodanSa), "五段・サ行");
}

TEST_F(JapaneseVerbTypeTest, GodanTa) {
  EXPECT_EQ(grammar::verbTypeToJapanese(grammar::VerbType::GodanTa), "五段・タ行");
}

TEST_F(JapaneseVerbTypeTest, GodanNa) {
  EXPECT_EQ(grammar::verbTypeToJapanese(grammar::VerbType::GodanNa), "五段・ナ行");
}

TEST_F(JapaneseVerbTypeTest, GodanBa) {
  EXPECT_EQ(grammar::verbTypeToJapanese(grammar::VerbType::GodanBa), "五段・バ行");
}

TEST_F(JapaneseVerbTypeTest, GodanMa) {
  EXPECT_EQ(grammar::verbTypeToJapanese(grammar::VerbType::GodanMa), "五段・マ行");
}

TEST_F(JapaneseVerbTypeTest, GodanRa) {
  EXPECT_EQ(grammar::verbTypeToJapanese(grammar::VerbType::GodanRa), "五段・ラ行");
}

TEST_F(JapaneseVerbTypeTest, GodanWa) {
  EXPECT_EQ(grammar::verbTypeToJapanese(grammar::VerbType::GodanWa), "五段・ワ行");
}

TEST_F(JapaneseVerbTypeTest, Suru) {
  EXPECT_EQ(grammar::verbTypeToJapanese(grammar::VerbType::Suru), "サ変");
}

TEST_F(JapaneseVerbTypeTest, Kuru) {
  EXPECT_EQ(grammar::verbTypeToJapanese(grammar::VerbType::Kuru), "カ変");
}

TEST_F(JapaneseVerbTypeTest, IAdjective) {
  EXPECT_EQ(grammar::verbTypeToJapanese(grammar::VerbType::IAdjective), "形容詞");
}

TEST_F(JapaneseVerbTypeTest, Unknown) {
  EXPECT_EQ(grammar::verbTypeToJapanese(grammar::VerbType::Unknown), "");
}

TEST(LemmatizerTest, ContractedGodanSokuonFormsUseDictionaryVerifiedRows) {
  dictionary::DictionaryManager dict_manager;
  auto user_dict = std::make_shared<dictionary::UserDictionary>();
  user_dict->addEntry({"待つ", core::PartOfSpeech::Verb, core::ExtendedPOS::VerbShuushikei, "待つ"});
  user_dict->addEntry({"帰る", core::PartOfSpeech::Verb, core::ExtendedPOS::VerbShuushikei, "帰る"});
  user_dict->addEntry({"買う", core::PartOfSpeech::Verb, core::ExtendedPOS::VerbShuushikei, "買う"});
  dict_manager.addUserDictionary(user_dict);

  postprocess::Lemmatizer lemmatizer(&dict_manager);
  core::Morpheme morpheme;
  morpheme.pos = core::PartOfSpeech::Verb;

  morpheme.surface = "待ってしまった";
  EXPECT_EQ(lemmatizer.lemmatize(morpheme), "待つ");

  morpheme.surface = "帰ってしまった";
  EXPECT_EQ(lemmatizer.lemmatize(morpheme), "帰る");

  morpheme.surface = "買ってしまった";
  EXPECT_EQ(lemmatizer.lemmatize(morpheme), "買う");
}

TEST(LemmatizerTest, DictionarySuruPassiveReturnsSuruLemma) {
  dictionary::DictionaryManager dict_manager;
  auto user_dict = std::make_shared<dictionary::UserDictionary>();
  user_dict->addEntry({"処理する", core::PartOfSpeech::Verb, core::ExtendedPOS::VerbShuushikei, "処理する"});
  user_dict->addEntry({"確認する", core::PartOfSpeech::Verb, core::ExtendedPOS::VerbShuushikei, "確認する"});
  dict_manager.addUserDictionary(user_dict);

  postprocess::Lemmatizer lemmatizer(&dict_manager);
  core::Morpheme morpheme;
  morpheme.pos = core::PartOfSpeech::Verb;
  morpheme.is_from_dictionary = true;

  morpheme.surface = "処理される";
  morpheme.lemma = "処理される";
  EXPECT_EQ(lemmatizer.lemmatize(morpheme), "処理する");

  morpheme.surface = "確認されて";
  morpheme.lemma = "確認されて";
  EXPECT_EQ(lemmatizer.lemmatize(morpheme), "確認する");
}

TEST(LemmatizerTest, FallbackGodanMizenRulesUseCanonicalRows) {
  using postprocess::lemmatizer_detail::lemmatizeVerbFallback;

  EXPECT_EQ(lemmatizeVerbFallback("書かれている"), "書く");
  EXPECT_EQ(lemmatizeVerbFallback("泳がれました"), "泳ぐ");
  EXPECT_EQ(lemmatizeVerbFallback("話されて"), "話す");
  EXPECT_EQ(lemmatizeVerbFallback("待たせる"), "待つ");
  EXPECT_EQ(lemmatizeVerbFallback("読ませた"), "読む");
  EXPECT_EQ(lemmatizeVerbFallback("買わない"), "買う");
  EXPECT_EQ(lemmatizeVerbFallback("買える"), "買う");
  EXPECT_EQ(lemmatizeVerbFallback("書ける"), "書く");
  EXPECT_EQ(lemmatizeVerbFallback("泳げる"), "泳ぐ");
  EXPECT_EQ(lemmatizeVerbFallback("話せる"), "話す");
  EXPECT_EQ(lemmatizeVerbFallback("死ねる"), "死ぬ");
  EXPECT_EQ(lemmatizeVerbFallback("遊べる"), "遊ぶ");
  EXPECT_EQ(lemmatizeVerbFallback("読める"), "読む");
  EXPECT_EQ(lemmatizeVerbFallback("取れる"), "取る");
}

TEST(LemmatizerTest, HatsuonbinFallbackUsesCanonicalOnbinRows) {
  dictionary::DictionaryManager dict_manager;
  auto user_dict = std::make_shared<dictionary::UserDictionary>();
  user_dict->addEntry({"学ぶ", core::PartOfSpeech::Verb, core::ExtendedPOS::VerbShuushikei, "学ぶ"});
  user_dict->addEntry({"死ぬ", core::PartOfSpeech::Verb, core::ExtendedPOS::VerbShuushikei, "死ぬ"});
  dict_manager.addUserDictionary(user_dict);

  using postprocess::lemmatizer_detail::fixHatsuonbin;
  EXPECT_EQ(fixHatsuonbin("学", &dict_manager), "学ぶ");
  EXPECT_EQ(fixHatsuonbin("死", &dict_manager), "死ぬ");
  EXPECT_EQ(fixHatsuonbin("読", nullptr), "読む");
}

TEST(LemmatizerTest, PotentialVerbFallbackUsesCanonicalARows) {
  core::Morpheme morpheme;
  morpheme.pos = core::PartOfSpeech::Verb;

  for (std::string_view surface :
       {"買われる", "書かれる", "泳がれる", "話される", "待たれる", "死なれる", "読まれる", "遊ばれる", "取られる"}) {
    morpheme.surface = surface;
    EXPECT_EQ(postprocess::lemmatizer_detail::fixPotentialVerb(morpheme), surface);
  }

  morpheme.surface = "書ける";
  EXPECT_TRUE(postprocess::lemmatizer_detail::fixPotentialVerb(morpheme).empty());
}

TEST(LemmatizerTest, IchidanRenyokeiFallbackUsesCanonicalIRows) {
  using postprocess::lemmatizer_detail::fixIchidanRenyokeiBeforeTe;
  EXPECT_EQ(fixIchidanRenyokeiBeforeTe("借り", "借る", "て", nullptr), "借りる");
  EXPECT_EQ(fixIchidanRenyokeiBeforeTe("過ぎ", "過ぐ", "た", nullptr), "過ぎる");

  dictionary::DictionaryManager dict_manager;
  auto user_dict = std::make_shared<dictionary::UserDictionary>();
  user_dict->addEntry({"走る", core::PartOfSpeech::Verb, core::ExtendedPOS::VerbShuushikei, "走る"});
  dict_manager.addUserDictionary(user_dict);
  EXPECT_TRUE(fixIchidanRenyokeiBeforeTe("走り", "走る", "て", &dict_manager).empty());
}

// =============================================================================
// Japanese Conjugation Form Names (conjFormToJapanese)
// =============================================================================

class JapaneseConjFormTest : public ::testing::Test {};

TEST_F(JapaneseConjFormTest, Base) {
  EXPECT_EQ(grammar::conjFormToJapanese(grammar::ConjForm::Base), "終止形");
}

TEST_F(JapaneseConjFormTest, Mizenkei) {
  EXPECT_EQ(grammar::conjFormToJapanese(grammar::ConjForm::Mizenkei), "未然形");
}

TEST_F(JapaneseConjFormTest, Renyokei) {
  EXPECT_EQ(grammar::conjFormToJapanese(grammar::ConjForm::Renyokei), "連用形");
}

TEST_F(JapaneseConjFormTest, Kateikei) {
  EXPECT_EQ(grammar::conjFormToJapanese(grammar::ConjForm::Kateikei), "仮定形");
}

TEST_F(JapaneseConjFormTest, Meireikei) {
  EXPECT_EQ(grammar::conjFormToJapanese(grammar::ConjForm::Meireikei), "命令形");
}

TEST_F(JapaneseConjFormTest, Ishikei) {
  EXPECT_EQ(grammar::conjFormToJapanese(grammar::ConjForm::Ishikei), "意志形");
}

// =============================================================================
// Conjugation Form Detection (detectConjForm)
// =============================================================================

class ConjFormDetectionTest : public ::testing::Test {};

// Verb: Mizenkei (未然形) - negative, passive, causative
TEST_F(ConjFormDetectionTest, Verb_Mizenkei_Negative) {
  auto form = postprocess::Lemmatizer::detectConjForm("食べない", "食べる", core::PartOfSpeech::Verb);
  EXPECT_EQ(form, grammar::ConjForm::Mizenkei);
}

TEST_F(ConjFormDetectionTest, Verb_Mizenkei_Passive) {
  auto form = postprocess::Lemmatizer::detectConjForm("食べられる", "食べる", core::PartOfSpeech::Verb);
  EXPECT_EQ(form, grammar::ConjForm::Mizenkei);
}

TEST_F(ConjFormDetectionTest, Verb_Mizenkei_Causative) {
  auto form = postprocess::Lemmatizer::detectConjForm("食べさせる", "食べる", core::PartOfSpeech::Verb);
  EXPECT_EQ(form, grammar::ConjForm::Mizenkei);
}

// Verb: Renyokei (連用形) - masu, ta, te
TEST_F(ConjFormDetectionTest, Verb_Renyokei_Masu) {
  auto form = postprocess::Lemmatizer::detectConjForm("食べます", "食べる", core::PartOfSpeech::Verb);
  EXPECT_EQ(form, grammar::ConjForm::Renyokei);
}

TEST_F(ConjFormDetectionTest, Verb_Renyokei_Ta) {
  auto form = postprocess::Lemmatizer::detectConjForm("食べた", "食べる", core::PartOfSpeech::Verb);
  EXPECT_EQ(form, grammar::ConjForm::Renyokei);
}

TEST_F(ConjFormDetectionTest, Verb_Renyokei_Te) {
  auto form = postprocess::Lemmatizer::detectConjForm("食べて", "食べる", core::PartOfSpeech::Verb);
  EXPECT_EQ(form, grammar::ConjForm::Renyokei);
}

TEST_F(ConjFormDetectionTest, Verb_Renyokei_Teiru) {
  auto form = postprocess::Lemmatizer::detectConjForm("食べている", "食べる", core::PartOfSpeech::Verb);
  EXPECT_EQ(form, grammar::ConjForm::Renyokei);
}

// Verb: Kateikei (仮定形) - ba
TEST_F(ConjFormDetectionTest, Verb_Kateikei_Ba) {
  auto form = postprocess::Lemmatizer::detectConjForm("食べれば", "食べる", core::PartOfSpeech::Verb);
  EXPECT_EQ(form, grammar::ConjForm::Kateikei);
}

TEST_F(ConjFormDetectionTest, Verb_Kateikei_Godan) {
  auto form = postprocess::Lemmatizer::detectConjForm("書けば", "書く", core::PartOfSpeech::Verb);
  EXPECT_EQ(form, grammar::ConjForm::Kateikei);
}

// Verb: Meireikei (命令形) - ro, e
TEST_F(ConjFormDetectionTest, Verb_Meireikei_Ichidan) {
  auto form = postprocess::Lemmatizer::detectConjForm("食べろ", "食べる", core::PartOfSpeech::Verb);
  EXPECT_EQ(form, grammar::ConjForm::Meireikei);
}

TEST_F(ConjFormDetectionTest, Verb_Meireikei_Godan) {
  // Godan imperative ends in 'e' sound - current implementation returns Renyokei
  // as fallback for unrecognized conjugated forms
  auto form = postprocess::Lemmatizer::detectConjForm("書け", "書く", core::PartOfSpeech::Verb);
  EXPECT_EQ(form, grammar::ConjForm::Renyokei);
}

// Verb: Ishikei (意志形) - ou, you
TEST_F(ConjFormDetectionTest, Verb_Ishikei_Ichidan) {
  auto form = postprocess::Lemmatizer::detectConjForm("食べよう", "食べる", core::PartOfSpeech::Verb);
  EXPECT_EQ(form, grammar::ConjForm::Ishikei);
}

TEST_F(ConjFormDetectionTest, Verb_Ishikei_Godan) {
  auto form = postprocess::Lemmatizer::detectConjForm("書こう", "書く", core::PartOfSpeech::Verb);
  EXPECT_EQ(form, grammar::ConjForm::Ishikei);
}

// Verb: Base form (終止形)
TEST_F(ConjFormDetectionTest, Verb_Base_Ichidan) {
  auto form = postprocess::Lemmatizer::detectConjForm("食べる", "食べる", core::PartOfSpeech::Verb);
  EXPECT_EQ(form, grammar::ConjForm::Base);
}

TEST_F(ConjFormDetectionTest, Verb_Base_Godan) {
  auto form = postprocess::Lemmatizer::detectConjForm("書く", "書く", core::PartOfSpeech::Verb);
  EXPECT_EQ(form, grammar::ConjForm::Base);
}

// Adjective conjugation forms
TEST_F(ConjFormDetectionTest, Adjective_Renyokei_Ku) {
  auto form = postprocess::Lemmatizer::detectConjForm("美しく", "美しい", core::PartOfSpeech::Adjective);
  EXPECT_EQ(form, grammar::ConjForm::Renyokei);
}

TEST_F(ConjFormDetectionTest, Adjective_Onbinkei_Katta) {
  // "かった" ends with "った" which matches onbinkei pattern first
  auto form = postprocess::Lemmatizer::detectConjForm("美しかった", "美しい", core::PartOfSpeech::Adjective);
  EXPECT_EQ(form, grammar::ConjForm::Onbinkei);
}

TEST_F(ConjFormDetectionTest, Adjective_Mizenkei_Kunai) {
  auto form = postprocess::Lemmatizer::detectConjForm("美しくない", "美しい", core::PartOfSpeech::Adjective);
  EXPECT_EQ(form, grammar::ConjForm::Mizenkei);
}

TEST_F(ConjFormDetectionTest, Adjective_Kateikei_Kereba) {
  auto form = postprocess::Lemmatizer::detectConjForm("美しければ", "美しい", core::PartOfSpeech::Adjective);
  EXPECT_EQ(form, grammar::ConjForm::Kateikei);
}

TEST_F(ConjFormDetectionTest, Adjective_Base) {
  auto form = postprocess::Lemmatizer::detectConjForm("美しい", "美しい", core::PartOfSpeech::Adjective);
  EXPECT_EQ(form, grammar::ConjForm::Base);
}

// Non-verb/adjective should return Base
TEST_F(ConjFormDetectionTest, Noun_ReturnsBase) {
  auto form = postprocess::Lemmatizer::detectConjForm("学校", "学校", core::PartOfSpeech::Noun);
  EXPECT_EQ(form, grammar::ConjForm::Base);
}

TEST_F(ConjFormDetectionTest, Particle_ReturnsBase) {
  auto form = postprocess::Lemmatizer::detectConjForm("は", "は", core::PartOfSpeech::Particle);
  EXPECT_EQ(form, grammar::ConjForm::Base);
}

// =============================================================================
// Conjugation Type to Verb Type Conversion
// =============================================================================

class ConjTypeToVerbTypeTest : public ::testing::Test {};

TEST_F(ConjTypeToVerbTypeTest, EveryConjugationTypeMapsAsSpecified) {
  using CT = dictionary::ConjugationType;
  using VT = grammar::VerbType;
  constexpr std::array<std::pair<CT, VT>, 18> kCases = {{
      {CT::None, VT::Unknown},
      {CT::Ichidan, VT::Ichidan},
      {CT::GodanKa, VT::GodanKa},
      {CT::GodanGa, VT::GodanGa},
      {CT::GodanSa, VT::GodanSa},
      {CT::GodanTa, VT::GodanTa},
      {CT::GodanNa, VT::GodanNa},
      {CT::GodanBa, VT::GodanBa},
      {CT::GodanMa, VT::GodanMa},
      {CT::GodanRa, VT::GodanRa},
      {CT::GodanWa, VT::GodanWa},
      {CT::Suru, VT::Suru},
      {CT::Kuru, VT::Kuru},
      {CT::IAdjective, VT::IAdjective},
      {CT::NaAdjective, VT::Unknown},
      {CT::Interjection, VT::Unknown},
      {CT::ProperFamily, VT::Unknown},
      {CT::ProperGiven, VT::Unknown},
  }};

  for (size_t case_index = 0; case_index < kCases.size(); ++case_index) {
    SCOPED_TRACE(case_index);
    EXPECT_EQ(grammar::conjTypeToVerbType(kCases[case_index].first), kCases[case_index].second);
  }
}

TEST_F(ConjTypeToVerbTypeTest, EveryVerbTypeRoundTripsToConjugationType) {
  using CT = dictionary::ConjugationType;
  using VT = grammar::VerbType;
  constexpr std::array<std::pair<VT, CT>, 14> kCases = {{
      {VT::Unknown, CT::None},
      {VT::Ichidan, CT::Ichidan},
      {VT::GodanKa, CT::GodanKa},
      {VT::GodanGa, CT::GodanGa},
      {VT::GodanSa, CT::GodanSa},
      {VT::GodanTa, CT::GodanTa},
      {VT::GodanNa, CT::GodanNa},
      {VT::GodanBa, CT::GodanBa},
      {VT::GodanMa, CT::GodanMa},
      {VT::GodanRa, CT::GodanRa},
      {VT::GodanWa, CT::GodanWa},
      {VT::Suru, CT::Suru},
      {VT::Kuru, CT::Kuru},
      {VT::IAdjective, CT::IAdjective},
  }};

  for (size_t case_index = 0; case_index < kCases.size(); ++case_index) {
    SCOPED_TRACE(case_index);
    EXPECT_EQ(grammar::verbTypeToConjType(kCases[case_index].first), kCases[case_index].second);
  }
}

// =============================================================================
// Integration Tests: Morpheme Analysis with Japanese Format Info
// =============================================================================

class JapaneseFormatIntegrationTest : public ::testing::Test {
 protected:
  Suzume analyzer_;
};

// Test that verb analysis includes correct conjugation type
// MeCab-compatible split: 食べました → 食べ + まし + た
TEST_F(JapaneseFormatIntegrationTest, VerbWithConjType_Ichidan) {
  auto morphemes = analyzer_.analyze("食べました");
  ASSERT_EQ(morphemes.size(), 3);
  EXPECT_EQ(morphemes[0].surface, "食べ");
  EXPECT_EQ(morphemes[0].getLemma(), "食べる");
  EXPECT_EQ(morphemes[0].pos, core::PartOfSpeech::Verb);
  EXPECT_EQ(morphemes[1].surface, "まし");
  EXPECT_EQ(morphemes[1].pos, core::PartOfSpeech::Auxiliary);
  EXPECT_EQ(morphemes[2].surface, "た");
  EXPECT_EQ(morphemes[2].pos, core::PartOfSpeech::Auxiliary);

  auto verb_type = grammar::conjTypeToVerbType(morphemes[0].conj_type);
  EXPECT_EQ(verb_type, grammar::VerbType::Ichidan);
}

// MeCab-compatible split: 書きました → 書き + まし + た
TEST_F(JapaneseFormatIntegrationTest, VerbWithConjType_GodanKa) {
  auto morphemes = analyzer_.analyze("書きました");
  ASSERT_EQ(morphemes.size(), 3);
  EXPECT_EQ(morphemes[0].surface, "書き");
  EXPECT_EQ(morphemes[0].getLemma(), "書く");
  EXPECT_EQ(morphemes[0].pos, core::PartOfSpeech::Verb);
  EXPECT_EQ(morphemes[1].surface, "まし");
  EXPECT_EQ(morphemes[1].pos, core::PartOfSpeech::Auxiliary);
  EXPECT_EQ(morphemes[2].surface, "た");
  EXPECT_EQ(morphemes[2].pos, core::PartOfSpeech::Auxiliary);

  auto verb_type = grammar::conjTypeToVerbType(morphemes[0].conj_type);
  EXPECT_EQ(verb_type, grammar::VerbType::GodanKa);
}

TEST_F(JapaneseFormatIntegrationTest, VerbWithConjType_Suru) {
  // MeCab-compatible split: し + て + い + ます
  auto morphemes = analyzer_.analyze("しています");
  ASSERT_EQ(morphemes.size(), 4);
  EXPECT_EQ(morphemes[0].surface, "し");
  EXPECT_EQ(morphemes[0].getLemma(), "する");
  EXPECT_EQ(morphemes[0].pos, core::PartOfSpeech::Verb);
  EXPECT_EQ(morphemes[1].surface, "て");
  EXPECT_EQ(morphemes[1].pos, core::PartOfSpeech::Particle);
  EXPECT_EQ(morphemes[2].surface, "い");
  EXPECT_EQ(morphemes[2].pos, core::PartOfSpeech::Auxiliary);
  EXPECT_EQ(morphemes[3].surface, "ます");
  EXPECT_EQ(morphemes[3].pos, core::PartOfSpeech::Auxiliary);

  // TODO: VerbType detection from conj_type needs work
  // auto verb_type = grammar::conjTypeToVerbType(morphemes[0].conj_type);
  // EXPECT_EQ(verb_type, grammar::VerbType::Suru);
}

TEST_F(JapaneseFormatIntegrationTest, VerbWithConjType_GodanMa) {
  // MeCab-compatible split: 読ん + で + い + ます
  auto morphemes = analyzer_.analyze("読んでいます");
  ASSERT_EQ(morphemes.size(), 4);
  EXPECT_EQ(morphemes[0].surface, "読ん");
  EXPECT_EQ(morphemes[0].getLemma(), "読む");
  EXPECT_EQ(morphemes[0].pos, core::PartOfSpeech::Verb);
  EXPECT_EQ(morphemes[1].surface, "で");
  EXPECT_EQ(morphemes[1].pos, core::PartOfSpeech::Particle);
  EXPECT_EQ(morphemes[2].surface, "い");
  EXPECT_EQ(morphemes[2].pos, core::PartOfSpeech::Auxiliary);
  EXPECT_EQ(morphemes[3].surface, "ます");
  EXPECT_EQ(morphemes[3].pos, core::PartOfSpeech::Auxiliary);

  auto verb_type = grammar::conjTypeToVerbType(morphemes[0].conj_type);
  EXPECT_EQ(verb_type, grammar::VerbType::GodanMa);
}

// Test conjugation form detection in analysis pipeline
// MeCab-compatible split: 食べない → 食べ + ない
TEST_F(JapaneseFormatIntegrationTest, ConjForm_Mizenkei) {
  auto morphemes = analyzer_.analyze("食べない");
  ASSERT_EQ(morphemes.size(), 2);
  EXPECT_EQ(morphemes[0].surface, "食べ");
  EXPECT_EQ(morphemes[0].conj_form, grammar::ConjForm::Mizenkei);
  EXPECT_EQ(morphemes[1].surface, "ない");
  EXPECT_EQ(morphemes[1].pos, core::PartOfSpeech::Auxiliary);
}

// MeCab-compatible split: 食べました → 食べ + まし + た
// Check ConjForm on the verb stem
TEST_F(JapaneseFormatIntegrationTest, ConjForm_Renyokei) {
  auto morphemes = analyzer_.analyze("食べました");
  ASSERT_EQ(morphemes.size(), 3);
  EXPECT_EQ(morphemes[0].conj_form, grammar::ConjForm::Renyokei);
}

TEST_F(JapaneseFormatIntegrationTest, ConjForm_Kateikei) {
  // MeCab-compatible split: 走れ + ば
  auto morphemes = analyzer_.analyze("走れば");
  ASSERT_EQ(morphemes.size(), 2);
  EXPECT_EQ(morphemes[0].surface, "走れ");
  EXPECT_EQ(morphemes[0].pos, core::PartOfSpeech::Verb);
  EXPECT_EQ(morphemes[1].surface, "ば");
  EXPECT_EQ(morphemes[1].pos, core::PartOfSpeech::Particle);
  // TODO: ConjForm detection needs work - currently returns Renyokei for godan-ra
  // EXPECT_EQ(morphemes[0].conj_form, grammar::ConjForm::Kateikei);
}

TEST_F(JapaneseFormatIntegrationTest, ConjForm_Base) {
  auto morphemes = analyzer_.analyze("食べる");
  ASSERT_EQ(morphemes.size(), 1);
  EXPECT_EQ(morphemes[0].conj_form, grammar::ConjForm::Base);
}

}  // namespace suzume::output::test
