# Standalone consumer project: builds the C and C++ examples against an INSTALLED
# suzume via find_package. This doubles as the packaging smoke test — point it at
# an install prefix:
#
#   cmake -B build -DCMAKE_PREFIX_PATH=/path/to/install
#   cmake --build build
#   ./build/hello_c   ./build/hello_cpp
cmake_minimum_required(VERSION 3.15)
project(suzume_consumer LANGUAGES C CXX)

find_package(suzume CONFIG REQUIRED)

# Link the shared library when it was installed, otherwise the static one.
if(TARGET suzume::suzume_shared)
  set(SUZUME_LINK_TARGET suzume::suzume_shared)
else()
  set(SUZUME_LINK_TARGET suzume::suzume)
endif()

add_executable(hello_c ../hello.c)
target_link_libraries(hello_c PRIVATE ${SUZUME_LINK_TARGET})
# When linking the static core, a C program needs the C++ linker driver so the
# C++ runtime is pulled in. Harmless when linking the shared library.
set_target_properties(hello_c PROPERTIES LINKER_LANGUAGE CXX)

add_executable(hello_cpp ../hello.cpp)
target_link_libraries(hello_cpp PRIVATE ${SUZUME_LINK_TARGET})
target_compile_features(hello_cpp PRIVATE cxx_std_17)

foreach(example_name basic search_indexer tags user_dictionary)
  add_executable(cpp_${example_name} ../cpp/${example_name}.cpp)
  target_link_libraries(cpp_${example_name} PRIVATE ${SUZUME_LINK_TARGET})
  target_compile_features(cpp_${example_name} PRIVATE cxx_std_17)
endforeach()

# Run both as tests so `ctest` exercises them cross-platform (handles the
# multi-config exe path on MSVC automatically).
enable_testing()
add_test(NAME hello_c COMMAND hello_c)
add_test(NAME hello_cpp COMMAND hello_cpp)
foreach(example_name basic search_indexer tags user_dictionary)
  add_test(NAME cpp_${example_name} COMMAND cpp_${example_name})
endforeach()
