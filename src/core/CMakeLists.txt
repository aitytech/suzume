# Core module CMakeLists.txt

add_library(suzume_core OBJECT
  types.cpp
  error.cpp
  lattice.cpp
)

target_include_directories(suzume_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
