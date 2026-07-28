# Main source CMakeLists.txt

# Add subdirectories
add_subdirectory(core)
add_subdirectory(normalize)
add_subdirectory(dictionary)
add_subdirectory(grammar)
add_subdirectory(pretokenizer)
add_subdirectory(analysis)
add_subdirectory(postprocess)

# The core is split into OBJECT libraries so the assembled library (static or
# shared) bakes every object in and is self-contained — a downstream project
# links a single libsuzume with no transitive suzume_* targets to resolve.
set(SUZUME_OBJECT_LIBS
  suzume_core
  suzume_normalize
  suzume_dictionary
  suzume_grammar
  suzume_pretokenizer
  suzume_analysis
  suzume_postprocess
)

# Consume the object libraries via $<TARGET_OBJECTS:> (not target_link_libraries)
# so install(EXPORT) does not require the object libraries to be in the export
# set and their internal include dirs never leak into the public interface.
set(SUZUME_OBJECTS "")
foreach(obj_lib ${SUZUME_OBJECT_LIBS})
  list(APPEND SUZUME_OBJECTS "$<TARGET_OBJECTS:${obj_lib}>")
endforeach()

# Top-level translation units. suzume_c.cpp is the C ABI: it is compiled into the
# native library so static/shared consumers get the C entry points directly. For
# WASM the C ABI is compiled into the suzume-wasm executable instead.
set(SUZUME_TOP_SOURCES suzume.cpp)
if(NOT BUILD_WASM)
  list(APPEND SUZUME_TOP_SOURCES suzume_c.cpp)
endif()

# Optionally embed the compiled dictionaries so the library carries them with no
# filesystem access (always for WASM; opt-in for native via SUZUME_EMBED_DICT).
if(BUILD_WASM OR SUZUME_EMBED_DICT)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(EMBEDDED_DICTIONARIES_CPP "${CMAKE_CURRENT_BINARY_DIR}/embedded_dictionaries.cpp")
  add_custom_command(
    OUTPUT "${EMBEDDED_DICTIONARIES_CPP}"
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/scripts/generate_embedded_dictionaries.py"
            --core "${CMAKE_SOURCE_DIR}/data/core.dic"
            --user "${CMAKE_SOURCE_DIR}/data/user.dic"
            --output "${EMBEDDED_DICTIONARIES_CPP}"
    DEPENDS
      "${CMAKE_SOURCE_DIR}/scripts/generate_embedded_dictionaries.py"
      "${CMAKE_SOURCE_DIR}/data/core.dic"
      "${CMAKE_SOURCE_DIR}/data/user.dic"
    COMMENT "Generating embedded dictionary arrays"
    VERBATIM
  )
  list(APPEND SUZUME_TOP_SOURCES "${EMBEDDED_DICTIONARIES_CPP}")
endif()

# Compile definitions shared by every assembled form of the library.
set(SUZUME_PRIVATE_DEFS SUZUME_VERSION="${PROJECT_VERSION}")
if(SUZUME_EMBED_DICT AND NOT BUILD_WASM)
  list(APPEND SUZUME_PRIVATE_DEFS SUZUME_EMBED_DICT)
elseif(NOT BUILD_WASM)
  # Let the runtime find the dictionaries installed under the active prefix.
  list(APPEND SUZUME_PRIVATE_DEFS
    SUZUME_DATA_INSTALL_DIR="${CMAKE_INSTALL_FULL_DATADIR}/suzume")
endif()

# Public include directories: src/ for in-tree consumers (tests, CLI), the public
# C ABI header dir, and the installed include dir for downstream find_package.
set(SUZUME_PUBLIC_INCLUDES
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Static library — always built. Self-contained; the canonical target for
# embedded/static integration.
add_library(suzume STATIC ${SUZUME_TOP_SOURCES} ${SUZUME_OBJECTS})
target_include_directories(suzume PUBLIC ${SUZUME_PUBLIC_INCLUDES})
# Static consumers must see SUZUME_STATIC so the C ABI header emits no
# dllimport/dllexport decoration.
target_compile_definitions(suzume PUBLIC SUZUME_STATIC)
target_compile_definitions(suzume PRIVATE ${SUZUME_PRIVATE_DEFS})
# A pure-C program linking the static archive still needs the C++ runtime.
set_target_properties(suzume PROPERTIES LINKER_LANGUAGE CXX)

# WASM executable (produces .js + .wasm)
if(BUILD_WASM)
  # wasm-opt ships with emsdk, but its location has moved across releases
  # (upstream/bin in current emsdk, binaryen/bin in older layouts) and is not
  # always on PATH, so search the known emsdk locations explicitly.
  get_filename_component(EMSCRIPTEN_ROOT "${CMAKE_CXX_COMPILER}" DIRECTORY)
  find_program(WASM_OPT wasm-opt
    HINTS
      "${EMSCRIPTEN_ROOT}/../bin"
      "${EMSCRIPTEN_ROOT}/binaryen/bin"
      "$ENV{EMSDK}/upstream/bin"
    REQUIRED)

  add_executable(suzume-wasm suzume_c.cpp)
  target_link_libraries(suzume-wasm PRIVATE suzume)
  target_compile_definitions(suzume-wasm PRIVATE ${SUZUME_PRIVATE_DEFS})
  set_target_properties(suzume-wasm PROPERTIES OUTPUT_NAME suzume)

  # Copy the Emscripten output to the npm package directory.
  add_custom_command(TARGET suzume-wasm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${WASM_OUTPUT_DIR}"
    # Emscripten has already optimized the module, but one final Binaryen -Oz
    # pass still removes about 0.2 KB raw / 0.1 KB gzip. It adds roughly one
    # second to the WASM build and has no runtime lookup-path cost. Do not run
    # it repeatedly: a second pass only saved tens of raw bytes in measurement
    # and made gzip results unstable by changing function order.
    COMMAND "${WASM_OPT}"
      "$<TARGET_FILE_DIR:suzume-wasm>/suzume.wasm"
      -Oz --enable-bulk-memory --enable-nontrapping-float-to-int
      -o "$<TARGET_FILE_DIR:suzume-wasm>/suzume.optimized.wasm"
    COMMAND ${CMAKE_COMMAND} -E rename
      "$<TARGET_FILE_DIR:suzume-wasm>/suzume.optimized.wasm"
      "$<TARGET_FILE_DIR:suzume-wasm>/suzume.wasm"
    COMMAND ${CMAKE_COMMAND} -E rm -f
      "${WASM_OUTPUT_DIR}/suzume-wasm.wasm"
    COMMAND ${CMAKE_COMMAND} -E copy
      "$<TARGET_FILE_DIR:suzume-wasm>/suzume.js"
      "${WASM_OUTPUT_DIR}/suzume.js"
    COMMAND ${CMAKE_COMMAND} -E copy
      "$<TARGET_FILE_DIR:suzume-wasm>/suzume.wasm"
      "${WASM_OUTPUT_DIR}/suzume.wasm"
    COMMENT "Copying WASM files to dist/"
  )
endif()

# Shared C-ABI library (libsuzume.{so,dylib,dll}) — the Python ctypes binding and
# system packages consume this. Self-contained like the static library.
if(BUILD_SHARED AND NOT BUILD_WASM)
  add_library(suzume_shared SHARED ${SUZUME_TOP_SOURCES} ${SUZUME_OBJECTS})
  set_target_properties(suzume_shared PROPERTIES
    OUTPUT_NAME suzume
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
  )
  # SOVERSION for system installs; the Python wheel bundles a flat, unversioned
  # libsuzume loaded via @loader_path/$ORIGIN, so it turns this off.
  if(SUZUME_LIB_SOVERSION)
    set_target_properties(suzume_shared PROPERTIES
      VERSION ${PROJECT_VERSION}
      SOVERSION ${PROJECT_VERSION_MAJOR}
    )
  endif()
  target_include_directories(suzume_shared PUBLIC ${SUZUME_PUBLIC_INCLUDES})
  # SUZUME_EXPORTS marks this translation unit as the one exporting symbols.
  target_compile_definitions(suzume_shared PRIVATE SUZUME_EXPORTS ${SUZUME_PRIVATE_DEFS})
  if(WIN32)
    # Avoid the import-library name clash with libsuzume static (both -> suzume.lib).
    set_target_properties(suzume PROPERTIES OUTPUT_NAME suzume_static)
  endif()
  if(APPLE)
    if(SUZUME_LIB_SOVERSION)
      # System/package installs are found through the consuming target's rpath.
      # @loader_path would incorrectly require the dylib beside every executable.
      set_target_properties(suzume_shared PROPERTIES
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_NAME_DIR ON
      )
    else()
      # The flat Python wheel bundles the unversioned dylib beside the package.
      set_target_properties(suzume_shared PROPERTIES
        INSTALL_NAME_DIR "@loader_path"
        BUILD_WITH_INSTALL_NAME_DIR ON
      )
    endif()
    target_link_options(suzume_shared PRIVATE "LINKER:-dead_strip")
  elseif(UNIX)
    target_link_options(suzume_shared PRIVATE "LINKER:--gc-sections")
  endif()
endif()
