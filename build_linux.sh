#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -P "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-linux"

CLEAN=0
RUN=0
TEST=0
INSTALL_DESKTOP=0
for arg in "$@"; do
    case "$arg" in
        --clean) CLEAN=1 ;;
        --run)   RUN=1 ;;
        --test)  TEST=1 ;;
        --install-desktop) INSTALL_DESKTOP=1 ;;
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

for tool in cmake make g++; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "build_linux.sh: missing required tool '$tool'" >&2
        echo "  Arch:   sudo pacman -S --needed base-devel cmake" >&2
        echo "  Debian: sudo apt install build-essential cmake" >&2
        exit 1
    fi
done

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

if [[ ! -x "$BUILD_DIR/brain" ]]; then
    echo "build_linux.sh: build succeeded but $BUILD_DIR/brain not found" >&2
    exit 1
fi

size=$(stat -c '%s' "$BUILD_DIR/brain" 2>/dev/null || wc -c <"$BUILD_DIR/brain")
echo "build_linux.sh: built $BUILD_DIR/brain ($size bytes)"

if [[ $TEST -eq 1 ]]; then
    echo "build_linux.sh: running tests (ctest)..."
    cmake --build . --target test_ansi -- -j"$(nproc 2>/dev/null || echo 2)"
    ctest --output-on-failure
fi

if [[ $INSTALL_DESKTOP -eq 1 ]]; then
    echo "build_linux.sh: installing desktop entry + hicolor icons (user)..."
    DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
    install -Dm644 "$SCRIPT_DIR/resources/brain.desktop" "$DATA/applications/brain.desktop"
    sed -i "s|^Exec=.*|Exec=$BUILD_DIR/brain|" "$DATA/applications/brain.desktop"
    for sz in 16 24 32 48 64 128 256; do
        src="$SCRIPT_DIR/resources/icons/hicolor/${sz}x${sz}/apps/brain.png"
        [[ -f "$src" ]] && install -Dm644 "$src" \
            "$DATA/icons/hicolor/${sz}x${sz}/apps/brain.png"
    done
    command -v gtk-update-icon-cache >/dev/null 2>&1 \
        && gtk-update-icon-cache -f -t "$DATA/icons/hicolor" >/dev/null 2>&1 || true
    command -v update-desktop-database >/dev/null 2>&1 \
        && update-desktop-database "$DATA/applications" >/dev/null 2>&1 || true
    echo "build_linux.sh: installed brain.desktop + icons under $DATA"
fi

if [[ $RUN -eq 1 ]]; then
    echo "build_linux.sh: launching brain..."
    exec "$BUILD_DIR/brain"
fi
