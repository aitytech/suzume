# Source-order manifest for the generated WASM L1 representation.
# Keep this order identical to CoreDictionary::initializeEntries(): stable sorting
# preserves it for duplicate surfaces.
set(SUZUME_PACKED_CORE_ENTRY_SOURCES
  ${CMAKE_SOURCE_DIR}/src/dictionary/entries/particles.cpp
  ${CMAKE_SOURCE_DIR}/src/dictionary/entries/compound_particles.cpp
  ${CMAKE_SOURCE_DIR}/src/dictionary/entries/auxiliaries.cpp
  ${CMAKE_SOURCE_DIR}/src/dictionary/entries/conjunctions.cpp
  ${CMAKE_SOURCE_DIR}/src/dictionary/entries/determiners.cpp
  ${CMAKE_SOURCE_DIR}/src/dictionary/entries/pronouns.cpp
  ${CMAKE_SOURCE_DIR}/src/dictionary/entries/formal_nouns.cpp
  ${CMAKE_SOURCE_DIR}/src/dictionary/entries/interjections.cpp
)
