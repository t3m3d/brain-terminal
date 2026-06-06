#!/usr/bin/env bash
# Build the native macOS brain as a .app bundle. No Qt, no CMake.
# Output: ./brain.app   Run it:  open brain.app   (or double-click in Finder)
set -euo pipefail
cd "$(dirname "$0")/../.."   # repo root

APP="brain.app"
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
  platform/macos/MetalRenderer.mm \
  platform/macos/Config.mm \
  platform/macos/main_macos.mm \
  -framework Cocoa -framework Metal -framework QuartzCore \
  -o "$MACOS/brain"

cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>            <string>brain</string>
    <key>CFBundleDisplayName</key>     <string>brain</string>
    <key>CFBundleIdentifier</key>      <string>com.kryptonbytes.brain</string>
    <key>CFBundleExecutable</key>      <string>brain</string>
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

echo "built ./brain.app  (open brain.app)"
