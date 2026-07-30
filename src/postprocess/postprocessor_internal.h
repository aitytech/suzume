#ifndef SUZUME_POSTPROCESS_POSTPROCESSOR_INTERNAL_H_
#define SUZUME_POSTPROCESS_POSTPROCESSOR_INTERNAL_H_

#include <vector>

#include "core/morpheme.h"

namespace suzume::postprocess {

// Resolver stage boundaries used by Postprocessor::process(). Their order is a
// behavioral contract: the pre-prefix stage establishes grammatical roles,
// PREFIX+VERB nominalization consumes them, and the post-prefix/final stages
// observe the resulting token categories.
void resolvePrePrefixMorphemeRoles(std::vector<core::Morpheme>& morphemes,
                                   const dictionary::DictionaryManager* dict_manager);
void resolvePostPrefixMorphemeRoles(std::vector<core::Morpheme>& morphemes);
void resolveFinalMorphemeRoles(std::vector<core::Morpheme>& morphemes,
                               const dictionary::DictionaryManager* dict_manager);

}  // namespace suzume::postprocess

#endif  // SUZUME_POSTPROCESS_POSTPROCESSOR_INTERNAL_H_
