/**
 * @file conjugator.cpp
 * @brief Dynamic conjugation stem generator implementation
 */

#include "conjugator.h"

namespace suzume::grammar {

Conjugator::Conjugator() = default;

std::string Conjugator::getStem(const std::string& base_form, VerbType type) const {
  return conjugation_.getStem(base_form, type);
}

VerbType Conjugator::detectType(const std::string& base_form) const {
  return conjugation_.detectType(base_form);
}

std::vector<StemForm> Conjugator::generateStems(const std::string& base_form, VerbType type) const {
  std::string stem = getStem(base_form, type);

  if (isGodanVerbType(type)) {
    return generateGodanStems(stem, base_form, type);
  }

  switch (type) {
    case VerbType::Ichidan:
      return generateIchidanStems(stem, base_form);

    case VerbType::Suru:
      return generateSuruStems(stem, base_form);

    case VerbType::Kuru:
      return generateKuruStems(stem, base_form);

    default:
      return {};
  }
}

std::vector<StemForm> Conjugator::generateGodanStems(const std::string& stem, const std::string& base_form,
                                                     VerbType type) const {
  std::vector<StemForm> forms;
  const auto* row = Conjugation::getGodanRow(type);
  if (row == nullptr) {
    return forms;
  }

  const GodanVowels vowels = encodeGodanVowels(*row);
  const std::string& base_suffix = vowels.base;
  const std::string& a_suffix = vowels.a;
  const std::string& i_suffix = vowels.i;

  // 終止形 (Base)
  forms.push_back({base_form, type, base_suffix, conn::kVerbBase});

  // 未然形 (Mizenkei): 書か
  forms.push_back({stem + a_suffix, type, base_suffix, conn::kVerbMizenkei});

  // 連用形 (Renyokei): 書き
  forms.push_back({stem + i_suffix, type, base_suffix, conn::kVerbRenyokei});

  // 音便形 (Onbinkei): 書い, 読ん, 持っ
  if (!row->onbin.empty()) {
    forms.push_back({stem + row->onbin, type, base_suffix, conn::kVerbOnbinkei});
  } else {
    // サ行: 連用形が音便形を兼ねる (話し + た)
    forms.push_back({stem + i_suffix, type, base_suffix, conn::kVerbOnbinkei});
  }

  return forms;
}

std::vector<StemForm> Conjugator::generateIchidanStems(const std::string& stem, const std::string& base_form) const {
  std::vector<StemForm> forms;
  VerbType type = VerbType::Ichidan;

  // 一段動詞: 語幹 = 食べ (食べる - る)
  // 終止形
  forms.push_back({base_form, type, "る", conn::kVerbBase});

  // 未然形・連用形・音便形はすべて語幹と同じ
  forms.push_back({stem, type, "る", conn::kVerbMizenkei});
  forms.push_back({stem, type, "る", conn::kVerbRenyokei});
  forms.push_back({stem, type, "る", conn::kVerbOnbinkei});

  return forms;
}

std::vector<StemForm> Conjugator::generateSuruStems(const std::string& stem, const std::string& base_form) const {
  std::vector<StemForm> forms;
  VerbType type = VerbType::Suru;

  // する stems produced here: base form, し (連用形・音便形), さ (未然形).
  // The し/せ mizenkei variants (しない, せず) are supplied by the Conjugation
  // analyzer on the tokenizer path; this generator feeds dictionary inspection.
  forms.push_back({base_form, type, "する", conn::kVerbBase});
  forms.push_back({stem + "し", type, "する", conn::kVerbRenyokei});
  forms.push_back({stem + "し", type, "する", conn::kVerbOnbinkei});
  forms.push_back({stem + "さ", type, "する", conn::kVerbMizenkei});

  return forms;
}

std::vector<StemForm> Conjugator::generateKuruStems(const std::string& stem, const std::string& base_form) const {
  std::vector<StemForm> forms;
  VerbType type = VerbType::Kuru;

  // 来る: き (連用形), こ (未然形), こい (命令形)
  forms.push_back({base_form, type, base_form, conn::kVerbBase});
  forms.push_back({stem + "き", type, base_form, conn::kVerbRenyokei});
  forms.push_back({stem + "き", type, base_form, conn::kVerbOnbinkei});
  forms.push_back({stem + "こ", type, base_form, conn::kVerbMizenkei});
  forms.push_back({stem + "い", type, base_form, conn::kVerbMeireikei});

  return forms;
}

}  // namespace suzume::grammar
