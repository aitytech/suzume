/**
 * @file honorific_verbs.cpp
 * @brief Closed-class honorific, benefactive, modal, and aspectual subsidiary verb data
 */

#include "grammar/honorific_verbs.h"

#include <cstddef>

#include "core/utf8_constants.h"

namespace suzume::grammar {

using ::utf8::startsWithAny;

namespace {

// The kanji orthography of いたす belongs to the same closed class: 確認致します
// is the same construction as 確認いたします, and without the variant the
// continuative is absorbed into the preceding kanji run (確認致 + し).
constexpr std::string_view kHumbleHonorificRenyokei[] = {"いたし", "致し", "くださ", "いただき"};
// 給ふ and 候ふ are the classical members of the same class: they follow a
// continuative or an auxiliary exactly as くださる does (読ませ+給へ, 書かせ+候ふ).
constexpr std::string_view kHumbleHonorificLemmas[] = {"いたす", "致す",       "くださる", "いただく", "なさる",
                                                       "はする", "申し上げる", "給ふ",     "候ふ"};
constexpr std::string_view kBenefactiveRenyokei[] = {"もらい", "あげ"};
constexpr std::string_view kBenefactiveLemmas[] = {"もらう", "もらえる", "くれる", "あげる"};
constexpr std::string_view kPotentialBenefactiveLemmas[] = {"いただける", "もらえる"};
constexpr std::string_view kModalSubsidiaryRenyokei[] = {"かね"};
// 続ける is deliberately absent: productive V1+続ける compounds are themselves the
// search unit, so it belongs to the lexical V2 lexicon rather than to this class.
constexpr std::string_view kAspectualSubsidiaryLemmas[] = {"始める", "はじめる", "終わる", "おわる",
                                                           "終える", "おえる",   "過ぎる", "すぎる"};
// Suffixes that address or name a person. 様/氏 are absent on purpose: their
// kanji orthography cannot be confused with predicate material, so no caller
// needs a host check for them.
constexpr std::string_view kPersonalAddressSuffixes[] = {"さん", "ちゃん", "くん", "さま", "たん", "にゃん"};

bool contains(const std::string_view* entries, size_t count, std::string_view value) {
  for (size_t index = 0; index < count; ++index) {
    const std::string_view entry = entries[index];
    if (entry == value) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool isHumbleHonorificRenyokei(std::string_view surface) {
  return contains(kHumbleHonorificRenyokei, sizeof(kHumbleHonorificRenyokei) / sizeof(kHumbleHonorificRenyokei[0]),
                  surface);
}

bool isHumbleHonorificLemma(std::string_view lemma) {
  return contains(kHumbleHonorificLemmas, sizeof(kHumbleHonorificLemmas) / sizeof(kHumbleHonorificLemmas[0]), lemma);
}

bool isPotentialBenefactiveLemma(std::string_view lemma) {
  return contains(kPotentialBenefactiveLemmas,
                  sizeof(kPotentialBenefactiveLemmas) / sizeof(kPotentialBenefactiveLemmas[0]), lemma);
}

bool isBenefactiveLemma(std::string_view lemma) {
  return contains(kBenefactiveLemmas, sizeof(kBenefactiveLemmas) / sizeof(kBenefactiveLemmas[0]), lemma);
}

bool isSubsidiaryHonorificRenyokei(std::string_view surface) {
  return isHumbleHonorificRenyokei(surface) ||
         contains(kBenefactiveRenyokei, sizeof(kBenefactiveRenyokei) / sizeof(kBenefactiveRenyokei[0]), surface);
}

bool isModalSubsidiaryRenyokei(std::string_view surface) {
  return contains(kModalSubsidiaryRenyokei, sizeof(kModalSubsidiaryRenyokei) / sizeof(kModalSubsidiaryRenyokei[0]),
                  surface);
}

bool startsHonorificSubsidiaryVerb(std::string_view surface) {
  return startsWithAny(surface, {"くださ", "いただ", "いたし", "いたす", "致し", "致す"});
}

bool isAspectualSubsidiaryLemma(std::string_view lemma) {
  return contains(kAspectualSubsidiaryLemmas,
                  sizeof(kAspectualSubsidiaryLemmas) / sizeof(kAspectualSubsidiaryLemmas[0]), lemma);
}

bool isPersonalAddressSuffix(std::string_view surface) {
  return contains(kPersonalAddressSuffixes, sizeof(kPersonalAddressSuffixes) / sizeof(kPersonalAddressSuffixes[0]),
                  surface);
}

}  // namespace suzume::grammar
