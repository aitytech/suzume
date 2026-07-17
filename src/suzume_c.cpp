/**
 * @file suzume_c.cpp
 * @brief C API implementation for Suzume
 */

#include "suzume/suzume_c.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>
#include <optional>
#include <string>
#include <string_view>

#include "grammar/conjugation.h"
#include "normalize/utf8.h"
#include "postprocess/tag_generator.h"
#include "suzume.h"

// Internal handle structure
struct SuzumeHandle {
  suzume::Suzume instance;

  SuzumeHandle() : instance() {}
  explicit SuzumeHandle(const suzume::SuzumeOptions& opts) : instance(opts) {}
};

namespace {

thread_local std::string last_error;

void clearLastError() {
  last_error.clear();
}

void setLastError(std::string_view message) {
  last_error = message;
}

void setLastErrorFromException() {
#if defined(__cpp_exceptions) && __cpp_exceptions
  try {
    throw;
  } catch (const std::exception& err) {
    setLastError(err.what());
  } catch (...) {
    setLastError("Unknown C API error");
  }
#else
  setLastError("Unknown C API error");
#endif
}

// Exception firewall for the C ABI. With exceptions enabled every entry point
// turns an unexpected C++ exception into a last-error string plus an error
// return; with -fno-exceptions the guards compile away — nothing in the core
// throws, and an allocation failure terminates as is standard for such builds.
#if defined(__cpp_exceptions) && __cpp_exceptions
#define SUZUME_C_TRY try
#define SUZUME_C_CATCH(fallback) \
  catch (...) {                  \
    setLastErrorFromException(); \
    fallback;                    \
  }
#else
#define SUZUME_C_TRY
#define SUZUME_C_CATCH(fallback)
#endif

constexpr size_t alignUp(size_t value, size_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

char* copyStringToArena(std::string_view str, char*& cursor) {
  char* result = cursor;
  std::memcpy(result, str.data(), str.size());
  result[str.size()] = '\0';
  cursor += str.size() + 1;
  return result;
}

std::optional<suzume::core::AnalysisMode> parseAnalysisMode(int mode) {
  switch (mode) {
    case 1:
      return suzume::core::AnalysisMode::Search;
    case 2:
      return suzume::core::AnalysisMode::Split;
    case 0:
      return suzume::core::AnalysisMode::Normal;
    default:
      return std::nullopt;
  }
}

suzume_tags_t* makeTagsResult(const std::vector<suzume::postprocess::TagEntry>& tags) {
  size_t strings_size = 0;
  for (const auto& tag : tags) {
    strings_size += tag.tag.size() + 1;
  }

  const size_t tags_offset = alignUp(sizeof(suzume_tags_t), alignof(char*));
  const size_t pos_offset = tags_offset + tags.size() * sizeof(char*);
  const size_t strings_offset = pos_offset + tags.size() * sizeof(const char*);
  auto* memory = static_cast<std::byte*>(::operator new(strings_offset + strings_size));
  auto* result = reinterpret_cast<suzume_tags_t*>(memory);
  result->count = tags.size();
  result->tags = tags.empty() ? nullptr : reinterpret_cast<char**>(memory + tags_offset);
  result->pos = tags.empty() ? nullptr : reinterpret_cast<const char**>(memory + pos_offset);

  char* cursor = reinterpret_cast<char*>(memory + strings_offset);
  for (size_t idx = 0; idx < tags.size(); ++idx) {
    result->tags[idx] = copyStringToArena(tags[idx].tag, cursor);
    // POS names are immutable string literals owned by the core type table.
    result->pos[idx] = suzume::core::posToString(tags[idx].pos).data();
  }
  return result;
}

}  // namespace

extern "C" {

SUZUME_EXPORT suzume_t suzume_create(void) {
  clearLastError();
  SUZUME_C_TRY {
    return new SuzumeHandle();
  }
  SUZUME_C_CATCH(return nullptr)
}

SUZUME_EXPORT void suzume_init_extended_options(suzume_extended_options_t* options) {
  if (options == nullptr) {
    return;
  }
  options->preserve_vu = 1;
  options->preserve_case = 1;
  options->preserve_symbols = 0;
  options->mode = 0;
  options->lemmatize = 1;
  options->merge_compounds = 0;
}

SUZUME_EXPORT void suzume_init_tag_options(suzume_tag_options_t* options) {
  if (options == nullptr) {
    return;
  }
  options->pos_filter = 0;
  options->exclude_basic = 0;
  options->use_lemma = 1;
  options->min_length = 2;
  options->max_tags = 0;
  options->exclude_particles = 1;
  options->exclude_auxiliaries = 1;
  options->exclude_formal_nouns = 1;
  options->exclude_low_info = 1;
  options->remove_duplicates = 1;
}

SUZUME_EXPORT suzume_t suzume_create_with_extended_options(const suzume_extended_options_t* options) {
  clearLastError();
  SUZUME_C_TRY {
    suzume::SuzumeOptions opts;
    if (options != nullptr) {
      opts.normalize_options.preserve_vu = (options->preserve_vu != 0);
      opts.normalize_options.preserve_case = (options->preserve_case != 0);
      opts.remove_symbols = (options->preserve_symbols == 0);
      auto mode = parseAnalysisMode(options->mode);
      if (!mode.has_value()) {
        setLastError("suzume_create_with_extended_options: invalid mode");
        return nullptr;
      }
      opts.mode = *mode;
      opts.lemmatize = (options->lemmatize != 0);
      opts.merge_compounds = (options->merge_compounds != 0);
    }
    return new SuzumeHandle(opts);
  }
  SUZUME_C_CATCH(return nullptr)
}

SUZUME_EXPORT void suzume_destroy(suzume_t handle) {
  delete handle;
}

SUZUME_EXPORT suzume_result_t* suzume_analyze(suzume_t handle, const char* text) {
  if (handle == nullptr || text == nullptr) {
    setLastError("suzume_analyze: null handle or text");
    return nullptr;
  }

  // Distinguish malformed input from a legitimately empty result: the public
  // analyze() returns an empty vector for both, so validate at the boundary.
  if (!suzume::normalize::isValidUtf8(text)) {
    setLastError("suzume_analyze: invalid UTF-8 input");
    return nullptr;
  }

  clearLastError();
  SUZUME_C_TRY {
    auto morphemes = handle->instance.analyze(text);

    size_t strings_size = 0;
    for (const auto& morph : morphemes) {
      strings_size += morph.surface.size() + 1;
      strings_size += morph.getLemma().size() + 1;
    }

    const size_t morphemes_offset = alignUp(sizeof(suzume_result_t), alignof(suzume_morpheme_t));
    const size_t strings_offset = morphemes_offset + morphemes.size() * sizeof(suzume_morpheme_t);
    auto* memory = static_cast<std::byte*>(::operator new(strings_offset + strings_size));
    auto* result = reinterpret_cast<suzume_result_t*>(memory);
    result->count = morphemes.size();
    result->morphemes = morphemes.empty() ? nullptr : reinterpret_cast<suzume_morpheme_t*>(memory + morphemes_offset);
    if (result->morphemes != nullptr) {
      std::memset(result->morphemes, 0, morphemes.size() * sizeof(suzume_morpheme_t));
    }

    char* cursor = reinterpret_cast<char*>(memory + strings_offset);
    for (size_t idx = 0; idx < morphemes.size(); ++idx) {
      const auto& morph = morphemes[idx];

      result->morphemes[idx].surface = copyStringToArena(morph.surface, cursor);

      auto pos_str = suzume::core::posToString(morph.pos);
      result->morphemes[idx].pos = pos_str.data();

      auto lemma = morph.getLemma();
      result->morphemes[idx].base_form = copyStringToArena(lemma, cursor);

      // Japanese POS
      auto pos_ja_str = suzume::core::posToJapanese(morph.pos);
      result->morphemes[idx].pos_ja = pos_ja_str.data();

      // Conjugation type and form (for verbs and adjectives)
      if (morph.pos == suzume::core::PartOfSpeech::Verb || morph.pos == suzume::core::PartOfSpeech::Adjective) {
        auto verb_type = suzume::grammar::conjTypeToVerbType(morph.conj_type);
        auto conj_type_str = suzume::grammar::verbTypeToJapanese(verb_type);
        result->morphemes[idx].conj_type = conj_type_str.empty() ? nullptr : conj_type_str.data();

        auto conj_form_str = suzume::grammar::conjFormToJapanese(morph.conj_form);
        result->morphemes[idx].conj_form = conj_form_str.empty() ? nullptr : conj_form_str.data();
      } else {
        result->morphemes[idx].conj_type = nullptr;
        result->morphemes[idx].conj_form = nullptr;
      }

      // Extended POS
      auto epos_str = suzume::core::extendedPosToString(morph.extended_pos);
      result->morphemes[idx].extended_pos = epos_str.data();

      result->morphemes[idx].start = morph.start;
      result->morphemes[idx].end = morph.end;
      result->morphemes[idx].is_user_dict = morph.features.is_user_dict ? 1 : 0;
      result->morphemes[idx].is_formal_noun = morph.features.is_formal_noun ? 1 : 0;
      result->morphemes[idx].is_low_info = morph.features.is_low_info ? 1 : 0;
      result->morphemes[idx].is_unknown = morph.is_unknown ? 1 : 0;
      result->morphemes[idx].is_from_dictionary = morph.is_from_dictionary ? 1 : 0;
      result->morphemes[idx].score = morph.features.score;
    }

    return result;
  }
  SUZUME_C_CATCH(return nullptr)
}

SUZUME_EXPORT void suzume_result_free(suzume_result_t* result) {
  if (result == nullptr) {
    return;
  }

  ::operator delete(result);
}

SUZUME_EXPORT suzume_tags_t* suzume_generate_tags(suzume_t handle, const char* text) {
  if (handle == nullptr || text == nullptr) {
    setLastError("suzume_generate_tags: null handle or text");
    return nullptr;
  }

  clearLastError();
  SUZUME_C_TRY {
    return makeTagsResult(handle->instance.generateTags(text));
  }
  SUZUME_C_CATCH(return nullptr)
}

SUZUME_EXPORT suzume_tags_t* suzume_generate_tags_with_options(suzume_t handle, const char* text,
                                                               const suzume_tag_options_t* options) {
  if (handle == nullptr || text == nullptr || options == nullptr) {
    setLastError("suzume_generate_tags_with_options: null handle, text, or options");
    return nullptr;
  }

  clearLastError();
  SUZUME_C_TRY {
    suzume::postprocess::TagGeneratorOptions tag_opts;
    tag_opts.pos_filter = options->pos_filter;
    tag_opts.exclude_basic = (options->exclude_basic != 0);
    tag_opts.use_lemma = (options->use_lemma != 0);
    tag_opts.min_tag_length = options->min_length;
    tag_opts.max_tags = options->max_tags;
    tag_opts.exclude_particles = (options->exclude_particles != 0);
    tag_opts.exclude_auxiliaries = (options->exclude_auxiliaries != 0);
    tag_opts.exclude_formal_nouns = (options->exclude_formal_nouns != 0);
    tag_opts.exclude_low_info = (options->exclude_low_info != 0);
    tag_opts.remove_duplicates = (options->remove_duplicates != 0);

    return makeTagsResult(handle->instance.generateTags(text, tag_opts));
  }
  SUZUME_C_CATCH(return nullptr)
}

SUZUME_EXPORT void suzume_tags_free(suzume_tags_t* tags) {
  if (tags == nullptr) {
    return;
  }

  ::operator delete(tags);
}

SUZUME_EXPORT int suzume_load_user_dict(suzume_t handle, const char* data, size_t size) {
  if (handle == nullptr || data == nullptr) {
    setLastError("suzume_load_user_dict: null handle or data");
    return 0;
  }

  clearLastError();
  SUZUME_C_TRY {
    auto result = handle->instance.loadUserDictionaryFromMemoryResult(data, size);
    if (result.hasValue()) {
      return 1;
    }
    setLastError(result.error().message);
    return 0;
  }
  SUZUME_C_CATCH(return 0)
}

SUZUME_EXPORT int suzume_load_binary_dict(suzume_t handle, const uint8_t* data, size_t size) {
  if (handle == nullptr || data == nullptr) {
    setLastError("suzume_load_binary_dict: null handle or data");
    return 0;
  }

  clearLastError();
  SUZUME_C_TRY {
    auto result = handle->instance.loadBinaryDictionaryResult(data, size);
    if (result.hasValue()) {
      return 1;
    }
    setLastError(result.error().message);
    return 0;
  }
  SUZUME_C_CATCH(return 0)
}

SUZUME_EXPORT const char* suzume_version(void) {
  static std::string version_str = suzume::Suzume::version();
  return version_str.c_str();
}

SUZUME_EXPORT const char* suzume_last_error(void) {
  return last_error.c_str();
}

SUZUME_EXPORT size_t suzume_dictionary_warning_count(suzume_t handle) {
  if (handle == nullptr) {
    return 0;
  }
  return handle->instance.dictionaryWarnings().size();
}

SUZUME_EXPORT const char* suzume_dictionary_warning(suzume_t handle, size_t index) {
  if (handle == nullptr) {
    setLastError("suzume_dictionary_warning: null handle");
    return nullptr;
  }
  const auto& warnings = handle->instance.dictionaryWarnings();
  if (index >= warnings.size()) {
    setLastError("suzume_dictionary_warning: index out of range");
    return nullptr;
  }
  clearLastError();
  thread_local std::string warning;
  warning = warnings[index];
  return warning.c_str();
}

SUZUME_EXPORT size_t suzume_sizeof_result(void) {
  return sizeof(suzume_result_t);
}

SUZUME_EXPORT size_t suzume_sizeof_morpheme(void) {
  return sizeof(suzume_morpheme_t);
}

SUZUME_EXPORT size_t suzume_sizeof_tags(void) {
  return sizeof(suzume_tags_t);
}

SUZUME_EXPORT size_t suzume_sizeof_tag_options(void) {
  return sizeof(suzume_tag_options_t);
}

SUZUME_EXPORT size_t suzume_sizeof_extended_options(void) {
  return sizeof(suzume_extended_options_t);
}

SUZUME_EXPORT size_t suzume_offsetof_result(uint32_t field) {
  switch (field) {
    case 0:
      return offsetof(suzume_result_t, morphemes);
    case 1:
      return offsetof(suzume_result_t, count);
    default:
      return static_cast<size_t>(-1);
  }
}

SUZUME_EXPORT size_t suzume_offsetof_morpheme(uint32_t field) {
  switch (field) {
    case 0:
      return offsetof(suzume_morpheme_t, surface);
    case 1:
      return offsetof(suzume_morpheme_t, pos);
    case 2:
      return offsetof(suzume_morpheme_t, base_form);
    case 3:
      return offsetof(suzume_morpheme_t, pos_ja);
    case 4:
      return offsetof(suzume_morpheme_t, conj_type);
    case 5:
      return offsetof(suzume_morpheme_t, conj_form);
    case 6:
      return offsetof(suzume_morpheme_t, extended_pos);
    case 7:
      return offsetof(suzume_morpheme_t, start);
    case 8:
      return offsetof(suzume_morpheme_t, end);
    case 9:
      return offsetof(suzume_morpheme_t, is_user_dict);
    case 10:
      return offsetof(suzume_morpheme_t, is_formal_noun);
    case 11:
      return offsetof(suzume_morpheme_t, is_low_info);
    case 12:
      return offsetof(suzume_morpheme_t, is_unknown);
    case 13:
      return offsetof(suzume_morpheme_t, is_from_dictionary);
    case 14:
      return offsetof(suzume_morpheme_t, score);
    default:
      return static_cast<size_t>(-1);
  }
}

SUZUME_EXPORT size_t suzume_offsetof_tags(uint32_t field) {
  switch (field) {
    case 0:
      return offsetof(suzume_tags_t, tags);
    case 1:
      return offsetof(suzume_tags_t, pos);
    case 2:
      return offsetof(suzume_tags_t, count);
    default:
      return static_cast<size_t>(-1);
  }
}

SUZUME_EXPORT size_t suzume_offsetof_tag_options(uint32_t field) {
  switch (field) {
    case 0:
      return offsetof(suzume_tag_options_t, pos_filter);
    case 1:
      return offsetof(suzume_tag_options_t, exclude_basic);
    case 2:
      return offsetof(suzume_tag_options_t, use_lemma);
    case 3:
      return offsetof(suzume_tag_options_t, min_length);
    case 4:
      return offsetof(suzume_tag_options_t, max_tags);
    case 5:
      return offsetof(suzume_tag_options_t, exclude_particles);
    case 6:
      return offsetof(suzume_tag_options_t, exclude_auxiliaries);
    case 7:
      return offsetof(suzume_tag_options_t, exclude_formal_nouns);
    case 8:
      return offsetof(suzume_tag_options_t, exclude_low_info);
    case 9:
      return offsetof(suzume_tag_options_t, remove_duplicates);
    default:
      return static_cast<size_t>(-1);
  }
}

SUZUME_EXPORT size_t suzume_offsetof_extended_options(uint32_t field) {
  switch (field) {
    case 0:
      return offsetof(suzume_extended_options_t, preserve_vu);
    case 1:
      return offsetof(suzume_extended_options_t, preserve_case);
    case 2:
      return offsetof(suzume_extended_options_t, preserve_symbols);
    case 3:
      return offsetof(suzume_extended_options_t, mode);
    case 4:
      return offsetof(suzume_extended_options_t, lemmatize);
    case 5:
      return offsetof(suzume_extended_options_t, merge_compounds);
    default:
      return static_cast<size_t>(-1);
  }
}

SUZUME_EXPORT void* suzume_malloc(size_t size) {
  return std::malloc(size);
}

SUZUME_EXPORT void suzume_free(void* ptr) {
  std::free(ptr);
}

}  // extern "C"
