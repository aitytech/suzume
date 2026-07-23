#include "cmd_analyze.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <vector>

#include "grammar/conjugation.h"
#include "normalize/utf8.h"
#include "suzume.h"

namespace suzume::cli {

namespace {

void outputMorphemes(const std::vector<core::Morpheme>& morphemes) {
  for (const auto& morpheme : morphemes) {
    std::cout << morpheme.surface << "\t" << core::posToString(morpheme.pos) << "\t" << morpheme.lemma << "\t"
              << morpheme.start_pos << "\t" << morpheme.end_pos << "\n";
  }
}

void outputTags(const std::vector<postprocess::TagEntry>& tags) {
  for (const auto& tag : tags) {
    std::cout << tag.tag << "\t" << core::posToString(tag.pos) << "\n";
  }
}

void outputJson(const std::string& input, const std::vector<core::Morpheme>& morphemes) {
  std::cout << "{\n";
  std::cout << R"(  "input": ")" << jsonEscape(input) << "\",\n";
  std::cout << "  \"morphemes\": [\n";

  for (size_t idx = 0; idx < morphemes.size(); ++idx) {
    const auto& mor = morphemes[idx];
    std::cout << "    {";
    std::cout << R"("surface": ")" << jsonEscape(mor.surface) << "\", ";
    std::cout << R"("pos": ")" << core::posToString(mor.pos) << "\", ";
    std::cout << R"("lemma": ")" << jsonEscape(mor.lemma) << "\", ";
    std::cout << R"("start": )" << mor.start_pos << ", ";
    std::cout << R"("end": )" << mor.end_pos << ", ";
    std::cout << R"("extended_pos": ")" << core::extendedPosToString(mor.extended_pos) << "\", ";
    std::cout << R"("is_user_dict": )" << (mor.features.is_user_dict ? "true" : "false") << ", ";
    std::cout << R"("is_formal_noun": )" << (mor.features.is_formal_noun ? "true" : "false") << ", ";
    std::cout << R"("is_low_info": )" << (mor.features.is_low_info ? "true" : "false") << ", ";
    std::cout << R"("is_unknown": )" << (mor.is_unknown ? "true" : "false") << ", ";
    std::cout << R"("is_from_dictionary": )" << (mor.is_from_dictionary ? "true" : "false") << ", ";
    std::cout << R"("score": )" << std::setprecision(std::numeric_limits<float>::max_digits10) << mor.features.score;
    std::cout << "}";
    if (idx + 1 < morphemes.size()) {
      std::cout << ",";
    }
    std::cout << "\n";
  }

  std::cout << "  ]\n";
  std::cout << "}\n";
}

void outputChasen(const std::vector<core::Morpheme>& morphemes) {
  for (const auto& mor : morphemes) {
    // Surface form
    std::cout << mor.surface << "\t";

    // Reading column placeholder (Suzume does not generate readings)
    std::cout << "*\t";

    // Lemma (base form)
    std::cout << mor.getLemma() << "\t";

    // Part of speech (Japanese)
    std::cout << core::posToJapanese(mor.pos) << "\t";

    // Conjugation type and form (for verbs and adjectives)
    if (mor.pos == core::PartOfSpeech::Verb || mor.pos == core::PartOfSpeech::Adjective) {
      auto verb_type = grammar::conjTypeToVerbType(mor.conj_type);
      std::cout << grammar::verbTypeToJapanese(verb_type) << "\t";
      std::cout << grammar::conjFormToJapanese(mor.conj_form);
    } else {
      std::cout << "*\t*";
    }
    std::cout << "\n";
  }
  std::cout << "EOS\n";
}

core::AnalysisMode parseMode(const std::string& mode_str) {
  if (mode_str == "search") {
    return core::AnalysisMode::Search;
  }
  if (mode_str == "split") {
    return core::AnalysisMode::Split;
  }
  return core::AnalysisMode::Normal;
}

bool isBinaryDictionaryPath(std::string_view path) {
  constexpr std::string_view kExtension = ".dic";
  if (path.size() < kExtension.size()) {
    return false;
  }
  const std::string_view suffix = path.substr(path.size() - kExtension.size());
  for (size_t idx = 0; idx < kExtension.size(); ++idx) {
    const auto actual = static_cast<unsigned char>(suffix[idx]);
    if (static_cast<char>(std::tolower(actual)) != kExtension[idx]) {
      return false;
    }
  }
  return true;
}

core::Expected<size_t, core::Error> loadBinaryDictionaryFile(Suzume* analyzer, const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return core::makeUnexpected(
        core::Error(core::ErrorCode::FileNotFound, "Failed to open binary dictionary file: " + path));
  }

  std::vector<char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (file.bad()) {
    return core::makeUnexpected(
        core::Error(core::ErrorCode::DictionaryLoadFailed, "Failed to read binary dictionary file: " + path));
  }
  return analyzer->loadBinaryDictionaryResult(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

}  // namespace

int cmdAnalyze(const CommandArgs& args) {
  if (args.help) {
    printAnalyzeHelp();
    return 0;
  }

  // Get input text
  std::string text;
  if (!args.args.empty()) {
    // Join all positional arguments as text
    std::ostringstream oss;
    for (size_t idx = 0; idx < args.args.size(); ++idx) {
      if (idx > 0) {
        oss << " ";
      }
      oss << args.args[idx];
    }
    text = oss.str();
  } else if (!isTerminal()) {
    // Read stdin byte-for-byte so embedded newlines are preserved.
    std::ostringstream oss;
    oss << std::cin.rdbuf();
    text = oss.str();
  }
  stripUtf8Bom(&text);
  // Match one std::getline-style EOF boundary: remove one final LF (and its CR
  // in CRLF input), while preserving all preceding newlines.
  if (text.empty() == false && text.back() == '\n') {
    text.pop_back();
    if (text.empty() == false && text.back() == '\r') {
      text.pop_back();
    }
  }

  // Validate once before analysis or any format-specific output. This keeps
  // malformed scalar sequences away from both the tokenizer and JSON output.
  if (!normalize::isValidUtf8(text)) {
    printError("Invalid UTF-8 input");
    return 1;
  }

  if (text.empty()) {
    printError("No input text provided");
    printAnalyzeHelp();
    return 1;
  }

  if (args.debug) {
    // Debug level is cached on first use, which can happen during dictionary loading.
#ifdef _WIN32
    _putenv_s("SUZUME_DEBUG", "1");
#else
    setenv("SUZUME_DEBUG", "1", 1);
#endif
  }

  // Create analyzer
  SuzumeOptions options;
  options.mode = parseMode(args.mode);
  // Default is preserve (true), flags invert to normalize
  options.normalize_options.preserve_vu = !args.normalize_vu;
  options.normalize_options.preserve_case = !args.lowercase;
  // Default is remove symbols (true), flag inverts to preserve
  options.remove_symbols = !args.preserve_symbols;
  options.skip_user_dictionary = args.no_user_dict;
  options.skip_core_dictionary = args.no_core_dict;
  options.lemmatize = !args.no_lemmatize;
  options.merge_compounds = args.merge_compounds;
  options.report_scorer_config = args.verbose;

  Suzume analyzer(options);
  for (const auto& warning : analyzer.dictionaryWarnings()) {
    printWarning(warning);
  }

  size_t binary_dictionary_count = 0;
  for (const auto& dict_path : args.dict_paths) {
    if (isBinaryDictionaryPath(dict_path)) {
      ++binary_dictionary_count;
    }
  }
  if (binary_dictionary_count > 1) {
    printError("Only one binary .dic dictionary may be loaded; additional binary dictionaries would replace it");
    return 1;
  }

  // Text dictionaries are additive. The core has one replaceable binary user
  // dictionary slot, so the count check above prevents silent replacement.
  for (const auto& dict_path : args.dict_paths) {
    auto load_result = isBinaryDictionaryPath(dict_path) ? loadBinaryDictionaryFile(&analyzer, dict_path)
                                                         : analyzer.loadUserDictionaryResult(dict_path);
    if (!load_result.hasValue()) {
      printError("Failed to load dictionary: " + dict_path + ": " + load_result.error().message);
      return 1;
    } else if (args.verbose) {
      printInfo("Loaded dictionary: " + dict_path);
    }
  }

  // Compare mode
  if (args.compare) {
    // Analyze without user dictionary
    SuzumeOptions base_options = options;
    base_options.skip_user_dictionary = true;
    Suzume base_analyzer(base_options);
    for (const auto& warning : base_analyzer.dictionaryWarnings()) {
      printWarning(warning);
    }
    auto base_morphemes = base_analyzer.analyze(text);

    std::cout << "[Without user dictionary]\n";
    outputMorphemes(base_morphemes);
    std::cout << "\n";

    // Analyze with user dictionary
    auto morphemes = analyzer.analyze(text);

    std::cout << "[With user dictionary]\n";
    outputMorphemes(morphemes);
    std::cout << "\n";

    // Show diff (simplified)
    std::cout << "[Diff]\n";
    if (base_morphemes.size() != morphemes.size()) {
      std::cout << "Morpheme count: " << base_morphemes.size() << " -> " << morphemes.size() << "\n";
    } else {
      std::cout << "No structural difference\n";
    }

    return 0;
  }

  // Debug mode - show lattice candidates
  if (args.debug) {
    std::cout << "=== Debug Mode ===\n";
    std::cout << "Input: \"" << text << "\"\n\n";

    core::Lattice lattice(0);
    auto morphemes = analyzer.analyzeDebug(text, &lattice);

    std::cout << "\n=== Lattice Candidates ===\n";
    for (size_t pos = 0; pos < lattice.textLength(); ++pos) {
      const auto& edges = lattice.edgesAt(pos);
      if (!edges.empty()) {
        std::cout << "Position " << pos << ":\n";
        for (const auto& edge : edges) {
          std::cout << "  [" << edge.start << "-" << edge.end << "] " << edge.surface << " ("
                    << core::posToString(edge.pos) << ") cost=" << edge.cost;
          if (!edge.lemma.empty()) {
            std::cout << " lemma=" << edge.lemma;
          }
          // Show source info
          if (edge.fromDictionary()) {
            std::cout << " [dict";
            if (edge.fromUserDict()) {
              std::cout << ":user";
            }
            std::cout << "]";
          }
          if (edge.isUnknown()) {
            std::cout << " [unk]";
          }
          std::cout << " id=" << edge.id;
          std::cout << "\n";
        }
      }
    }

    std::cout << "\n=== Result ===\n";
    outputMorphemes(morphemes);
    return 0;
  }

  // Normal analysis
  switch (args.format) {
    case OutputFormat::Morpheme:
    case OutputFormat::Tsv: {
      auto morphemes = analyzer.analyze(text);
      outputMorphemes(morphemes);
      break;
    }
    case OutputFormat::Tags: {
      postprocess::TagGeneratorOptions tag_options;
      tag_options.exclude_particles = !args.tag_include_particles;
      tag_options.exclude_auxiliaries = !args.tag_include_auxiliaries;
      tag_options.exclude_formal_nouns = !args.tag_include_formal_nouns;
      tag_options.exclude_low_info = !args.tag_include_low_info;
      tag_options.remove_duplicates = !args.tag_keep_duplicates;
      tag_options.use_lemma = !args.tag_use_surface;
      tag_options.min_tag_length = args.tag_min_length;
      tag_options.max_tags = args.tag_max_tags;
      tag_options.pos_filter = args.tag_pos_filter;
      tag_options.exclude_basic = args.tag_exclude_basic;
      auto tags = analyzer.generateTags(text, tag_options);
      outputTags(tags);
      break;
    }
    case OutputFormat::Json: {
      auto morphemes = analyzer.analyze(text);
      outputJson(text, morphemes);
      break;
    }
    case OutputFormat::Chasen: {
      auto morphemes = analyzer.analyze(text);
      outputChasen(morphemes);
      break;
    }
  }

  return 0;
}

}  // namespace suzume::cli
