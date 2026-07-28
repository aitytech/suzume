// Minimal JSON loader for test case files.

#pragma once

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "test_case.h"

namespace suzume::test {

// Simple JSON parser for test cases
class JsonLoader {
 public:
  // Load test suite from file
  static TestSuite loadFromFile(const std::string& path);

  // Load test suite from string
  static TestSuite loadFromString(const std::string& json);

 private:
  explicit JsonLoader(const std::string& json) : json_(json), pos_(0) {}

  TestSuite parse();
  TestCase parseTestCase();
  ExpectedMorpheme parseMorpheme();
  AcceptedDiff parseAcceptedDiff();
  std::vector<std::string> parseStringArray();
  std::vector<ExpectedMorpheme> parseMorphemeArray();
  std::string parseString();
  void skipWhitespace();
  char peek();
  char consume();
  void expect(char c);
  void expectKey(const std::string& key);
  bool tryKey(const std::string& key);

  std::string json_;
  size_t pos_;
};

// Inline implementations for header-only usage

inline TestSuite JsonLoader::loadFromFile(const std::string& path) {
  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error("Cannot open file: " + path);
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return loadFromString(buffer.str());
}

inline TestSuite JsonLoader::loadFromString(const std::string& json) {
  JsonLoader loader(json);
  return loader.parse();
}

inline TestSuite JsonLoader::parse() {
  TestSuite suite;
  skipWhitespace();
  expect('{');

  while (peek() != '}') {
    skipWhitespace();
    if (tryKey("version")) {
      expect(':');
      suite.version = parseString();
    } else if (tryKey("cases")) {
      expect(':');
      skipWhitespace();
      expect('[');
      skipWhitespace();
      while (peek() != ']') {
        suite.cases.push_back(parseTestCase());
        skipWhitespace();
        if (peek() == ',')
          consume();
        skipWhitespace();
      }
      expect(']');
    } else {
      // Skip unknown key
      parseString();
      expect(':');
      // Skip value (simplified: just skip until comma or closing brace)
      int depth = 0;
      while (pos_ < json_.size()) {
        char c = peek();
        if (c == '{' || c == '[')
          depth++;
        else if (c == '}' || c == ']') {
          if (depth == 0)
            break;
          depth--;
        } else if (c == ',' && depth == 0)
          break;
        else if (c == '"')
          parseString();
        else
          consume();
      }
    }
    skipWhitespace();
    if (peek() == ',')
      consume();
    skipWhitespace();
  }
  expect('}');
  return suite;
}

inline TestCase JsonLoader::parseTestCase() {
  TestCase tc;
  skipWhitespace();
  expect('{');

  while (peek() != '}') {
    skipWhitespace();
    if (tryKey("id")) {
      expect(':');
      tc.id = parseString();
    } else if (tryKey("input")) {
      expect(':');
      tc.input = parseString();
    } else if (tryKey("description")) {
      expect(':');
      tc.description = parseString();
    } else if (tryKey("tags")) {
      expect(':');
      tc.tags = parseStringArray();
    } else if (tryKey("expected")) {
      expect(':');
      tc.expected = parseMorphemeArray();
      // Banned oracle overrides. Parsed rather than ignored so the suite can reject
      // them; the unknown-key branch below would swallow them silently. See TestCase.
    } else if (tryKey("suzume_expected")) {
      expect(':');
      tc.suzume_expected = parseMorphemeArray();
    } else if (tryKey("accepted_diff")) {
      expect(':');
      tc.accepted_diff = parseAcceptedDiff();
    } else {
      // Skip unknown key-value pair
      parseString();
      expect(':');
      if (peek() == '"')
        parseString();
      else if (peek() == '[') {
        int depth = 1;
        consume();
        while (depth > 0 && pos_ < json_.size()) {
          if (peek() == '[')
            depth++;
          else if (peek() == ']')
            depth--;
          consume();
        }
      } else if (peek() == '{') {
        int depth = 1;
        consume();
        while (depth > 0 && pos_ < json_.size()) {
          if (peek() == '{')
            depth++;
          else if (peek() == '}')
            depth--;
          consume();
        }
      }
    }
    skipWhitespace();
    if (peek() == ',')
      consume();
    skipWhitespace();
  }
  expect('}');
  if (tc.id.empty()) {
    throw std::runtime_error("Test case id must not be empty");
  }
  if (tc.input.empty()) {
    throw std::runtime_error("Test case input must not be empty: " + tc.id);
  }
  if (tc.expected.empty()) {
    throw std::runtime_error("Test case expected must not be empty: " + tc.id);
  }
  for (const auto& expected : tc.expected) {
    if (expected.surface.empty()) {
      throw std::runtime_error("Expected surface must not be empty: " + tc.id);
    }
    try {
      static_cast<void>(expected.posEnum());
    } catch (const std::invalid_argument& error) {
      throw std::runtime_error("Invalid expected POS in " + tc.id + ": " + error.what());
    }
  }
  return tc;
}

inline ExpectedMorpheme JsonLoader::parseMorpheme() {
  ExpectedMorpheme mor;
  skipWhitespace();
  expect('{');

  while (peek() != '}') {
    skipWhitespace();
    if (tryKey("surface")) {
      expect(':');
      mor.surface = parseString();
    } else if (tryKey("pos")) {
      expect(':');
      mor.pos = parseString();
    } else if (tryKey("lemma")) {
      expect(':');
      mor.lemma = parseString();
    } else {
      // Skip unknown key-value
      parseString();
      expect(':');
      parseString();
    }
    skipWhitespace();
    if (peek() == ',')
      consume();
    skipWhitespace();
  }
  expect('}');
  return mor;
}

inline std::vector<ExpectedMorpheme> JsonLoader::parseMorphemeArray() {
  std::vector<ExpectedMorpheme> result;
  skipWhitespace();
  expect('[');
  skipWhitespace();
  while (peek() != ']') {
    result.push_back(parseMorpheme());
    skipWhitespace();
    if (peek() == ',')
      consume();
    skipWhitespace();
  }
  expect(']');
  return result;
}

inline AcceptedDiff JsonLoader::parseAcceptedDiff() {
  AcceptedDiff diff;
  skipWhitespace();
  expect('{');

  while (peek() != '}') {
    skipWhitespace();
    if (tryKey("reason")) {
      expect(':');
      diff.reason = parseString();
    } else if (tryKey("category")) {
      expect(':');
      diff.category = parseString();
    } else {
      // Skip unknown key-value
      parseString();
      expect(':');
      parseString();
    }
    skipWhitespace();
    if (peek() == ',')
      consume();
    skipWhitespace();
  }
  expect('}');
  return diff;
}

inline std::vector<std::string> JsonLoader::parseStringArray() {
  std::vector<std::string> result;
  skipWhitespace();
  expect('[');
  skipWhitespace();
  while (peek() != ']') {
    result.push_back(parseString());
    skipWhitespace();
    if (peek() == ',')
      consume();
    skipWhitespace();
  }
  expect(']');
  return result;
}

inline std::string JsonLoader::parseString() {
  skipWhitespace();
  expect('"');
  std::string result;
  while (pos_ < json_.size() && json_[pos_] != '"') {
    if (json_[pos_] == '\\' && pos_ + 1 < json_.size()) {
      pos_++;
      switch (json_[pos_]) {
        case 'n':
          result += '\n';
          break;
        case 't':
          result += '\t';
          break;
        case 'r':
          result += '\r';
          break;
        case '"':
          result += '"';
          break;
        case '\\':
          result += '\\';
          break;
        case 'u': {
          const auto parse_hex_code_unit = [this](size_t start) {
            if (start + 4 > json_.size()) {
              throw std::runtime_error("Incomplete Unicode escape");
            }
            uint32_t value = 0;
            for (size_t offset = 0; offset < 4; ++offset) {
              const char digit = json_[start + offset];
              value <<= 4;
              if (digit >= '0' && digit <= '9') {
                value += static_cast<uint32_t>(digit - '0');
              } else if (digit >= 'a' && digit <= 'f') {
                value += static_cast<uint32_t>(digit - 'a' + 10);
              } else if (digit >= 'A' && digit <= 'F') {
                value += static_cast<uint32_t>(digit - 'A' + 10);
              } else {
                throw std::runtime_error("Invalid Unicode escape");
              }
            }
            return value;
          };
          uint32_t codepoint = parse_hex_code_unit(pos_ + 1);
          if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            if (pos_ + 10 >= json_.size() || json_[pos_ + 5] != '\\' || json_[pos_ + 6] != 'u') {
              throw std::runtime_error("High surrogate without low surrogate");
            }
            const uint32_t low_surrogate = parse_hex_code_unit(pos_ + 7);
            if (low_surrogate < 0xDC00 || low_surrogate > 0xDFFF) {
              throw std::runtime_error("High surrogate followed by invalid low surrogate");
            }
            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low_surrogate - 0xDC00);
            pos_ += 10;
          } else {
            if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
              throw std::runtime_error("Low surrogate without high surrogate");
            }
            pos_ += 4;
          }
          if (codepoint < 0x80) {
            result += static_cast<char>(codepoint);
          } else if (codepoint < 0x800) {
            result += static_cast<char>(0xC0 | (codepoint >> 6));
            result += static_cast<char>(0x80 | (codepoint & 0x3F));
          } else if (codepoint < 0x10000) {
            result += static_cast<char>(0xE0 | (codepoint >> 12));
            result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (codepoint & 0x3F));
          } else {
            result += static_cast<char>(0xF0 | (codepoint >> 18));
            result += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (codepoint & 0x3F));
          }
          break;
        }
        default:
          result += json_[pos_];
          break;
      }
    } else {
      result += json_[pos_];
    }
    pos_++;
  }
  expect('"');
  return result;
}

inline void JsonLoader::skipWhitespace() {
  while (pos_ < json_.size() &&
         (json_[pos_] == ' ' || json_[pos_] == '\t' || json_[pos_] == '\n' || json_[pos_] == '\r')) {
    pos_++;
  }
}

inline char JsonLoader::peek() {
  if (pos_ >= json_.size()) {
    throw std::runtime_error("Unexpected end of JSON");
  }
  return json_[pos_];
}

inline char JsonLoader::consume() {
  return json_[pos_++];
}

inline void JsonLoader::expect(char c) {
  skipWhitespace();
  if (peek() != c) {
    throw std::runtime_error(std::string("Expected '") + c + "' but got '" + peek() + "' at position " +
                             std::to_string(pos_));
  }
  consume();
}

inline void JsonLoader::expectKey(const std::string& key) {
  if (!tryKey(key)) {
    throw std::runtime_error("Expected key: " + key);
  }
}

inline bool JsonLoader::tryKey(const std::string& key) {
  size_t saved_pos = pos_;
  skipWhitespace();
  if (peek() != '"') {
    pos_ = saved_pos;
    return false;
  }
  std::string parsed = parseString();
  if (parsed != key) {
    pos_ = saved_pos;
    return false;
  }
  return true;
}

}  // namespace suzume::test
