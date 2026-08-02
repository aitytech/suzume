#include "postprocess/postprocessor.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "dictionary/user_dict.h"
#include "postprocess/lemmatizer_internal.h"
#include "postprocess/postprocessor_resolvers_internal.h"

namespace suzume::postprocess {
namespace {

core::Morpheme makeMorpheme(std::string surface, core::PartOfSpeech pos, core::ExtendedPOS extended_pos, size_t start,
                            size_t end) {
  core::Morpheme morpheme;
  morpheme.surface = std::move(surface);
  morpheme.lemma = morpheme.surface;
  morpheme.pos = pos;
  morpheme.extended_pos = extended_pos;
  morpheme.start = start;
  morpheme.end = end;
  return morpheme;
}

TEST(PostprocessorTest, ExactDictionaryEvidenceIgnoresShorterVerbPrefix) {
  auto dictionary = std::make_shared<dictionary::UserDictionary>();
  dictionary->addEntry({"歩", core::PartOfSpeech::Verb, core::ExtendedPOS::VerbShuushikei, "歩"});
  dictionary->addEntry({"歩く", core::PartOfSpeech::Verb, core::ExtendedPOS::VerbShuushikei, "歩く"});
  dictionary::DictionaryManager manager;
  manager.addUserDictionary(dictionary);

  auto morpheme = makeMorpheme("歩い", core::PartOfSpeech::Verb, core::ExtendedPOS::VerbOnbinkei, 0, 2);
  morpheme.conj_type = dictionary::ConjugationType::GodanWa;

  EXPECT_EQ(Lemmatizer(&manager).lemmatize(morpheme), "歩く");
}

TEST(LemmatizerTest, NounBeforeCaseDeRemainsNominal) {
  Lemmatizer lemmatizer;
  std::vector<core::Morpheme> morphemes{
      makeMorpheme("笑い", core::PartOfSpeech::Noun, core::ExtendedPOS::Noun, 0, 2),
      makeMorpheme("で", core::PartOfSpeech::Particle, core::ExtendedPOS::ParticleCase, 2, 3),
  };

  lemmatizer.lemmatizeAll(morphemes);

  EXPECT_EQ(morphemes[0].pos, core::PartOfSpeech::Noun);
  EXPECT_EQ(morphemes[0].extended_pos, core::ExtendedPOS::Noun);
  EXPECT_EQ(morphemes[0].lemma, "笑い");
}

TEST(LemmatizerTest, DictionaryVerifiedGodanWaBeforeDeKeepsWaLemma) {
  auto dictionary = std::make_shared<dictionary::UserDictionary>();
  dictionary->addEntry({"使う", core::PartOfSpeech::Verb, core::ExtendedPOS::VerbShuushikei, "使う"});
  dictionary::DictionaryManager manager;
  manager.addUserDictionary(dictionary);
  Lemmatizer lemmatizer(&manager);
  std::vector<core::Morpheme> morphemes{
      makeMorpheme("使い", core::PartOfSpeech::Verb, core::ExtendedPOS::VerbRenyokei, 0, 2),
      makeMorpheme("で", core::PartOfSpeech::Particle, core::ExtendedPOS::ParticleConj, 2, 3),
  };

  lemmatizer.lemmatizeAll(morphemes);

  EXPECT_EQ(morphemes[0].pos, core::PartOfSpeech::Verb);
  EXPECT_EQ(morphemes[0].lemma, "使う");
}

TEST(PostprocessorTest, ProductiveMasuAndNaiFallbacksChooseTheConjugationRow) {
  EXPECT_EQ(lemmatizer_detail::lemmatizeVerbFallback("書きます"), "書く");
  EXPECT_EQ(lemmatizer_detail::lemmatizeVerbFallback("食べます"), "食べる");
  EXPECT_EQ(lemmatizer_detail::lemmatizeVerbFallback("書かない"), "書く");
  EXPECT_EQ(lemmatizer_detail::lemmatizeVerbFallback("食べない"), "食べる");
}

TEST(PostprocessorTest, VerbFallbackPrefersSpecificSuffixesAndNeverFabricatesERowTerminals) {
  EXPECT_EQ(lemmatizer_detail::lemmatizeVerbFallback("見られる"), "見る");
  EXPECT_EQ(lemmatizer_detail::lemmatizeVerbFallback("勉強させる"), "勉強する");
  EXPECT_EQ(lemmatizer_detail::lemmatizeVerbFallback("食べる"), "食べる");
}

TEST(PostprocessorTest, MergedNounCompoundClearsDictionaryProvenance) {
  auto first = makeMorpheme("東京", core::PartOfSpeech::Noun, core::ExtendedPOS::Noun, 0, 2);
  first.flags = core::EdgeFlags::FromDictionary | core::EdgeFlags::FromUserDict;
  auto second = makeMorpheme("駅", core::PartOfSpeech::Noun, core::ExtendedPOS::Noun, 2, 3);
  second.flags = core::EdgeFlags::FromDictionary;

  PostprocessOptions options;
  options.merge_noun_compounds = true;
  options.remove_symbols = false;
  const auto result = Postprocessor(options).process({first, second});

  ASSERT_EQ(result.size(), 1U);
  EXPECT_EQ(result[0].surface, "東京駅");
  EXPECT_EQ(result[0].start, 0U);
  EXPECT_EQ(result[0].end, 3U);
  EXPECT_FALSE(result[0].fromDictionary());
  EXPECT_FALSE(result[0].fromUserDict());
}

TEST(PostprocessorTest, OverlappingSymbolSpanDoesNotTerminateTheProcess) {
  auto noun = makeMorpheme("東京", core::PartOfSpeech::Noun, core::ExtendedPOS::Noun, 0, 2);
  auto symbol = makeMorpheme("、", core::PartOfSpeech::Symbol, core::ExtendedPOS::Symbol, 1, 2);
  PostprocessOptions options;
  options.remove_symbols = false;

  const auto result = Postprocessor(options).process({noun, symbol});

  ASSERT_EQ(result.size(), 2U);
  EXPECT_EQ(result[0].surface, "東京");
  EXPECT_EQ(result[1].surface, "、");
}

TEST(PostprocessorTest, IndefiniteCounterMergeUsesNumericNounCategories) {
  auto interrogative = makeMorpheme("何", core::PartOfSpeech::Pronoun, core::ExtendedPOS::PronounInterrogative, 0, 1);
  auto counter = makeMorpheme("ヶ月", core::PartOfSpeech::Suffix, core::ExtendedPOS::Suffix, 1, 3);

  PostprocessOptions options;
  options.remove_symbols = false;
  const auto result = Postprocessor(options).process({interrogative, counter});

  ASSERT_EQ(result.size(), 1U);
  EXPECT_EQ(result[0].surface, "何ヶ月");
  EXPECT_EQ(result[0].pos, core::PartOfSpeech::Noun);
  EXPECT_EQ(result[0].extended_pos, core::ExtendedPOS::NounNumber);
}

TEST(PostprocessorTest, NominalizedVerbsClearConjugationMetadata) {
  PostprocessOptions options;
  options.remove_symbols = false;

  auto prefix = makeMorpheme("お", core::PartOfSpeech::Prefix, core::ExtendedPOS::Prefix, 0, 1);
  auto honorific_stem = makeMorpheme("読み", core::PartOfSpeech::Verb, core::ExtendedPOS::VerbRenyokei, 1, 3);
  honorific_stem.conj_type = dictionary::ConjugationType::GodanMa;
  honorific_stem.conj_form = grammar::ConjForm::Renyokei;
  auto honorific_result = Postprocessor(options).process({prefix, honorific_stem});

  ASSERT_EQ(honorific_result.size(), 2U);
  EXPECT_EQ(honorific_result[1].pos, core::PartOfSpeech::Noun);
  EXPECT_EQ(honorific_result[1].conj_type, dictionary::ConjugationType::None);
  EXPECT_EQ(honorific_result[1].conj_form, grammar::ConjForm::Base);

  auto stem = makeMorpheme("飲み", core::PartOfSpeech::Verb, core::ExtendedPOS::VerbRenyokei, 0, 2);
  stem.conj_type = dictionary::ConjugationType::GodanMa;
  stem.conj_form = grammar::ConjForm::Renyokei;
  auto formal_noun = makeMorpheme("もの", core::PartOfSpeech::Noun, core::ExtendedPOS::NounFormal, 2, 4);
  formal_noun.flags = formal_noun.flags | core::EdgeFlags::IsFormalNoun;
  auto merged_result = Postprocessor(options).process({stem, formal_noun});

  ASSERT_EQ(merged_result.size(), 1U);
  EXPECT_EQ(merged_result[0].pos, core::PartOfSpeech::Noun);
  EXPECT_EQ(merged_result[0].conj_type, dictionary::ConjugationType::None);
  EXPECT_EQ(merged_result[0].conj_form, grammar::ConjForm::Base);
}

TEST(PostprocessorTest, DisabledLemmatizationRestoresRoleResolverLemmas) {
  PostprocessOptions options;
  options.lemmatize = false;
  options.remove_symbols = false;
  auto stem = makeMorpheme("食べ", core::PartOfSpeech::Verb, core::ExtendedPOS::VerbRenyokei, 0, 2);
  auto sou = makeMorpheme("そう", core::PartOfSpeech::Adverb, core::ExtendedPOS::Adverb, 2, 4);
  auto na = makeMorpheme("な", core::PartOfSpeech::Particle, core::ExtendedPOS::ParticleFinal, 4, 5);
  auto noun = makeMorpheme("人", core::PartOfSpeech::Noun, core::ExtendedPOS::Noun, 5, 6);

  const auto result = Postprocessor(options).process({stem, sou, na, noun});

  ASSERT_EQ(result.size(), 4U);
  EXPECT_EQ(result[2].pos, core::PartOfSpeech::Auxiliary);
  EXPECT_EQ(result[2].lemma, "な");
}

TEST(PostprocessorResolverTest, TeFormConnectiveRequiresBothRoleAndSurface) {
  auto connective = makeMorpheme("て", core::PartOfSpeech::Particle, core::ExtendedPOS::ParticleConj, 0, 1);
  EXPECT_TRUE(resolver::followsTeFormConnective(connective));

  connective.extended_pos = core::ExtendedPOS::ParticleCase;
  EXPECT_FALSE(resolver::followsTeFormConnective(connective));
  connective.extended_pos = core::ExtendedPOS::ParticleConj;
  connective.surface = "に";
  EXPECT_FALSE(resolver::followsTeFormConnective(connective));
}

TEST(PostprocessorResolverTest, AppearanceSouUsesOnePredicateClassification) {
  constexpr core::ExtendedPOS kPredicateRoles[] = {
      core::ExtendedPOS::VerbRenyokei,    core::ExtendedPOS::VerbShuushikei, core::ExtendedPOS::AdjStem,
      core::ExtendedPOS::AuxAspectShimau, core::ExtendedPOS::AuxAspectIru,
  };
  for (const auto role : kPredicateRoles) {
    const core::PartOfSpeech predicate_pos =
        role == core::ExtendedPOS::AdjStem ? core::PartOfSpeech::Adjective : core::PartOfSpeech::Verb;
    auto predicate = makeMorpheme("高", predicate_pos, role, 0, 1);
    auto sou = makeMorpheme("そう", core::PartOfSpeech::Adjective, core::ExtendedPOS::AdjNaAdj, 1, 2);
    auto copula = makeMorpheme("だ", core::PartOfSpeech::Auxiliary, core::ExtendedPOS::AuxCopulaDa, 2, 3);
    std::vector<core::Morpheme> morphemes{predicate, sou, copula};

    resolver::resolveAppearanceSouPredicate(morphemes);

    EXPECT_EQ(morphemes[1].pos, core::PartOfSpeech::Auxiliary);
    EXPECT_EQ(morphemes[1].extended_pos, core::ExtendedPOS::AuxAppearanceSou);
  }
}

TEST(LemmatizerTest, TariAdverbLengthUsesCodepoints) {
  EXPECT_EQ(lemmatizer_detail::fixTariAdverb("𠮷々と"), "𠮷々");
  EXPECT_EQ(lemmatizer_detail::fixTariAdverb("堂々と"), "堂々");
  EXPECT_TRUE(lemmatizer_detail::fixTariAdverb("一々と").empty());
}

TEST(LemmatizerTest, IchidanStemDetectionHandlesFourByteStem) {
  EXPECT_EQ(Lemmatizer::detectConjForm("𠮷", "𠮷る", core::PartOfSpeech::Verb, "ない"), grammar::ConjForm::Mizenkei);
}

}  // namespace
}  // namespace suzume::postprocess
