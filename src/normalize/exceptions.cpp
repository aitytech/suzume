#include "normalize/exceptions.h"

namespace suzume::normalize {

// Particles that should not be treated as verb endings when generating
// verb candidates from kanji + hiragana patterns.
const std::unordered_set<std::string_view> kParticleStrings = {
    // Case particles (格助詞)
    "が",
    "を",
    "に",
    "で",
    "と",
    "へ",
    "の",
    // Binding particles (係助詞)
    "は",
    "も",
    // Other particles (副助詞・接続助詞)
    "や",
    "か",
    // Compound particles (複合助詞)
    "から",
    "まで",
    "より",
    "ほど",
};

// Copula and auxiliary patterns that should not be treated as verb endings.
const std::unordered_set<std::string_view> kCopulaStrings = {
    // Basic copula (基本形)
    "だ",
    "です",
    // Past forms (過去形)
    "だった",
    "でした",
    // Partial forms (途中形)
    "でし",
    // Formal copula (文語形)
    "である",
};

// Formal nouns (形式名詞) with abstract grammatical functions. These remain
// recognizable even when a dictionary lookup did not flag the candidate.
const std::unordered_set<std::string_view> kFormalNounStrings = {
    "所", "物", "事", "時", "方", "為",
};

// Case and binding particles used for character-level candidate filtering.
const std::unordered_set<char32_t> kParticleCodepoints = {
    // Case particles (格助詞)
    U'が',
    U'を',
    U'に',
    U'で',
    U'と',
    U'へ',
    U'の',
    // Binding particles (係助詞)
    U'は',
    U'も',
    // Other particles (副助詞)
    U'や',
    U'か',
};

}  // namespace suzume::normalize
