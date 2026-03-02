#!/usr/bin/env bash
# Notepad++ Linux port build script
# Usage: ./build.sh [--clean] [--debug] [--jobs N] [--install]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
BUILD_TYPE="Release"
CLEAN=0
INSTALL=0
JOBS=$(nproc 2>/dev/null || echo 4)

print_usage() {
    echo "Usage: $0 [--clean] [--debug] [--jobs N] [--install]"
    echo "  --clean    Remove build directory before building"
    echo "  --debug    Build in Debug mode"
    echo "  --jobs N   Number of parallel jobs (default: $(nproc))"
    echo "  --install  Install after building (may require sudo)"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean)   CLEAN=1 ;;
        --debug)   BUILD_TYPE="Debug" ;;
        --jobs)    JOBS="$2"; shift ;;
        --install) INSTALL=1 ;;
        --help|-h) print_usage; exit 0 ;;
        *) echo "Unknown option: $1"; print_usage; exit 1 ;;
    esac
    shift
done

check_dep() {
    if ! command -v "$1" &>/dev/null; then
        echo "ERROR: '$1' not found. Install it with: sudo apt install $2"
        exit 1
    fi
}

check_dep cmake cmake
check_dep pkg-config pkg-config
check_dep g++ build-essential

if ! pkg-config --exists gtk+-3.0; then
    echo "ERROR: GTK3 development libraries not found."
    echo "Install with: sudo apt install libgtk-3-dev"
    exit 1
fi

echo "============================================"
echo "  Notepad++ Linux Port Builder"
echo "  Build type : $BUILD_TYPE"
echo "  Jobs       : $JOBS"
echo "  Build dir  : $BUILD_DIR"
echo "============================================"

if [[ $CLEAN -eq 1 ]]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake "$SCRIPT_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX=/usr/local

cmake --build . --parallel "$JOBS"

echo ""
echo "Build successful!"
echo "Binary: $BUILD_DIR/notepad++"
echo ""
echo "Run with: $BUILD_DIR/notepad++ [file...]"

if [[ $INSTALL -eq 1 ]]; then
    echo ""
    echo "Installing to /usr/local/bin ..."
    cmake --install . --prefix /usr/local
    echo "Done."
fi
