/**
 * @file tokenizer_dictionary.cpp
 * @brief Dictionary-backed candidate generation for the tokenizer
 */

/**
 * @file tokenizer.cpp
 * @brief Tokenizer that builds lattice from text
 *
 * This file orchestrates candidate generation for tokenization:
 * - Dictionary candidates (direct lookup)
 * - Unknown word candidates (delegated to UnknownWordGenerator)
 * - Split candidates (delegated to split_candidates.h)
 * - Join candidates (delegated to join_candidates.h)
 */

#include "analysis/category_cost.h"
#include "analysis/tokenizer.h"
#include "candidate_constants.h"
#include "core/debug.h"
#include "core/kana_constants.h"
#include "core/utf8_constants.h"
#include "grammar/char_patterns.h"
#include "grammar/honorific_verbs.h"
#include "join_candidates.h"
#include "normalize/exceptions.h"
#include "normalize/utf8.h"
#include "split_candidates.h"
#include "suffix_candidates.h"
#include "tokenizer_dictionary_internal.h"
#include "tokenizer_utils.h"
#include "verb_candidates_helpers.h"

namespace suzume::analysis {

namespace {

// A kanji run ending in な is an attributive na-adjective candidate.  A
// preceding one-kanji formal noun remains a separate grammatical unit in this
// environment (時 + 不思議 + な), unlike an ordinary lexical kanji compound.
bool isKanjiRunFollowedByAttributiveNa(const std::vector<char32_t>& codepoints, size_t start_pos) {
  size_t pos = start_pos;
  while (pos < codepoints.size() && normalize::isKanjiCodepoint(codepoints[pos])) {
    ++pos;
  }
  return pos > start_pos && pos < codepoints.size() && codepoints[pos] == U'な';
}

// The emphatic interrogative construction (何と+し+て+も, 誰と+し+て+も)
// is compositional.  Its quoted particle and する te-form must not be hidden
// by the otherwise valid compound-particle candidate として.  Look for a
// dictionary-verified interrogative ending exactly at the candidate boundary;
// this keeps ordinary nominal uses such as 道具としても intact.
bool hasInterrogativeEndingAt(const dictionary::DictionaryManager& dict_manager, std::string_view text,
                              const ByteOffsets& byte_offsets, size_t end_pos) {
  for (size_t start_pos = 0; start_pos < end_pos; ++start_pos) {
    const size_t byte_pos = byteOffsetAt(byte_offsets, start_pos);
    for (const auto& result : dict_manager.lookup(text, byte_pos)) {
      if (result.entry != nullptr && result.entry->extended_pos == core::ExtendedPOS::PronounInterrogative &&
          start_pos + result.length == end_pos) {
        return true;
      }
    }
  }
  return false;
}

// A lexicalized noun beginning with お/ご can contain a suffix that happens to
// be a verb form.  Once the lattice has reached that suffix, prefer the whole
// dictionary noun and do not reopen it as a low-cost verb/auxiliary chain.
// The verb-tail check is essential: ordinary prefixed nouns such as おかし
// retain their independently searchable prefix + noun analysis.
bool startsHonorificPrefixedNounWithVerbTail(const dictionary::DictionaryManager& dict_manager, std::string_view text,
                                             const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                             size_t start_pos) {
  if (start_pos == 0 || !grammar::isHonorificPrefix(extractSubstring(codepoints, start_pos - 1, start_pos))) {
    return false;
  }

  const size_t prefix_pos = start_pos - 1;
  const size_t prefix_byte_pos = byteOffsetAt(byte_offsets, prefix_pos);
  for (const auto& result : dict_manager.lookup(text, prefix_byte_pos)) {
    if (result.entry == nullptr || result.entry->pos != core::PartOfSpeech::Noun || result.length <= 1) {
      continue;
    }

    const size_t noun_end = prefix_pos + result.length;
    if (noun_end <= start_pos || noun_end > codepoints.size()) {
      continue;
    }

    const std::string verb_tail = extractSubstring(codepoints, start_pos, noun_end);
    if (dict_manager.lookupExact(verb_tail, core::PartOfSpeech::Verb) != nullptr) {
      return true;
    }
  }
  return false;
}

// A pure-hiragana na-adjective can share its surface with the interior of a
// kanji-led inflected verb. If a previously generated verb edge already
// crosses this position, the adjective cannot begin here without cutting the
// verb stem (読まれ, 生まれて, 止まれ). Scan only the immediately preceding
// kanji run; this keeps the check bounded and leaves genuine clause-initial or
// post-particle adjective uses available.
bool startsInsideKanjiLedVerb(const core::Lattice& lattice, const std::vector<char32_t>& codepoints, size_t start_pos) {
  if (start_pos == 0 || !normalize::isKanjiCodepoint(codepoints[start_pos - 1])) {
    return false;
  }

  size_t kanji_start = start_pos;
  while (kanji_start > 0 && normalize::isKanjiCodepoint(codepoints[kanji_start - 1])) {
    --kanji_start;
  }
  for (size_t pos = kanji_start; pos < start_pos; ++pos) {
    for (const uint32_t edge_id : lattice.edgeIdsAt(pos)) {
      const auto& edge = lattice.getEdge(edge_id);
      if (edge.pos == core::PartOfSpeech::Verb && edge.end > start_pos && edge.lemmaVerified()) {
        return true;
      }
    }
  }
  return false;
}

// A dictionary adverb cannot begin inside an already verified inflected
// predicate.  Short literary adverbs can be homographic with the tail of an
// adjective or auxiliary followed by a particle (ない+と, らしい+と).  Keep
// the adverb available at a real boundary while protecting the longer
// grammatical edge that crosses this position.
bool startsInsideVerifiedPredicate(const core::Lattice& lattice, size_t start_pos) {
  for (size_t edge_start = 0; edge_start < start_pos; ++edge_start) {
    for (const uint32_t edge_id : lattice.edgeIdsAt(edge_start)) {
      const auto& edge = lattice.getEdge(edge_id);
      if (edge.end <= start_pos || !edge.lemmaVerified()) {
        continue;
      }
      if (edge.pos == core::PartOfSpeech::Verb || edge.pos == core::PartOfSpeech::Adjective ||
          edge.pos == core::PartOfSpeech::Auxiliary) {
        return true;
      }
    }
  }
  return false;
}

// A dictionary adverb ending in に may overlap the second kanji of a
// two-kanji content word plus an independent case particle (事実+に,
// 確実+に), even though the same adverb is valid at a real boundary
// (実に+難しい).  Preserve the longer content edge only when it starts
// exactly one kanji before the adverb and ends immediately before a case に.
// This uses lattice structure rather than enumerating open-class words.
bool overlapsTwoKanjiContentBeforeCaseNi(const core::Lattice& lattice,
                                         const dictionary::DictionaryManager& dict_manager,
                                         const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos) {
  if (start_pos == 0 || end_pos <= start_pos + 1 || end_pos > codepoints.size() || codepoints[end_pos - 1] != U'に' ||
      !normalize::isKanjiCodepoint(codepoints[start_pos - 1]) || !normalize::isKanjiCodepoint(codepoints[start_pos])) {
    return false;
  }
  const auto* particle = dict_manager.lookupExact("に", core::PartOfSpeech::Particle);
  if (particle == nullptr || particle->extended_pos != core::ExtendedPOS::ParticleCase) {
    return false;
  }
  const size_t content_start = start_pos - 1;
  const size_t content_end = end_pos - 1;
  for (const uint32_t edge_id : lattice.edgeIdsAt(content_start)) {
    const auto& edge = lattice.getEdge(edge_id);
    if (edge.end == content_end &&
        (edge.pos == core::PartOfSpeech::Noun || edge.pos == core::PartOfSpeech::Adjective)) {
      return true;
    }
  }
  return false;
}

// A dictionary-verified lexical compound owns every boundary inside its
// active inflectional span. Short dictionary verbs and closed function words
// may be accidental homographs of that interior (思い出+し, 見落+として).
// Require the explicit LemmaVerified flag: compound join candidates also carry
// FromDictionary for their generation evidence, which alone does not attest
// the complete compound lemma.
bool conflictsWithVerifiedCompoundBoundary(const core::Lattice& lattice,
                                           const dictionary::DictionaryManager& dict_manager,
                                           const std::vector<char32_t>& codepoints, size_t candidate_start,
                                           size_t candidate_end, core::PartOfSpeech candidate_pos) {
  const bool is_grammatical_candidate =
      candidate_pos == core::PartOfSpeech::Verb || candidate_pos == core::PartOfSpeech::Particle ||
      candidate_pos == core::PartOfSpeech::Auxiliary || candidate_pos == core::PartOfSpeech::Suffix;
  if (!is_grammatical_candidate) {
    return false;
  }
  const size_t compound_end = verifiedCompoundEndCovering(lattice, candidate_start);
  if (compound_end != 0 && candidate_end <= compound_end) {
    return true;
  }
  const size_t onbinkei_end = dictionarySokuonbinEndCovering(lattice, candidate_start);
  if (onbinkei_end != 0 && candidate_end <= onbinkei_end && onbinkei_end < codepoints.size() &&
      (codepoints[onbinkei_end] == U'た' || codepoints[onbinkei_end] == U'て')) {
    return true;
  }
  if (candidate_pos != core::PartOfSpeech::Particle) {
    return false;
  }
  // A structurally valid compound does not need lexical registration to
  // protect its connective boundary from a larger particle that begins in
  // its interior. Requiring the outside remainder itself to be a particle
  // keeps ordinary compound-particle uses available at real boundaries.
  const size_t structural_compound_end = compoundVerbEndCovering(lattice, candidate_start);
  if (structural_compound_end == 0 || candidate_end <= structural_compound_end) {
    return false;
  }
  const std::string outside_suffix = extractSubstring(codepoints, structural_compound_end, candidate_end);
  return dict_manager.lookupExact(outside_suffix, core::PartOfSpeech::Particle) != nullptr;
}

// The temporal adverb いま overlaps the full polite forms of いる
// (います/いました/いません/…).  At a clause boundary the closed inflectional
// chain is more specific than the accidental いま+verb path.  Do not apply
// this inside a longer lexical continuation: いますぐ remains いま+すぐ.
bool startsIruPoliteFormAt(const std::vector<char32_t>& codepoints, size_t start_pos) {
  if (start_pos >= codepoints.size() || codepoints[start_pos] != U'い') {
    return false;
  }
  const size_t masu_length = verb_helpers::finiteMasuFormLengthAt(codepoints, start_pos + 1);
  if (masu_length == 0) {
    return false;
  }
  const size_t end_pos = start_pos + 1 + masu_length;
  if (end_pos >= codepoints.size()) {
    return true;
  }
  const char32_t following = codepoints[end_pos];
  return normalize::isExtendedParticle(following) || following == U'。' || following == U'、' || following == U'」' ||
         following == U'）';
}

// The literary conjunctive expression ～につけ attaches to a preceding finite
// predicate and introduces a following clause (聞くにつけ、思い出す). It must
// not compete with the unrelated verb つける in sentence-initial につけて or
// in a construction such as 順位につけている, so require both the preceding
// lattice verb boundary and the clause-separating comma.
bool startsLiteraryNitsukeAt(const core::Lattice& lattice, const std::vector<char32_t>& codepoints, size_t start_pos) {
  constexpr size_t kNitsukeLength = 3;
  if (start_pos == 0 || start_pos + kNitsukeLength >= codepoints.size() || codepoints[start_pos] != U'に' ||
      codepoints[start_pos + 1] != U'つ' || codepoints[start_pos + 2] != U'け' ||
      codepoints[start_pos + kNitsukeLength] != U'、') {
    return false;
  }
  return hasPrecedingExtendedPOS(lattice, start_pos, core::ExtendedPOS::VerbShuushikei);
}

// The method suffix 方 attaches to a kanji-containing deverbal noun
// (打ち合わせ+方). The unknown-word path can create the deverbal noun before
// the suffix position but has no all-kanji suffix rule to supply 方 itself.
bool hasPrecedingDeverbalNoun(const core::Lattice& lattice, size_t start_pos) {
  bool has_noun = false;
  bool has_renyokei = false;
  for (size_t edge_start = 0; edge_start < start_pos; ++edge_start) {
    for (const uint32_t edge_id : lattice.edgeIdsAt(edge_start)) {
      const auto& edge = lattice.getEdge(edge_id);
      if (edge.end != start_pos) {
        continue;
      }
      if (grammar::containsKanji(edge.surface) && edge.pos == core::PartOfSpeech::Noun) {
        has_noun = true;
      }
      if (grammar::containsKanji(edge.surface) && edge.extended_pos == core::ExtendedPOS::VerbRenyokei) {
        has_renyokei = true;
      }
    }
  }
  return has_noun && has_renyokei;
}

// A polite-auxiliary homograph is not a real boundary when it begins inside a
// longer, dictionary-verified verb renyokei ending at the same position
// (醒まし/て, さまし/て).  Requiring both the shared end and verified lemma
// keeps ordinary polite chains such as 食べ/まし/て and 読み/まし/て intact.
bool hasCoveringVerifiedVerbRenyokei(const core::Lattice& lattice, size_t interior_start, size_t shared_end) {
  for (size_t edge_start = 0; edge_start < interior_start; ++edge_start) {
    for (const uint32_t edge_id : lattice.edgeIdsAt(edge_start)) {
      const auto& edge = lattice.getEdge(edge_id);
      if (edge.end == shared_end && edge.extended_pos == core::ExtendedPOS::VerbRenyokei && edge.lemmaVerified()) {
        return true;
      }
    }
  }
  return false;
}

// A volitional edge is structurally meaningful here only when it closes a
// verb mizenkei immediately before it.  Looking merely for any volitional
// candidate ending at start_pos mistakes the final う of a lexical verb such
// as いう for an independent auxiliary and suppresses the following particle.
bool hasPrecedingVerbVolitionalChain(const core::Lattice& lattice, size_t start_pos) {
  for (size_t edge_start = 0; edge_start < start_pos; ++edge_start) {
    for (const uint32_t edge_id : lattice.edgeIdsAt(edge_start)) {
      const auto& edge = lattice.getEdge(edge_id);
      if (edge.end != start_pos || edge.extended_pos != core::ExtendedPOS::AuxVolitional) {
        continue;
      }
      for (size_t verb_start = 0; verb_start < edge.start; ++verb_start) {
        for (const uint32_t verb_id : lattice.edgeIdsAt(verb_start)) {
          const auto& verb = lattice.getEdge(verb_id);
          if (verb.end == edge.start && verb.extended_pos == core::ExtendedPOS::VerbMizenkei) {
            return true;
          }
        }
      }
    }
  }
  return false;
}

bool hasPrecedingNominal(const core::Lattice& lattice, size_t start_pos) {
  constexpr PartOfSpeechMask kNominalMask =
      partOfSpeechMask(core::PartOfSpeech::Noun) | partOfSpeechMask(core::PartOfSpeech::Pronoun);
  return hasPrecedingPartOfSpeech(lattice, start_pos, kNominalMask);
}

bool startsFormalNounParticleAfterPredicate(const core::Lattice& lattice,
                                            const dictionary::DictionaryManager& dict_manager,
                                            const std::vector<char32_t>& codepoints, size_t start_pos, size_t end_pos) {
  constexpr PartOfSpeechMask kPredicateMask =
      partOfSpeechMask(core::PartOfSpeech::Verb) | partOfSpeechMask(core::PartOfSpeech::Adjective);
  if (!hasPrecedingPartOfSpeech(lattice, start_pos, kPredicateMask)) {
    return false;
  }
  for (size_t split = start_pos + 1; split < end_pos; ++split) {
    const std::string nominal = extractSubstring(codepoints, start_pos, split);
    const auto* noun = dict_manager.lookupExact(nominal, core::PartOfSpeech::Noun);
    if (noun == nullptr || noun->extended_pos != core::ExtendedPOS::NounFormal) {
      continue;
    }
    const std::string particle = extractSubstring(codepoints, split, end_pos);
    if (dict_manager.lookupExact(particle, core::PartOfSpeech::Particle) != nullptr) {
      return true;
    }
  }
  return false;
}

// A case particle immediately before an ABAB mimetic is a stronger boundary
// than a homographic multi-mora dictionary entry beginning at that particle
// (鈴+が+りんりんと, not がり+んりんと).
bool startsParticleBeforeReduplicatedMimetic(const std::vector<char32_t>& codepoints, size_t start_pos) {
  if (start_pos + 5 >= codepoints.size() || !normalize::isParticleCodepoint(codepoints[start_pos])) {
    return false;
  }
  const size_t rest = start_pos + 1;
  return codepoints[rest] == codepoints[rest + 2] && codepoints[rest + 1] == codepoints[rest + 3] &&
         codepoints[rest + 4] == U'と';
}

}  // namespace

void Tokenizer::addDictionaryCandidates(core::Lattice& lattice, std::string_view text,
                                        const std::vector<char32_t>& codepoints, const ByteOffsets& byte_offsets,
                                        size_t start_pos) const {
  // Convert to byte position for dictionary lookup
  size_t byte_pos = byteOffsetAt(byte_offsets, start_pos);

  // Lookup in dictionary
  auto results = dict_manager_.lookup(text, byte_pos);
  const bool suppress_prefixed_noun_interior =
      startsHonorificPrefixedNounWithVerbTail(dict_manager_, text, codepoints, byte_offsets, start_pos);

  if (startsLiteraryNitsukeAt(lattice, codepoints, start_pos)) {
    lattice.addEdge("につけ", static_cast<uint32_t>(start_pos), static_cast<uint32_t>(start_pos + 3),
                    core::PartOfSpeech::Particle, getCategoryCost(core::ExtendedPOS::ParticleConj),
                    core::LatticeEdge::kFromDictionary, "につけ", dictionary::ConjugationType::None,
                    core::CandidateOrigin::Dictionary, candidate::kDictionaryOriginConfidence, {},
                    core::ExtendedPOS::ParticleConj, "literary_nitsuke");
  }

  if (codepoints[start_pos] == U'方' && hasPrecedingDeverbalNoun(lattice, start_pos)) {
    lattice.addEdge("方", static_cast<uint32_t>(start_pos), static_cast<uint32_t>(start_pos + 1),
                    core::PartOfSpeech::Suffix, candidate::kDeverbalMethodSuffixCost,
                    core::LatticeEdge::kFromDictionary, "方", dictionary::ConjugationType::None,
                    core::CandidateOrigin::SuffixPattern, candidate::kDictionaryOriginConfidence, {},
                    core::ExtendedPOS::Suffix, "deverbal_method_suffix");
  }

  // Interrogative + か forms an indefinite pronoun (誰+か, 何+か,
  // どこ+か). Generate the adverbial-particle homograph only at that
  // verified boundary so a global one-mora entry cannot split lexical words
  // containing か (かかる, 静か, うれしかった).
  if (codepoints[start_pos] == U'か' && hasInterrogativeEndingAt(dict_manager_, text, byte_offsets, start_pos)) {
    lattice.addEdge("か", static_cast<uint32_t>(start_pos), static_cast<uint32_t>(start_pos + 1),
                    core::PartOfSpeech::Particle, getCategoryCost(core::ExtendedPOS::ParticleAdverbial),
                    core::LatticeEdge::kFromDictionary, "か", dictionary::ConjugationType::None,
                    core::CandidateOrigin::Dictionary, candidate::kDictionaryOriginConfidence, {},
                    core::ExtendedPOS::ParticleAdverbial, "indefinite_particle_ka");
  }

  // In the closed interrogative frame か+どう+か, the first か is an
  // adverbial choice particle after a finite predicate. Generate it
  // contextually rather than
  // admitting a global one-mora candidate inside arbitrary hiragana words.
  const bool opens_interrogative_frame = codepoints[start_pos] == U'か' && start_pos + 3 < codepoints.size() &&
                                         codepoints[start_pos + 1] == U'ど' && codepoints[start_pos + 2] == U'う' &&
                                         codepoints[start_pos + 3] == U'か' &&
                                         hasPrecedingExtendedPOS(lattice, start_pos, core::ExtendedPOS::VerbShuushikei);
  const bool closes_interrogative_frame =
      codepoints[start_pos] == U'か' && start_pos >= 3 && codepoints[start_pos - 3] == U'か' &&
      codepoints[start_pos - 2] == U'ど' && codepoints[start_pos - 1] == U'う' &&
      hasPrecedingExtendedPOS(lattice, start_pos - 3, core::ExtendedPOS::VerbShuushikei);
  if (opens_interrogative_frame || closes_interrogative_frame) {
    constexpr auto frame_epos = core::ExtendedPOS::ParticleAdverbial;
    lattice.addEdge("か", static_cast<uint32_t>(start_pos), static_cast<uint32_t>(start_pos + 1),
                    core::PartOfSpeech::Particle, getCategoryCost(frame_epos), core::LatticeEdge::kFromDictionary, "か",
                    dictionary::ConjugationType::None, core::CandidateOrigin::Dictionary,
                    candidate::kDictionaryOriginConfidence, {}, frame_epos, "interrogative_frame_ka");
  }

  size_t longest_conjunction = 0;
  size_t longest_interjection = 0;
  size_t longest_adverb = 0;
  size_t longest_noun = 0;
  size_t longest_potential_benefactive = 0;
  for (const auto& result : results) {
    if (result.entry != nullptr && result.entry->pos == core::PartOfSpeech::Conjunction) {
      longest_conjunction = std::max(longest_conjunction, result.length);
    }
    if (result.entry != nullptr && result.entry->pos == core::PartOfSpeech::Interjection) {
      longest_interjection = std::max(longest_interjection, result.length);
    }
    const size_t result_end = start_pos + result.length;
    const bool adverb_absorbs_quoted_question =
        result.entry != nullptr && result.entry->pos == core::PartOfSpeech::Adverb && result.length > 1 &&
        codepoints[result_end - 1] == U'か' && result_end + 2 < codepoints.size() && codepoints[result_end] == U'と' &&
        codepoints[result_end + 1] == U'い' && codepoints[result_end + 2] == U'う';
    if (result.entry != nullptr && result.entry->pos == core::PartOfSpeech::Adverb && !adverb_absorbs_quoted_question) {
      longest_adverb = std::max(longest_adverb, result.length);
    }
    if (result.entry != nullptr && result.entry->pos == core::PartOfSpeech::Noun) {
      longest_noun = std::max(longest_noun, result.length);
    }
    if (result.entry != nullptr && grammar::isPotentialBenefactiveLemma(result.entry->lemma)) {
      longest_potential_benefactive = std::max(longest_potential_benefactive, result.length);
    }
  }

  for (const auto& result : results) {
    if (result.entry == nullptr) {
      continue;
    }

    // Calculate end position in characters before context-sensitive candidate
    // guards below inspect the following lexical head.
    size_t end_pos = start_pos + result.length;

    // A context-licensed particle must not absorb the beginning of a complete
    // following adverb (裏+で+しばらく, 時+は+すでに). Restricting the guard to
    // an observed left content/predicate edge avoids kana homographs inside
    // open words such as adjectives.
    if (result.entry->pos != core::PartOfSpeech::Particle &&
        joinsParticleToDictionaryAdverb(lattice, dict_manager_, text, byte_offsets, start_pos, end_pos)) {
      continue;
    }

    // A dictionary terminal verb must yield to a longer, structurally valid
    // i-onbin stem immediately selected by て/で. This recovers open Godan-ka/
    // Godan-ga forms such as あるい+て without registering the lexical verb.
    if (result.entry->pos == core::PartOfSpeech::Verb && end_pos + 1 < codepoints.size() &&
        codepoints[end_pos] == U'い' && (codepoints[end_pos + 1] == U'て' || codepoints[end_pos + 1] == U'で')) {
      const std::string longer_stem = extractSubstring(codepoints, start_pos, end_pos + 1);
      const auto& longer_analyses = inflection_.analyze(longer_stem);
      const bool has_longer_ionbin = std::any_of(
          longer_analyses.begin(), longer_analyses.end(), [&](const grammar::InflectionCandidate& candidate) {
            return (candidate.verb_type == grammar::VerbType::GodanKa ||
                    candidate.verb_type == grammar::VerbType::GodanGa) &&
                   candidate.base_form != result.entry->lemma &&
                   candidate.confidence >= candidate::kParticleVerbBoundaryMinConfidence;
          });
      if (has_longer_ionbin) {
        continue;
      }
    }

    // Exact dictionary nouns are tokenizer search units.  If multiple noun
    // entries share a start, keep the longest one instead of letting the
    // negative lexical costs of two shorter noun edges defeat it.  Competing
    // grammatical categories remain available, so this changes only the
    // ownership relation among exact Noun homographs.
    if (result.entry->pos == core::PartOfSpeech::Noun && result.length < longest_noun) {
      continue;
    }

    if (conflictsWithVerifiedCompoundBoundary(lattice, dict_manager_, codepoints, start_pos, end_pos,
                                              result.entry->pos)) {
      continue;
    }

    // A determiner must introduce a nominal constituent.  If a closed case
    // particle starts exactly where this candidate ends, the homographic
    // surface belongs to a compositional predicate instead (と+いう+より),
    // so do not admit the fused determiner path at all.  This is a category
    // constraint, independent of the individual determiner or particle.
    const bool ends_at_sentence_boundary =
        end_pos >= codepoints.size() || normalize::classifyChar(codepoints[end_pos]) == normalize::CharType::Symbol;
    if (result.entry->pos == core::PartOfSpeech::Determiner && ends_at_sentence_boundary) {
      const bool has_same_span_predicate = std::any_of(results.begin(), results.end(), [&](const auto& other) {
        return other.entry != nullptr && other.length == result.length &&
               (other.entry->pos == core::PartOfSpeech::Verb || other.entry->pos == core::PartOfSpeech::Adjective);
      });
      if (has_same_span_predicate) {
        continue;
      }
    }
    if (result.entry->pos == core::PartOfSpeech::Determiner && ends_at_sentence_boundary &&
        hasPrecedingExtendedPOS(lattice, start_pos, core::ExtendedPOS::AuxCopulaDa) && start_pos >= 2 &&
        codepoints[start_pos - 2] == U'の' &&
        hasPrecedingExtendedPOS(lattice, start_pos - 1, core::ExtendedPOS::ParticleNo)) {
      continue;
    }
    if (result.entry->pos == core::PartOfSpeech::Determiner && end_pos < codepoints.size()) {
      const size_t following_byte_pos = byteOffsetAt(byte_offsets, end_pos);
      const auto following_results = dict_manager_.lookup(text, following_byte_pos);
      const bool followed_by_case_particle =
          std::any_of(following_results.begin(), following_results.end(), [](const auto& following) {
            return following.entry != nullptr && following.entry->extended_pos == core::ExtendedPOS::ParticleCase;
          });
      if (followed_by_case_particle) {
        continue;
      }
    }

    // When the same dictionary span has both noun and adverb readings, a
    // following nominal particle selects the noun use (一切+の/は/を).  Keep
    // the adverb when it directly modifies a predicate (一切+確認しない).
    if (result.entry->pos == core::PartOfSpeech::Adverb && end_pos < codepoints.size()) {
      const bool has_same_span_noun = std::any_of(results.begin(), results.end(), [&](const auto& other) {
        return other.entry != nullptr && other.length == result.length && other.entry->pos == core::PartOfSpeech::Noun;
      });
      const auto following_results = dict_manager_.lookup(text, byteOffsetAt(byte_offsets, end_pos));
      const bool followed_by_nominal_particle =
          std::any_of(following_results.begin(), following_results.end(), [](const auto& following) {
            if (following.entry == nullptr) {
              return false;
            }
            const auto extended_pos = following.entry->extended_pos;
            return extended_pos == core::ExtendedPOS::ParticleNo || extended_pos == core::ExtendedPOS::ParticleTopic ||
                   extended_pos == core::ExtendedPOS::ParticleCase;
          });
      if (has_same_span_noun && followed_by_nominal_particle) {
        continue;
      }

      // An interrogative pronoun followed by a closed adverbial particle is
      // compositional before the genitive/nominalizer の (どれ+ほど+の...).
      // The same full-span adverb remains valid when it directly modifies a
      // predicate, so require both internal dictionary categories and the
      // right-hand nominal particle instead of naming any lexical surface.
      const bool followed_by_no =
          std::any_of(following_results.begin(), following_results.end(), [](const auto& following) {
            return following.entry != nullptr && following.entry->extended_pos == core::ExtendedPOS::ParticleNo;
          });
      if (followed_by_no) {
        bool has_interrogative_particle_split = false;
        for (const auto& prefix : results) {
          if (prefix.entry == nullptr || prefix.length >= result.length ||
              prefix.entry->extended_pos != core::ExtendedPOS::PronounInterrogative) {
            continue;
          }
          const size_t suffix_pos = start_pos + prefix.length;
          const auto suffix_results = dict_manager_.lookup(text, byteOffsetAt(byte_offsets, suffix_pos));
          has_interrogative_particle_split =
              std::any_of(suffix_results.begin(), suffix_results.end(), [&](const auto& suffix) {
                return suffix.entry != nullptr && suffix.length == result.length - prefix.length &&
                       suffix.entry->extended_pos == core::ExtendedPOS::ParticleAdverbial;
              });
          if (has_interrogative_particle_split) {
            break;
          }
        }
        if (has_interrogative_particle_split) {
          continue;
        }
      }
    }

    if (result.entry->extended_pos == core::ExtendedPOS::AuxTenseMasu &&
        utf8::equalsAny(result.entry->surface, {"まし"}) && end_pos < codepoints.size() &&
        codepoints[end_pos] == U'て' && hasCoveringVerifiedVerbRenyokei(lattice, start_pos, end_pos)) {
      continue;
    }

    // A closed interval suffix can be homographic with a verb continuative
    // (1時間+おき).  After a verified number expression, select the suffix
    // only in a nominal environment; an auxiliary continuation such as
    // 1時間+おき+ます keeps the verb candidate.
    if (result.entry->pos == core::PartOfSpeech::Verb &&
        hasPrecedingExtendedPOS(lattice, start_pos, core::ExtendedPOS::NounNumber)) {
      const bool has_same_span_suffix = std::any_of(results.begin(), results.end(), [&](const auto& other) {
        return other.entry != nullptr && other.length == result.length &&
               other.entry->pos == core::PartOfSpeech::Suffix;
      });
      bool has_nominal_right_context = end_pos >= codepoints.size();
      if (!has_nominal_right_context && normalize::classifyChar(codepoints[end_pos]) == normalize::CharType::Symbol) {
        has_nominal_right_context = true;
      }
      if (!has_nominal_right_context) {
        const auto following_results = dict_manager_.lookup(text, byteOffsetAt(byte_offsets, end_pos));
        has_nominal_right_context =
            std::any_of(following_results.begin(), following_results.end(), [](const auto& following) {
              return following.entry != nullptr && following.entry->pos == core::PartOfSpeech::Particle;
            });
      }
      if (has_same_span_suffix && has_nominal_right_context) {
        continue;
      }
    }

    // Nominalizing/final-particle homographs of さ cannot occur between a verb
    // mizenkei and a passive auxiliary.  In a causative-passive chain
    // (読ま+さ+れ, 考え込ま+さ+れ), keeping either homograph creates a
    // spurious adjective path which can defeat the generated verb candidate.
    if (result.entry->extended_pos != core::ExtendedPOS::VerbMizenkei && result.length == 1 &&
        codepoints[start_pos] == core::hiragana::kSa && result.entry->pos != core::PartOfSpeech::Verb &&
        hasPrecedingExtendedPOS(lattice, start_pos, core::ExtendedPOS::VerbMizenkei) && end_pos < codepoints.size()) {
      const size_t following_byte_pos = byteOffsetAt(byte_offsets, end_pos);
      const auto following_results = dict_manager_.lookup(text, following_byte_pos);
      const bool followed_by_passive =
          std::any_of(following_results.begin(), following_results.end(), [](const auto& following) {
            return following.entry != nullptr && following.entry->extended_pos == core::ExtendedPOS::AuxPassive;
          });
      if (followed_by_passive) {
        continue;
      }
    }

    // Resolve dictionary homographs from a closed na-adjective continuation.
    // When the same full surface has an AdjNaAdj entry, attributive な,
    // adverbial に, and appearance そう select that entry rather than the noun
    // homograph. Noun-only words remain untouched.
    if (result.entry->pos == core::PartOfSpeech::Noun && end_pos < codepoints.size()) {
      const bool has_same_surface_na_adjective = std::any_of(results.begin(), results.end(), [&](const auto& other) {
        return other.entry != nullptr && other.length == result.length &&
               other.entry->extended_pos == core::ExtendedPOS::AdjNaAdj;
      });
      const bool na_adjective_continuation =
          codepoints[end_pos] == U'に' ||
          (codepoints[end_pos] == U'な' && (end_pos + 1 >= codepoints.size() || codepoints[end_pos + 1] != U'ら')) ||
          (end_pos + 1 < codepoints.size() && codepoints[end_pos] == U'そ' && codepoints[end_pos + 1] == U'う');
      if (has_same_surface_na_adjective && na_adjective_continuation) {
        continue;
      }
    }

    // A shorter adverb prefix cannot split a longer dictionary na-adjective
    // immediately before attributive な (めちゃくちゃな, もっともな).
    if (result.entry->pos == core::PartOfSpeech::Adverb) {
      const bool longer_attributive_na_adjective = std::any_of(results.begin(), results.end(), [&](const auto& other) {
        const size_t other_end = start_pos + other.length;
        return other.entry != nullptr && other.length > result.length &&
               other.entry->extended_pos == core::ExtendedPOS::AdjNaAdj && other_end < codepoints.size() &&
               codepoints[other_end] == U'な';
      });
      if (longer_attributive_na_adjective) {
        continue;
      }
    }

    const bool follows_volitional = hasPrecedingVerbVolitionalChain(lattice, start_pos);

    // Prefer the longest member only within the same particle class. This
    // keeps closed concessives such as ども/けれども intact without
    // suppressing productive boundaries whose shorter member has another
    // grammatical role (で+も, と+も).
    if (result.entry->pos == core::PartOfSpeech::Particle) {
      // After an explicit volitional auxiliary, a multi-mora case particle
      // would hide the productive quotative + suru sequence
      // (書こ+う+と+し+て). Keep the one-mora quotative candidate even when a
      // longer case-particle entry shares its prefix.
      if (follows_volitional && result.entry->extended_pos == core::ExtendedPOS::ParticleCase && result.length > 1) {
        continue;
      }
      const bool has_longer_same_class = std::any_of(results.begin(), results.end(), [&](const auto& other) {
        return other.entry != nullptr && other.entry->pos == core::PartOfSpeech::Particle &&
               other.entry->extended_pos == result.entry->extended_pos && other.length > result.length;
      });
      const bool keep_interrogative_quotative =
          result.entry->extended_pos == core::ExtendedPOS::ParticleCase && result.length == 1 &&
          grammar::isSingleHiragana(result.entry->surface, core::hiragana::kTo) &&
          std::any_of(results.begin(), results.end(),
                      [&](const auto& other) {
                        const size_t other_end = start_pos + other.length;
                        return other.entry != nullptr && other.entry->extended_pos == core::ExtendedPOS::ParticleCase &&
                               grammar::isQuotativeSuruTeCompoundParticle(other.entry->surface) &&
                               other_end < codepoints.size() && codepoints[other_end] == U'も';
                      }) &&
          hasInterrogativeEndingAt(dict_manager_, text, byte_offsets, start_pos);
      const bool keep_volitional_quotative =
          follows_volitional && result.entry->extended_pos == core::ExtendedPOS::ParticleCase && result.length == 1;
      if (has_longer_same_class && !keep_volitional_quotative && !keep_interrogative_quotative) {
        continue;
      }
    }

    if (result.entry->pos == core::PartOfSpeech::Adverb && result.length > 1 && codepoints[end_pos - 1] == U'か' &&
        end_pos + 2 < codepoints.size() && codepoints[end_pos] == U'と' && codepoints[end_pos + 1] == U'い' &&
        codepoints[end_pos + 2] == U'う') {
      continue;
    }

    // A lexical adverb homographic with a dictionary-verified verb te-form
    // cannot govern the progressive auxiliary いる. Preserve the verb stem +
    // connective boundary in that environment while leaving ordinary adverb
    // uses untouched.
    if (result.entry->pos == core::PartOfSpeech::Adverb && end_pos + 1 < codepoints.size() &&
        codepoints[end_pos] == U'い' && codepoints[end_pos + 1] == U'る' &&
        utf8::endsWithAny(result.entry->surface, {"て", "で"})) {
      bool is_verified_verb_te_form = false;
      for (const auto& inflection_candidate : inflection_.analyze(result.entry->surface)) {
        if (inflection_candidate.verb_type != grammar::VerbType::IAdjective &&
            (verb_helpers::isVerbInDictionary(&dict_manager_, inflection_candidate.base_form) ||
             inflection_candidate.confidence >= candidate::kAdverbVerbTeHomographMinConfidence)) {
          is_verified_verb_te_form = true;
          break;
        }
      }
      if (is_verified_verb_te_form) {
        continue;
      }
    }

    // A conjunction that is also a productive verb+particle sequence is
    // lexical only at a clause boundary. Inside a phrase, keep the ordinary
    // predicate boundary (もしか+する+と).
    if (result.entry->pos == core::PartOfSpeech::Conjunction && start_pos > 0) {
      bool decomposes_as_verb_particle = false;
      for (size_t split = 1; split < result.length; ++split) {
        const std::string left = extractSubstring(codepoints, start_pos, start_pos + split);
        const std::string right = extractSubstring(codepoints, start_pos + split, end_pos);
        if (dict_manager_.lookupExact(left, core::PartOfSpeech::Verb) != nullptr &&
            dict_manager_.lookupExact(right, core::PartOfSpeech::Particle) != nullptr) {
          decomposes_as_verb_particle = true;
          break;
        }
      }
      const bool coordinates_nominals = hasPrecedingNominal(lattice, start_pos) && end_pos < codepoints.size() &&
                                        (normalize::isKanjiCodepoint(codepoints[end_pos]) ||
                                         normalize::classifyChar(codepoints[end_pos]) == normalize::CharType::Katakana);
      const bool follows_completed_clause = hasPrecedingExtendedPOS(lattice, start_pos, core::ExtendedPOS::AuxTenseTa);
      if (decomposes_as_verb_particle && !coordinates_nominals && !follows_completed_clause &&
          normalize::classifyChar(codepoints[start_pos - 1]) != normalize::CharType::Symbol) {
        continue;
      }
    }

    if (result.length > 1 && startsParticleBeforeReduplicatedMimetic(codepoints, start_pos) &&
        end_pos > start_pos + 1) {
      continue;
    }

    if (result.entry->pos == core::PartOfSpeech::Adverb && result.length == 2 &&
        startsIruPoliteFormAt(codepoints, start_pos)) {
      continue;
    }

    if (result.entry->pos == core::PartOfSpeech::Adverb && startsInsideVerifiedPredicate(lattice, start_pos)) {
      continue;
    }

    if (result.entry->pos == core::PartOfSpeech::Adverb &&
        overlapsTwoKanjiContentBeforeCaseNi(lattice, dict_manager_, codepoints, start_pos, end_pos)) {
      continue;
    }

    // Do not reopen the interior of a kanji-led verb as a pure-hiragana
    // dictionary na-adjective. The same adjective remains available at a real
    // boundary (sentence start or after a particle).
    if (result.entry->extended_pos == core::ExtendedPOS::AdjNaAdj && grammar::isPureHiragana(result.entry->surface) &&
        startsInsideKanjiLedVerb(lattice, codepoints, start_pos)) {
      continue;
    }

    // A period suffix cannot head an interval compound.  In a numeral-led
    // expression such as 10分間隔, the counter generator already supplies
    // 10分 and the following lexical noun must remain 間隔, not 間+隔.
    if (result.entry->extended_pos == core::ExtendedPOS::Suffix && result.length == 1 &&
        start_pos + 1 < codepoints.size() && codepoints[start_pos] == U'間' &&
        normalize::isIntervalCompoundSecondKanji(codepoints[start_pos + 1])) {
      continue;
    }

    // 時間接尾辞「後」は終了+後・三日+後のように内容語へ直接接合
    // する。ひらがな活用や助詞の後では独立時間名詞なので、suffix
    // edgeを出さず既存のnoun候補へ任せる（食べた+後、ので+後）。
    if (result.entry->extended_pos == core::ExtendedPOS::Suffix &&
        grammar::isDirectAttachmentTemporalSuffix(result.entry->surface)) {
      if (start_pos == 0) {
        continue;
      }
      const auto preceding_type = normalize::classifyChar(codepoints[start_pos - 1]);
      const bool directly_attached_to_nominal =
          preceding_type == normalize::CharType::Kanji || preceding_type == normalize::CharType::Katakana ||
          preceding_type == normalize::CharType::Alphabet || preceding_type == normalize::CharType::Digit;
      if (!directly_attached_to_nominal) {
        continue;
      }
    }

    // A one-kanji formal noun cannot head an adjacent kanji compound.  The
    // formal reading remains available at a word boundary (ない+事), while a
    // lexical compound such as 事情 or 事実 keeps its complete search unit.
    if (result.entry->extended_pos == core::ExtendedPOS::NounFormal && result.length == 1 &&
        end_pos < codepoints.size() && normalize::isKanjiCodepoint(codepoints[end_pos]) &&
        !isKanjiRunFollowedByAttributiveNa(codepoints, end_pos)) {
      continue;
    }

    // わりに is an adverb at clause start, but after an attributive の or a
    // finite predicate it is the formal noun わり followed by the case
    // particle に (本の+わりに, 読む+わりに). Before an adjective it instead
    // forms the fixed comparative adverb (年齢の+わりに+若い).
    if (result.entry->pos == core::PartOfSpeech::Adverb && result.entry->lemma == "わりに" && start_pos > 0) {
      if (startsInsideKanjiLedVerb(lattice, codepoints, start_pos)) {
        continue;
      }
      const char32_t preceding = codepoints[start_pos - 1];
      bool followed_by_adjective = false;
      if (end_pos < codepoints.size()) {
        const size_t following_byte_pos = byteOffsetAt(byte_offsets, end_pos);
        for (const auto& following : dict_manager_.lookup(text, following_byte_pos)) {
          if (following.entry != nullptr && following.entry->pos == core::PartOfSpeech::Adjective) {
            followed_by_adjective = true;
            break;
          }
        }
      }
      if (!followed_by_adjective &&
          (preceding == U'の' || preceding == U'る' || preceding == U'く' || preceding == U'む' || preceding == U'ぶ' ||
           preceding == U'ぬ' || preceding == U'す' || preceding == U'つ' || preceding == U'ぐ')) {
        continue;
      }
    }

    if (result.entry->pos == core::PartOfSpeech::Adverb &&
        startsFormalNounParticleAfterPredicate(lattice, dict_manager_, codepoints, start_pos, end_pos)) {
      continue;
    }

    const bool fused_demo_after_te_form = result.length == 2 && codepoints[start_pos] == U'で' &&
                                          codepoints[start_pos + 1] == U'も' &&
                                          hasPrecedingExtendedPOS(lattice, start_pos, core::ExtendedPOS::VerbOnbinkei);
    if (fused_demo_after_te_form) {
      continue;
    }

    if (suppress_prefixed_noun_interior) {
      continue;
    }

    // Prefer the maximal closed-class conjunction at this position: 又は,
    // not 又+は. Shorter prefixes remain available when no longer conjunction
    // matches the input.
    if (result.entry->pos == core::PartOfSpeech::Conjunction && result.length < longest_conjunction) {
      continue;
    }

    // Members of the closed adverb lexicon use maximal matching within their
    // own class (必ずしも, どうしても). Shorter dictionary adverbs remain
    // available whenever no longer adverb actually covers the input.
    if (result.entry->pos == core::PartOfSpeech::Adverb && result.length < longest_adverb) {
      continue;
    }

    // A complete member of the closed potential-benefactive paradigm is
    // authoritative over shorter homographs starting at the same position.
    // This keeps いただけ(る/ない/ます) from reopening as い+た+だけ while
    // leaving every position without that exact closed-class match untouched.
    if (result.length < longest_potential_benefactive) {
      continue;
    }

    // At sentence start, a longer closed-class conjunction takes precedence
    // over a homographic auxiliary prefix.  After a topic/focus particle,
    // suppress only the polite auxiliary prefix: ます requires a verb
    // renyokei, so に+も+まし+て cannot be a polite chain.  Other auxiliaries
    // remain available (本+も+だ+けど).
    const bool sentence_initial_auxiliary = start_pos == 0 && result.entry->pos == core::PartOfSpeech::Auxiliary;
    const bool unlicensed_polite_after_topic =
        result.entry->extended_pos == core::ExtendedPOS::AuxTenseMasu &&
        hasPrecedingExtendedPOS(lattice, start_pos, core::ExtendedPOS::ParticleTopic);
    if ((sentence_initial_auxiliary || unlicensed_polite_after_topic) && result.length < longest_conjunction) {
      continue;
    }

    // Past た/だ is an auxiliary boundary, not part of a dictionary verb
    // token.  Inflected dictionary entries still provide the stem/onbin edge;
    // discard only the fused full-past alternative.
    if (result.entry->extended_pos == core::ExtendedPOS::VerbTaForm &&
        utf8::endsWithAny(result.entry->surface, {"た", "だ"})) {
      continue;
    }

    if (result.entry->pos == core::PartOfSpeech::Verb && utf8::endsWith(result.entry->surface, "ぬ") &&
        result.entry->lemma != result.entry->surface) {
      const std::string stem_surface = std::string(utf8::dropLastChar(result.entry->surface));
      lattice.addEdge(stem_surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(end_pos - 1),
                      core::PartOfSpeech::Verb, getCategoryCost(core::ExtendedPOS::VerbMizenkei),
                      core::LatticeEdge::kFromDictionary, result.entry->lemma, dictionary::ConjugationType::None,
                      core::CandidateOrigin::Dictionary, candidate::kDictionaryOriginConfidence, {},
                      core::ExtendedPOS::VerbMizenkei, "dictionary_classical_negative_stem");
      continue;
    }

    if ((result.entry->pos == core::PartOfSpeech::Verb || result.entry->pos == core::PartOfSpeech::Adjective ||
         result.entry->pos == core::PartOfSpeech::Noun) &&
        result.length > 1 && codepoints[start_pos] == U'は' && codepoints[end_pos - 1] == U'な' &&
        end_pos + 1 < codepoints.size() && codepoints[end_pos] == U'か' && codepoints[end_pos + 1] == U'っ') {
      continue;
    }

    if (result.entry->pos == core::PartOfSpeech::Noun && end_pos < codepoints.size() &&
        codepoints[end_pos - 1] == U'し' && codepoints[end_pos] == U'て') {
      const std::string verb_base = std::string(utf8::dropLastChar(result.entry->surface)) + "す";
      const auto* verb = dict_manager_.lookupExact(verb_base, core::PartOfSpeech::Verb);
      if (verb != nullptr) {
        lattice.addEdge(result.entry->surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(end_pos),
                        core::PartOfSpeech::Verb,
                        getCategoryCost(core::ExtendedPOS::VerbRenyokei) + candidate::kVerifiedTailCompoundVerbBonus +
                            candidate::kVerifiedVerbBonus,
                        core::LatticeEdge::kFromDictionary, verb_base, dictionary::ConjugationType::GodanSa,
                        core::CandidateOrigin::Dictionary, candidate::kDictionaryOriginConfidence, {},
                        core::ExtendedPOS::VerbRenyokei, "dictionary_godan_sa_renyokei");
      }
    }

    if (result.entry->pos == core::PartOfSpeech::Noun && end_pos < codepoints.size() &&
        normalize::isKanjiCodepoint(codepoints[end_pos]) && grammar::isIRowCodepoint(codepoints[end_pos - 1])) {
      const std::string_view base_suffix = grammar::godanBaseSuffixFromIRow(codepoints[end_pos - 1]);
      if (!base_suffix.empty()) {
        const std::string verb_base = std::string(utf8::dropLastChar(result.entry->surface)) + std::string(base_suffix);
        const auto* verb = dict_manager_.lookupExact(verb_base, core::PartOfSpeech::Verb);
        if (verb != nullptr) {
          const auto conj_type = grammar::verbTypeToConjType(
              grammar::verbTypeFromBaseCodepoint(utf8::decodeFirstChar(utf8::lastChar(verb_base))));
          lattice.addEdge(result.entry->surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(end_pos),
                          core::PartOfSpeech::Verb, getCategoryCost(core::ExtendedPOS::VerbRenyokei),
                          core::LatticeEdge::kFromDictionary, verb_base, conj_type, core::CandidateOrigin::Dictionary,
                          candidate::kDictionaryOriginConfidence, {}, core::ExtendedPOS::VerbRenyokei,
                          "dictionary_godan_renyokei_before_predicate");
        }
      }
    }

    if (result.entry->extended_pos == core::ExtendedPOS::AuxInability &&
        !hasPrecedingExtendedPOS(lattice, start_pos, core::ExtendedPOS::VerbRenyokei)) {
      continue;
    }

    // The one-mora classical desiderative auxiliary ま is valid only as the
    // first component of まほしき.  Keeping it context-gated prevents a
    // common temporal adverb such as いま from being split as い+ま.
    if (result.entry->extended_pos == core::ExtendedPOS::AuxDesireTai &&
        grammar::isClassicalDesiderativeMarker(result.entry->surface) &&
        !grammar::startsClassicalDesiderativeSequence(text.substr(byteOffsetAt(byte_offsets, start_pos)))) {
      continue;
    }

    // The classical honorific たまふ is represented as た+ま+ふ.  Its
    // one-mora pieces are admitted only inside that exact auxiliary chain.
    if (result.entry->extended_pos == core::ExtendedPOS::AuxHonorific &&
        grammar::isClassicalHonorificComponent(result.entry->surface)) {
      const bool is_marker = grammar::isClassicalDesiderativeMarker(result.entry->surface);
      const bool has_honorific_start =
          grammar::startsClassicalHonorificSequence(text.substr(byteOffsetAt(byte_offsets, start_pos)));
      const bool follows_honorific_marker = start_pos > 0 && grammar::isClassicalDesiderativeMarker(extractSubstring(
                                                                 codepoints, start_pos - 1, start_pos));
      if ((is_marker && !has_honorific_start) || (!is_marker && !follows_honorific_marker)) {
        continue;
      }
    }

    // The historical terminal component ふ is meaningful only after a kanji
    // stem.  The positional gate retains separations such as 候+ふ and 思+ふ
    // without admitting a free one-mora verb in ordinary hiragana text.
    if (result.entry->pos == core::PartOfSpeech::Verb &&
        result.entry->extended_pos == core::ExtendedPOS::VerbShuushikei &&
        grammar::isClassicalFuruTerminal(result.entry->surface) &&
        (start_pos == 0 || !normalize::isKanjiCodepoint(codepoints[start_pos - 1]))) {
      continue;
    }

    // A dictionary noun homographic with a verb renyokei (知らせ) cannot
    // precede the closed classical honorific auxiliary chain たまふ.  Keep the
    // verb boundary available in that grammatical environment.
    if (result.entry->pos == core::PartOfSpeech::Noun &&
        grammar::startsClassicalHonorificAuxiliaryChain(text.substr(byteOffsetAt(byte_offsets, end_pos)))) {
      continue;
    }

    // In an interrogative emphatic sequence, として is not the viewpoint
    // compound particle: it is と+し+て before the focus particle も.
    if (result.entry->extended_pos == core::ExtendedPOS::ParticleCase &&
        grammar::isQuotativeSuruTeCompoundParticle(result.entry->surface) && end_pos < codepoints.size() &&
        codepoints[end_pos] == U'も' && hasInterrogativeEndingAt(dict_manager_, text, byte_offsets, start_pos)) {
      continue;
    }

    if (result.entry->extended_pos == core::ExtendedPOS::AuxAspectOku && end_pos < codepoints.size() &&
        codepoints[end_pos] == U'う') {
      continue;
    }

    if (result.entry->pos == core::PartOfSpeech::Particle && utf8::equalsAny(result.entry->surface, {"だの"}) &&
        end_pos < codepoints.size() && codepoints[end_pos] == U'は' &&
        hasPrecedingExtendedPOS(lattice, start_pos, core::ExtendedPOS::VerbOnbinkei)) {
      continue;
    }

    // The contrastive nominal construction のでは keeps the nominalizer,
    // copular connective, and topic particle independently searchable.  The
    // causal compound particle ので cannot consume its initial two morae.
    if (result.entry->extended_pos == core::ExtendedPOS::ParticleConj &&
        grammar::isCausalParticleBeforeTopic(result.entry->surface, text.substr(byteOffsetAt(byte_offsets, end_pos)))) {
      continue;
    }

    // Skip a dictionary adjective ending in double い when its final い is the
    // leading い of the receptive auxiliary いただく: the adjective reading
    // would fuse a wa-row renyokei's い with the auxiliary's onset
    // (お使いいただく → 使い+いただく, not 使+いい+ただく). Plain いい in
    // predicate/attributive position is untouched (no ただ+inflection follows).
    if (result.entry->pos == core::PartOfSpeech::Adjective && result.length >= 2 && codepoints[end_pos - 1] == U'い' &&
        codepoints[end_pos - 2] == U'い' && verb_helpers::itadakuParadigmStartsAt(codepoints, end_pos - 1)) {
      continue;
    }

    // Create edge
    // v0.8: flags derived from extended_pos, cost from getCategoryCost()
    uint8_t flags = core::LatticeEdge::kFromDictionary;
    if (result.from_user_dict) {
      flags |= core::LatticeEdge::kFromUserDict;
    }
    if (result.entry->extended_pos == core::ExtendedPOS::NounFormal) {
      flags |= core::LatticeEdge::kIsFormalNoun;
    }
    // Note: is_low_info removed - can be derived from extended_pos if needed

    // Cost is now derived from ExtendedPOS via getCategoryCost()
    float cost = analysis::getCategoryCost(result.entry->extended_pos);

    if (result.entry->pos == core::PartOfSpeech::Noun && result.length >= 2 &&
        grammar::isAllKanji(result.entry->surface)) {
      cost += candidate::kVerifiedMultiCharacterNounBonus;
      flags |= core::LatticeEdge::kHasCustomCost;
    }

    if (result.entry->extended_pos == core::ExtendedPOS::PronounInterrogative &&
        result.length >= longest_interjection) {
      cost += candidate::kInterrogativePronounBonus;
      flags |= core::LatticeEdge::kHasCustomCost;
    }

    if (result.entry->pos == core::PartOfSpeech::Verb &&
        result.entry->extended_pos == core::ExtendedPOS::VerbShuushikei &&
        utf8::endsWith(result.entry->surface, "せる")) {
      cost += candidate::kLexicalSeruBaseBonus;
      flags |= core::LatticeEdge::kHasCustomCost;
    }

    if (result.entry->extended_pos == core::ExtendedPOS::NounFormal && end_pos + 1 < codepoints.size() &&
        codepoints[end_pos] == U'で' && (codepoints[end_pos + 1] == U'は' || codepoints[end_pos + 1] == U'も')) {
      cost += candidate::kFormalNounCopularTopicBonus;
      flags |= core::LatticeEdge::kHasCustomCost;
    }

    if (result.entry->pos == core::PartOfSpeech::Adverb && end_pos + 1 < codepoints.size() &&
        codepoints[end_pos] == U'な' && codepoints[end_pos + 1] == U'の') {
      cost += candidate::kAdverbExplanatoryCopulaBonus;
      flags |= core::LatticeEdge::kHasCustomCost;
    }

    // In the explanatory interrogative opener, an adverb ends before the
    // sentence-final question particle and quotative predicate (なぜ+かというと).
    // Keep this productive boundary available instead of preferring an
    // accidental lexicalized adverb that absorbs か.
    if (result.entry->pos == core::PartOfSpeech::Adverb &&
        grammar::startsInterrogativeQuoteIntroduction(text.substr(byteOffsetAt(byte_offsets, end_pos)))) {
      cost += candidate::kInterrogativeQuoteIntroductionBonus;
      flags |= core::LatticeEdge::kHasCustomCost;
    }

    // A dictionary-backed mixed-script noun can be a lexicalized compound
    // containing an inflected verbal segment. Prefer that registered search
    // unit over a coincidental inflection path.
    if (result.entry->pos == core::PartOfSpeech::Noun && result.length >= 3) {
      bool has_kanji = false;
      bool has_hiragana = false;
      for (size_t idx = start_pos; idx < end_pos; ++idx) {
        has_kanji = has_kanji || normalize::isKanjiCodepoint(codepoints[idx]);
        has_hiragana = has_hiragana || kana::isHiraganaCodepoint(codepoints[idx]);
      }
      const bool ichidan_predicate_continuation =
          has_kanji && has_hiragana && end_pos < codepoints.size() &&
          dict_manager_.lookupExact(result.entry->surface + "る", core::PartOfSpeech::Verb) != nullptr &&
          (codepoints[end_pos] == U'て' ||
           (end_pos + 1 < codepoints.size() && codepoints[end_pos] == U'ら' && codepoints[end_pos + 1] == U'れ'));
      if (has_kanji && has_hiragana && !ichidan_predicate_continuation) {
        cost += candidate::kLexicalizedMixedScriptNounBonus;
        flags |= core::LatticeEdge::kHasCustomCost;
      }
    }

    const bool is_fused_demo = result.length == 2 && end_pos >= 2 && codepoints[end_pos - 2] == U'で' &&
                               codepoints[end_pos - 1] == U'も' &&
                               (result.entry->extended_pos == core::ExtendedPOS::ParticleAdverbial ||
                                result.entry->extended_pos == core::ExtendedPOS::Conjunction);
    if (is_fused_demo && verb_helpers::naiNegativeFollowsAt(codepoints, end_pos) &&
        hasPrecedingNominal(lattice, start_pos)) {
      continue;
    }
    if (is_fused_demo && verb_helpers::naiNegativeFollowsAt(codepoints, end_pos)) {
      cost += candidate::kFusedDemoNegativePenalty;
      flags |= core::LatticeEdge::kHasCustomCost;
    }

    // A bare え-row dict-verb imperative closing a clause (書け, 止まれ) is the 命令形 of the
    // base verb, not the potential-verb renyokei; without this the spurious 未然+受身れ split
    // (止ま+れ, lemma 止む) wins. Gated so any auxiliary/ば continuation (走れます/走れば/止まれる)
    // leaves the connection scores byte-identical.
    if (result.entry->pos == core::PartOfSpeech::Verb &&
        (result.entry->extended_pos == core::ExtendedPOS::VerbKateikei ||
         result.entry->extended_pos == core::ExtendedPOS::VerbMeireikei) &&
        grammar::containsKanji(result.entry->surface)) {
      const bool continues = end_pos < codepoints.size() &&
                             (codepoints[end_pos] == U'ば' ||
                              verb_helpers::isPassiveAuxContinuation(codepoints, end_pos, /*strict_masu=*/true));
      if (!continues) {
        cost += candidate::verb_cost::kImperativeFinalBonus;
        // Flag the tuned cost so the scorer honours it even when it lands on exactly 0.0
        // (0.0 is otherwise read as "unset" and falls back to the category cost).
        flags |= core::LatticeEdge::kHasCustomCost;
      }
    }

    // A single-token godan potential (読める) is analyzed as an independent ichidan verb, so its
    // lemma is its surface. The boost lets that dict form beat an unrelated ichidan reading. Excluded: independent
    // ichidan verbs (割れる==割れる have lemma == surface, and 自他 pairs like 切れる are registered
    // as ICHIDAN so no potential form is generated); られる passive/potential (来られる); and
    // irregular L1 forms whose lemma differs for other reasons (す→する) that do not end え-row + る.
    const bool is_godan_potential =
        result.entry->pos == core::PartOfSpeech::Verb &&
        result.entry->extended_pos == core::ExtendedPOS::VerbShuushikei &&
        std::string_view(result.entry->lemma) != std::string_view(result.entry->surface) &&
        utf8::endsWith(result.entry->surface, "る") && !utf8::endsWith(result.entry->surface, "られる") &&
        grammar::endsWithERow(
            std::string_view(result.entry->surface).substr(0, result.entry->surface.size() - core::kJapaneseCharBytes));
    if (is_godan_potential) {
      cost += candidate::verb_cost::kImperativeFinalBonus;
      flags |= core::LatticeEdge::kHasCustomCost;
    }

    const std::string_view lemma =
        is_godan_potential ? std::string_view(result.entry->surface) : std::string_view(result.entry->lemma);
    dictionary::ConjugationType conj_type = dictionary::ConjugationType::None;
    // Dictionary entries deliberately omit conjugation metadata. For a verb
    // whose dictionary-form ending uniquely identifies a Godan row, preserve
    // that information on the lattice edge so a low-cost dictionary match does
    // not discard the type carried by an equivalent generated candidate.
    if (result.entry->pos == core::PartOfSpeech::Verb && !lemma.empty()) {
      const char32_t final_cp = utf8::decodeFirstChar(utf8::lastChar(lemma));
      conj_type = grammar::verbTypeToConjType(grammar::verbTypeFromBaseCodepoint(final_cp));
    }

    // A godan e-row form followed by past た cannot be a conditional or an
    // imperative; it is the continuative stem of the derived potential verb
    // (書け+た, 見渡せ+た). Keep the dictionary's conditional edge for ば,
    // and add this context-licensed potential edge without registering every
    // productive potential form as a separate verb.
    if (result.entry->pos == core::PartOfSpeech::Verb &&
        result.entry->extended_pos == core::ExtendedPOS::VerbKateikei && end_pos < codepoints.size() &&
        codepoints[end_pos] == U'た' && grammar::endsWithERow(result.entry->surface)) {
      lattice.addEdge(result.entry->surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(end_pos),
                      core::PartOfSpeech::Verb, getCategoryCost(core::ExtendedPOS::VerbRenyokei),
                      core::LatticeEdge::kFromDictionary, std::string(result.entry->surface) + "る",
                      dictionary::ConjugationType::Ichidan, core::CandidateOrigin::Dictionary,
                      candidate::kDictionaryOriginConfidence, {}, core::ExtendedPOS::VerbRenyokei,
                      "dictionary_potential_renyokei_before_past");
    }
    lattice.addEdge(result.entry->surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(end_pos),
                    result.entry->pos, cost, flags, lemma, conj_type, core::CandidateOrigin::Dictionary, 1.0F, {},
                    result.entry->extended_pos, "dict");

    // Extend predicates and adverbs with colloquial emphasis
    // (ですっ, 行くーー, きたあああ). Unknown candidates use the same matcher.
    if (end_pos < codepoints.size() &&
        (result.entry->pos == core::PartOfSpeech::Verb || result.entry->pos == core::PartOfSpeech::Auxiliary ||
         result.entry->pos == core::PartOfSpeech::Adjective || result.entry->pos == core::PartOfSpeech::Adverb)) {
      // A dictionary irrealis stem cannot absorb っ before て/た as emphasis:
      // 染まっ+て belongs to the GodanRa verb 染まる, not 染ま(染む)+っ+て.
      const bool irrealis_before_te_or_ta =
          result.entry->extended_pos == core::ExtendedPOS::VerbMizenkei && end_pos + 1 < codepoints.size() &&
          codepoints[end_pos] == core::hiragana::kSmallTsu &&
          (codepoints[end_pos + 1] == core::hiragana::kTe || codepoints[end_pos + 1] == core::hiragana::kTa);
      const auto emphatic = irrealis_before_te_or_ta
                                ? verb_helpers::EmphaticSuffixMatch{}
                                : verb_helpers::matchEmphaticSuffix(codepoints, end_pos, result.entry->pos,
                                                                    verb_helpers::SokuonOnsetPolicy::DictionaryEntry);
      if (!emphatic.empty()) {
        // Determine extended_pos for emphatic form
        // Sokuon-ending verb forms should be VerbOnbinkei (音便形)
        core::ExtendedPOS emphatic_epos = result.entry->extended_pos;
        if (result.entry->pos == core::PartOfSpeech::Verb && emphatic.suffix == "っ") {
          // E.g., い(連用形) + っ → いっ(音便形) for と+いっ+て pattern
          emphatic_epos = core::ExtendedPOS::VerbOnbinkei;
        }

        const std::string emphatic_surface = result.entry->surface + emphatic.suffix;
        const bool preserves_emphatic_surface =
            result.entry->pos == core::PartOfSpeech::Auxiliary ||
            (result.entry->pos == core::PartOfSpeech::Adjective &&
             (emphatic.standard_char_count >= 2 || emphatic.repeated_vowel_count >= 3));
        const std::string_view dictionary_lemma = result.entry->lemma.empty() ? std::string_view(result.entry->surface)
                                                                              : std::string_view(result.entry->lemma);
        const std::string_view emphatic_lemma =
            preserves_emphatic_surface ? std::string_view(emphatic_surface) : dictionary_lemma;
        lattice.addEdge(emphatic_surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(emphatic.end),
                        result.entry->pos, cost + verb_helpers::emphaticCostAdjustment(emphatic), flags, emphatic_lemma,
                        dictionary::ConjugationType::None, core::CandidateOrigin::Dictionary, 1.0F, {}, emphatic_epos,
                        "dict_emphatic");
      }
    }
  }

  tokenizer_dictionary_detail::appendSpecialGrammarCandidates(lattice, text, codepoints, start_pos, byte_pos);
}

}  // namespace suzume::analysis
