# Analysis module CMakeLists.txt

add_library(suzume_analysis STATIC
  adjective_candidates_hiragana.cpp
  adjective_candidates_internal.cpp
  adjective_candidates_kanji.cpp
  adjective_candidates_stem.cpp
  analyzer.cpp
  bigram_table.cpp
  bigram_table_aux_noun.cpp
  bigram_table_particle_lexical.cpp
  bigram_table_verb_adj.cpp
  join_compound_verb.cpp
  join_compound_verb_hiragana.cpp
  join_noun.cpp
  join_te_aux.cpp
  scorer.cpp
  scorer_connection_cost.cpp
  scorer_connection_rules_lexical.cpp
  scorer_connection_rules_verb.cpp
  scorer_word_cost.cpp
  split_candidates.cpp
  suffix_candidates_compound.cpp
  suffix_candidates_counter.cpp
  suffix_candidates_prefix.cpp
  suffix_candidates_suffix.cpp
  tokenizer.cpp
  tokenizer_utils.cpp
  unknown.cpp
  unknown_same_type.cpp
  unknown_speech.cpp
  verb_candidates.cpp
  verb_candidates_helpers.cpp
  verb_candidates_hiragana.cpp
  verb_candidates_hiragana_mizenkei.cpp
  verb_candidates_hiragana_onbin.cpp
  verb_candidates_kanji.cpp
  verb_candidates_kanji_ichidan.cpp
  verb_candidates_kanji_inflection.cpp
  verb_candidates_kanji_mizenkei.cpp
  verb_candidates_kanji_onbin.cpp
  verb_candidates_kanji_renyokei.cpp
)

target_include_directories(suzume_analysis
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(suzume_analysis
  PUBLIC
    suzume_core
    suzume_normalize
    suzume_dictionary
    suzume_grammar
    suzume_pretokenizer
)
