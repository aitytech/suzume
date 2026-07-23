#ifndef SUZUME_CLI_CLI_COMMON_H_
#define SUZUME_CLI_CLI_COMMON_H_

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace suzume::cli {

/**
 * @brief Output format for analysis results
 */
enum class OutputFormat {
  Morpheme,  // Default: surface TAB pos TAB lemma
  Tags,      // Tags only, one per line
  Json,      // JSON format
  Tsv,       // TSV with all fields
  Chasen     // ChaSen-like format (Japanese POS, conjugation info)
};

/**
 * @brief Print error message to stderr
 */
void printError(std::string_view message);

/**
 * @brief Print warning message to stderr
 */
void printWarning(std::string_view message);

/**
 * @brief Print info message to stderr
 */
void printInfo(std::string_view message);

/**
 * @brief Parse a non-negative integer option.
 */
bool parseSizeOption(std::string_view value, size_t* out);

/**
 * @brief Escape a string for JSON output.
 */
std::string jsonEscape(std::string_view value);

/**
 * @brief Remove a UTF-8 byte order mark at the start of a string, if present.
 */
void stripUtf8Bom(std::string* value);

/**
 * @brief Translate a shell-style wildcard pattern (* and ?) into an ECMAScript
 *        regex string, escaping all other regex metacharacters.
 */
std::string wildcardToRegex(std::string_view pattern);

/**
 * @brief True if @p path ends with @p ext (e.g. ".dic"). ASCII, case-sensitive.
 */
bool hasExtension(std::string_view path, std::string_view ext);

/**
 * @brief Swap a trailing @p from_ext for @p to_ext (e.g. ".tsv" -> ".dic"); if
 *        @p path does not end with @p from_ext, @p to_ext is appended instead.
 */
std::string swapOrAppendExtension(std::string_view path, std::string_view from_ext, std::string_view to_ext);

/**
 * @brief Check if input is from terminal (not piped)
 */
bool isTerminal();

/**
 * @brief Get version string
 */
std::string getVersionString();

/**
 * @brief Print version information
 */
void printVersion();

/**
 * @brief Command argument structure
 */
struct CommandArgs {
  std::string command;
  std::vector<std::string> args;

  // Common options
  std::vector<std::string> dict_paths;
  std::string mode = "normal";
  OutputFormat format = OutputFormat::Morpheme;
  bool verbose = false;
  bool very_verbose = false;
  bool debug = false;
  bool help = false;
  bool version = false;
  bool no_user_dict = false;
  bool no_core_dict = false;
  bool compare = false;
  std::string parse_error;

  // Normalization options (defaults preserve original)
  bool normalize_vu = false;  // --normalize-vu: convert ヴ→ビ
  bool lowercase = false;     // --lowercase: convert to lowercase

  // Postprocess options
  bool preserve_symbols = false;  // --preserve-symbols: keep symbols in output
  bool no_lemmatize = false;
  bool merge_compounds = false;

  // Tag generation options
  bool tag_include_particles = false;
  bool tag_include_auxiliaries = false;
  bool tag_include_formal_nouns = false;
  bool tag_include_low_info = false;
  bool tag_keep_duplicates = false;
  bool tag_use_surface = false;
  uint8_t tag_pos_filter = 0;
  bool tag_exclude_basic = false;
  size_t tag_min_length = 2;
  size_t tag_max_tags = 0;
};

/**
 * @brief Parse command line arguments
 * @param argc Argument count
 * @param argv Argument values
 * @return Parsed CommandArgs structure
 */
CommandArgs parseArgs(int argc, char* argv[]);

/**
 * @brief Print main help message
 */
void printHelp();

/**
 * @brief Print help for analyze command
 */
void printAnalyzeHelp();

/**
 * @brief Print help for dict command
 */
void printDictHelp();

/**
 * @brief Print help for test command
 */
void printTestHelp();

}  // namespace suzume::cli

#endif  // SUZUME_CLI_CLI_COMMON_H_
