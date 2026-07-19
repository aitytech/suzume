# Dictionary module CMakeLists.txt

add_library(suzume_dictionary OBJECT
  dictionary.cpp
  core_dict.cpp
  user_dict.cpp
  trie.cpp
  double_array.cpp
  binary_dict.cpp
  entries/auxiliaries.cpp
  entries/compound_particles.cpp
  entries/conjunctions.cpp
  entries/determiners.cpp
  entries/formal_nouns.cpp
  entries/interjections.cpp
  entries/particles.cpp
  entries/pronouns.cpp
)

target_include_directories(suzume_dictionary
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/entries
)

target_link_libraries(suzume_dictionary
  PUBLIC
    suzume_core
)

if(BUILD_WASM)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  include(${CMAKE_CURRENT_SOURCE_DIR}/entries/packed_core_entries_sources.cmake)
  set(PACKED_CORE_ENTRIES_CPP ${CMAKE_CURRENT_BINARY_DIR}/packed_core_entries.cpp)
  add_custom_command(
    OUTPUT ${PACKED_CORE_ENTRIES_CPP}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/generate_packed_core_entries.py
            --output ${PACKED_CORE_ENTRIES_CPP} ${SUZUME_PACKED_CORE_ENTRY_SOURCES}
    DEPENDS ${CMAKE_SOURCE_DIR}/scripts/generate_packed_core_entries.py ${SUZUME_PACKED_CORE_ENTRY_SOURCES}
    VERBATIM
  )
  target_sources(suzume_dictionary PRIVATE ${PACKED_CORE_ENTRIES_CPP})
endif()
