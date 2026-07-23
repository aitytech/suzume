#include "cli_common.h"

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include <cstdio>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

#include "postprocess/tag_generator.h"
#include "suzume.h"

namespace suzume::cli {

namespace {

bool isKnownCommand(std::string_view value) {
  return value == "analyze" || value == "dict" || value == "test" || value == "version" || value == "help";
}

bool isValidMode(std::string_view value) {
  return value == "normal" || value == "search" || value == "split";
}

bool tryParseOutputFormat(std::string_view value, OutputFormat* output) {
  if (output == nullptr) {
    return false;
  }
  if (value == "morpheme") {
    *output = OutputFormat::Morpheme;
  } else if (value == "tags") {
    *output = OutputFormat::Tags;
  } else if (value == "json") {
    *output = OutputFormat::Json;
  } else if (value == "tsv") {
    *output = OutputFormat::Tsv;
  } else if (value == "chasen") {
    *output = OutputFormat::Chasen;
  } else {
    return false;
  }
  return true;
}

bool takeOptionValue(int argc, char* argv[], int* index, std::string_view option, std::string* value,
                     std::string* error) {
  if (*index + 1 >= argc || argv[*index + 1][0] == '-') {
    *error = "Missing value for " + std::string(option);
    return false;
  }
  *value = argv[++(*index)];
  return true;
}

bool parseTagPos(std::string_view value, uint8_t* filter) {
  uint8_t bit = 0;
  if (value == "noun") {
    bit = postprocess::kTagPosNoun;
  } else if (value == "verb") {
    bit = postprocess::kTagPosVerb;
  } else if (value == "adjective") {
    bit = postprocess::kTagPosAdjective;
  } else if (value == "adverb") {
    bit = postprocess::kTagPosAdverb;
  } else {
    return false;
  }
  *filter = static_cast<uint8_t>(*filter | bit);  // NOLINT(hicpp-signed-bitwise): bit flag operation
  return true;
}

}  // namespace

void printError(std::string_view message) {
  std::cerr << "error: " << message << "\n";
}

void printWarning(std::string_view message) {
  std::cerr << "warning: " << message << "\n";
}

void printInfo(std::string_view message) {
  std::cerr << "info: " << message << "\n";
}

bool parseSizeOption(std::string_view value, size_t* out) {
  if (out == nullptr || value.empty()) {
    return false;
  }
  for (char chr : value) {
    if (chr < '0' || chr > '9') {
      return false;
    }
  }

  try {
    size_t parsed_len = 0;
    unsigned long long parsed = std::stoull(std::string(value), &parsed_len, 10);
    if (parsed_len != value.size()) {
      return false;
    }
    // std::stoull already rejects values exceeding unsigned long long. Only when
    // size_t is narrower than unsigned long long (e.g. ILP32) can a parsed value
    // still overflow size_t; on LP64 the two share a width, so this guard is
    // compiled out rather than left as an always-false comparison.
    if constexpr (std::numeric_limits<size_t>::max() < std::numeric_limits<unsigned long long>::max()) {
      if (parsed > std::numeric_limits<size_t>::max()) {
        return false;
      }
    }
    *out = static_cast<size_t>(parsed);
    return true;
  } catch (...) {
    return false;
  }
}

std::string jsonEscape(std::string_view value) {
  std::ostringstream out;
  out << std::hex << std::setfill('0');

  size_t idx = 0;
  while (idx < value.size()) {
    unsigned char chr = static_cast<unsigned char>(value[idx]);

    // ASCII range: apply JSON escaping for control/special characters.
    if (chr < 0x80) {
      switch (chr) {
        case '"':
          out << "\\\"";
          break;
        case '\\':
          out << "\\\\";
          break;
        case '\b':
          out << "\\b";
          break;
        case '\f':
          out << "\\f";
          break;
        case '\n':
          out << "\\n";
          break;
        case '\r':
          out << "\\r";
          break;
        case '\t':
          out << "\\t";
          break;
        default:
          if (chr < 0x20) {
            out << "\\u" << std::setw(4) << static_cast<int>(chr);
          } else {
            out << static_cast<char>(chr);
          }
          break;
      }
      ++idx;
      continue;
    }

    // Multi-byte UTF-8: determine the expected sequence length from the lead
    // byte, then verify every continuation byte before passing it through.
    size_t seq_len = 0;
    bool scalar_prefix_valid = false;
    if (chr >= 0xC2 && chr <= 0xDF) {
      seq_len = 2;
      scalar_prefix_valid = true;
    } else if (chr >= 0xE0 && chr <= 0xEF) {
      seq_len = 3;
      scalar_prefix_valid = true;
    } else if (chr >= 0xF0 && chr <= 0xF4) {
      seq_len = 4;
      scalar_prefix_valid = true;
    }

    bool valid = scalar_prefix_valid && idx + seq_len <= value.size();
    if (valid) {
      for (size_t off = 1; off < seq_len; ++off) {
        if ((static_cast<unsigned char>(value[idx + off]) & 0xC0) != 0x80) {
          valid = false;
          break;
        }
      }
    }
    if (valid && seq_len == 3) {
      const auto second = static_cast<unsigned char>(value[idx + 1]);
      valid = !((chr == 0xE0 && second < 0xA0) || (chr == 0xED && second >= 0xA0));
    }
    if (valid && seq_len == 4) {
      const auto second = static_cast<unsigned char>(value[idx + 1]);
      valid = !((chr == 0xF0 && second < 0x90) || (chr == 0xF4 && second > 0x8F));
    }

    if (valid) {
      // Emit the validated multi-byte sequence unchanged.
      for (size_t off = 0; off < seq_len; ++off) {
        out << value[idx + off];
      }
      idx += seq_len;
    } else {
      // Replace a stray/invalid byte with the Unicode replacement character so
      // the output stays well-formed JSON.
      out << "\\ufffd";
      ++idx;
    }
  }

  return out.str();
}

void stripUtf8Bom(std::string* value) {
  if (value != nullptr && value->size() >= 3 && static_cast<unsigned char>((*value)[0]) == 0xEF &&
      static_cast<unsigned char>((*value)[1]) == 0xBB && static_cast<unsigned char>((*value)[2]) == 0xBF) {
    value->erase(0, 3);
  }
}

bool hasExtension(std::string_view path, std::string_view ext) {
  return path.size() >= ext.size() && path.substr(path.size() - ext.size()) == ext;
}

std::string swapOrAppendExtension(std::string_view path, std::string_view from_ext, std::string_view to_ext) {
  if (hasExtension(path, from_ext)) {
    return std::string(path.substr(0, path.size() - from_ext.size())) + std::string(to_ext);
  }
  return std::string(path) + std::string(to_ext);
}

std::string wildcardToRegex(std::string_view pattern) {
  std::string regex_str;
  regex_str.reserve(pattern.size() * 2);
  for (char chr : pattern) {
    if (chr == '*') {
      regex_str += ".*";
    } else if (chr == '?') {
      regex_str += ".";
    } else if (chr == '.' || chr == '^' || chr == '$' || chr == '+' || chr == '(' || chr == ')' || chr == '[' ||
               chr == ']' || chr == '|' || chr == '\\') {
      regex_str += '\\';
      regex_str += chr;
    } else {
      regex_str += chr;
    }
  }
  return regex_str;
}

bool isTerminal() {
#ifdef _WIN32
  return _isatty(_fileno(stdin)) != 0;
#else
  return isatty(STDIN_FILENO) != 0;
#endif
}

std::string getVersionString() {
  return Suzume::version();
}

void printVersion() {
  std::cout << "suzume-cli " << getVersionString() << "\n";
  std::cout << "Japanese morphological analyzer\n";
}

CommandArgs parseArgs(int argc, char* argv[]) {
  CommandArgs args;

  int idx = 1;
  bool positional_only = false;
  while (idx < argc) {
    std::string arg = argv[idx];

    // After a literal "--", every remaining argument is positional.
    if (positional_only) {
      if (args.command.empty()) {
        args.command = "analyze";
      }
      args.args.push_back(arg);
      ++idx;
      continue;
    }

    if (arg == "--") {
      if (args.command.empty()) {
        args.command = "analyze";
      }
      positional_only = true;
      ++idx;
      continue;
    }

    if (arg == "-h" || arg == "--help") {
      args.help = true;
      ++idx;
      continue;
    }

    if (arg == "-v" || arg == "--version") {
      args.version = true;
      ++idx;
      continue;
    }

    if (arg == "-V" || arg == "--verbose") {
      args.verbose = true;
      ++idx;
      continue;
    }

    if (arg == "-VV" || arg == "--very-verbose") {
      args.verbose = true;
      args.very_verbose = true;
      args.debug = true;
      ++idx;
      continue;
    }

    // Dict and test own their remaining option grammar. Keep their flags and
    // values intact, except for the common test dictionary option.
    if (args.command == "dict" || args.command == "test") {
      if (args.command == "test" && (arg == "-d" || arg == "--dict")) {
        std::string value;
        if (!takeOptionValue(argc, argv, &idx, arg, &value, &args.parse_error)) {
          return args;
        }
        args.dict_paths.push_back(std::move(value));
      } else if (args.command == "test" && arg.rfind("--dict=", 0) == 0) {
        std::string value = arg.substr(std::strlen("--dict="));
        if (value.empty()) {
          args.parse_error = "Missing value for --dict";
          return args;
        }
        args.dict_paths.push_back(std::move(value));
      } else {
        args.args.push_back(arg);
      }
      ++idx;
      continue;
    }

    if (arg[0] != '-') {
      if (args.command.empty() && isKnownCommand(arg)) {
        args.command = arg;
      } else {
        if (args.command.empty()) {
          args.command = "analyze";
        }
        args.args.push_back(arg);
      }
      ++idx;
      continue;
    }

    if (arg == "--debug") {
      args.debug = true;
      ++idx;
      continue;
    }

    if (arg == "-d" || arg == "--dict") {
      std::string value;
      if (!takeOptionValue(argc, argv, &idx, arg, &value, &args.parse_error)) {
        return args;
      }
      args.dict_paths.push_back(std::move(value));
      ++idx;
      continue;
    }
    if (arg.rfind("--dict=", 0) == 0) {
      std::string value = arg.substr(std::strlen("--dict="));
      if (value.empty()) {
        args.parse_error = "Missing value for --dict";
        return args;
      }
      args.dict_paths.push_back(std::move(value));
      ++idx;
      continue;
    }

    if (arg == "-m" || arg == "--mode") {
      std::string value;
      if (!takeOptionValue(argc, argv, &idx, arg, &value, &args.parse_error)) {
        return args;
      }
      if (!isValidMode(value)) {
        args.parse_error = "Invalid mode: " + value + " (expected normal, search, or split)";
        return args;
      }
      args.mode = std::move(value);
      ++idx;
      continue;
    }
    if (arg.rfind("--mode=", 0) == 0) {
      std::string value = arg.substr(std::strlen("--mode="));
      if (!isValidMode(value)) {
        args.parse_error = "Invalid mode: " + value + " (expected normal, search, or split)";
        return args;
      }
      args.mode = std::move(value);
      ++idx;
      continue;
    }

    if (arg == "-f" || arg == "--format") {
      std::string value;
      if (!takeOptionValue(argc, argv, &idx, arg, &value, &args.parse_error)) {
        return args;
      }
      if (!tryParseOutputFormat(value, &args.format)) {
        args.parse_error = "Invalid format: " + value + " (expected morpheme, tags, json, tsv, or chasen)";
        return args;
      }
      ++idx;
      continue;
    }
    if (arg.rfind("--format=", 0) == 0) {
      std::string value = arg.substr(std::strlen("--format="));
      if (!tryParseOutputFormat(value, &args.format)) {
        args.parse_error = "Invalid format: " + value + " (expected morpheme, tags, json, tsv, or chasen)";
        return args;
      }
      ++idx;
      continue;
    }

    if (arg == "--no-user-dict") {
      args.no_user_dict = true;
      ++idx;
      continue;
    }

    if (arg == "--no-core-dict") {
      args.no_core_dict = true;
      ++idx;
      continue;
    }

    if (arg == "--compare") {
      args.compare = true;
      ++idx;
      continue;
    }

    if (arg == "--normalize-vu") {
      args.normalize_vu = true;
      ++idx;
      continue;
    }

    if (arg == "--lowercase") {
      args.lowercase = true;
      ++idx;
      continue;
    }

    if (arg == "--preserve-symbols") {
      args.preserve_symbols = true;
      ++idx;
      continue;
    }

    if (arg == "--no-lemmatize") {
      args.no_lemmatize = true;
      ++idx;
      continue;
    }

    if (arg == "--merge-compounds") {
      args.merge_compounds = true;
      ++idx;
      continue;
    }

    if (arg == "--include-particles") {
      args.tag_include_particles = true;
      ++idx;
      continue;
    }

    if (arg == "--include-auxiliaries") {
      args.tag_include_auxiliaries = true;
      ++idx;
      continue;
    }

    if (arg == "--include-formal-nouns") {
      args.tag_include_formal_nouns = true;
      ++idx;
      continue;
    }

    if (arg == "--include-low-info") {
      args.tag_include_low_info = true;
      ++idx;
      continue;
    }

    if (arg == "--tag-keep-duplicates") {
      args.tag_keep_duplicates = true;
      ++idx;
      continue;
    }

    if (arg == "--tag-use-surface") {
      args.tag_use_surface = true;
      ++idx;
      continue;
    }

    if (arg == "--tag-pos") {
      std::string value;
      if (!takeOptionValue(argc, argv, &idx, arg, &value, &args.parse_error)) {
        return args;
      }
      if (!parseTagPos(value, &args.tag_pos_filter)) {
        args.parse_error = "Invalid --tag-pos value: " + value + " (expected noun, verb, adjective, or adverb)";
        return args;
      }
      ++idx;
      continue;
    }
    if (arg.rfind("--tag-pos=", 0) == 0) {
      std::string value = arg.substr(std::strlen("--tag-pos="));
      if (!parseTagPos(value, &args.tag_pos_filter)) {
        args.parse_error = "Invalid --tag-pos value: " + value + " (expected noun, verb, adjective, or adverb)";
        return args;
      }
      ++idx;
      continue;
    }

    if (arg == "--tag-exclude-basic") {
      args.tag_exclude_basic = true;
      ++idx;
      continue;
    }

    if (arg == "--tag-min-length") {
      std::string value;
      if (!takeOptionValue(argc, argv, &idx, arg, &value, &args.parse_error)) {
        return args;
      }
      if (!parseSizeOption(value, &args.tag_min_length)) {
        args.parse_error = "Invalid --tag-min-length value: " + value;
        return args;
      }
      ++idx;
      continue;
    }
    if (arg.rfind("--tag-min-length=", 0) == 0) {
      std::string value = arg.substr(std::strlen("--tag-min-length="));
      if (!parseSizeOption(value, &args.tag_min_length)) {
        args.parse_error = "Invalid --tag-min-length value: " + value;
        return args;
      }
      ++idx;
      continue;
    }

    if (arg == "--tag-max-tags") {
      std::string value;
      if (!takeOptionValue(argc, argv, &idx, arg, &value, &args.parse_error)) {
        return args;
      }
      if (!parseSizeOption(value, &args.tag_max_tags)) {
        args.parse_error = "Invalid --tag-max-tags value: " + value;
        return args;
      }
      ++idx;
      continue;
    }
    if (arg.rfind("--tag-max-tags=", 0) == 0) {
      std::string value = arg.substr(std::strlen("--tag-max-tags="));
      if (!parseSizeOption(value, &args.tag_max_tags)) {
        args.parse_error = "Invalid --tag-max-tags value: " + value;
        return args;
      }
      ++idx;
      continue;
    }

    if (args.command.empty()) {
      args.command = "analyze";
    }
    args.parse_error = "Unknown analysis option: " + arg;
    return args;
  }

  // Default command is analyze
  if (args.command.empty() && !args.help) {
    args.command = "analyze";
  }

  return args;
}

void printHelp() {
  std::cout << R"(suzume-cli - Japanese morphological analyzer

Usage:
  suzume-cli [command] [options] [arguments]

Commands:
  analyze     Morphological analysis (default)
  dict        Dictionary management
  test        Verification and testing
  version     Show version information
  help        Show this help

Global Options:
  -d, --dict PATH        Load user dictionary (can specify multiple)
  -m, --mode MODE        Analysis mode: normal, search, split
  -f, --format FMT       Output format: morpheme, tags, json, tsv, chasen
  -V, --verbose          Verbose output
  -VV, --very-verbose    Very verbose output (includes lattice dump)
  --no-user-dict         Disable user dictionary
  --no-core-dict         Disable auto-loaded core.dic
  --compare              Compare with/without user dictionary
  --normalize-vu         Normalize ヴ to ビ etc. (default: preserve)
  --lowercase            Convert ASCII to lowercase (default: preserve)
  --preserve-symbols     Keep symbols/emoji in output (default: remove)
  --no-lemmatize         Keep surface forms as lemmas
  --merge-compounds      Merge consecutive noun compounds
  --include-particles    Include particles in tag output
  --include-auxiliaries  Include auxiliaries in tag output
  --include-formal-nouns Include formal nouns in tag output
  --include-low-info     Include low-information words in tag output
  --tag-keep-duplicates  Keep duplicate tags
  --tag-use-surface      Use surface instead of lemma for tags
  --tag-pos POS          Include noun, verb, adjective, or adverb (repeatable)
  --tag-exclude-basic    Exclude hiragana-only basic words
  --tag-min-length N     Minimum tag length (default: 2)
  --tag-max-tags N       Maximum tags (default: 0, unlimited)
  -h, --help             Show help
  -v, --version          Show version

Examples:
  suzume-cli "text"                  Analyze text
  suzume-cli analyze -f json "text"  Analyze with JSON output
  suzume-cli dict compile user.tsv   Compile dictionary
  suzume-cli dict -i user.tsv        Interactive dictionary editor

Use 'suzume-cli [command] --help' for command-specific help.
)";
}

void printAnalyzeHelp() {
  std::cout << R"(suzume-cli analyze - Morphological analysis

Usage:
  suzume-cli analyze [options] [text]
  suzume-cli [options] [text]         (analyze is default)

Options:
  -d, --dict PATH        Load user dictionary (can specify multiple)
  -m, --mode MODE        Analysis mode: normal, search, split
  -f, --format FMT       Output format: morpheme, tags, json, tsv, chasen
  -V, --verbose          Verbose output
  --no-user-dict         Disable user dictionary
  --no-core-dict         Disable auto-loaded core.dic
  --compare              Compare with/without user dictionary
  --normalize-vu         Normalize ヴ to ビ etc. (default: preserve)
  --lowercase            Convert ASCII to lowercase (default: preserve)
  --preserve-symbols     Keep symbols/emoji in output (default: remove)
  --no-lemmatize         Keep surface forms as lemmas
  --merge-compounds      Merge consecutive noun compounds
  --include-particles    Include particles in tag output
  --include-auxiliaries  Include auxiliaries in tag output
  --include-formal-nouns Include formal nouns in tag output
  --include-low-info     Include low-information words in tag output
  --tag-keep-duplicates  Keep duplicate tags
  --tag-use-surface      Use surface instead of lemma for tags
  --tag-pos POS          Include noun, verb, adjective, or adverb (repeatable)
  --tag-exclude-basic    Exclude hiragana-only basic words
  --tag-min-length N     Minimum tag length (default: 2)
  --tag-max-tags N       Maximum tags (default: 0, unlimited)
  -h, --help             Show this help

Output Formats:
  morpheme               surface TAB pos TAB lemma TAB start TAB end (default)
  tags                   tag TAB pos, one per line
  json                   JSON format
  tsv                    TSV with all fields (surface, pos, lemma, start, end)
  chasen                 ChaSen-like format (Japanese POS, conjugation info)

Examples:
  suzume-cli "text"
  suzume-cli analyze "text"
  suzume-cli analyze -d user.dic "text"
  suzume-cli analyze -f json "text"
  suzume-cli analyze -f chasen "text"
  suzume-cli analyze --compare -d user.dic "text"
  suzume-cli analyze --normalize-vu "ヴァイオリン"
  echo "text" | suzume-cli analyze
)";
}

void printDictHelp() {
  std::cout << R"(suzume-cli dict - Dictionary management

Usage:
  suzume-cli dict [subcommand] [options] [arguments]

Subcommands:
  list <file> [--pos=POS] [--pattern=PATTERN] [--limit=N]
                         List entries in TSV or binary dictionary
  search <file> <pattern>
                         Search entries by pattern in file
  lookup <word>          Look up word in built-in L1 and source L2 TSV files
  new <file.tsv>         Create new dictionary file
  info [file]            Show dictionary information
  validate [file]        Validate dictionary
  compile <in.tsv> [out.dic]
                         Compile to binary format (default: in.dic)
  decompile <in.dic> [out.tsv]
                         Decompile binary to TSV (default: in.tsv)
  -i, --interactive [file.tsv]
                         Interactive mode

POS Values:
  NOUN, PROPN, VERB, ADJECTIVE, ADVERB, PARTICLE,
  AUXILIARY, SYMBOL, OTHER

Conjugation Types (for VERB/ADJECTIVE):
  ICHIDAN, GODAN_KA, GODAN_GA, GODAN_SA, GODAN_TA,
  GODAN_NA, GODAN_BA, GODAN_MA, GODAN_RA, GODAN_WA,
  SURU, KURU, I_ADJ, NA_ADJ

Examples:
  suzume-cli dict lookup すぎる
  suzume-cli dict new user.tsv
  suzume-cli dict list user.tsv --pos=NOUN --limit=10
  suzume-cli dict compile user.tsv
  suzume-cli dict -i user.tsv
)";
}

void printTestHelp() {
  std::cout << R"(suzume-cli test - Verification and testing

Usage:
  suzume-cli test [subcommand] [options] [arguments]

Subcommands:
  <text> --expect <tags>
                         Test single input with expected output
  -f, --file <tests.tsv>
                         Run tests from file
  benchmark [--iterations=N] [--samples=N] [--warmup=N] [-f <corpus.txt>]
                         Report median initialize, first-analysis, and steady-analysis timing
Options:
  -d, --dict PATH        Load user dictionary
  -h, --help             Show this help

Test File Format (TSV):
  input<TAB>expected_tags (comma-separated)

Examples:
  suzume-cli test "text" --expect "tag1,tag2"
  suzume-cli test -f tests.tsv
  suzume-cli test -f tests.tsv -d user.dic
  suzume-cli test benchmark --iterations=1000 --samples=5 --warmup=1
)";
}

}  // namespace suzume::cli
