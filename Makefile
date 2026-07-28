# Suzume Makefile
# Convenience wrapper for CMake build system

.PHONY: help build test mcp-test clean clean-build rebuild format format-check lint configure \
        wasm wasm-configure wasm-dict wasm-test wasm-bench wasm-clean wasm-rebuild dict \
        python-build python-test python-wheel python-sync version-check \
        install uninstall examples embedded consumer-smoke cmake-smoke

# Build directories
BUILD_DIR := build
WASM_BUILD_DIR := build-wasm

# Install prefix used by consumer-smoke. Kept inside the repo (not /tmp) so that it
# matches the gitignored build-* pattern and `make clean` reclaims it too.
SMOKE_PREFIX := $(CURDIR)/build-smoke-prefix

# clang-format command (can be overridden: make CLANG_FORMAT=clang-format-18 format)
CLANG_FORMAT ?= clang-format

# Python environments. `rye run` does not provision on demand the way `uv run`
# did, so every Python target depends on its venv and re-syncs when the lock
# moves. `rye sync` touches the venv directory, which is what make compares.
MCP_VENV := scripts/mcp/.venv
PYBINDING_VENV := bindings/python/.venv

$(MCP_VENV): scripts/mcp/requirements-dev.lock scripts/mcp/pyproject.toml
	cd scripts/mcp && rye sync

$(PYBINDING_VENV): bindings/python/requirements-dev.lock bindings/python/pyproject.toml
	cd bindings/python && rye sync

# Force a re-provision of both Python environments.
python-sync:
	cd scripts/mcp && rye sync --force
	cd bindings/python && rye sync --force

# Default target
.DEFAULT_GOAL := build

help:
	@echo "Suzume Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  make build        - Build the project (default)"
	@echo "  make dict         - Build dictionaries"
	@echo "  make test         - Run all tests (includes dict)"
	@echo "  make mcp-test     - Run MCP server/oracle tests"
	@echo "  make clean        - Remove $(BUILD_DIR) and every scratch build-* directory"
	@echo "  make clean-build  - Remove $(BUILD_DIR) only"
	@echo "  make rebuild      - Clean $(BUILD_DIR) and rebuild"
	@echo "  make format       - Auto-fix format/lint: C++, scripts/MCP, WASM, Python"
	@echo "  make format-check - Check formatting across all languages"
	@echo "  make lint         - Run read-only scripts/MCP, WASM, and Python static checks"
	@echo "  make configure    - Configure CMake"
	@echo "  make python-sync  - Re-provision both rye-managed Python environments"
	@echo "  make version-check - Verify version is consistent across binding manifests"
	@echo "  make cmake-smoke  - Verify supported CMake build/install configurations"
	@echo ""
	@echo "C/C++ integration targets:"
	@echo "  make install      - Install libs + headers + find_package/pkg-config (PREFIX=/usr/local)"
	@echo "  make uninstall    - Remove the installed files (run before 'make clean')"
	@echo "  make examples     - Build the in-tree C and C++ examples"
	@echo "  make embedded     - Build the embedded (no-filesystem, dict baked-in) static library"
	@echo "  make consumer-smoke - Install to a temp prefix and build examples via find_package"
	@echo ""
	@echo "Python binding targets:"
	@echo "  make python-build - Build the shared C-ABI library (libsuzume)"
	@echo "  make python-test  - Run Python binding tests (pytest/ruff/mypy)"
	@echo "  make python-wheel - Build a platform-tagged wheel"
	@echo ""
	@echo "WASM targets (debug info disabled for smaller binary):"
	@echo "  make wasm-configure - Configure the Emscripten build"
	@echo "  make wasm         - Build WASM module (includes wasm-dict)"
	@echo "  make wasm-dict    - Build dictionaries for the WASM link"
	@echo "  make wasm-test    - Run WASM tests"
	@echo "  make wasm-bench   - Measure public-API WASM throughput and memory"
	@echo "  make wasm-clean   - Clean WASM build"
	@echo "  make wasm-rebuild - Clean and rebuild WASM"
	@echo ""
	@echo "Options:"
	@echo "  CMAKE_OPTIONS     - Extra CMake options (e.g., -DENABLE_DEBUG_INFO=OFF)"
	@echo ""
	@echo "  make help         - Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make                                  # Build the project"
	@echo "  make test                             # Run all tests"
	@echo "  make wasm                             # Build WASM module"
	@echo "  make CMAKE_OPTIONS=-DENABLE_DEBUG_INFO=OFF  # Build without debug info"

# Configure CMake (always runs to pick up new test files)
configure:
	@mkdir -p $(BUILD_DIR)
	@cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release $(CMAKE_OPTIONS)

# Build the project
build: configure
	@echo "Building Suzume..."
	cmake --build $(BUILD_DIR) --parallel
	@echo "Build complete!"

# Build dictionaries
dict: build
	@echo "Building dictionaries..."
	cmake --build $(BUILD_DIR) --target build-dict
	@echo "Dictionary build complete!"

# Run tests
test: dict mcp-test
	@echo "Running tests..."
	ctest --test-dir $(BUILD_DIR) --output-on-failure
	@echo "Tests complete!"

# Run the MCP server/oracle test suite from its project root so pytest uses
# scripts/mcp/pyproject.toml and imports the local package correctly.
mcp-test: $(MCP_VENV)
	@echo "Running MCP server/oracle tests..."
	cd scripts/mcp && rye run pytest -q

# Clean the primary build directory only
clean-build:
	@echo "Cleaning $(BUILD_DIR)..."
	rm -rf $(BUILD_DIR)

# Clean the primary build directory plus every scratch build-* directory produced by
# the ad-hoc CMake configurations across the repo (build-wasm, build-python,
# build-embedded, build-smoke, ...). All of them are gitignored.
# Run `make uninstall` BEFORE this if an install needs undoing: the manifest that
# uninstall reads lives under build-install and is removed here.
clean: clean-build
	@echo "Cleaning scratch build directories..."
	rm -rf build-*/ cmake-build-*/
	@echo "Clean complete!"

# Rebuild from scratch. Deliberately only recreates $(BUILD_DIR) — a full clean would
# also drop build-wasm and force an expensive Emscripten rebuild.
rebuild: clean-build build

# ============================================
# C/C++ integration targets
# ============================================

# Install prefix for `make install` (override: make PREFIX=/opt/suzume install)
PREFIX ?= /usr/local

# Build the shared + static libraries, headers, CMake package config, pkg-config,
# and dictionaries, then install them under $(PREFIX) for find_package / pkg-config.
install:
	@echo "Configuring install build (shared + static) ..."
	cmake -B build-install -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED=ON -DBUILD_TESTING=OFF \
		-DCMAKE_INSTALL_PREFIX=$(PREFIX) $(CMAKE_OPTIONS)
	cmake --build build-install --parallel
	cmake --build build-install --target build-dict
	cmake --install build-install
	@echo "Installed suzume under $(PREFIX)"

# Remove what `make install` placed under $(PREFIX), using the manifest CMake wrote.
# Must run before `make clean`, which deletes build-install along with the manifest.
uninstall:
	@if [ -f build-install/install_manifest.txt ]; then \
		xargs rm -f < build-install/install_manifest.txt; \
		echo "Uninstalled suzume from the recorded prefix."; \
	else \
		echo "build-install/install_manifest.txt not found - nothing to uninstall."; \
		echo "It is written by 'make install' and removed by 'make clean'."; \
		exit 1; \
	fi

# Build the in-tree C and C++ examples.
examples: dict
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DSUZUME_BUILD_EXAMPLES=ON $(CMAKE_OPTIONS)
	cmake --build $(BUILD_DIR) --target suzume_example_c suzume_example_cpp --parallel
	@echo "Examples built: $(BUILD_DIR)/bin/suzume_example_{c,cpp}"

# Build the embedded (no-filesystem) configuration: dictionaries baked in, static
# library, no CLI/tests. Useful for hosted-embedded / RTOS targets.
embedded:
	cmake -B build-embedded -DCMAKE_BUILD_TYPE=Release -DSUZUME_EMBED_DICT=ON \
		-DBUILD_TESTING=OFF -DSUZUME_INSTALL=OFF $(CMAKE_OPTIONS)
	cmake --build build-embedded --target suzume --parallel
	@echo "Embedded static library built: build-embedded/lib/"

# Packaging smoke test: install (static) to a throwaway prefix inside the repo, then
# build + run the C/C++ examples against it via find_package.
consumer-smoke:
	rm -rf build-smoke $(SMOKE_PREFIX) build-smoke-consumer
	cmake -B build-smoke -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
		-DCMAKE_INSTALL_PREFIX=$(SMOKE_PREFIX)
	cmake --build build-smoke --parallel
	cmake --build build-smoke --target build-dict
	cmake --install build-smoke
	cmake -S examples/consumer -B build-smoke-consumer -DCMAKE_PREFIX_PATH=$(SMOKE_PREFIX)
	cmake --build build-smoke-consumer
	ctest --test-dir build-smoke-consumer --output-on-failure
	@echo "Consumer smoke test passed."

# Reproducible build/install matrix covering library-only, no-install, and
# multi-config dictionary builds without touching the shared build directories.
cmake-smoke:
	scripts/check_cmake_configurations.sh

# Auto-fix formatting/lint across every language in the repo:
# C++ core (clang-format), MCP/repository scripts (ruff), WASM binding (biome),
# Python binding (ruff).
format: $(MCP_VENV) $(PYBINDING_VENV)
	@echo "Formatting C++ (clang-format)..."
	@find src include tools tests examples -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" -o -name "*.c" \) | xargs $(CLANG_FORMAT) -i
	@echo "Formatting MCP server and repository scripts (ruff)..."
	cd scripts/mcp && rye run ruff format . ../../scripts/*.py && rye run ruff check --fix . ../../scripts/*.py
	@echo "Formatting WASM binding (biome)..."
	cd bindings/wasm && yarn lint:fix
	@echo "Formatting Python binding (ruff)..."
	cd bindings/python && rye run ruff format . && rye run ruff check --fix .
	$(MAKE) lint
	@echo "Format complete!"

# Check-only counterpart for CI (same language fan-out, no writes).
format-check: $(MCP_VENV) $(PYBINDING_VENV)
	@echo "Checking C++ formatting (clang-format)..."
	@find src include tools tests examples -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" -o -name "*.c" \) | xargs $(CLANG_FORMAT) --dry-run --Werror
	@echo "Checking MCP server and repository script formatting (ruff)..."
	cd scripts/mcp && rye run ruff format --check . ../../scripts/*.py
	@echo "Checking Python binding formatting (ruff)..."
	cd bindings/python && rye run ruff format --check .
	$(MAKE) lint
	@echo "Format check passed!"

# Read-only static analysis. This repository does not configure clang-tidy, so
# C++ is covered by clang-format in format-check rather than a nominal lint step.
lint: $(MCP_VENV) $(PYBINDING_VENV)
	@echo "Linting MCP server and repository scripts (ruff)..."
	cd scripts/mcp && rye run ruff check . ../../scripts/*.py
	@echo "Linting WASM binding (biome)..."
	cd bindings/wasm && yarn lint
	@echo "Linting Python binding (ruff)..."
	cd bindings/python && rye run ruff check .

# ============================================
# Python binding targets
# ============================================

# Build shared library + Python package (editable) into a local venv
python-build:
	@echo "Building shared library for Python binding..."
	cmake -B build-shared -DBUILD_SHARED=ON -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release \
		-DENABLE_DEBUG_INFO=OFF -DENABLE_DEBUG_LOG=OFF -DSUZUME_LIB_SOVERSION=OFF
	cmake --build build-shared --target suzume_shared --parallel
	@echo "Shared library built: build-shared/lib/"

# Run the Python binding test suite in the rye-managed venv
python-test: python-build $(PYBINDING_VENV)
	@echo "Running Python binding tests..."
	cd bindings/python && rye run pytest -q \
		&& rye run ruff check . \
		&& rye run ruff format --check . \
		&& rye run mypy src/suzume

# Build a platform-tagged wheel
python-wheel:
	cd bindings/python && ./build_wheel.sh

# Verify the version is consistent across every binding manifest
version-check:
	scripts/check_version_mirror.sh

# ============================================
# WASM Targets
# ============================================

# Configure WASM build (debug info disabled for smaller binary)
wasm-configure:
	@echo "Configuring WASM build..."
	emcmake cmake -B $(WASM_BUILD_DIR) -DBUILD_WASM=ON -DENABLE_DEBUG_INFO=OFF -DCMAKE_BUILD_TYPE=Release

# Build dictionaries for the WASM link using the native CLI.
# Produces the same full data/*.dic that the native build-dict target and CI/publish
# generate, so the embedded WASM dictionary matches every other target exactly.
wasm-dict:
	@if [ ! -f $(BUILD_DIR)/bin/suzume-cli ]; then \
		echo "Native CLI not found. Run 'make build' first."; \
		exit 1; \
	fi
	@echo "Building dictionaries for WASM (git-tracked entries only)..."
	$(BUILD_DIR)/bin/suzume-cli dict compile data/core/*.tsv data/core.dic
	@# The WASM binary ships publicly, so embed only git-tracked user
	@# dictionaries. Any gitignored, local-only dictionary is excluded here
	@# without being named, matching the CI checkout (which lacks ignored files).
	$(BUILD_DIR)/bin/suzume-cli dict compile $$(git ls-files 'data/user/*.tsv') data/user.dic

# Build WASM module
wasm: wasm-dict wasm-configure
	@echo "Building WASM module..."
	@# Force re-link so updated embedded dictionaries are picked up
	@rm -f $(WASM_BUILD_DIR)/bin/suzume.wasm $(WASM_BUILD_DIR)/bin/suzume.js
	cmake --build $(WASM_BUILD_DIR) --parallel
	@echo "WASM build complete!"
	@ls -lh bindings/wasm/dist/suzume.wasm bindings/wasm/dist/suzume.js

# Run WASM tests
wasm-test: wasm
	@echo "Running WASM tests..."
	cd bindings/wasm && yarn build:js && yarn test
	@echo "WASM tests complete!"

# Measure the shipped public JS API, including result decoding. Override the
# workload with WASM_BENCH_ARGS, for example:
#   make wasm-bench WASM_BENCH_ARGS="--iterations=100 --samples=3"
wasm-bench: wasm
	node scripts/measure_wasm_metrics.mjs $(WASM_BENCH_ARGS)

# Clean WASM build
wasm-clean:
	@echo "Cleaning WASM build..."
	rm -rf $(WASM_BUILD_DIR)
	@echo "WASM clean complete!"

# Rebuild WASM from scratch
wasm-rebuild: wasm-clean wasm
