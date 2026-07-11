# Suzume Makefile
# Convenience wrapper for CMake build system

.PHONY: help build test clean rebuild format format-check configure \
        wasm wasm-dict wasm-test wasm-clean wasm-rebuild dict \
        python-build python-test python-wheel version-check

# Build directories
BUILD_DIR := build
WASM_BUILD_DIR := build-wasm

# clang-format command (can be overridden: make CLANG_FORMAT=clang-format-18 format)
CLANG_FORMAT ?= clang-format

# Default target
.DEFAULT_GOAL := build

help:
	@echo "Suzume Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  make build        - Build the project (default)"
	@echo "  make dict         - Build dictionaries"
	@echo "  make test         - Run all tests (includes dict)"
	@echo "  make clean        - Clean build directory"
	@echo "  make rebuild      - Clean and rebuild"
	@echo "  make format       - Auto-fix format/lint: C++, MCP, WASM, Python bindings"
	@echo "  make format-check - Check formatting across all languages"
	@echo "  make configure    - Configure CMake"
	@echo "  make version-check - Verify version is consistent across binding manifests"
	@echo ""
	@echo "Python binding targets:"
	@echo "  make python-build - Build the shared C-ABI library (libsuzume)"
	@echo "  make python-test  - Run Python binding tests (pytest/ruff/mypy)"
	@echo "  make python-wheel - Build a platform-tagged wheel"
	@echo ""
	@echo "WASM targets (debug info disabled for smaller binary):"
	@echo "  make wasm         - Build WASM module (includes wasm-dict)"
	@echo "  make wasm-dict    - Build dictionaries for WASM (--filter-trivial)"
	@echo "  make wasm-test    - Run WASM tests"
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
test: dict
	@echo "Running tests..."
	ctest --test-dir $(BUILD_DIR) --output-on-failure
	@echo "Tests complete!"

# Clean build directory
clean:
	@echo "Cleaning build directory..."
	rm -rf $(BUILD_DIR)
	@echo "Clean complete!"

# Rebuild from scratch
rebuild: clean build

# Auto-fix formatting/lint across every language in the repo:
# C++ core (clang-format), MCP server (ruff), WASM binding (biome), Python binding (ruff).
format:
	@echo "Formatting C++ (clang-format)..."
	@find src include tests examples -type f \( -name "*.cpp" -o -name "*.h" \) | xargs $(CLANG_FORMAT) -i
	@echo "Formatting MCP server (ruff)..."
	cd scripts/mcp && uv run ruff format . && uv run ruff check --fix .
	@echo "Formatting WASM binding (biome)..."
	cd bindings/wasm && yarn lint:fix
	@echo "Formatting Python binding (ruff)..."
	cd bindings/python && uv run --extra dev ruff format . && uv run --extra dev ruff check --fix .
	@echo "Format complete!"

# Check-only counterpart for CI (same language fan-out, no writes).
format-check:
	@echo "Checking C++ formatting (clang-format)..."
	@find src include tests examples -type f \( -name "*.cpp" -o -name "*.h" \) | xargs $(CLANG_FORMAT) --dry-run --Werror
	@echo "Checking MCP server formatting (ruff)..."
	cd scripts/mcp && uv run ruff format --check . && uv run ruff check .
	@echo "Checking WASM binding formatting (biome)..."
	cd bindings/wasm && yarn lint
	@echo "Checking Python binding formatting (ruff)..."
	cd bindings/python && uv run --extra dev ruff format --check . && uv run --extra dev ruff check .
	@echo "Format check passed!"

# ============================================
# Python binding targets
# ============================================

# Build shared library + Python package (editable) into a local venv
python-build:
	@echo "Building shared library for Python binding..."
	cmake -B build-shared -DBUILD_SHARED=ON -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release \
		-DENABLE_DEBUG_INFO=OFF -DENABLE_DEBUG_LOG=OFF
	cmake --build build-shared --target suzume_shared --parallel
	@echo "Shared library built: build-shared/lib/"

# Run the Python binding test suite via uv (auto-provisions dev deps)
python-test: python-build
	@echo "Running Python binding tests..."
	cd bindings/python && uv run --extra dev pytest -q \
		&& uv run --extra dev ruff check . \
		&& uv run --extra dev ruff format --check . \
		&& uv run --extra dev mypy src/suzume

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

# Build WASM dictionaries using native CLI with --filter-trivial
wasm-dict:
	@if [ ! -f $(BUILD_DIR)/bin/suzume-cli ]; then \
		echo "Native CLI not found. Run 'make build' first."; \
		exit 1; \
	fi
	@echo "Building dictionaries for WASM (--filter-trivial)..."
	$(BUILD_DIR)/bin/suzume-cli dict compile data/core/*.tsv data/core.dic
	$(BUILD_DIR)/bin/suzume-cli dict compile --filter-trivial data/user/*.tsv data/user.dic
	@echo "[WASM] user.dic built with --filter-trivial (trivial entries removed to reduce binary size)"

# Build WASM module
wasm: wasm-dict wasm-configure
	@echo "Building WASM module..."
	@# Force re-link so updated embedded dictionaries are picked up
	@rm -f $(WASM_BUILD_DIR)/bin/suzume-wasm.wasm $(WASM_BUILD_DIR)/bin/suzume-wasm.js
	cmake --build $(WASM_BUILD_DIR) --parallel
	@echo "WASM build complete!"
	@ls -lh bindings/wasm/dist/suzume-wasm.wasm bindings/wasm/dist/suzume.js

# Run WASM tests
wasm-test: wasm
	@echo "Running WASM tests..."
	cd bindings/wasm && yarn build:js && yarn test
	@echo "WASM tests complete!"

# Clean WASM build
wasm-clean:
	@echo "Cleaning WASM build..."
	rm -rf $(WASM_BUILD_DIR)
	@echo "WASM clean complete!"

# Rebuild WASM from scratch
wasm-rebuild: wasm-clean wasm
