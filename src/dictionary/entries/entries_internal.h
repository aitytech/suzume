#ifndef SUZUME_DICTIONARY_ENTRIES_ENTRIES_INTERNAL_H_
#define SUZUME_DICTIONARY_ENTRIES_ENTRIES_INTERNAL_H_

#include <vector>

#include "core/types.h"
#include "dictionary/dictionary.h"

namespace suzume::dictionary::entries {

using POS = core::PartOfSpeech;
using EPOS = core::ExtendedPOS;

// =============================================================================
// Helper functions for concise DictionaryEntry construction (v0.8 simplified)
//
// Design: No cost parameter - cost is derived from ExtendedPOS via getCategoryCost()
// =============================================================================

// Particle helper: creates PARTICLE entry
// Usage: particle("が", EPOS::ParticleCase)
inline DictionaryEntry particle(const char* s, EPOS epos) {
  return {s, POS::Particle, epos, ""};
}

// Auxiliary helper: creates AUXILIARY entry
// Usage: aux("た", "た", EPOS::AuxTenseTa)
inline DictionaryEntry aux(const char* s, const char* lemma, EPOS epos) {
  return {s, POS::Auxiliary, epos, lemma};
}

// Verb helper: creates VERB entry
// Usage: verb("し", "する", EPOS::VerbRenyokei)
inline DictionaryEntry verb(const char* s, const char* lemma, EPOS epos) {
  return {s, POS::Verb, epos, lemma};
}

// Adjective helper: creates ADJECTIVE entry
// Usage: adj("美しい", "", EPOS::AdjBasic)
inline DictionaryEntry adj(const char* s, const char* lemma, EPOS epos) {
  return {s, POS::Adjective, epos, lemma};
}

// Na-adjective helper: creates NA_ADJECTIVE entry
// Usage: na_adj("静か", "")
inline DictionaryEntry na_adj(const char* s, const char* lemma = "") {
  return {s, POS::Adjective, EPOS::AdjNaAdj, lemma};
}

// I-adjective helper: creates I_ADJECTIVE entry
// Usage: i_adj("寒い", "")
inline DictionaryEntry i_adj(const char* s, const char* lemma = "") {
  return {s, POS::Adjective, EPOS::AdjBasic, lemma};
}

// Determiner helper: creates DETERMINER entry
// Usage: det("この")
inline DictionaryEntry det(const char* s, const char* lemma = "") {
  return {s, POS::Determiner, EPOS::Determiner, lemma};
}

// Noun helper: creates NOUN entry
// Usage: noun("机"), noun("こと", "", EPOS::NounFormal)
inline DictionaryEntry noun(const char* s, const char* lemma = "", EPOS epos = EPOS::Noun) {
  return {s, POS::Noun, epos, lemma};
}

// Formal noun helper: creates FORMAL_NOUN entry (形式名詞)
// Usage: formal_noun("こと")
inline DictionaryEntry formal_noun(const char* s, const char* lemma = "") {
  return {s, POS::Noun, EPOS::NounFormal, lemma};
}

// Time noun helper: creates TIME_NOUN entry (時間名詞)
// Usage: time_noun("今日")
inline DictionaryEntry time_noun(const char* s, const char* lemma = "") {
  return {s, POS::Noun, EPOS::Noun, lemma};
}

// Conjunction helper: creates CONJUNCTION entry
// Usage: conj("しかし")
inline DictionaryEntry conj(const char* s, const char* lemma = "") {
  return {s, POS::Conjunction, EPOS::Conjunction, lemma};
}

// Adverb helper: creates ADVERB entry
// Usage: adv("とても")
inline DictionaryEntry adv(const char* s, const char* lemma = "") {
  return {s, POS::Adverb, EPOS::Adverb, lemma};
}

// Suffix helper: creates SUFFIX entry
// Usage: suffix("さん")
inline DictionaryEntry suffix(const char* s, const char* lemma = "") {
  return {s, POS::Suffix, EPOS::Suffix, lemma};
}

// Prefix helper: creates PREFIX entry
// Usage: prefix("お"), prefix("ご")
inline DictionaryEntry prefix(const char* s, const char* lemma = "") {
  return {s, POS::Prefix, EPOS::Prefix, lemma};
}

// Pronoun helper: creates PRONOUN entry
// Usage: pronoun("私")
inline DictionaryEntry pronoun(const char* s, const char* lemma = "") {
  return {s, POS::Pronoun, EPOS::Pronoun, lemma};
}

// Interjection helper: creates INTERJECTION entry
// Usage: intj("えっ")
inline DictionaryEntry intj(const char* s, const char* lemma = "") {
  return {s, POS::Interjection, EPOS::Interjection, lemma};
}

// =============================================================================
// Particles (助詞)

}  // namespace suzume::dictionary::entries

#endif  // SUZUME_DICTIONARY_ENTRIES_ENTRIES_INTERNAL_H_
