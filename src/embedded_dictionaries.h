#ifndef SUZUME_EMBEDDED_DICTIONARIES_H_
#define SUZUME_EMBEDDED_DICTIONARIES_H_

#include <cstddef>
#include <cstdint>

namespace suzume::embedded {

extern const uint8_t kCoreDictionary[];
extern const size_t kCoreDictionarySize;
extern const uint8_t kUserDictionary[];
extern const size_t kUserDictionarySize;

}  // namespace suzume::embedded

#endif  // SUZUME_EMBEDDED_DICTIONARIES_H_
