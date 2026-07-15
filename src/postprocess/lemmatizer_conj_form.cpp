#include <algorithm>

#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "grammar/conjugation.h"
#include "grammar/inflection_scorer_constants.h"
#include "normalize/char_type.h"
#include "normalize/utf8.h"
#include "postprocess/lemmatizer.h"

namespace suzume::postprocess {

// NOTE: core::detectVerbForm() also maps endings to conjugation forms, but over
// a different input domain (pre-merge verb surface plus suffix chain, verbs only)
// with intentionally different ending sets and tier order. The same ending can
// legitimately classify differently across the two (e.g. なければ: Mizenkei here
// via the negative tier, Kateikei there via the ば tier; って/った: Onbinkei here,
// TeForm/TaForm there), and Ishikei has no ExtendedPOS counterpart. The two
// mappings are not copies of one table and must not be merged.
grammar::ConjForm Lemmatizer::detectConjForm(std::string_view surface, std::string_view lemma, core::PartOfSpeech pos,
                                             std::string_view next_lemma) {
  // Only verbs and adjectives have conjugation forms
  if (pos != core::PartOfSpeech::Verb && pos != core::PartOfSpeech::Adjective) {
    return grammar::ConjForm::Base;
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
    if (lemma.size() >= 3 && surface.size() >= 3) {
      std::string_view lemma_stem(lemma.data(), lemma.size() - 3);  // Remove る (3 bytes)
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

  // Check for negative forms (mizenkei)
  if (utf8::endsWithAny(
          surface, {"ない", "なかった", "ぬ", "ず", "ません", "なく", "なくて", "なければ", "なきゃ", "なくても"})) {
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
