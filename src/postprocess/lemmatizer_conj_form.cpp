#include <algorithm>

#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "grammar/conjugation.h"
#include "grammar/inflection_scorer_constants.h"
#include "normalize/char_type.h"
#include "normalize/utf8.h"
#include "postprocess/lemmatizer.h"

namespace suzume::postprocess {

namespace {

grammar::ConjForm conjFormFromExtendedPos(core::ExtendedPOS extended_pos, core::ExtendedPOS next_extended_pos,
                                          std::string_view next_lemma) {
  using core::ExtendedPOS;
  using grammar::ConjForm;

  switch (extended_pos) {
    case ExtendedPOS::VerbShuushikei:
    case ExtendedPOS::VerbRentaikei:
    case ExtendedPOS::AdjBasic:
    case ExtendedPOS::AdjNaAdj:
      return ConjForm::Base;
    case ExtendedPOS::VerbRenyokei:
    case ExtendedPOS::AdjRenyokei:
    case ExtendedPOS::AdjStem:
      return ConjForm::Renyokei;
    case ExtendedPOS::VerbMizenkei:
      // Modern volition is represented as a mizenkei stem plus the closed
      // auxiliary う. Keep negative まい in the ordinary mizenkei cell.
      return next_extended_pos == ExtendedPOS::AuxVolitional && utf8::equalsAny(next_lemma, {"う"})
                 ? ConjForm::Ishikei
                 : ConjForm::Mizenkei;
    case ExtendedPOS::AdjMizenkei:
      return ConjForm::Mizenkei;
    case ExtendedPOS::VerbOnbinkei:
    case ExtendedPOS::VerbTeForm:
    case ExtendedPOS::VerbTaForm:
    case ExtendedPOS::VerbTaraForm:
    case ExtendedPOS::AdjKatt:
      return ConjForm::Onbinkei;
    case ExtendedPOS::VerbKateikei:
    case ExtendedPOS::AdjKeForm:
      return ConjForm::Kateikei;
    case ExtendedPOS::VerbMeireikei:
      return ConjForm::Meireikei;
    default:
      return ConjForm::Count_;
  }
}

}  // namespace

grammar::ConjForm Lemmatizer::detectConjForm(std::string_view surface, std::string_view lemma, core::PartOfSpeech pos,
                                             std::string_view next_lemma, core::ExtendedPOS extended_pos,
                                             core::ExtendedPOS next_extended_pos) {
  // Only verbs and adjectives have conjugation forms
  if (pos != core::PartOfSpeech::Verb && pos != core::PartOfSpeech::Adjective) {
    return grammar::ConjForm::Base;
  }

  // ExtendedPOS is the lattice's grammatical decision and is authoritative.
  // Surface rules below are retained only for callers and legacy morphemes
  // that do not carry a verb/adjective form.
  if (const grammar::ConjForm form = conjFormFromExtendedPos(extended_pos, next_extended_pos, next_lemma);
      form != grammar::ConjForm::Count_) {
    return form;
  }

  // If surface equals lemma, it's the base form
  if (surface == lemma) {
    return grammar::ConjForm::Base;
  }

  // For ichidan verbs, mizenkei and renyokei have the same surface form
  // (e.g., 食べ for both). Use next morpheme to distinguish:
  // - ない/ぬ/よう → Mizenkei
  // - て/た/ます → Renyokei
  if (pos == core::PartOfSpeech::Verb && utf8::endsWith(lemma, "る")) {
    // Check if this looks like an ichidan verb stem (lemma ends with る, surface is stem)
    // Ichidan verb stem = lemma without final る
    if (lemma.size() >= core::kJapaneseCharBytes && surface.size() >= core::kJapaneseCharBytes) {
      std::string_view lemma_stem(lemma.data(), lemma.size() - core::kJapaneseCharBytes);
      if (surface == lemma_stem && !next_lemma.empty()) {
        // This is an ichidan verb stem - check what follows
        if (utf8::equalsAny(next_lemma, {
                                            "ない", "ぬ", "ず", "よう", "まい",  // negative/volitional
                                            "れる", "られる",                    // passive
                                            "せる", "させる"                     // causative
                                        })) {
          return grammar::ConjForm::Mizenkei;
        }
        // て/た/ます → Renyokei (will be caught by default below)
      }
    }
  }

  // Check for negative forms (mizenkei). A lexical adjective whose lemma
  // itself ends in ない is not a negative construction (少ない→少なく).
  const bool lemma_contains_nai = utf8::endsWith(lemma, "ない");
  if (!lemma_contains_nai && utf8::endsWithAny(surface, {"ない", "なかった", "ぬ", "ず", "ません", "なく", "なくて",
                                                         "なければ", "なきゃ", "なくても"})) {
    return grammar::ConjForm::Mizenkei;
  }

  // Check for passive/causative (mizenkei)
  if (utf8::endsWithAny(surface,
                        {"れる", "られる", "せる", "させる", "れた", "られた", "せた", "させた", "される", "された"})) {
    return grammar::ConjForm::Mizenkei;
  }

  // Check for volitional form (ishikei)
  if (utf8::endsWithAny(surface, {"う", "よう", "まい"})) {
    // Distinguish from godan base form ending in う
    if (surface != lemma) {
      return grammar::ConjForm::Ishikei;
    }
  }

  // Check for conditional form (kateikei)
  if (utf8::endsWithAny(surface, {"ば", "れば"})) {
    return grammar::ConjForm::Kateikei;
  }

  // Check for imperative form (meireikei)
  if (utf8::endsWithAny(surface, {"ろ", "よ", "なさい"})) {
    // Check if it's likely an imperative
    if (surface.size() > core::kJapaneseCharBytes && surface != lemma) {
      return grammar::ConjForm::Meireikei;
    }
  }

  // Check for te-form onbin patterns (onbinkei)
  if (utf8::endsWithAny(surface, {"って", "いて", "いで", "んで", "った", "いた", "いだ", "んだ"})) {
    return grammar::ConjForm::Onbinkei;
  }

  // Check for renyokei (te-form, ta-form, masu-form, etc.)
  if (utf8::endsWithAny(
          surface, {"て",     "で",     "た",     "だ",     "ます",   "ました",     "まして",   "ている",  "ていた",
                    "ておく", "てある", "てみる", "てくる", "ていく", "てしまう",   "ちゃう",   "たい",    "たかった",
                    "たら",   "たり",   "きた",   "してる", "してた", "しています", "していた", "しました"})) {
    return grammar::ConjForm::Renyokei;
  }

  // For i-adjectives
  if (pos == core::PartOfSpeech::Adjective) {
    if (utf8::endsWithAny(surface, {"く", "くて", "かった", "ければ", "さ", "そう"})) {
      return grammar::ConjForm::Renyokei;
    }
  }

  // Default to renyokei for conjugated forms we couldn't classify
  if (surface != lemma) {
    return grammar::ConjForm::Renyokei;
  }

  return grammar::ConjForm::Base;
}

}  // namespace suzume::postprocess
