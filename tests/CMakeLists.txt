# Tests CMakeLists.txt

# Fetch Google Test
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG release-1.12.1
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Enable testing
enable_testing()

# Include Google Test module
include(GoogleTest)

# Common test utilities
set(TEST_COMMON_SOURCES
  common/test_helpers.cpp
  common/test_case.cpp
)

# CLI source files needed for CLI tests
set(CLI_SOURCES_FOR_TESTS
  ${CMAKE_SOURCE_DIR}/tools/cli/dict_compiler.cpp
  ${CMAKE_SOURCE_DIR}/tools/cli/tsv_parser.cpp
  ${CMAKE_SOURCE_DIR}/tools/cli/cli_common.cpp
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
include(${CMAKE_SOURCE_DIR}/src/dictionary/entries/packed_core_entries_sources.cmake)
set(PACKED_CORE_ENTRIES_TEST_CPP ${CMAKE_CURRENT_BINARY_DIR}/packed_core_entries.cpp)
add_custom_command(
  OUTPUT ${PACKED_CORE_ENTRIES_TEST_CPP}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/generate_packed_core_entries.py
          --output ${PACKED_CORE_ENTRIES_TEST_CPP} ${SUZUME_PACKED_CORE_ENTRY_SOURCES}
  DEPENDS ${CMAKE_SOURCE_DIR}/scripts/generate_packed_core_entries.py ${SUZUME_PACKED_CORE_ENTRY_SOURCES}
  VERBATIM
)

# Test source files
set(TEST_SOURCES
  ${CMAKE_SOURCE_DIR}/src/suzume_c.cpp
  ${TEST_COMMON_SOURCES}
  ${CLI_SOURCES_FOR_TESTS}
  cli/cli_common_test.cpp
  cli/dict_compiler_test.cpp
  core/types_test.cpp
  core/types_extended_test.cpp
  core/error_test.cpp
  core/viterbi_test.cpp
  normalize/utf8_test.cpp
  normalize/char_type_test.cpp
  normalize/normalizer_test.cpp
  dictionary/trie_test.cpp
  dictionary/double_array_test.cpp
  dictionary/binary_dict_test.cpp
  dictionary/core_dict_test.cpp
  dictionary/packed_core_entries_test.cpp
  ${PACKED_CORE_ENTRIES_TEST_CPP}
  dictionary/user_dict_test.cpp
  dictionary/conj_type_test.cpp
  grammar/conjugator_test.cpp
  grammar/conjugation_test.cpp
  grammar/inflection_basic_test.cpp
  grammar/inflection_potential_test.cpp
  grammar/inflection_compound_test.cpp
  grammar/inflection_complex_test.cpp
  grammar/inflection_imperative_test.cpp
  grammar/auxiliary_generator_test.cpp
  grammar/patterns_test.cpp
  pretokenizer/pretokenizer_url_test.cpp
  pretokenizer/pretokenizer_number_test.cpp
  pretokenizer/pretokenizer_text_test.cpp
  analysis/scorer_options_loader_test.cpp
  analysis/bigram_table_test.cpp
  analysis/contracted_completive_boundary_test.cpp
  analysis/dependent_grammar_roles_test.cpp
  analysis/destination_suffix_test.cpp
  analysis/deverbal_noun_naku_test.cpp
  analysis/hiragana_godan_terminal_test.cpp
  analysis/noun_nagara_ni_test.cpp
  analysis/scorer_word_cost_test.cpp
  analysis/suffix_candidates_counter_test.cpp
  analysis/user_dict_precedence_test.cpp
  analysis/verb_guard_family_test.cpp
  output/japanese_format_test.cpp
  postprocess/tag_generator_test.cpp
  integration/suzume_api_test.cpp
  integration/suzume_c_api_test.cpp
  # Universal test: auto-discovers all JSON files and cases in tests/data/tokenization/.
  # New regression cases are picked up without creating dedicated C++ files.
  integration/universal_tokenization_test.cpp
)

# Create test executable
add_executable(suzume_test ${TEST_SOURCES})
target_compile_definitions(suzume_test PRIVATE SUZUME_VERSION="${PROJECT_VERSION}")
if(BUILD_CLI)
  # Keep the Python CLI contract test reliable even when callers build only the
  # main test target before invoking CTest.
  add_dependencies(suzume_test suzume-cli)
endif()

# Include directories
target_include_directories(suzume_test PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/tools/cli
  ${CMAKE_CURRENT_SOURCE_DIR}/common
)

# Link libraries
target_link_libraries(suzume_test PRIVATE
  suzume
  GTest::gtest_main
)

# Register tests with working directory set to project root
# This allows tests to access data files via relative paths (e.g., "data/core/verbs.tsv")
gtest_discover_tests(suzume_test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

if(BUILD_CLI)
  add_test(
    NAME native_cli_contract
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/cli/native_cli_contract_test.py
            $<TARGET_FILE:suzume-cli>
  )
  set_tests_properties(native_cli_contract PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )
endif()
