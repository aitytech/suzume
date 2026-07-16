/**
 * @file suzume_c.h
 * @brief C API for Suzume Japanese morphological analyzer
 *
 * This header provides a C-compatible API for use with WebAssembly
 * and other language bindings.
 */

#ifndef SUZUME_SUZUME_C_H_
#define SUZUME_SUZUME_C_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __EMSCRIPTEN__
#define SUZUME_EXPORT __attribute__((used))
#elif defined(_WIN32)
#define SUZUME_EXPORT __declspec(dllexport)
#elif defined(__GNUC__)
#define SUZUME_EXPORT __attribute__((visibility("default")))
#else
#define SUZUME_EXPORT
#endif

/**
 * @brief Opaque handle to Suzume instance
 *
 * A handle is NOT thread-safe: the analyzer keeps per-handle mutable state,
 * so calling suzume_analyze / suzume_generate_tags concurrently on the same
 * handle is undefined behavior. Use one handle per thread, or serialize all
 * calls that share a handle. Distinct handles may be used concurrently.
 */
typedef struct SuzumeHandle* suzume_t;

/**
 * @brief Morpheme data structure
 */
typedef struct {
  const char* surface;      /**< Surface form (UTF-8) */
  const char* pos;          /**< Part of speech (English) */
  const char* base_form;    /**< Base/dictionary form */
  const char* pos_ja;       /**< Part of speech (Japanese) */
  const char* conj_type;    /**< Conjugation type (Japanese) */
  const char* conj_form;    /**< Conjugation form (Japanese) */
  const char* extended_pos; /**< Extended POS (English, e.g. "VerbRenyokei") */
  size_t start;             /**< Start character offset in normalized text */
  size_t end;               /**< End character offset in normalized text */
  int is_user_dict;         /**< Non-zero if from user dictionary */
  int is_formal_noun;       /**< Non-zero if formal noun */
  int is_low_info;          /**< Non-zero if low information word */
  int is_unknown;           /**< Non-zero if unknown word */
  int is_from_dictionary;   /**< Non-zero if from dictionary */
  float score;              /**< Candidate score/cost */
} suzume_morpheme_t;

/**
 * @brief Analysis result structure
 */
typedef struct {
  suzume_morpheme_t* morphemes; /**< Array of morphemes */
  size_t count;                 /**< Number of morphemes */
} suzume_result_t;

/**
 * @brief Tag generation result structure
 */
typedef struct {
  char** tags;      /**< Array of tag strings */
  const char** pos; /**< Array of POS strings (English, e.g. "NOUN", "VERB") */
  size_t count;     /**< Number of tags */
} suzume_tags_t;

/**
 * @brief Analysis options structure.
 *
 * This basic form exposes normalization toggles and symbol handling only.
 * Use suzume_create_with_extended_options() with suzume_extended_options_t
 * for analysis mode, lemmatization, and compound merging.
 */
typedef struct {
  int preserve_vu;      /**< Preserve ヴ (don't normalize to ビ etc.) */
  int preserve_case;    /**< Preserve case (don't lowercase ASCII) */
  int preserve_symbols; /**< Preserve symbols/emoji (don't remove from output) */
} suzume_options_t;

/**
 * @brief Extended analysis options structure.
 *
 * Set size to sizeof(suzume_extended_options_t). Unknown future fields are ignored
 * when size is smaller than the field offset. Use
 * suzume_init_extended_options() before overriding individual fields so default
 * true values such as preserve_case and lemmatize are preserved.
 */
typedef struct {
  uint32_t size;        /**< Structure size for forward/backward compatibility */
  int preserve_vu;      /**< Preserve ヴ (don't normalize to ビ etc.) */
  int preserve_case;    /**< Preserve case (don't lowercase ASCII) */
  int preserve_symbols; /**< Preserve symbols/emoji (don't remove from output) */
  int mode;             /**< 0=normal, 1=search, 2=split */
  int lemmatize;        /**< Apply lemmatization */
  int merge_compounds;  /**< Merge consecutive noun compounds */
} suzume_extended_options_t;

// --- Lifecycle functions ---

/**
 * @brief Create a new Suzume instance with default options
 * @return Handle to Suzume instance, or NULL on failure
 * @note The returned handle is not thread-safe for concurrent analysis
 *       calls; see suzume_t.
 */
SUZUME_EXPORT suzume_t suzume_create(void);

/**
 * @brief Create a new Suzume instance with options
 * @param options Pointer to options structure
 * @return Handle to Suzume instance, or NULL on failure
 */
SUZUME_EXPORT suzume_t suzume_create_with_options(const suzume_options_t* options);

/**
 * @brief Initialize extended options with Suzume defaults
 * @param options Pointer to options structure to initialize
 * @note Passing NULL is allowed and has no effect.
 */
SUZUME_EXPORT void suzume_init_extended_options(suzume_extended_options_t* options);

/**
 * @brief Create a new Suzume instance with extended options
 * @param options Pointer to extended options structure
 * @return Handle to Suzume instance, or NULL on failure
 */
SUZUME_EXPORT suzume_t suzume_create_with_extended_options(const suzume_extended_options_t* options);

/**
 * @brief Destroy Suzume instance and free resources
 * @param handle Suzume handle
 * @note Passing NULL is allowed and has no effect.
 */
SUZUME_EXPORT void suzume_destroy(suzume_t handle);

// --- Analysis functions ---

/**
 * @brief Analyze Japanese text into morphemes
 * @param handle Suzume handle
 * @param text UTF-8 encoded Japanese text
 * @return Analysis result allocated by Suzume, or NULL on failure.
 *         Invalid UTF-8 input fails with NULL and a suzume_last_error()
 *         message; empty input succeeds with an empty result (count == 0).
 *         Non-NULL results must be freed exactly once with suzume_result_free.
 * @note Not thread-safe with respect to other calls on the same handle;
 *       see suzume_t.
 */
SUZUME_EXPORT suzume_result_t* suzume_analyze(suzume_t handle, const char* text);

/**
 * @brief Free analysis result
 * @param result Result to free
 * @note Passing NULL is allowed and has no effect.
 */
SUZUME_EXPORT void suzume_result_free(suzume_result_t* result);

/**
 * @brief Generate tags from Japanese text
 * @param handle Suzume handle
 * @param text UTF-8 encoded Japanese text
 * @return Tags result allocated by Suzume, or NULL on failure.
 *         Non-NULL results must be freed exactly once with suzume_tags_free.
 */
SUZUME_EXPORT suzume_tags_t* suzume_generate_tags(suzume_t handle, const char* text);

/**
 * @brief Tag generation options.
 *
 * Set size to sizeof(suzume_tag_options_t), or call suzume_init_tag_options()
 * to populate size together with the documented defaults. The fields listed
 * before size are always read; fields appended in future versions are read
 * only when size covers them, so structs from callers compiled against an
 * older layout stay valid. size trails the pre-existing fields (instead of
 * leading as in suzume_extended_options_t) to keep their offsets stable.
 */
typedef struct {
  uint8_t pos_filter;       /**< POS bitmask: 1=noun, 2=verb, 4=adjective, 8=adverb (0=all) */
  int exclude_basic;        /**< Exclude basic words (hiragana-only lemma) */
  int use_lemma;            /**< Use lemma instead of surface (default: 1) */
  size_t min_length;        /**< Minimum tag length in characters (default: 2) */
  size_t max_tags;          /**< Maximum number of tags (0=unlimited) */
  int exclude_particles;    /**< Exclude particles (default: 1) */
  int exclude_auxiliaries;  /**< Exclude auxiliaries (default: 1) */
  int exclude_formal_nouns; /**< Exclude formal nouns (default: 1) */
  int exclude_low_info;     /**< Exclude low information words (default: 1) */
  int remove_duplicates;    /**< Remove duplicate tags (default: 1) */
  uint32_t size;            /**< Structure size for forward compatibility */
} suzume_tag_options_t;

/**
 * @brief Initialize tag options with Suzume defaults
 * @param options Pointer to options structure to initialize
 * @note Passing NULL is allowed and has no effect.
 */
SUZUME_EXPORT void suzume_init_tag_options(suzume_tag_options_t* options);

/**
 * @brief Generate tags from Japanese text with options
 * @param handle Suzume handle
 * @param text UTF-8 encoded Japanese text
 * @param options Tag generation options
 * @return Tags result allocated by Suzume, or NULL on failure.
 *         Non-NULL results must be freed exactly once with suzume_tags_free.
 */
SUZUME_EXPORT suzume_tags_t* suzume_generate_tags_with_options(suzume_t handle, const char* text,
                                                               const suzume_tag_options_t* options);

/**
 * @brief Free tags result
 * @param tags Tags to free
 * @note Passing NULL is allowed and has no effect.
 */
SUZUME_EXPORT void suzume_tags_free(suzume_tags_t* tags);

// --- Dictionary functions ---

/**
 * @brief Load user dictionary from memory
 * @param handle Suzume handle
 * @param data Dictionary data (CSV format)
 * @param size Data size in bytes
 * @return 1 on success, 0 on failure
 */
SUZUME_EXPORT int suzume_load_user_dict(suzume_t handle, const char* data, size_t size);

/**
 * @brief Load binary dictionary from memory (as user dictionary)
 * @param handle Suzume handle
 * @param data Binary dictionary data (.dic format)
 * @param size Data size in bytes
 * @return 1 on success, 0 on failure
 */
SUZUME_EXPORT int suzume_load_binary_dict(suzume_t handle, const uint8_t* data, size_t size);

// --- Utility functions ---

/**
 * @brief Get Suzume version string
 * @return Version string (static, do not free)
 */
SUZUME_EXPORT const char* suzume_version(void);

/**
 * @brief Get the last C API error message for the current thread/runtime
 * @return Last error string (static/thread-local, do not free)
 */
SUZUME_EXPORT const char* suzume_last_error(void);

/**
 * @brief Get number of dictionary warnings from auto-loading dictionaries
 * @param handle Suzume handle
 * @return Warning count, or 0 for null handle
 */
SUZUME_EXPORT size_t suzume_dictionary_warning_count(suzume_t handle);

/**
 * @brief Get dictionary warning message by index
 * @param handle Suzume handle
 * @param index Warning index
 * @return Warning string owned by Suzume, or NULL if out of range.
 *         The pointer is valid only until the next suzume_dictionary_warning
 *         call from the same thread or until the handle is destroyed,
 *         whichever comes first; copy the string to retain it.
 */
SUZUME_EXPORT const char* suzume_dictionary_warning(suzume_t handle, size_t index);

/**
 * @brief Get sizeof(suzume_result_t)
 */
SUZUME_EXPORT size_t suzume_sizeof_result(void);

/**
 * @brief Get sizeof(suzume_morpheme_t)
 */
SUZUME_EXPORT size_t suzume_sizeof_morpheme(void);

/**
 * @brief Get sizeof(suzume_tags_t)
 */
SUZUME_EXPORT size_t suzume_sizeof_tags(void);

/**
 * @brief Get sizeof(suzume_tag_options_t)
 */
SUZUME_EXPORT size_t suzume_sizeof_tag_options(void);

/**
 * @brief Get sizeof(suzume_extended_options_t)
 */
SUZUME_EXPORT size_t suzume_sizeof_extended_options(void);

/**
 * @brief Get sizeof(suzume_options_t)
 */
SUZUME_EXPORT size_t suzume_sizeof_options(void);

/**
 * @brief Get byte offset of field in suzume_result_t
 * @param field 0=morphemes, 1=count
 */
SUZUME_EXPORT size_t suzume_offsetof_result(uint32_t field);

/**
 * @brief Get byte offset of field in suzume_morpheme_t
 * @param field 0=surface, 1=pos, 2=base_form, 3=pos_ja,
 *              4=conj_type, 5=conj_form, 6=extended_pos,
 *              7=start, 8=end, 9=is_user_dict, 10=is_formal_noun,
 *              11=is_low_info, 12=is_unknown, 13=is_from_dictionary, 14=score
 */
SUZUME_EXPORT size_t suzume_offsetof_morpheme(uint32_t field);

/**
 * @brief Get byte offset of field in suzume_tags_t
 * @param field 0=tags, 1=pos, 2=count
 */
SUZUME_EXPORT size_t suzume_offsetof_tags(uint32_t field);

/**
 * @brief Get byte offset of field in suzume_tag_options_t
 * @param field 0=pos_filter, 1=exclude_basic, 2=use_lemma,
 *              3=min_length, 4=max_tags, 5=exclude_particles,
 *              6=exclude_auxiliaries, 7=exclude_formal_nouns,
 *              8=exclude_low_info, 9=remove_duplicates, 10=size
 */
SUZUME_EXPORT size_t suzume_offsetof_tag_options(uint32_t field);

/**
 * @brief Get byte offset of field in suzume_extended_options_t
 * @param field 0=size, 1=preserve_vu, 2=preserve_case,
 *              3=preserve_symbols, 4=mode, 5=lemmatize,
 *              6=merge_compounds
 */
SUZUME_EXPORT size_t suzume_offsetof_extended_options(uint32_t field);

/**
 * @brief Get byte offset of field in suzume_options_t
 * @param field 0=preserve_vu, 1=preserve_case, 2=preserve_symbols
 */
SUZUME_EXPORT size_t suzume_offsetof_options(uint32_t field);

/**
 * @brief Allocate memory (for WASM interop)
 * @param size Size in bytes
 * @return Pointer to allocated memory
 */
SUZUME_EXPORT void* suzume_malloc(size_t size);

/**
 * @brief Free memory (for WASM interop)
 * @param ptr Pointer to free
 * @note Passing NULL is allowed and has no effect.
 */
SUZUME_EXPORT void suzume_free(void* ptr);

#ifdef __cplusplus
}
#endif

#endif  // SUZUME_SUZUME_C_H_
