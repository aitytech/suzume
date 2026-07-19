#ifndef SUZUME_ANALYSIS_SCORER_OPTIONS_LOADER_H_
#define SUZUME_ANALYSIS_SCORER_OPTIONS_LOADER_H_

// =============================================================================
// Scorer Options Loader
// =============================================================================
// Loads scorer options from JSON configuration files and environment variables.
// Provides partial override capability - only specified fields are updated.
//
// Environment variable format: SUZUME_SCORER_{SECTION}_{KEY}=value
//   SECTION: JOIN, SPLIT, UNARY, BIGRAM, VERB, INFL
//   KEY: Field name (e.g., compound_verb_bonus)
//
// Priority: Default < JSON file < Environment variables
// =============================================================================

#include <cstddef>
#include <string>

#include "candidate_options.h"
#include "scorer.h"
#include "scorer_bigram_overrides.h"

namespace suzume::analysis {

namespace scorer_options_loader_detail {
struct JsonValue;
}

using JsonValue = scorer_options_loader_detail::JsonValue;

/// Result of loading scorer options from environment
struct ScorerLoadResult {
  std::string config_path;    // Path to JSON config file (if loaded)
  int env_override_count{0};  // Number of individual env overrides applied

  bool hasConfig() const { return !config_path.empty() || env_override_count > 0; }
};

/// Parses JSON and loads scorer options
class ScorerOptionsLoader {
 public:
  /// Load scorer options from file (simple interface)
  /// @param path Path to JSON config file
  /// @param options Output options (modified on success)
  /// @param error_msg Optional error message output (nullptr to ignore)
  /// @return true on success, false on failure
  static bool loadFromFile(const std::string& path, ScorerOptions& options, std::string* error_msg = nullptr);

#ifndef __EMSCRIPTEN__
  /// Apply environment variable overrides to scorer options
  /// Environment variables: SUZUME_SCORER_{SECTION}_{KEY}=value
  /// @param options Options to modify
  /// @return Number of overrides applied
  static int applyEnvOverrides(ScorerOptions& options);
  static int applyEnvOverrides(ScorerOptions& options, bool report_warnings);

  /// Load scorer options from environment variables
  /// Checks SUZUME_SCORER_CONFIG for JSON file path, then individual overrides
  /// @param options Options to modify
  /// @return Result containing what was loaded
  static ScorerLoadResult loadFromEnv(ScorerOptions& options);
  static ScorerLoadResult loadFromEnv(ScorerOptions& options, bool report_warnings);
#endif

 private:
  /// Apply join options from JSON
  static void applyJoinOptions(JoinOptions& opts, const JsonValue& json);

  /// Apply split options from JSON
  static void applySplitOptions(SplitOptions& opts, const JsonValue& json);

  /// Apply unary options (POS priors, penalties, bonuses) from JSON
  static void applyUnaryOptions(ScorerOptions& opts, const JsonValue& json);

  /// Apply bigram override options from JSON
  static void applyBigramOptions(ScorerOptions::BigramOverrides& opts, const JsonValue& json);

  /// Apply verb candidate options from JSON
  static void applyVerbCandidateOptions(VerbCandidateOptions& opts, const JsonValue& json);

  /// Apply inflection scorer options from JSON
  static void applyInflectionOptions(grammar::InflectionScorerOptions& opts, const JsonValue& json);

  // JSON parser internals (exception-free)
  class Parser {
   public:
    explicit Parser(const std::string& json) : json_(json), pos_(0) {}
    JsonValue parse();

    // Error state accessors
    bool hasError() const { return has_error_; }
    const std::string& errorMessage() const { return error_message_; }

   private:
    JsonValue parseValue();
    JsonValue parseObject();
    JsonValue parseArray();
    JsonValue parseString();
    JsonValue parseNumber();
    void skipWhitespace();
    char peek() const;
    char consume();
    bool match(char c);
    void setError(const char* msg);

    std::string json_;
    size_t pos_{0};
    bool has_error_{false};
    std::string error_message_;
  };
};

}  // namespace suzume::analysis

#endif  // SUZUME_ANALYSIS_SCORER_OPTIONS_LOADER_H_
