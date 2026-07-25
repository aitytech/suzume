/**
 * @file suzume.hpp
 * @brief Header-only C++ wrapper over the Suzume C ABI (suzume_c.h).
 *
 * This is a thin, exception-free convenience layer: it owns the handle via RAII,
 * copies results into owning std::string/std::vector, and never throws (matching
 * the core's Expected<T, Error> style). It depends only on the stable C ABI, so
 * it stays ABI-compatible across releases. Include this from C++; C code includes
 * suzume_c.h directly.
 */

#ifndef SUZUME_SUZUME_HPP_
#define SUZUME_SUZUME_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "suzume/suzume_c.h"

namespace suzume {

/** @brief Analysis mode; mirrors the C ABI mode codes. */
enum class Mode : std::uint8_t {
  Normal = 0,
  Search = 1,
  Split = 2,
};

/** @brief Tokenizer construction options (see suzume_extended_options_t). */
struct Options {
  bool preserve_vu = true;
  bool preserve_case = true;
  bool preserve_symbols = false;
  Mode mode = Mode::Normal;
  bool lemmatize = true;
  bool merge_compounds = false;
};

/** @brief A single analyzed morpheme with owning strings. */
struct Morpheme {
  std::string surface;       ///< Surface form (UTF-8).
  std::string pos;           ///< Part of speech (English).
  std::string lemma;         ///< Base/dictionary form.
  std::string pos_ja;        ///< Part of speech (Japanese).
  std::string conj_type;     ///< Conjugation type (Japanese); empty when N/A.
  std::string conj_form;     ///< Conjugation form (Japanese); empty when N/A.
  std::string extended_pos;  ///< Stable extended POS code (e.g. "VERB_連用").
  std::size_t start = 0;     ///< Start character offset in normalized text.
  std::size_t end = 0;       ///< End character offset in normalized text.
  bool is_user_dict = false;
  bool is_formal_noun = false;
  bool is_low_info = false;
  bool is_unknown = false;
  bool is_from_dictionary = false;
  float score = 0.0F;
};

/** @brief Tag-generation options (see suzume_tag_options_t). */
struct TagOptions {
  std::uint8_t pos_filter = 0;  ///< POS bitmask: 1=noun,2=verb,4=adj,8=adverb (0=all).
  bool exclude_basic = false;
  bool use_lemma = true;
  std::size_t min_length = 2;
  std::size_t max_tags = 0;
  bool exclude_particles = true;
  bool exclude_auxiliaries = true;
  bool exclude_formal_nouns = true;
  bool exclude_low_info = true;
  bool remove_duplicates = true;
};

/** @brief A generated tag (surface/lemma text plus its POS). */
struct Tag {
  std::string text;
  std::string pos;
};

/**
 * @brief RAII wrapper around a suzume_t handle.
 *
 * Move-only. A handle is not thread-safe for concurrent analysis; use one
 * Tokenizer per thread or serialize calls that share one.
 */
class Tokenizer {
 public:
  Tokenizer() : handle_(suzume_create()) {}

  explicit Tokenizer(const Options& options) : handle_(nullptr) {
    suzume_extended_options_t copts;
    suzume_init_extended_options(&copts);
    copts.preserve_vu = options.preserve_vu ? 1 : 0;
    copts.preserve_case = options.preserve_case ? 1 : 0;
    copts.preserve_symbols = options.preserve_symbols ? 1 : 0;
    copts.mode = static_cast<std::uint8_t>(options.mode);
    copts.lemmatize = options.lemmatize ? 1 : 0;
    copts.merge_compounds = options.merge_compounds ? 1 : 0;
    handle_ = suzume_create_with_extended_options(&copts);
  }

  Tokenizer(const Tokenizer&) = delete;
  Tokenizer& operator=(const Tokenizer&) = delete;

  Tokenizer(Tokenizer&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }

  Tokenizer& operator=(Tokenizer&& other) noexcept {
    if (this != &other) {
      suzume_destroy(handle_);
      handle_ = other.handle_;
      other.handle_ = nullptr;
    }
    return *this;
  }

  ~Tokenizer() { suzume_destroy(handle_); }

  /** @brief True when the handle was created successfully. */
  bool valid() const noexcept { return handle_ != nullptr; }
  explicit operator bool() const noexcept { return valid(); }

  /**
   * @brief Analyze text into morphemes.
   * @return Morphemes, or an empty vector on failure (see lastError()).
   */
  std::vector<Morpheme> analyze(std::string_view text) const {
    std::vector<Morpheme> out;
    if (handle_ == nullptr) {
      return out;
    }
    const std::string input(text);
    suzume_result_t* result = suzume_analyze(handle_, input.c_str());
    if (result == nullptr) {
      return out;
    }
    out.reserve(result->count);
    for (std::size_t idx = 0; idx < result->count; ++idx) {
      const suzume_morpheme_t& src = result->morphemes[idx];
      Morpheme morph;
      morph.surface = cstr(src.surface);
      morph.pos = posLabel(src.pos, false);
      morph.lemma = cstr(src.base_form);
      morph.pos_ja = posLabel(src.pos, true);
      const bool conjugates = src.pos == SUZUME_POS_VERB || src.pos == SUZUME_POS_ADJECTIVE;
      morph.conj_type = conjugates ? conjugationTypeLabel(src.conjugation_type) : std::string();
      morph.conj_form = conjugates ? conjugationFormLabel(src.conjugation_form) : std::string();
      morph.extended_pos = extendedPosLabel(src.extended_pos);
      morph.start = src.start;
      morph.end = src.end;
      morph.is_user_dict = (src.flags & SUZUME_MORPHEME_USER_DICT) != 0;
      morph.is_formal_noun = (src.flags & SUZUME_MORPHEME_FORMAL_NOUN) != 0;
      morph.is_low_info = (src.flags & SUZUME_MORPHEME_LOW_INFO) != 0;
      morph.is_unknown = (src.flags & SUZUME_MORPHEME_UNKNOWN) != 0;
      morph.is_from_dictionary = (src.flags & SUZUME_MORPHEME_FROM_DICTIONARY) != 0;
      morph.score = src.score;
      out.push_back(std::move(morph));
    }
    suzume_result_free(result);
    return out;
  }

  /** @brief Generate search tags with default options. */
  std::vector<Tag> generateTags(std::string_view text) const {
    if (handle_ == nullptr) {
      return {};
    }
    const std::string input(text);
    return collectTags(suzume_generate_tags(handle_, input.c_str()));
  }

  /** @brief Generate search tags with explicit options. */
  std::vector<Tag> generateTags(std::string_view text, const TagOptions& options) const {
    if (handle_ == nullptr) {
      return {};
    }
    suzume_tag_options_t copts;
    suzume_init_tag_options(&copts);
    copts.pos_filter = options.pos_filter;
    copts.exclude_basic = options.exclude_basic ? 1 : 0;
    copts.use_lemma = options.use_lemma ? 1 : 0;
    copts.min_length = options.min_length;
    copts.max_tags = options.max_tags;
    copts.exclude_particles = options.exclude_particles ? 1 : 0;
    copts.exclude_auxiliaries = options.exclude_auxiliaries ? 1 : 0;
    copts.exclude_formal_nouns = options.exclude_formal_nouns ? 1 : 0;
    copts.exclude_low_info = options.exclude_low_info ? 1 : 0;
    copts.remove_duplicates = options.remove_duplicates ? 1 : 0;
    const std::string input(text);
    return collectTags(suzume_generate_tags_with_options(handle_, input.c_str(), &copts));
  }

  /**
   * @brief Load additional user-dictionary entries from TSV bytes.
   * @return True on success; see lastError() on failure.
   */
  bool loadUserDictionary(std::string_view tsv) const {
    if (handle_ == nullptr) {
      return false;
    }
    return suzume_load_user_dict(handle_, tsv.data(), tsv.size()) != 0;
  }

  /**
   * @brief Load a compiled binary dictionary from memory.
   * @return True on success; see lastError() on failure.
   */
  bool loadBinaryDictionary(const std::uint8_t* data, std::size_t size) const {
    if (handle_ == nullptr) {
      return false;
    }
    return suzume_load_binary_dict(handle_, data, size) != 0;
  }

  /** @brief Last error message for this thread (empty when none). */
  static std::string lastError() { return cstr(suzume_last_error()); }

  /** @brief Library version string. */
  static std::string version() { return cstr(suzume_version()); }

 private:
  static std::string cstr(const char* str) { return str != nullptr ? std::string(str) : std::string(); }

  static std::string posLabel(std::uint8_t code, bool japanese) {
    static constexpr std::array<const char*, 15> english = {"OTHER",    "NOUN",   "VERB", "ADJ",    "ADV",
                                                            "PARTICLE", "AUX",    "CONJ", "DET",    "PRON",
                                                            "PREFIX",   "SUFFIX", "INTJ", "SYMBOL", "OTHER"};
    static constexpr std::array<const char*, 15> japanese_labels = {"その他", "名詞",   "動詞",   "形容詞", "副詞",
                                                                    "助詞",   "助動詞", "接続詞", "連体詞", "代名詞",
                                                                    "接頭辞", "接尾辞", "感動詞", "記号",   "その他"};
    if (code >= english.size()) {
      code = 0;
    }
    return japanese ? japanese_labels[code] : english[code];
  }

  static std::string conjugationTypeLabel(std::uint8_t code) {
    static constexpr std::array<const char*, 14> labels = {
        "",           "一段",       "五段・カ行", "五段・ガ行", "五段・サ行", "五段・タ行", "五段・ナ行",
        "五段・バ行", "五段・マ行", "五段・ラ行", "五段・ワ行", "サ変",       "カ変",       "形容詞"};
    return code < labels.size() ? labels[code] : "";
  }

  static std::string conjugationFormLabel(std::uint8_t code) {
    static constexpr std::array<const char*, 7> labels = {"終止形", "未然形", "連用形", "連用形",
                                                          "仮定形", "命令形", "意志形"};
    return code < labels.size() ? labels[code] : "";
  }

  static std::string extendedPosLabel(std::uint8_t code) {
    static constexpr std::array<const char*, 83> labels = {"UNKNOWN",
                                                           "VERB_終止",
                                                           "VERB_連用",
                                                           "VERB_未然",
                                                           "VERB_音便",
                                                           "VERB_て形",
                                                           "VERB_仮定",
                                                           "VERB_命令",
                                                           "VERB_連体",
                                                           "VERB_た形",
                                                           "VERB_たら形",
                                                           "ADJ_終止",
                                                           "ADJ_連用",
                                                           "ADJ_語幹",
                                                           "ADJ_かっ",
                                                           "ADJ_け形",
                                                           "ADJ_NA",
                                                           "AUX_過去",
                                                           "AUX_丁寧",
                                                           "AUX_否定",
                                                           "AUX_否定古",
                                                           "AUX_願望",
                                                           "AUX_意志",
                                                           "AUX_受身",
                                                           "AUX_使役",
                                                           "AUX_可能",
                                                           "AUX_継続",
                                                           "AUX_完了",
                                                           "AUX_準備",
                                                           "AUX_試行",
                                                           "AUX_進行",
                                                           "AUX_接近",
                                                           "AUX_開始",
                                                           "AUX_様態",
                                                           "AUX_推定",
                                                           "AUX_みたい",
                                                           "AUX_断定",
                                                           "AUX_丁寧断定",
                                                           "AUX_尊敬",
                                                           "AUX_丁重",
                                                           "AUX_過度",
                                                           "AUX_ガル",
                                                           "PART_格",
                                                           "PART_係",
                                                           "PART_終",
                                                           "PART_接続",
                                                           "PART_引用",
                                                           "PART_副",
                                                           "PART_準体",
                                                           "PART_係結",
                                                           "NOUN",
                                                           "NOUN_形式",
                                                           "NOUN_転成",
                                                           "NOUN_固有",
                                                           "NOUN_姓",
                                                           "NOUN_名",
                                                           "NOUN_数",
                                                           "PRON",
                                                           "PRON_疑問",
                                                           "ADV",
                                                           "ADV_引用",
                                                           "CONJ",
                                                           "DET",
                                                           "PREFIX",
                                                           "SUFFIX",
                                                           "SYMBOL",
                                                           "INTJ",
                                                           "OTHER",
                                                           "ADJ_未然",
                                                           "AUX_打消推量",
                                                           "AUX_文語断定",
                                                           "AUX_文語過去",
                                                           "AUX_文語断定連体",
                                                           "AUX_文語完了",
                                                           "AUX_文語当為",
                                                           "AUX_不可能",
                                                           "AUX_授受",
                                                           "SUFFIX_直後",
                                                           "SUFFIX_傾向",
                                                           "DET_引用",
                                                           "AUX_よう",
                                                           "AUX_KURUWA_POLITE",
                                                           "AUX_文語過去キ"};
    return code < labels.size() ? labels[code] : labels[0];
  }

  static std::vector<Tag> collectTags(suzume_tags_t* tags) {
    std::vector<Tag> out;
    if (tags == nullptr) {
      return out;
    }
    out.reserve(tags->count);
    for (std::size_t idx = 0; idx < tags->count; ++idx) {
      Tag tag;
      tag.text = cstr(tags->tags != nullptr ? tags->tags[idx] : nullptr);
      tag.pos = tags->pos != nullptr ? posLabel(tags->pos[idx], false) : std::string();
      out.push_back(std::move(tag));
    }
    suzume_tags_free(tags);
    return out;
  }

  suzume_t handle_;
};

}  // namespace suzume

#endif  // SUZUME_SUZUME_HPP_
