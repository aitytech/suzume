# Suzume examples

These programs are compiled or type-checked by the repository build.

| File | API | Purpose |
|------|-----|---------|
| [`hello.c`](hello.c) | C ABI | Minimal native consumer |
| [`hello.cpp`](hello.cpp) | C++ wrapper | Minimal native consumer |
| [`cpp/basic.cpp`](cpp/basic.cpp) | C++ core API | Morphological analysis |
| [`cpp/search_indexer.cpp`](cpp/search_indexer.cpp) | C++ core API | Inverted search index |
| [`cpp/tags.cpp`](cpp/tags.cpp) | C++ core API | Search-tag generation |
| [`cpp/user_dictionary.cpp`](cpp/user_dictionary.cpp) | C++ core API | Runtime user dictionary |
| [`ts/basic.ts`](ts/basic.ts) | WASM/TypeScript | Morphological analysis |
| [`ts/search_indexer.ts`](ts/search_indexer.ts) | WASM/TypeScript | Inverted search index |
| [`ts/tags.ts`](ts/tags.ts) | WASM/TypeScript | Search-tag generation |
| [`ts/user_dictionary.ts`](ts/user_dictionary.ts) | WASM/TypeScript | Runtime user dictionary |
| [`python/basic.py`](python/basic.py) | Python package | Morphological analysis and normalized offsets |

## Build and check in-tree

```bash
make examples   # all C/C++ examples
make wasm-test  # builds/tests the package and executes all TypeScript examples
make python-test # tests the package and executes the Python example
./build/bin/suzume_example_cpp "東京に行きました"
```

The C++ examples link the single in-tree `suzume` target. A downstream project
should use `find_package(suzume CONFIG REQUIRED)` as shown below.

## Build against an installed package (find_package)

[`consumer/`](consumer) is a standalone project that finds Suzume with
`find_package(suzume CONFIG REQUIRED)`. It doubles as the packaging smoke test:

```bash
make install PREFIX=/tmp/suzume            # install the library + package config
cmake -S examples/consumer -B build-consumer -DCMAKE_PREFIX_PATH=/tmp/suzume
cmake --build build-consumer
ctest --test-dir build-consumer --output-on-failure
```

`make consumer-smoke` runs the whole install → find_package → run sequence in one step.

## Embedded (no filesystem)

Bake the dictionaries into the binary so the library needs no data files at
runtime:

```bash
make embedded          # -DSUZUME_EMBED_DICT=ON, static, no CLI/tests
```
