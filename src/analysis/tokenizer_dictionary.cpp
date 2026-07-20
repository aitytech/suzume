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
#include "join_candidates.h"
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
  for (size_t edge_start = 0; edge_start < start_pos; ++edge_start) {
    for (const uint32_t edge_id : lattice.edgeIdsAt(edge_start)) {
      const auto& edge = lattice.getEdge(edge_id);
      if (edge.end == start_pos && edge.extended_pos == core::ExtendedPOS::VerbShuushikei) {
        return true;
      }
    }
  }
  return false;
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

  size_t longest_conjunction = 0;
  size_t longest_interjection = 0;
  for (const auto& result : results) {
    if (result.entry != nullptr && result.entry->pos == core::PartOfSpeech::Conjunction) {
      longest_conjunction = std::max(longest_conjunction, result.length);
    }
    if (result.entry != nullptr && result.entry->pos == core::PartOfSpeech::Interjection) {
      longest_interjection = std::max(longest_interjection, result.length);
    }
  }

  for (const auto& result : results) {
    if (result.entry == nullptr) {
      continue;
    }

    // Calculate end position in characters before context-sensitive candidate
    // guards below inspect the following lexical head.
    size_t end_pos = start_pos + result.length;

    if (result.entry->pos == core::PartOfSpeech::Adverb && result.length == 2 &&
        startsIruPoliteFormAt(codepoints, start_pos)) {
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

    if (suppress_prefixed_noun_interior) {
      continue;
    }

    // Prefer the maximal closed-class conjunction at this position: 又は,
    // not 又+は. Shorter prefixes remain available when no longer conjunction
    // matches the input.
    if (result.entry->pos == core::PartOfSpeech::Conjunction && result.length < longest_conjunction) {
      continue;
    }

    // At sentence start, a longer closed-class conjunction takes precedence
    // over a homographic auxiliary prefix (だけども, だからこそ).
    if (start_pos == 0 && result.entry->pos == core::PartOfSpeech::Auxiliary && result.length < longest_conjunction) {
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
      if (has_kanji && has_hiragana) {
        cost += candidate::kLexicalizedMixedScriptNounBonus;
        flags |= core::LatticeEdge::kHasCustomCost;
      }
    }

    const bool is_fused_demo = result.length == 2 && end_pos >= 2 && codepoints[end_pos - 2] == U'で' &&
                               codepoints[end_pos - 1] == U'も' &&
                               (result.entry->extended_pos == core::ExtendedPOS::ParticleAdverbial ||
                                result.entry->extended_pos == core::ExtendedPOS::Conjunction);
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
    lattice.addEdge(result.entry->surface, static_cast<uint32_t>(start_pos), static_cast<uint32_t>(end_pos),
                    result.entry->pos, cost, flags, lemma, conj_type, core::CandidateOrigin::Dictionary, 1.0F, {},
                    result.entry->extended_pos, "dict");

    // Extend verbs, auxiliaries, and adjectives with colloquial emphasis
    // (ですっ, 行くーー, きたあああ). Unknown candidates use the same matcher.
    if (end_pos < codepoints.size() &&
        (result.entry->pos == core::PartOfSpeech::Verb || result.entry->pos == core::PartOfSpeech::Auxiliary ||
         result.entry->pos == core::PartOfSpeech::Adjective)) {
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
        const std::string_view emphatic_lemma =
            preserves_emphatic_surface ? std::string_view(emphatic_surface) : std::string_view(result.entry->lemma);
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
