#!/usr/bin/env bash
# build.sh — build the native macOS kterm as a .app bundle (no Qt, no CMake).
# Output: ./kterm.app   Run it:  open kterm.app   (or double-click in Finder)
set -euo pipefail
cd "$(dirname "$0")/../.."   # repo root

APP="kterm.app"
MACOS="$APP/Contents/MacOS"
mkdir -p "$MACOS"

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
  -o "$MACOS/kterm"

cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>            <string>kterm</string>
    <key>CFBundleDisplayName</key>     <string>kterm</string>
    <key>CFBundleIdentifier</key>      <string>com.kryptonbytes.kterm</string>
    <key>CFBundleExecutable</key>      <string>kterm</string>
    <key>CFBundlePackageType</key>     <string>APPL</string>
    <key>CFBundleVersion</key>         <string>0.1</string>
    <key>CFBundleShortVersionString</key><string>0.1</string>
    <key>NSPrincipalClass</key>        <string>NSApplication</string>
    <key>NSHighResolutionCapable</key> <true/>
    <key>LSMinimumSystemVersion</key>  <string>11.0</string>
</dict>
</plist>
PLIST

# Ad-hoc sign so the GUI launches cleanly.
codesign --force --sign - "$APP" >/dev/null 2>&1 || true

echo "built ./kterm.app — launch it with:  open kterm.app"
