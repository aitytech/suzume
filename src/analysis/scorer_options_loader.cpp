#include "analysis/scorer_options_loader.h"

#ifndef __EMSCRIPTEN__

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

namespace suzume::analysis {

namespace scorer_options_loader_detail {

struct JsonValue {
  enum class Type { Null, Number, String, Object, Array };
  Type type = Type::Null;
  float number_value{};
  std::string string_value;
  std::map<std::string, JsonValue> object_value;

  bool isNumber() const { return type == Type::Number; }
  bool isObject() const { return type == Type::Object; }

  float asFloat() const { return number_value; }
  const JsonValue* get(const std::string& key) const {
    const auto iterator = object_value.find(key);
    return iterator != object_value.end() ? &iterator->second : nullptr;
  }
};

}  // namespace scorer_options_loader_detail

JsonValue ScorerOptionsLoader::Parser::parse() {
  skipWhitespace();
  return parseValue();
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
  if (c == 'n') {  // null
    pos_ += 4;
    return JsonValue{};
  }
  if (c == 't') {  // true
    pos_ += 4;
    JsonValue v;
    v.type = JsonValue::Type::Number;
    v.number_value = 1.0F;
    return v;
  }
  if (c == 'f') {  // false
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
  while (!has_error_ && peek() != '}' && peek() != '\0') {
    auto key = parseString();
    if (has_error_)
      return result;
    skipWhitespace();
    if (!match(':')) {
      setError("Expected ':' in object");
      return result;
    }
    skipWhitespace();
    result.object_value[key.string_value] = parseValue();
    if (has_error_)
      return result;
    skipWhitespace();
    if (peek() == ',')
      consume();
    skipWhitespace();
  }
  if (peek() == '}')
    consume();  // '}'
  return result;
}

JsonValue ScorerOptionsLoader::Parser::parseArray() {
  JsonValue result;
  if (has_error_)
    return result;
  result.type = JsonValue::Type::Array;
  consume();  // '['
  skipWhitespace();
  while (!has_error_ && peek() != ']' && peek() != '\0') {
    parseValue();  // Skip array values for now
    if (has_error_)
      return result;
    skipWhitespace();
    if (peek() == ',')
      consume();
    skipWhitespace();
  }
  if (peek() == ']')
    consume();  // ']'
  return result;
}

JsonValue ScorerOptionsLoader::Parser::parseString() {
  JsonValue result;
  if (has_error_)
    return result;
  result.type = JsonValue::Type::String;
  consume();  // '"'
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
  // Exception-free parse: std::strtof never throws (unlike std::stof). Validate
  // that the whole token was consumed and the magnitude is in range, mirroring
  // the environment-variable path (env_override_internal::tryGetEnvFloat).
  std::string number_str = json_.substr(start, pos_ - start);
  const char* begin_ptr = number_str.c_str();
  char* end_ptr = nullptr;
  errno = 0;
  float parsed = std::strtof(begin_ptr, &end_ptr);
  if (end_ptr == begin_ptr || *end_ptr != '\0' || errno == ERANGE) {
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

// Helper macro to set option from JSON
#define SET_OPT(opts, field, json, key) \
  do {                                  \
    auto* v = json.get(key);            \
    if (v && v->isNumber())             \
      opts.field = v->asFloat();        \
  } while (0)

void ScorerOptionsLoader::applyJoinOptions(JoinOptions& opts, const JsonValue& json) {
  SET_OPT(opts, compound_verb_bonus, json, "compound_verb_bonus");
  SET_OPT(opts, verified_v1_bonus, json, "verified_v1_bonus");
  SET_OPT(opts, verified_noun_bonus, json, "verified_noun_bonus");
  SET_OPT(opts, te_form_aux_bonus, json, "te_form_aux_bonus");
}

void ScorerOptionsLoader::applySplitOptions(SplitOptions& opts, const JsonValue& json) {
  SET_OPT(opts, alpha_kanji_bonus, json, "alpha_kanji_bonus");
  SET_OPT(opts, alpha_katakana_bonus, json, "alpha_katakana_bonus");
  SET_OPT(opts, digit_kanji_1_bonus, json, "digit_kanji_1_bonus");
  SET_OPT(opts, digit_kanji_2_bonus, json, "digit_kanji_2_bonus");
  SET_OPT(opts, duration_period_bonus, json, "duration_period_bonus");
  SET_OPT(opts, digit_kanji_3_penalty, json, "digit_kanji_3_penalty");
  SET_OPT(opts, dict_split_bonus, json, "dict_split_bonus");
  SET_OPT(opts, split_base_cost, json, "split_base_cost");
  SET_OPT(opts, noun_verb_split_bonus, json, "noun_verb_split_bonus");
  SET_OPT(opts, verified_verb_bonus, json, "verified_verb_bonus");
}

void ScorerOptionsLoader::applyUnaryOptions(ScorerOptions& opts, const JsonValue& json) {
  // POS priors
  SET_OPT(opts, noun_prior, json, "noun_prior");
  SET_OPT(opts, verb_prior, json, "verb_prior");
  SET_OPT(opts, adj_prior, json, "adj_prior");
  SET_OPT(opts, adv_prior, json, "adv_prior");
  SET_OPT(opts, particle_prior, json, "particle_prior");
  SET_OPT(opts, aux_prior, json, "aux_prior");
  SET_OPT(opts, pronoun_prior, json, "pronoun_prior");

  // Bonuses
  SET_OPT(opts, user_dict_bonus, json, "user_dict_bonus");
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
  // Confidence thresholds
  SET_OPT(opts, confidence_low, json, "confidence_low");
  SET_OPT(opts, confidence_standard, json, "confidence_standard");
  SET_OPT(opts, confidence_past_te, json, "confidence_past_te");
  SET_OPT(opts, confidence_ichidan_dict, json, "confidence_ichidan_dict");
  SET_OPT(opts, confidence_short_godan_base, json, "confidence_short_godan_base");
  SET_OPT(opts, confidence_dict_verb, json, "confidence_dict_verb");
  SET_OPT(opts, confidence_katakana, json, "confidence_katakana");
  SET_OPT(opts, confidence_high, json, "confidence_high");
  SET_OPT(opts, confidence_very_high, json, "confidence_very_high");
  // Base costs
  SET_OPT(opts, base_cost_standard, json, "base_cost_standard");
  SET_OPT(opts, base_cost_high, json, "base_cost_high");
  SET_OPT(opts, base_cost_low, json, "base_cost_low");
  SET_OPT(opts, base_cost_verified, json, "base_cost_verified");
  SET_OPT(opts, base_cost_long_verified, json, "base_cost_long_verified");
  // Bonuses
  SET_OPT(opts, bonus_ichidan, json, "bonus_ichidan");
  SET_OPT(opts, bonus_long_dict, json, "bonus_long_dict");
  SET_OPT(opts, bonus_long_verified, json, "bonus_long_verified");
  // Penalties
  SET_OPT(opts, penalty_single_char, json, "penalty_single_char");
  // Scaling
  SET_OPT(opts, confidence_cost_scale, json, "confidence_cost_scale");
}

void ScorerOptionsLoader::applyInflectionOptions(grammar::InflectionScorerOptions& opts, const JsonValue& json) {
  // Base configuration
  SET_OPT(opts, base_confidence, json, "base_confidence");
  SET_OPT(opts, confidence_floor, json, "confidence_floor");
  SET_OPT(opts, confidence_ceiling, json, "confidence_ceiling");

  // Stem length adjustments
  SET_OPT(opts, penalty_stem_very_long, json, "penalty_stem_very_long");
  SET_OPT(opts, penalty_stem_long, json, "penalty_stem_long");
  SET_OPT(opts, bonus_stem_two_char, json, "bonus_stem_two_char");
  SET_OPT(opts, bonus_aux_length_per_byte, json, "bonus_aux_length_per_byte");

  // Ichidan validation
  SET_OPT(opts, penalty_ichidan_potential_ambiguity, json, "penalty_ichidan_potential_ambiguity");
  SET_OPT(opts, bonus_ichidan_e_row, json, "bonus_ichidan_e_row");
  SET_OPT(opts, penalty_ichidan_looks_godan, json, "penalty_ichidan_looks_godan");
  SET_OPT(opts, penalty_ichidan_kanji_i, json, "penalty_ichidan_kanji_i");
  SET_OPT(opts, penalty_ichidan_kanji_hiragana_stem, json, "penalty_ichidan_kanji_hiragana_stem");
  SET_OPT(opts, penalty_ichidan_irregular_stem, json, "penalty_ichidan_irregular_stem");

  // I-Adjective validation
  SET_OPT(opts, penalty_i_adj_single_kanji, json, "penalty_i_adj_single_kanji");
  SET_OPT(opts, penalty_i_adj_verb_aux_pattern, json, "penalty_i_adj_verb_aux_pattern");
  SET_OPT(opts, bonus_i_adj_compound_yasui_nikui, json, "bonus_i_adj_compound_yasui_nikui");
  SET_OPT(opts, penalty_i_adj_e_row_stem, json, "penalty_i_adj_e_row_stem");
  SET_OPT(opts, penalty_i_adj_verb_rashii_pattern, json, "penalty_i_adj_verb_rashii_pattern");

  // Suru vs GodanSa disambiguation
  SET_OPT(opts, bonus_suru_two_kanji, json, "bonus_suru_two_kanji");
  SET_OPT(opts, penalty_godan_sa_two_kanji, json, "penalty_godan_sa_two_kanji");
  SET_OPT(opts, bonus_godan_sa_single_kanji, json, "bonus_godan_sa_single_kanji");
  SET_OPT(opts, penalty_suru_single_kanji, json, "penalty_suru_single_kanji");

  // Single hiragana stem penalties
  SET_OPT(opts, penalty_ichidan_single_hiragana_particle, json, "penalty_ichidan_single_hiragana_particle");
  SET_OPT(opts, penalty_pure_hiragana_stem, json, "penalty_pure_hiragana_stem");
  SET_OPT(opts, penalty_godan_single_hiragana_stem, json, "penalty_godan_single_hiragana_stem");
  SET_OPT(opts, penalty_godan_non_ra_pure_hiragana, json, "penalty_godan_non_ra_pure_hiragana");
}

#undef SET_OPT

bool ScorerOptionsLoader::loadFromFile(const std::string& path, ScorerOptions& options, std::string* error_msg) {
  std::ifstream file(path);
  if (!file) {
    if (error_msg)
      *error_msg = "Cannot open file: " + path;
    return false;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string json = buffer.str();

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

// Macro to check and apply single environment variable
#define TRY_ENV(section, field)                                                         \
  if (tryGetEnvFloat("SUZUME_SCORER_" section "_" #field, opts.field, report_warnings)) \
  count++

}  // namespace env_override_internal

int ScorerOptionsLoader::applyEnvOverrides(ScorerOptions& options) {
  return applyEnvOverrides(options, true);
}

int ScorerOptionsLoader::applyEnvOverrides(ScorerOptions& options, bool report_warnings) {
  using namespace env_override_internal;
  int count = 0;

  // Join options (SUZUME_SCORER_JOIN_*)
  {
    auto& opts = options.candidates.join;
    TRY_ENV("JOIN", compound_verb_bonus);
    TRY_ENV("JOIN", verified_v1_bonus);
    TRY_ENV("JOIN", verified_noun_bonus);
    TRY_ENV("JOIN", te_form_aux_bonus);
  }

  // Split options (SUZUME_SCORER_SPLIT_*)
  {
    auto& opts = options.candidates.split;
    TRY_ENV("SPLIT", alpha_kanji_bonus);
    TRY_ENV("SPLIT", alpha_katakana_bonus);
    TRY_ENV("SPLIT", digit_kanji_1_bonus);
    TRY_ENV("SPLIT", digit_kanji_2_bonus);
    TRY_ENV("SPLIT", duration_period_bonus);
    TRY_ENV("SPLIT", digit_kanji_3_penalty);
    TRY_ENV("SPLIT", dict_split_bonus);
    TRY_ENV("SPLIT", split_base_cost);
    TRY_ENV("SPLIT", noun_verb_split_bonus);
    TRY_ENV("SPLIT", verified_verb_bonus);
  }

  // Unary options (SUZUME_SCORER_UNARY_*)
  {
    auto& opts = options;
    // POS priors
    TRY_ENV("UNARY", noun_prior);
    TRY_ENV("UNARY", verb_prior);
    TRY_ENV("UNARY", adj_prior);
    TRY_ENV("UNARY", adv_prior);
    TRY_ENV("UNARY", particle_prior);
    TRY_ENV("UNARY", aux_prior);
    TRY_ENV("UNARY", pronoun_prior);
    // Bonuses
    TRY_ENV("UNARY", user_dict_bonus);
  }

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

  // Verb candidate options (SUZUME_SCORER_VERB_*)
  {
    auto& opts = options.candidates.verb;
    // Confidence thresholds
    TRY_ENV("VERB", confidence_low);
    TRY_ENV("VERB", confidence_standard);
    TRY_ENV("VERB", confidence_past_te);
    TRY_ENV("VERB", confidence_ichidan_dict);
    TRY_ENV("VERB", confidence_short_godan_base);
    TRY_ENV("VERB", confidence_dict_verb);
    TRY_ENV("VERB", confidence_katakana);
    TRY_ENV("VERB", confidence_high);
    TRY_ENV("VERB", confidence_very_high);
    // Base costs
    TRY_ENV("VERB", base_cost_standard);
    TRY_ENV("VERB", base_cost_high);
    TRY_ENV("VERB", base_cost_low);
    TRY_ENV("VERB", base_cost_verified);
    TRY_ENV("VERB", base_cost_long_verified);
    // Bonuses
    TRY_ENV("VERB", bonus_ichidan);
    TRY_ENV("VERB", bonus_long_dict);
    TRY_ENV("VERB", bonus_long_verified);
    // Penalties
    TRY_ENV("VERB", penalty_single_char);
    // Scaling
    TRY_ENV("VERB", confidence_cost_scale);
  }

  // Inflection scorer options (SUZUME_SCORER_INFL_*)
  {
    auto& opts = options.inflection;
    // Base configuration
    TRY_ENV("INFL", base_confidence);
    TRY_ENV("INFL", confidence_floor);
    TRY_ENV("INFL", confidence_ceiling);
    // Stem length adjustments
    TRY_ENV("INFL", penalty_stem_very_long);
    TRY_ENV("INFL", penalty_stem_long);
    TRY_ENV("INFL", bonus_stem_two_char);
    TRY_ENV("INFL", bonus_aux_length_per_byte);
    // Ichidan validation
    TRY_ENV("INFL", penalty_ichidan_potential_ambiguity);
    TRY_ENV("INFL", bonus_ichidan_e_row);
    TRY_ENV("INFL", penalty_ichidan_looks_godan);
    TRY_ENV("INFL", penalty_ichidan_kanji_i);
    TRY_ENV("INFL", penalty_ichidan_kanji_hiragana_stem);
    TRY_ENV("INFL", penalty_ichidan_irregular_stem);
    // I-Adjective validation
    TRY_ENV("INFL", penalty_i_adj_single_kanji);
    TRY_ENV("INFL", penalty_i_adj_verb_aux_pattern);
    TRY_ENV("INFL", bonus_i_adj_compound_yasui_nikui);
    TRY_ENV("INFL", penalty_i_adj_e_row_stem);
    TRY_ENV("INFL", penalty_i_adj_verb_rashii_pattern);
    // Suru vs GodanSa disambiguation
    TRY_ENV("INFL", bonus_suru_two_kanji);
    TRY_ENV("INFL", penalty_godan_sa_two_kanji);
    TRY_ENV("INFL", bonus_godan_sa_single_kanji);
    TRY_ENV("INFL", penalty_suru_single_kanji);
    // Single hiragana stem penalties
    TRY_ENV("INFL", penalty_ichidan_single_hiragana_particle);
    TRY_ENV("INFL", penalty_pure_hiragana_stem);
    TRY_ENV("INFL", penalty_godan_single_hiragana_stem);
    TRY_ENV("INFL", penalty_godan_non_ra_pure_hiragana);
  }

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

#undef TRY_ENV

#endif  // __EMSCRIPTEN__

}  // namespace suzume::analysis

#endif  // __EMSCRIPTEN__
