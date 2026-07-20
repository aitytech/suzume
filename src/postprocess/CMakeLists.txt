# Postprocess module CMakeLists.txt

add_library(suzume_postprocess OBJECT
  lemmatizer.cpp
  lemmatizer_batch.cpp
  lemmatizer_conj_form.cpp
  lemmatizer_grammar.cpp
  lemmatizer_rules.cpp
  postprocessor.cpp
  postprocessor_resolvers_appearance.cpp
  postprocessor_resolvers_common.cpp
  postprocessor_resolvers_nominal.cpp
  postprocessor_resolvers_stages.cpp
  postprocessor_resolvers_verb.cpp
  postprocessor_numeric.cpp
  tag_generator.cpp
)

target_include_directories(suzume_postprocess
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(suzume_postprocess
  PUBLIC
    suzume_core
    suzume_normalize
    suzume_grammar
)
