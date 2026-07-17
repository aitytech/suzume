# Suzume C / C++ examples

Minimal programs showing how to consume Suzume from native code.

| File | Language | API |
|------|----------|-----|
| [`hello.c`](hello.c) | C | C ABI (`suzume/suzume_c.h`) |
| [`hello.cpp`](hello.cpp) | C++ | header-only wrapper (`suzume/suzume.hpp`) |

## Build in-tree

```bash
make examples
./build/bin/suzume_example_cpp "東京に行きました"
```

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
