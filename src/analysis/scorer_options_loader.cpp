#include "analysis/scorer_options_loader.h"

#include <array>
#include <cctype>
#include <cfloat>
#include <cstdint>
#include <cstdlib>
#ifndef __EMSCRIPTEN__
#include <fstream>
#include <iostream>
#include <sstream>
#endif
#include <string_view>
#include <utility>
#include <vector>

namespace suzume::analysis {

namespace scorer_options_loader_detail {

struct JsonValue {
  enum class Type { Null, Number, String, Object, Array };
  Type type = Type::Null;
  float number_value{};
  std::string string_value;
  std::vector<std::pair<std::string, JsonValue>> object_value;

  bool isNumber() const { return type == Type::Number; }
  bool isObject() const { return type == Type::Object; }

  float asFloat() const { return number_value; }
  const JsonValue* get(const std::string& key) const {
    for (const auto& [name, value] : object_value) {
      if (name == key) {
        return &value;
      }
    }
    return nullptr;
  }
};

}  // namespace scorer_options_loader_detail

namespace {

template <typename Options>
struct FloatOptionSpec {
  const char* name;
  float Options::*value;
};

constexpr std::array<FloatOptionSpec<JoinOptions>, 3> kJoinOptionSpecs{{
    {"compound_verb_bonus", &JoinOptions::compound_verb_bonus},
    {"verified_v1_bonus", &JoinOptions::verified_v1_bonus},
    {"verified_noun_bonus", &JoinOptions::verified_noun_bonus},
}};

constexpr std::array<FloatOptionSpec<SplitOptions>, 10> kSplitOptionSpecs{{
    {"alpha_kanji_bonus", &SplitOptions::alpha_kanji_bonus},
    {"alpha_katakana_bonus", &SplitOptions::alpha_katakana_bonus},
    {"digit_kanji_1_bonus", &SplitOptions::digit_kanji_1_bonus},
    {"digit_kanji_2_bonus", &SplitOptions::digit_kanji_2_bonus},
    {"duration_period_bonus", &SplitOptions::duration_period_bonus},
    {"digit_kanji_3_penalty", &SplitOptions::digit_kanji_3_penalty},
    {"dict_split_bonus", &SplitOptions::dict_split_bonus},
    {"split_base_cost", &SplitOptions::split_base_cost},
    {"noun_verb_split_bonus", &SplitOptions::noun_verb_split_bonus},
    {"verified_verb_bonus", &SplitOptions::verified_verb_bonus},
}};

constexpr std::array<FloatOptionSpec<ScorerOptions>, 8> kUnaryOptionSpecs{{
    {"noun_prior", &ScorerOptions::noun_prior},
    {"verb_prior", &ScorerOptions::verb_prior},
    {"adj_prior", &ScorerOptions::adj_prior},
    {"adv_prior", &ScorerOptions::adv_prior},
    {"particle_prior", &ScorerOptions::particle_prior},
    {"aux_prior", &ScorerOptions::aux_prior},
    {"pronoun_prior", &ScorerOptions::pronoun_prior},
    {"user_dict_bonus", &ScorerOptions::user_dict_bonus},
}};

constexpr std::array<FloatOptionSpec<VerbCandidateOptions>, 21> kVerbOptionSpecs{{
    {"confidence_low", &VerbCandidateOptions::confidence_low},
    {"confidence_standard", &VerbCandidateOptions::confidence_standard},
    {"confidence_past_te", &VerbCandidateOptions::confidence_past_te},
    {"confidence_ichidan_dict", &VerbCandidateOptions::confidence_ichidan_dict},
    {"confidence_short_godan_base", &VerbCandidateOptions::confidence_short_godan_base},
    {"confidence_dict_verb", &VerbCandidateOptions::confidence_dict_verb},
    {"confidence_katakana", &VerbCandidateOptions::confidence_katakana},
    {"confidence_high", &VerbCandidateOptions::confidence_high},
    {"confidence_very_high", &VerbCandidateOptions::confidence_very_high},
    {"base_cost_standard", &VerbCandidateOptions::base_cost_standard},
    {"base_cost_high", &VerbCandidateOptions::base_cost_high},
    {"base_cost_low", &VerbCandidateOptions::base_cost_low},
    {"base_cost_verified", &VerbCandidateOptions::base_cost_verified},
    {"base_cost_long_verified", &VerbCandidateOptions::base_cost_long_verified},
    {"bonus_ichidan", &VerbCandidateOptions::bonus_ichidan},
    {"bonus_long_dict", &VerbCandidateOptions::bonus_long_dict},
    {"bonus_long_verified", &VerbCandidateOptions::bonus_long_verified},
    {"penalty_single_char", &VerbCandidateOptions::penalty_single_char},
    {"confidence_cost_scale", &VerbCandidateOptions::confidence_cost_scale},
    {"confidence_cost_scale_small", &VerbCandidateOptions::confidence_cost_scale_small},
    {"confidence_cost_scale_medium", &VerbCandidateOptions::confidence_cost_scale_medium},
}};

constexpr std::array<FloatOptionSpec<grammar::InflectionScorerOptions>, 28> kInflectionOptionSpecs{{
    {"base_confidence", &grammar::InflectionScorerOptions::base_confidence},
    {"confidence_floor", &grammar::InflectionScorerOptions::confidence_floor},
    {"confidence_ceiling", &grammar::InflectionScorerOptions::confidence_ceiling},
    {"penalty_stem_very_long", &grammar::InflectionScorerOptions::penalty_stem_very_long},
    {"penalty_stem_long", &grammar::InflectionScorerOptions::penalty_stem_long},
    {"bonus_stem_two_char", &grammar::InflectionScorerOptions::bonus_stem_two_char},
    {"bonus_aux_length_per_byte", &grammar::InflectionScorerOptions::bonus_aux_length_per_byte},
    {"penalty_ichidan_potential_ambiguity", &grammar::InflectionScorerOptions::penalty_ichidan_potential_ambiguity},
    {"bonus_ichidan_e_row", &grammar::InflectionScorerOptions::bonus_ichidan_e_row},
    {"penalty_ichidan_looks_godan", &grammar::InflectionScorerOptions::penalty_ichidan_looks_godan},
    {"penalty_ichidan_kanji_i", &grammar::InflectionScorerOptions::penalty_ichidan_kanji_i},
    {"penalty_ichidan_kanji_hiragana_stem", &grammar::InflectionScorerOptions::penalty_ichidan_kanji_hiragana_stem},
    {"penalty_ichidan_irregular_stem", &grammar::InflectionScorerOptions::penalty_ichidan_irregular_stem},
    {"penalty_i_adj_single_kanji", &grammar::InflectionScorerOptions::penalty_i_adj_single_kanji},
    {"penalty_i_adj_verb_aux_pattern", &grammar::InflectionScorerOptions::penalty_i_adj_verb_aux_pattern},
    {"bonus_i_adj_compound_yasui_nikui", &grammar::InflectionScorerOptions::bonus_i_adj_compound_yasui_nikui},
    {"penalty_i_adj_e_row_stem", &grammar::InflectionScorerOptions::penalty_i_adj_e_row_stem},
    {"penalty_i_adj_ru_stem_invalid", &grammar::InflectionScorerOptions::penalty_i_adj_ru_stem_invalid},
    {"penalty_i_adj_verb_rashii_pattern", &grammar::InflectionScorerOptions::penalty_i_adj_verb_rashii_pattern},
    {"bonus_suru_two_kanji", &grammar::InflectionScorerOptions::bonus_suru_two_kanji},
    {"penalty_godan_sa_two_kanji", &grammar::InflectionScorerOptions::penalty_godan_sa_two_kanji},
    {"bonus_godan_sa_single_kanji", &grammar::InflectionScorerOptions::bonus_godan_sa_single_kanji},
    {"penalty_suru_single_kanji", &grammar::InflectionScorerOptions::penalty_suru_single_kanji},
    {"penalty_ichidan_single_hiragana_particle",
     &grammar::InflectionScorerOptions::penalty_ichidan_single_hiragana_particle},
    {"penalty_pure_hiragana_stem", &grammar::InflectionScorerOptions::penalty_pure_hiragana_stem},
    {"penalty_godan_single_hiragana_stem", &grammar::InflectionScorerOptions::penalty_godan_single_hiragana_stem},
    {"penalty_godan_non_ra_pure_hiragana", &grammar::InflectionScorerOptions::penalty_godan_non_ra_pure_hiragana},
    {"penalty_godan_te_stem", &grammar::InflectionScorerOptions::penalty_godan_te_stem},
}};

template <typename Options, size_t Size>
void applyOptionSpecs(Options& options, const JsonValue& json,
                      const std::array<FloatOptionSpec<Options>, Size>& specs) {
  for (const auto& spec : specs) {
    const JsonValue* value = json.get(spec.name);
    if (value != nullptr && value->isNumber()) {
      options.*(spec.value) = value->asFloat();
    }
  }
}

template <typename Spec, size_t Size>
bool hasOptionName(std::string_view name, const std::array<Spec, Size>& specs) {
  for (const auto& spec : specs) {
    if (name == spec.name) {
      return true;
    }
  }
  return false;
}

template <size_t Size>
bool hasOptionName(std::string_view name, const std::array<std::string_view, Size>& names) {
  for (std::string_view candidate : names) {
    if (name == candidate) {
      return true;
    }
  }
  return false;
}

template <typename Spec, size_t Size>
bool validateOptionObject(const JsonValue& json, const std::array<Spec, Size>& specs, std::string_view path,
                          std::string* error_message) {
  for (const auto& [name, value] : json.object_value) {
    if (!hasOptionName(name, specs)) {
      if (error_message != nullptr) {
        *error_message = "Unknown scorer option: " + std::string(path) + "." + name;
      }
      return false;
    }
    if (!value.isNumber()) {
      if (error_message != nullptr) {
        *error_message = "Scorer option must be numeric: " + std::string(path) + "." + name;
      }
      return false;
    }
  }
  return true;
}

bool validateObject(const JsonValue& parent, const char* key, std::string_view path, const JsonValue** output,
                    std::string* error_message) {
  const JsonValue* value = parent.get(key);
  if (value == nullptr) {
    *output = nullptr;
    return true;
  }
  if (!value->isObject()) {
    if (error_message != nullptr) {
      *error_message = "Scorer section must be an object: " + std::string(path);
    }
    return false;
  }
  *output = value;
  return true;
}

bool validateConfig(const JsonValue& root, std::string* error_message) {
  constexpr std::array<std::string_view, 5> kRootSections{"candidates", "unary", "bigram", "verb_candidates",
                                                          "inflection"};
  for (const auto& [name, value] : root.object_value) {
    (void)value;
    if (!hasOptionName(name, kRootSections)) {
      if (error_message != nullptr) {
        *error_message = "Unknown scorer section: " + name;
      }
      return false;
    }
  }

  const JsonValue* candidates = nullptr;
  if (!validateObject(root, "candidates", "candidates", &candidates, error_message)) {
    return false;
  }
  if (candidates != nullptr) {
    constexpr std::array<std::string_view, 2> kCandidateSections{"join", "split"};
    for (const auto& [name, value] : candidates->object_value) {
      (void)value;
      if (!hasOptionName(name, kCandidateSections)) {
        if (error_message != nullptr) {
          *error_message = "Unknown scorer section: candidates." + name;
        }
        return false;
      }
    }
    const JsonValue* join = nullptr;
    const JsonValue* split = nullptr;
    if (!validateObject(*candidates, "join", "candidates.join", &join, error_message) ||
        !validateObject(*candidates, "split", "candidates.split", &split, error_message)) {
      return false;
    }
    if ((join != nullptr && !validateOptionObject(*join, kJoinOptionSpecs, "candidates.join", error_message)) ||
        (split != nullptr && !validateOptionObject(*split, kSplitOptionSpecs, "candidates.split", error_message))) {
      return false;
    }
  }

  const JsonValue* unary = nullptr;
  const JsonValue* bigram = nullptr;
  const JsonValue* verb = nullptr;
  const JsonValue* inflection = nullptr;
  if (!validateObject(root, "unary", "unary", &unary, error_message) ||
      !validateObject(root, "bigram", "bigram", &bigram, error_message) ||
      !validateObject(root, "verb_candidates", "verb_candidates", &verb, error_message) ||
      !validateObject(root, "inflection", "inflection", &inflection, error_message)) {
    return false;
  }
  return (unary == nullptr || validateOptionObject(*unary, kUnaryOptionSpecs, "unary", error_message)) &&
         (bigram == nullptr || validateOptionObject(*bigram, kBigramOverrideSpecs, "bigram", error_message)) &&
         (verb == nullptr || validateOptionObject(*verb, kVerbOptionSpecs, "verb_candidates", error_message)) &&
         (inflection == nullptr ||
          validateOptionObject(*inflection, kInflectionOptionSpecs, "inflection", error_message));
}

// Powers of ten that are exactly representable in double; 1e22 is the largest.
constexpr int kMaxExactPowerOfTen = 22;
constexpr std::array<double, kMaxExactPowerOfTen + 1> kPowersOfTen{{
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22,
}};

// A 19-digit significand is the most that fits a 64-bit integer exactly.
constexpr int kMaxSignificandDigits = 19;
// Beyond these decimal exponents no 19-digit significand can land inside the
// normal float range, so the value is rejected without scaling it digit by digit.
constexpr int kOverflowExponent = 39;
constexpr int kUnderflowExponent = -60;

// Convert a scanned JSON number token to float.
//
// std::strtof would do this, but the libc scanner it calls computes in 128-bit
// long double, which drags the software floating-point helpers (__addtf3,
// __multf3, __divtf3, fmodl and the scanner internals) into the WASM binary —
// about 7 KB of code for one configuration knob. The caller has already limited
// the token to [-]digits[.digits][(e|E)[+-]digits], so a direct conversion
// covers the whole accepted grammar.
//
// Rounding: the significand is accumulated exactly and scaled by exact powers of
// ten, so the double intermediate carries a single rounding at 2^-53 before it is
// narrowed to float at 2^-24. Magnitudes outside the normal float range are
// rejected, which is what the strtof path did through ERANGE.
bool convertDecimalToFloat(std::string_view text, float& out) {
  size_t pos = 0;
  bool negative = false;
  if (pos < text.size() && (text[pos] == '-' || text[pos] == '+')) {
    negative = text[pos] == '-';
    ++pos;
  }

  uint64_t significand = 0;
  int exponent = 0;
  int digits = 0;
  bool has_digit = false;
  while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
    has_digit = true;
    if (digits < kMaxSignificandDigits) {
      significand = significand * 10 + static_cast<uint64_t>(text[pos] - '0');
      if (significand != 0) {
        ++digits;
      }
    } else {
      // Digits past the significand still scale the value.
      ++exponent;
    }
    ++pos;
  }
  if (pos < text.size() && text[pos] == '.') {
    ++pos;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
      has_digit = true;
      if (digits < kMaxSignificandDigits) {
        significand = significand * 10 + static_cast<uint64_t>(text[pos] - '0');
        if (significand != 0) {
          ++digits;
        }
        --exponent;
      }
      ++pos;
    }
  }
  if (!has_digit) {
    return false;
  }

  if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
    ++pos;
    bool exponent_negative = false;
    if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
      exponent_negative = text[pos] == '-';
      ++pos;
    }
    if (pos >= text.size() || text[pos] < '0' || text[pos] > '9') {
      return false;
    }
    int written = 0;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
      if (written < 1000) {
        written = written * 10 + (text[pos] - '0');
      }
      ++pos;
    }
    exponent += exponent_negative ? -written : written;
  }
  if (pos != text.size()) {
    return false;
  }

  if (significand == 0) {
    // A zero significand still carries the sign the token spelled out.
    float zero{};
    out = negative ? -zero : zero;
    return true;
  }
  if (exponent > kOverflowExponent || exponent < kUnderflowExponent) {
    return false;
  }

  double value = static_cast<double>(significand);
  while (exponent > kMaxExactPowerOfTen) {
    value *= kPowersOfTen.back();
    exponent -= kMaxExactPowerOfTen;
  }
  while (exponent < -kMaxExactPowerOfTen) {
    value /= kPowersOfTen.back();
    exponent += kMaxExactPowerOfTen;
  }
  if (exponent > 0) {
    value *= kPowersOfTen[static_cast<size_t>(exponent)];
  } else if (exponent < 0) {
    value /= kPowersOfTen[static_cast<size_t>(-exponent)];
  }

  float result = static_cast<float>(negative ? -value : value);
  float magnitude = result < 0 ? -result : result;
  if (magnitude < FLT_MIN || magnitude > FLT_MAX) {
    return false;
  }
  out = result;
  return true;
}

}  // namespace

JsonValue ScorerOptionsLoader::Parser::parse() {
  skipWhitespace();
  JsonValue value = parseValue();
  skipWhitespace();
  if (!has_error_ && peek() != '\0') {
    setError("Trailing content after JSON value");
  }
  return value;
}

void ScorerOptionsLoader::Parser::setError(const char* msg) {
  if (!has_error_) {
    has_error_ = true;
    error_message_ = msg;
  }
}

JsonValue ScorerOptionsLoader::Parser::parseValue() {
  if (has_error_)
    return JsonValue{};
  skipWhitespace();
  char c = peek();
  if (c == '{')
    return parseObject();
  if (c == '[')
    return parseArray();
  if (c == '"')
    return parseString();
  if (c == '-' || (c >= '0' && c <= '9'))
    return parseNumber();
  if (c == 'n' && json_.compare(pos_, 4, "null") == 0) {
    pos_ += 4;
    return JsonValue{};
  }
  if (c == 't' && json_.compare(pos_, 4, "true") == 0) {
    pos_ += 4;
    JsonValue v;
    v.type = JsonValue::Type::Number;
    v.number_value = 1.0F;
    return v;
  }
  if (c == 'f' && json_.compare(pos_, 5, "false") == 0) {
    pos_ += 5;
    JsonValue v;
    v.type = JsonValue::Type::Number;
    v.number_value = 0.0F;
    return v;
  }
  setError("Unexpected character in JSON");
  return JsonValue{};
}

JsonValue ScorerOptionsLoader::Parser::parseObject() {
  JsonValue result;
  if (has_error_)
    return result;
  result.type = JsonValue::Type::Object;
  consume();  // '{'
  skipWhitespace();
  if (match('}')) {
    return result;
  }
  while (!has_error_) {
    if (peek() == '\0') {
      setError("Unterminated object");
      return result;
    }
    if (peek() != '"') {
      setError("Expected string key in object");
      return result;
    }
    auto key = parseString();
    if (has_error_)
      return result;
    skipWhitespace();
    if (!match(':')) {
      setError("Expected ':' in object");
      return result;
    }
    skipWhitespace();
    JsonValue value = parseValue();
    if (has_error_)
      return result;
    bool replaced = false;
    for (auto& [existing_key, existing_value] : result.object_value) {
      if (existing_key == key.string_value) {
        existing_value = std::move(value);
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      result.object_value.emplace_back(std::move(key.string_value), std::move(value));
    }
    skipWhitespace();
    if (match('}')) {
      return result;
    }
    if (!match(',')) {
      setError("Expected ',' or '}' in object");
      return result;
    }
    skipWhitespace();
  }
  return result;
}

JsonValue ScorerOptionsLoader::Parser::parseArray() {
  JsonValue result;
  if (has_error_)
    return result;
  result.type = JsonValue::Type::Array;
  consume();  // '['
  skipWhitespace();
  if (match(']')) {
    return result;
  }
  while (!has_error_) {
    if (peek() == '\0') {
      setError("Unterminated array");
      return result;
    }
    parseValue();  // Skip array values for now
    if (has_error_)
      return result;
    skipWhitespace();
    if (match(']')) {
      return result;
    }
    if (!match(',')) {
      setError("Expected ',' or ']' in array");
      return result;
    }
    skipWhitespace();
  }
  return result;
}

JsonValue ScorerOptionsLoader::Parser::parseString() {
  JsonValue result;
  if (has_error_)
    return result;
  result.type = JsonValue::Type::String;
  if (!match('"')) {
    setError("Expected string");
    return result;
  }
  while (!has_error_ && pos_ < json_.size() && json_[pos_] != '"') {
    if (json_[pos_] == '\\' && pos_ + 1 < json_.size()) {
      pos_++;
      switch (json_[pos_]) {
        case 'n':
          result.string_value += '\n';
          break;
        case 't':
          result.string_value += '\t';
          break;
        case '"':
          result.string_value += '"';
          break;
        case '\\':
          result.string_value += '\\';
          break;
        default:
          result.string_value += json_[pos_];
          break;
      }
    } else {
      result.string_value += json_[pos_];
    }
    pos_++;
  }
  if (pos_ < json_.size() && json_[pos_] == '"') {
    consume();  // '"'
  } else {
    setError("Unterminated string");
  }
  return result;
}

JsonValue ScorerOptionsLoader::Parser::parseNumber() {
  JsonValue result;
  if (has_error_)
    return result;
  result.type = JsonValue::Type::Number;
  size_t start = pos_;
  if (peek() == '-')
    consume();
  while (pos_ < json_.size() && (json_[pos_] >= '0' && json_[pos_] <= '9'))
    pos_++;
  if (pos_ < json_.size() && json_[pos_] == '.') {
    pos_++;
    while (pos_ < json_.size() && (json_[pos_] >= '0' && json_[pos_] <= '9'))
      pos_++;
  }
  if (pos_ < json_.size() && (json_[pos_] == 'e' || json_[pos_] == 'E')) {
    pos_++;
    if (pos_ < json_.size() && (json_[pos_] == '+' || json_[pos_] == '-'))
      pos_++;
    while (pos_ < json_.size() && (json_[pos_] >= '0' && json_[pos_] <= '9'))
      pos_++;
  }
  if (pos_ == start) {
    setError("Invalid number in JSON");
    return result;
  }
  // Exception-free parse: the converter reports a malformed or out-of-range token
  // instead of throwing, and rejects the same inputs the strtof path rejected
  // through a trailing character or ERANGE.
  float parsed{};
  if (!convertDecimalToFloat(std::string_view(json_).substr(start, pos_ - start), parsed)) {
    setError("Invalid number in JSON");
    return result;
  }
  result.number_value = parsed;
  return result;
}

void ScorerOptionsLoader::Parser::skipWhitespace() {
  while (pos_ < json_.size() &&
         (json_[pos_] == ' ' || json_[pos_] == '\t' || json_[pos_] == '\n' || json_[pos_] == '\r')) {
    pos_++;
  }
}

char ScorerOptionsLoader::Parser::peek() const {
  return pos_ < json_.size() ? json_[pos_] : '\0';
}

char ScorerOptionsLoader::Parser::consume() {
  if (pos_ >= json_.size()) {
    setError("Unexpected end of JSON");
    return '\0';
  }
  return json_[pos_++];
}

bool ScorerOptionsLoader::Parser::match(char c) {
  if (peek() == c) {
    consume();
    return true;
  }
  return false;
}

void ScorerOptionsLoader::applyJoinOptions(JoinOptions& opts, const JsonValue& json) {
  applyOptionSpecs(opts, json, kJoinOptionSpecs);
}

void ScorerOptionsLoader::applySplitOptions(SplitOptions& opts, const JsonValue& json) {
  applyOptionSpecs(opts, json, kSplitOptionSpecs);
}

void ScorerOptionsLoader::applyUnaryOptions(ScorerOptions& opts, const JsonValue& json) {
  applyOptionSpecs(opts, json, kUnaryOptionSpecs);
}

void ScorerOptionsLoader::applyBigramOptions(ScorerOptions::BigramOverrides& opts, const JsonValue& json) {
  for (const BigramOverrideSpec& spec : kBigramOverrideSpecs) {
    const JsonValue* value = json.get(spec.name);
    if (value != nullptr && value->isNumber()) {
      opts.*(spec.value) = value->asFloat();
    }
  }
}

void ScorerOptionsLoader::applyVerbCandidateOptions(VerbCandidateOptions& opts, const JsonValue& json) {
  applyOptionSpecs(opts, json, kVerbOptionSpecs);
}

void ScorerOptionsLoader::applyInflectionOptions(grammar::InflectionScorerOptions& opts, const JsonValue& json) {
  applyOptionSpecs(opts, json, kInflectionOptionSpecs);
}

bool ScorerOptionsLoader::loadFromFile(const std::string& path, ScorerOptions& options, std::string* error_msg) {
#ifdef __EMSCRIPTEN__
  (void)path;
  (void)options;
  if (error_msg != nullptr) {
    *error_msg = "File scorer configuration is unavailable in WebAssembly";
  }
  return false;
#else
  std::ifstream file(path);
  if (!file) {
    if (error_msg)
      *error_msg = "Cannot open file: " + path;
    return false;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return loadFromJsonString(buffer.str(), options, error_msg);
#endif
}

bool ScorerOptionsLoader::loadFromJsonString(const std::string& json, ScorerOptions& options, std::string* error_msg) {
  Parser parser(json);
  auto root = parser.parse();

  // Check for parse errors
  if (parser.hasError()) {
    if (error_msg)
      *error_msg = std::string("JSON parse error: ") + parser.errorMessage();
    return false;
  }

  if (!root.isObject()) {
    if (error_msg)
      *error_msg = "JSON root must be an object";
    return false;
  }

  if (!validateConfig(root, error_msg)) {
    return false;
  }

  // Apply candidates section
  if (auto* cands = root.get("candidates")) {
    if (cands->isObject()) {
      if (auto* join = cands->get("join")) {
        if (join->isObject())
          applyJoinOptions(options.candidates.join, *join);
      }
      if (auto* split = cands->get("split")) {
        if (split->isObject())
          applySplitOptions(options.candidates.split, *split);
      }
    }
  }

  // Apply unary section (POS priors, penalties, bonuses, optimal length)
  if (auto* unary = root.get("unary")) {
    if (unary->isObject()) {
      applyUnaryOptions(options, *unary);
    }
  }

  // Apply bigram section (POS pair cost overrides)
  if (auto* bigram = root.get("bigram")) {
    if (bigram->isObject()) {
      applyBigramOptions(options.bigram, *bigram);
    }
  }

  // Apply verb_candidates section (verb candidate generation options)
  if (auto* verb_cand = root.get("verb_candidates")) {
    if (verb_cand->isObject()) {
      applyVerbCandidateOptions(options.candidates.verb, *verb_cand);
    }
  }

  // Apply inflection section (inflection scorer confidence adjustments)
  if (auto* infl = root.get("inflection")) {
    if (infl->isObject()) {
      applyInflectionOptions(options.inflection, *infl);
    }
  }

  return true;
}

// =============================================================================
// Environment Variable Override Implementation
// =============================================================================

#ifndef __EMSCRIPTEN__

namespace env_override_internal {

// Helper to try parsing float from environment variable
bool tryGetEnvFloat(const char* name, float& out, bool report_warnings) {
  const char* value = std::getenv(name);
  if (!value)
    return false;

  char* end = nullptr;
  float parsed = std::strtof(value, &end);
  if (end == value || *end != '\0') {
    if (report_warnings) {
      std::cerr << "warning: Invalid value for " << name << ": " << value << "\n";
    }
    return false;
  }
  out = parsed;
  return true;
}

template <typename Options, size_t Size>
int applySpecs(const char* section, Options& options, const std::array<FloatOptionSpec<Options>, Size>& specs,
               bool report_warnings) {
  int count = 0;
  for (const auto& spec : specs) {
    const std::string variable_name = std::string("SUZUME_SCORER_") + section + "_" + spec.name;
    if (tryGetEnvFloat(variable_name.c_str(), options.*(spec.value), report_warnings)) {
      ++count;
    }
  }
  return count;
}

}  // namespace env_override_internal

int ScorerOptionsLoader::applyEnvOverrides(ScorerOptions& options) {
  return applyEnvOverrides(options, true);
}

int ScorerOptionsLoader::applyEnvOverrides(ScorerOptions& options, bool report_warnings) {
  using namespace env_override_internal;
  int count = 0;

  count += applySpecs("JOIN", options.candidates.join, kJoinOptionSpecs, report_warnings);
  count += applySpecs("SPLIT", options.candidates.split, kSplitOptionSpecs, report_warnings);
  count += applySpecs("UNARY", options, kUnaryOptionSpecs, report_warnings);

  // Bigram options (SUZUME_SCORER_BIGRAM_*)
  {
    auto& opts = options.bigram;
    for (const BigramOverrideSpec& spec : kBigramOverrideSpecs) {
      const std::string variable_name = std::string("SUZUME_SCORER_BIGRAM_") + spec.name;
      if (tryGetEnvFloat(variable_name.c_str(), opts.*(spec.value), report_warnings)) {
        ++count;
      }
    }
  }

  count += applySpecs("VERB", options.candidates.verb, kVerbOptionSpecs, report_warnings);
  count += applySpecs("INFL", options.inflection, kInflectionOptionSpecs, report_warnings);

  return count;
}

ScorerLoadResult ScorerOptionsLoader::loadFromEnv(ScorerOptions& options) {
  return loadFromEnv(options, true);
}

ScorerLoadResult ScorerOptionsLoader::loadFromEnv(ScorerOptions& options, bool report_warnings) {
  ScorerLoadResult result;

  // Check for SUZUME_SCORER_CONFIG environment variable (JSON file path)
  const char* config_path = std::getenv("SUZUME_SCORER_CONFIG");
  if (config_path && config_path[0] != '\0') {
    std::string error_msg;
    if (loadFromFile(config_path, options, &error_msg)) {
      result.config_path = config_path;
    } else {
      if (report_warnings) {
        std::cerr << "warning: Failed to load scorer config from SUZUME_SCORER_CONFIG: " << error_msg << "\n";
      }
    }
  }

  // Apply individual environment variable overrides (highest priority)
  result.env_override_count = applyEnvOverrides(options, report_warnings);

  return result;
}

#endif  // __EMSCRIPTEN__

}  // namespace suzume::analysis
