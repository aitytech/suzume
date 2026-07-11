#!/bin/bash
# Build the suzume shared library, compile the dictionaries, bundle both into the
# Python package, and produce a platform-tagged wheel.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
# Dedicated build dir so this never clobbers the dev build/ (which keeps tests on).
PYTHON_PKG="$SCRIPT_DIR/src/suzume"
BUILD_DIR="$PROJECT_ROOT/build-python"

echo "=== Building suzume shared library + CLI (Release) ==="
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED=ON -DBUILD_TESTING=OFF \
    -DENABLE_DEBUG_INFO=OFF -DENABLE_DEBUG_LOG=OFF "$PROJECT_ROOT"
cmake --build "$BUILD_DIR" --target suzume_shared suzume-cli --parallel

echo "=== Compiling dictionaries ==="
cmake --build "$BUILD_DIR" --target build-dict

echo "=== Bundling shared library + dictionaries into the package ==="
if [[ "$(uname)" == "Darwin" ]]; then
    LIB_NAME="libsuzume.dylib"
    cp "$BUILD_DIR/lib/$LIB_NAME" "$PYTHON_PKG/"
    install_name_tool -id "@loader_path/$LIB_NAME" "$PYTHON_PKG/$LIB_NAME" 2>/dev/null || true
elif [[ "$(uname)" == "Linux" ]]; then
    LIB_NAME="libsuzume.so"
    cp "$BUILD_DIR/lib/$LIB_NAME" "$PYTHON_PKG/"
else
    echo "Error: unsupported platform $(uname)" >&2
    exit 1
fi
echo "Bundled $LIB_NAME"

cp "$PROJECT_ROOT/data/core.dic" "$PYTHON_PKG/"
cp "$PROJECT_ROOT/data/user.dic" "$PYTHON_PKG/"
echo "Bundled core.dic + user.dic"

echo "=== Building wheel ==="
cd "$SCRIPT_DIR"
rm -rf dist/
python3 -m pip wheel . --no-deps -w dist/

echo "=== Re-tagging wheel with platform tag ==="
if [[ "$(uname)" == "Darwin" ]]; then
    ARCH="$(uname -m)"
    if [[ "$ARCH" == "arm64" ]]; then
        PLAT_TAG="macosx_11_0_arm64"
    else
        PLAT_TAG="macosx_10_15_x86_64"
    fi
elif [[ "$(uname)" == "Linux" ]]; then
    PLAT_TAG="manylinux_2_17_$(uname -m)"
fi

python3 -m wheel tags --platform-tag "$PLAT_TAG" --remove dist/*.whl

echo "=== Done ==="
ls -lh dist/*.whl 2>/dev/null
