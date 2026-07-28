/**
 * @file connection.h
 * @brief Morpheme connection rules and connection ID constants
 *
 * Design: MeCab-style connection system for morpheme boundaries.
 * Uses connection IDs to efficiently look up valid connections
 * and compute connection costs.
 */

#ifndef SUZUME_GRAMMAR_CONNECTION_H_
#define SUZUME_GRAMMAR_CONNECTION_H_

#include <cstdint>

namespace suzume::grammar {

/**
 * @brief Connection ID constants
 *
 * Organized by grammatical category:
 * - 0x00xx: Sentence boundaries
 * - 0x01xx: Verb stem endings
 * - 0x03xx: Auxiliary verb outputs (right connection)
 * - 0x04xx: Particles
 * - 0x05xx: Nouns
 */
namespace conn {

// === Sentence boundaries (0x00xx) ===
constexpr uint16_t kBOS = 0x0000;  // Beginning of sentence
constexpr uint16_t kEOS = 0x0001;  // End of sentence

// === Verb stem endings (0x01xx) ===
constexpr uint16_t kVerbBase = 0x0100;        // 終止形: 書く
constexpr uint16_t kVerbMizenkei = 0x0101;    // 未然形: 書か
constexpr uint16_t kVerbRenyokei = 0x0102;    // 連用形: 書き
constexpr uint16_t kVerbOnbinkei = 0x0103;    // 音便形: 書い (te/ta-ready)
constexpr uint16_t kVerbPotential = 0x0104;   // 可能形語幹: 書け (e-row)
constexpr uint16_t kIAdjStem = 0x0105;        // い形容詞語幹: 美し (ku-form ready)
constexpr uint16_t kVerbVolitional = 0x0106;  // 意志形: 書こう, 食べよう
constexpr uint16_t kVerbKatei = 0x0107;       // 仮定形: 書け (e-row for Godan)
constexpr uint16_t kVerbMeireikei = 0x0108;   // 命令形: 書け, 食べろ, しろ

// === Auxiliary outputs - what they provide (0x03xx) ===
constexpr uint16_t kAuxOutBase = 0x0300;  // Auxiliary in base form
constexpr uint16_t kAuxOutMasu = 0x0301;  // Auxiliary in ます form
constexpr uint16_t kAuxOutTa = 0x0302;    // Auxiliary in た form
constexpr uint16_t kAuxOutTe = 0x0303;    // Auxiliary in て form

// === Particles (0x04xx) ===
constexpr uint16_t kParticle = 0x0400;

// === Nouns (0x05xx) ===
constexpr uint16_t kNoun = 0x0500;

}  // namespace conn

}  // namespace suzume::grammar

#endif  // SUZUME_GRAMMAR_CONNECTION_H_
