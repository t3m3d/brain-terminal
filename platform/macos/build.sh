#!/usr/bin/env bash
# build.sh — build the native macOS kterm (no Qt, no CMake required).
# Output: ./kterm-native in the repo root. Run it: ./kterm-native
set -euo pipefail
cd "$(dirname "$0")/../.."   # repo root

clang++ -std=c++20 -fobjc-arc -O2 -Iinclude -Wno-unicode-whitespace \
  src/core/Terminal.cpp \
  src/renderer/Grid.cpp \
  src/parser/AnsiParser.cpp \
  src/pty/PTY.cpp \
  src/pty/PTYPlatform.cpp \
  src/scrollback/ScrollbackBuffer.cpp \
  platform/macos/TermView.mm \
  platform/macos/main_macos.mm \
  -framework Cocoa \
  -o kterm-native

echo "built ./kterm-native — run it with: ./kterm-native"
