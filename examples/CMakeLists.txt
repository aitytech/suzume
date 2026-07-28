# In-tree examples, enabled with -DSUZUME_BUILD_EXAMPLES=ON. They link the
# in-tree `suzume` target. The standalone find_package(suzume) variant lives in
# examples/consumer/ and is built separately against an installed package.

# The core library is CXX-only; the C example needs the C toolchain.
enable_language(C)

add_executable(suzume_example_c hello.c)
target_link_libraries(suzume_example_c PRIVATE suzume)
# The static core is C++; a C program linking it must link via the C++ driver so
# the C++ runtime is pulled in. (C consumers of the shared library need not.)
set_target_properties(suzume_example_c PROPERTIES LINKER_LANGUAGE CXX)

add_executable(suzume_example_cpp hello.cpp)
target_link_libraries(suzume_example_cpp PRIVATE suzume)

foreach(example_name basic search_indexer tags user_dictionary)
  add_executable(suzume_example_cpp_${example_name} cpp/${example_name}.cpp)
  target_link_libraries(suzume_example_cpp_${example_name} PRIVATE suzume)
endforeach()

if(BUILD_TESTING)
  add_test(NAME suzume_example_c COMMAND suzume_example_c)
  add_test(NAME suzume_example_cpp COMMAND suzume_example_cpp)
  foreach(example_name basic search_indexer tags user_dictionary)
    add_test(
      NAME suzume_example_cpp_${example_name}
      COMMAND suzume_example_cpp_${example_name}
    )
  endforeach()
endif()
