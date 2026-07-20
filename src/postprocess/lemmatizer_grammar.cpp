#include "grammar/inflection_scorer_constants.h"
#include "postprocess/lemmatizer.h"

namespace suzume::postprocess {

namespace {

constexpr float kUnverifiedLemmaConfidenceThreshold = 0.5F;

}  // namespace

std::string Lemmatizer::lemmatizeByGrammar(std::string_view surface, core::PartOfSpeech pos,
                                           dictionary::ConjugationType conj_type) const {
  if (dict_manager_ != nullptr) {
    for (const auto& result : dict_manager_->lookup(surface, 0)) {
      if (result.entry != nullptr && result.entry->surface == surface && result.entry->lemma == surface &&
          (result.entry->pos == core::PartOfSpeech::Verb || result.entry->pos == core::PartOfSpeech::Adjective)) {
        return std::string(surface);
      }
    }
  }

  const auto& all_candidates = inflection_.analyze(surface);
  if (all_candidates.empty()) {
    return std::string(surface);
  }

  std::vector<grammar::InflectionCandidate> filtered_storage;
  const std::vector<grammar::InflectionCandidate>* candidates = &all_candidates;
  if (pos == core::PartOfSpeech::Adjective) {
    for (const auto& candidate : *candidates) {
      if (candidate.verb_type == grammar::VerbType::IAdjective) {
        filtered_storage.push_back(candidate);
      }
    }
    if (filtered_storage.empty()) {
      return std::string(surface);
    }
    candidates = &filtered_storage;
  }

  if (conj_type != dictionary::ConjugationType::None) {
    std::vector<grammar::InflectionCandidate> conj_filtered;
    for (const auto& candidate : *candidates) {
      if (grammar::verbTypeToConjType(candidate.verb_type) == conj_type) {
        conj_filtered.push_back(candidate);
      }
    }
    if (!conj_filtered.empty()) {
      filtered_storage = std::move(conj_filtered);
      candidates = &filtered_storage;
    }
  }

  if (dict_manager_ != nullptr) {
    for (const auto& candidate : *candidates) {
      if (candidate.confidence > grammar::inflection::kConfidenceFloor && verifyCandidateWithDictionary(candidate)) {
        return candidate.base_form;
      }
    }
  }

  const auto& best = candidates->front();
  if (!best.base_form.empty() && best.confidence >= kUnverifiedLemmaConfidenceThreshold) {
    return best.base_form;
  }
  return std::string(surface);
}

}  // namespace suzume::postprocess
