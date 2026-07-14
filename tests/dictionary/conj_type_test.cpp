#include <gtest/gtest.h>

#include "dictionary/dictionary.h"

namespace suzume {
namespace dictionary {
namespace {

TEST(ConjTypeTest, CanonicalStringSpellings) {
  EXPECT_EQ(conjTypeToCanonicalString(ConjugationType::None), "");
  EXPECT_EQ(conjTypeToCanonicalString(ConjugationType::Ichidan), "ICHIDAN");
  EXPECT_EQ(conjTypeToCanonicalString(ConjugationType::GodanKa), "GODAN_KA");
  EXPECT_EQ(conjTypeToCanonicalString(ConjugationType::GodanWa), "GODAN_WA");
  EXPECT_EQ(conjTypeToCanonicalString(ConjugationType::IAdjective), "I_ADJ");
  EXPECT_EQ(conjTypeToCanonicalString(ConjugationType::NaAdjective), "NA_ADJ");
  EXPECT_EQ(conjTypeToCanonicalString(ConjugationType::Interjection), "INTJ");
  EXPECT_EQ(conjTypeToCanonicalString(ConjugationType::ProperFamily), "FAMILY");
  EXPECT_EQ(conjTypeToCanonicalString(ConjugationType::ProperGiven), "GIVEN");
}

TEST(ConjTypeTest, FromCanonicalAcceptsShortForms) {
  EXPECT_EQ(conjTypeFromCanonical(""), ConjugationType::None);
  EXPECT_EQ(conjTypeFromCanonical("NONE"), ConjugationType::None);
  EXPECT_EQ(conjTypeFromCanonical("ICHIDAN"), ConjugationType::Ichidan);
  EXPECT_EQ(conjTypeFromCanonical("GODAN_WA"), ConjugationType::GodanWa);
  EXPECT_EQ(conjTypeFromCanonical("I_ADJ"), ConjugationType::IAdjective);
  EXPECT_EQ(conjTypeFromCanonical("INTJ"), ConjugationType::Interjection);
  EXPECT_EQ(conjTypeFromCanonical("FAMILY"), ConjugationType::ProperFamily);
  EXPECT_EQ(conjTypeFromCanonical("GIVEN"), ConjugationType::ProperGiven);
}

TEST(ConjTypeTest, FromCanonicalRejectsLongAndUnknownForms) {
  // Long SCREAMING_SNAKE aliases are not canonical spellings.
  EXPECT_FALSE(conjTypeFromCanonical("INTERJECTION").has_value());
  EXPECT_FALSE(conjTypeFromCanonical("PROPER_FAMILY").has_value());
  EXPECT_FALSE(conjTypeFromCanonical("Ichidan").has_value());
  EXPECT_FALSE(conjTypeFromCanonical("NOPE").has_value());
}

TEST(ConjTypeTest, FromAnyAliasAcceptsPascalAndScreaming) {
  EXPECT_EQ(conjTypeFromAnyAlias("None"), ConjugationType::None);
  EXPECT_EQ(conjTypeFromAnyAlias("NONE"), ConjugationType::None);
  EXPECT_EQ(conjTypeFromAnyAlias("Ichidan"), ConjugationType::Ichidan);
  EXPECT_EQ(conjTypeFromAnyAlias("ICHIDAN"), ConjugationType::Ichidan);
  EXPECT_EQ(conjTypeFromAnyAlias("IAdjective"), ConjugationType::IAdjective);
  EXPECT_EQ(conjTypeFromAnyAlias("I_ADJ"), ConjugationType::IAdjective);
  EXPECT_EQ(conjTypeFromAnyAlias("Interjection"), ConjugationType::Interjection);
  EXPECT_EQ(conjTypeFromAnyAlias("INTERJECTION"), ConjugationType::Interjection);
  EXPECT_EQ(conjTypeFromAnyAlias("ProperFamily"), ConjugationType::ProperFamily);
  EXPECT_EQ(conjTypeFromAnyAlias("PROPER_FAMILY"), ConjugationType::ProperFamily);
}

TEST(ConjTypeTest, FromAnyAliasRejectsShortMarkerForms) {
  // The short INTJ/FAMILY/GIVEN spellings must NOT be treated as conjugation
  // types here: a TSV field spelled that way is a lemma, not a conj type.
  EXPECT_FALSE(conjTypeFromAnyAlias("INTJ").has_value());
  EXPECT_FALSE(conjTypeFromAnyAlias("FAMILY").has_value());
  EXPECT_FALSE(conjTypeFromAnyAlias("GIVEN").has_value());
  EXPECT_FALSE(conjTypeFromAnyAlias("NOPE").has_value());
}

TEST(ConjTypeTest, CanonicalRoundTrip) {
  for (int raw = 0; raw <= static_cast<int>(ConjugationType::ProperGiven); ++raw) {
    auto type = static_cast<ConjugationType>(raw);
    auto parsed = conjTypeFromCanonical(conjTypeToCanonicalString(type));
    ASSERT_TRUE(parsed.has_value()) << "raw=" << raw;
    EXPECT_EQ(*parsed, type) << "raw=" << raw;
  }
}

}  // namespace
}  // namespace dictionary
}  // namespace suzume
