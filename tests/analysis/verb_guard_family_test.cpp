/**
 * @file verb_guard_family_test.cpp
 * @brief Characterization tests for the fabricated closed-class absorption guard
 *        family (see verb_candidates_helpers.h).
 *
 * These pin the decision boundaries of the guards that stop verb/adjective
 * candidate generators from fabricating a non-dictionary conjugation that
 * swallows an adjacent closed-class morpheme — a 係助詞 (しか/さえ/すら), or a
 * te-form + 補助動詞 みる. A failure here means a guard's boundary
 * moved; verify the tokenization of the named example before adjusting an
 * expectation rather than relaxing the test to the new output.
 */

#include <gtest/gtest.h>

#include <string_view>
#include <vector>

#include "analysis/verb_candidates_helpers.h"
#include "analysis/verb_candidates_hiragana_internal.h"
#include "core/types.h"
#include "dictionary/dictionary.h"
#include "grammar/inflection.h"
#include "normalize/utf8.h"

namespace suzume::analysis {
namespace {

using core::ExtendedPOS;

std::vector<char32_t> cps(std::string_view text) {
  return normalize::toCodepoints(text);
}

// =============================================================================
// Tail guards (T): the run ends in [word] + particle (+ negative)
// =============================================================================

TEST(VerbGuardFamilyTail, FocusParticleTailDetectsBindingShika) {
  dictionary::DictionaryManager dict;
  // 副助詞 しか after a host, with and without the trailing negative ない.
  auto bare = cps("水しか");
  EXPECT_TRUE(verb_helpers::endsWithFocusParticleTail(&dict, bare, 0, bare.size()));
  auto negative = cps("水しかない");
  EXPECT_TRUE(verb_helpers::endsWithFocusParticleTail(&dict, negative, 0, negative.size()));
}

TEST(VerbGuardFamilyTail, FocusParticleTailDetectsBindingSaeAndSura) {
  dictionary::DictionaryManager dict;
  auto sae = cps("お金さえ");
  EXPECT_TRUE(verb_helpers::endsWithFocusParticleTail(&dict, sae, 0, sae.size()));
  auto sura = cps("水すらない");
  EXPECT_TRUE(verb_helpers::endsWithFocusParticleTail(&dict, sura, 0, sura.size()));
}

TEST(VerbGuardFamilyTail, ParticleClassIsDiscriminated) {
  dictionary::DictionaryManager dict;
  // しか and すら are 係助詞 (Binding). Each must not be mistaken for a 副助詞.
  auto shika = cps("水しかない");
  EXPECT_TRUE(verb_helpers::endsWithParticleTailOfPos(&dict, shika, 0, shika.size(), ExtendedPOS::ParticleBinding));
  EXPECT_FALSE(verb_helpers::endsWithParticleTailOfPos(&dict, shika, 0, shika.size(), ExtendedPOS::ParticleAdverbial));
  auto sura = cps("水すらない");
  EXPECT_TRUE(verb_helpers::endsWithParticleTailOfPos(&dict, sura, 0, sura.size(), ExtendedPOS::ParticleBinding));
  EXPECT_FALSE(verb_helpers::endsWithParticleTailOfPos(&dict, sura, 0, sura.size(), ExtendedPOS::ParticleAdverbial));
}

TEST(VerbGuardFamilyTail, GenuineGodanMizenkeiIsNotSwept) {
  dictionary::DictionaryManager dict;
  // 行かない is a genuine godan-ka mizenkei; the single-mora か must never match a
  // 2+ codepoint particle tail.
  auto iku = cps("行かない");
  EXPECT_FALSE(verb_helpers::endsWithFocusParticleTail(&dict, iku, 0, iku.size()));
}

TEST(VerbGuardFamilyTail, VerbPrefixBeforeBindingParticleSplits) {
  dictionary::DictionaryManager dict;
  grammar::Inflection infl;
  // Single-mora prefix (る) is a bare verb-ending fragment, never a word, so the
  // whole run is garbage — the production catch for やるしかない, where や splits
  // off first and the residual るしか reaches this guard.
  auto ru = cps("るしか");
  EXPECT_TRUE(hiragana_verb_detail::endsWithParticleAfterVerb(&dict, infl, ru, 0, ru.size()));
  // て + verb + しか (てみるしか) is a subsidiary-verb sequence: after stripping
  // the leading te-form, the probe みる analyzes as a verb, so the run splits.
  auto temiru = cps("てみるしか");
  EXPECT_TRUE(hiragana_verb_detail::endsWithParticleAfterVerb(&dict, infl, temiru, 0, temiru.size()));
}

TEST(VerbGuardFamilyTail, TooShortRunHasNoVerbPlusParticleSplit) {
  dictionary::DictionaryManager dict;
  grammar::Inflection infl;
  // Too short to hold a 1+ char prefix and a 2+ char particle suffix.
  auto shika = cps("しか");
  EXPECT_FALSE(hiragana_verb_detail::endsWithParticleAfterVerb(&dict, infl, shika, 0, shika.size()));
}

// =============================================================================
// Embed guards (E): an internal て/で + 補助動詞 splits the run
// =============================================================================

TEST(VerbGuardFamilyEmbed, TeFormMiruIsDetected) {
  auto yatte = cps("やってみる");
  EXPECT_TRUE(verb_helpers::embedsTeFormMiruAuxiliary(yatte, 0, yatte.size()));
  auto tabete = cps("食べてみれば");
  EXPECT_TRUE(verb_helpers::embedsTeFormMiruAuxiliary(tabete, 0, tabete.size()));
  // The voiced onbin form でみ (読んでみる) is also a te-form + みる boundary.
  auto yonde = cps("読んでみる");
  EXPECT_TRUE(verb_helpers::embedsTeFormMiruAuxiliary(yonde, 0, yonde.size()));
}

TEST(VerbGuardFamilyEmbed, LeadingTeOrDeIsExempt) {
  // A candidate that merely BEGINS with て/で is a different shape, left untouched.
  auto temiru = cps("てみる");
  EXPECT_FALSE(verb_helpers::embedsTeFormMiruAuxiliary(temiru, 0, temiru.size()));
  auto demiru = cps("でみる");
  EXPECT_FALSE(verb_helpers::embedsTeFormMiruAuxiliary(demiru, 0, demiru.size()));
  // No embedded てみ/でみ at all.
  auto taberu = cps("食べる");
  EXPECT_FALSE(verb_helpers::embedsTeFormMiruAuxiliary(taberu, 0, taberu.size()));
}

TEST(VerbGuardFamilyEmbed, TeFormAuxiliaryPatterns) {
  EXPECT_TRUE(verb_helpers::embedsTeFormAuxiliary("食べていく"));
  EXPECT_TRUE(verb_helpers::embedsTeFormAuxiliary("助けてもらう"));
  EXPECT_TRUE(verb_helpers::embedsTeFormAuxiliary("見てくれ"));
  // 〜ている / 〜ておく are intentionally NOT matched: they would strand a
  // te-ending stem (慌て+ている), and the plain split already wins there.
  EXPECT_FALSE(verb_helpers::embedsTeFormAuxiliary("食べている"));
  EXPECT_FALSE(verb_helpers::embedsTeFormAuxiliary("置いておく"));
}

}  // namespace
}  // namespace suzume::analysis
