/**
 * @file conjugator.cpp
 * @brief Dynamic conjugation stem generator implementation
 */

#include "conjugator.h"

#include "core/utf8_constants.h"
#include "normalize/utf8.h"

namespace suzume::grammar {

using normalize::encodeUtf8;

Conjugator::Conjugator() = default;

std::string Conjugator::getStem(const std::string& base_form, VerbType type) const {
  return conjugation_.getStem(base_form, type);
}

VerbType Conjugator::detectType(const std::string& base_form) const {
  return conjugation_.detectType(base_form);
}

std::vector<StemForm> Conjugator::generateStems(const std::string& base_form, VerbType type) const {
  std::string stem = getStem(base_form, type);

  // Get base suffix (e.g., く for GodanKa)
  std::string base_suffix;
  if (base_form.size() >= core::kJapaneseCharBytes) {
    base_suffix = std::string(utf8::lastChar(base_form));
  }

  switch (type) {
    case VerbType::Ichidan:
      return generateIchidanStems(stem, base_form);

    case VerbType::GodanKa:
    case VerbType::GodanGa:
    case VerbType::GodanSa:
    case VerbType::GodanTa:
    case VerbType::GodanNa:
    case VerbType::GodanBa:
    case VerbType::GodanMa:
    case VerbType::GodanRa:
    case VerbType::GodanWa:
      return generateGodanStems(stem, base_form, type);

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

  std::string base_suffix = encodeUtf8(row->base_vowel);
  std::string a_suffix = encodeUtf8(row->a_row);
  std::string i_suffix = encodeUtf8(row->i_row);

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

  // 来る: き (連用形), こ (未然形)
  forms.push_back({base_form, type, base_form, conn::kVerbBase});
  forms.push_back({stem + "き", type, base_form, conn::kVerbRenyokei});
  forms.push_back({stem + "き", type, base_form, conn::kVerbOnbinkei});
  forms.push_back({stem + "こ", type, base_form, conn::kVerbMizenkei});

  return forms;
}

}  // namespace suzume::grammar
