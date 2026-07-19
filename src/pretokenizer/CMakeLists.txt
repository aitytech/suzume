# PreTokenizer library
add_library(suzume_pretokenizer OBJECT
    pretokenizer.cpp
    pretokenizer_quantity.cpp
    pretokenizer_utils.cpp
    pretokenizer_web.cpp
)

target_include_directories(suzume_pretokenizer
    PUBLIC ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(suzume_pretokenizer
    PUBLIC suzume_core
    PUBLIC suzume_normalize
)
