/**
 * @file honorific_verbs.cpp
 * @brief Closed-class honorific, benefactive, and modal subsidiary verb data
 */

#include "grammar/honorific_verbs.h"

#include <cstddef>

namespace suzume::grammar {

namespace {

constexpr std::string_view kHumbleHonorificRenyokei[] = {"いたし", "くださ", "いただき"};
constexpr std::string_view kHumbleHonorificLemmas[] = {"いたす", "くださる", "いただく", "なさる", "はする"};
constexpr std::string_view kBenefactiveRenyokei[] = {"もらい", "あげ"};
constexpr std::string_view kPotentialBenefactiveLemmas[] = {"いただける"};
constexpr std::string_view kModalSubsidiaryRenyokei[] = {"かね"};

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

bool isSubsidiaryHonorificRenyokei(std::string_view surface) {
  return isHumbleHonorificRenyokei(surface) ||
         contains(kBenefactiveRenyokei, sizeof(kBenefactiveRenyokei) / sizeof(kBenefactiveRenyokei[0]), surface);
}

bool isModalSubsidiaryRenyokei(std::string_view surface) {
  return contains(kModalSubsidiaryRenyokei, sizeof(kModalSubsidiaryRenyokei) / sizeof(kModalSubsidiaryRenyokei[0]),
                  surface);
}

}  // namespace suzume::grammar
