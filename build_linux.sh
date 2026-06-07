#!/usr/bin/env bash
# build_linux.sh — build terk on Linux (Arch / Debian / Ubuntu / WSL2).
#
# terk is a Qt6 / C++20 terminal app. Its CMakeLists.txt is already
# cross-platform (handles UNIX-not-APPLE via the libutil link); this
# script just standardises the cmake invocation and the post-build
# verify.
#
# Usage:
#   ./build_linux.sh              # configure + build into ./build-linux
#   ./build_linux.sh --run        # ... then launch ./build-linux/terk
#   ./build_linux.sh --clean      # wipe build-linux first
#
# Prereqs: see BUILD_LINUX.md. Short version:
#   Arch:   sudo pacman -S --needed base-devel cmake qt6-base
#   Debian: sudo apt install build-essential cmake qt6-base-dev libutil-dev

set -euo pipefail

SCRIPT_DIR="$(cd -P "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-linux"

CLEAN=0
RUN=0
for arg in "$@"; do
    case "$arg" in
        --clean) CLEAN=1 ;;
        --run)   RUN=1 ;;
        -h|--help)
            grep '^#' "${BASH_SOURCE[0]}" | sed 's|^# \?||'
            exit 0 ;;
        *)
            echo "build_linux.sh: unknown arg '$arg'" >&2
            exit 1 ;;
    esac
done

if [[ $CLEAN -eq 1 && -d "$BUILD_DIR" ]]; then
    echo "build_linux.sh: wiping $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

# Sanity-check toolchain before cmake (better error message than cmake's).
for tool in cmake make g++; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "build_linux.sh: missing required tool '$tool'" >&2
        echo "  Arch:   sudo pacman -S --needed base-devel cmake" >&2
        echo "  Debian: sudo apt install build-essential cmake" >&2
        exit 1
    fi
done

# Qt6 path probing — cmake will give a clearer error if missing, but
# surface it up front so users don't waste a minute on the configure step.
# `cmake --find-package` is deprecated and unreliable (false negatives on
# modern CMake/Qt). Probe with pkg-config, then fall back to looking for
# Qt6Config.cmake in the usual lib dirs.
if ! pkg-config --exists Qt6Core 2>/dev/null \
   && ! ls /usr/lib*/cmake/Qt6/Qt6Config.cmake /usr/lib/*/cmake/Qt6/Qt6Config.cmake >/dev/null 2>&1; then
    echo "build_linux.sh: Qt6 not found - install qt6-base." >&2
    echo "  Arch:   sudo pacman -S --needed qt6-base" >&2
    echo "  Debian: sudo apt install qt6-base-dev" >&2
    exit 1
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "build_linux.sh: configuring (Release)..."
cmake -DCMAKE_BUILD_TYPE=Release "$SCRIPT_DIR"

echo "build_linux.sh: building (-j$(nproc 2>/dev/null || echo 2))..."
cmake --build . -- -j"$(nproc 2>/dev/null || echo 2)"

if [[ ! -x "$BUILD_DIR/terk" ]]; then
    echo "build_linux.sh: build succeeded but $BUILD_DIR/terk not found" >&2
    exit 1
fi

size=$(stat -c '%s' "$BUILD_DIR/terk" 2>/dev/null || wc -c <"$BUILD_DIR/terk")
echo "build_linux.sh: built $BUILD_DIR/terk ($size bytes)"

if [[ $RUN -eq 1 ]]; then
    echo "build_linux.sh: launching terk..."
    exec "$BUILD_DIR/terk"
fi
