cmake_minimum_required(VERSION 3.21)
project(brain LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt meta-object compiler: TerminalWidget uses Q_OBJECT, so moc must run to
# generate its vtable / signals-slots. Without this the link fails with
# "undefined reference to vtable for ...".
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Warnings
if (MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Collect all source files recursively
file(GLOB_RECURSE SOURCES
    "${PROJECT_SOURCE_DIR}/src/*.cpp"
)

# Headers too, so AUTOMOC scans the Q_OBJECT classes (TerminalWidget) directly
# rather than relying on include-resolution through the .cpp.
file(GLOB_RECURSE HEADERS
    "${PROJECT_SOURCE_DIR}/include/*.hpp"
)

# Qt6 discovery
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# Executable
qt_add_executable(brain ${SOURCES} ${HEADERS})

# Include directories (modern style)
target_include_directories(brain PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/collect
    ${PROJECT_SOURCE_DIR}/include/render
    ${PROJECT_SOURCE_DIR}/include/platform
    ${PROJECT_SOURCE_DIR}/include/utils
    ${PROJECT_SOURCE_DIR}/include/branding
)

# Link Qt6
target_link_libraries(brain PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# Windows-specific libraries + UTF-8 manifest.
#
# The manifest flips the process ACP to 65001 so byte-pumped output
# from the child shell renders Unicode correctly without per-call IAT
# thunking (see platform/windows/brain-terminal.manifest).
if (WIN32)
    # GUI subsystem — no parent console allocated on launch. Without
    # this, double-clicking brain.exe briefly flashes a cmd-window
    # ghost; worse, brain.exe inherits the launcher's console handles
    # and the ConPTY child can race with the inherited stdio.
    set_target_properties(brain PROPERTIES WIN32_EXECUTABLE TRUE)

    target_link_libraries(brain PRIVATE
        Ws2_32
        Iphlpapi
        Kernel32
        User32
    )

    # Embed the manifest as Win32 resource id 1 type 24 (RT_MANIFEST). Both
    # rc.exe (MSVC) and windres (MinGW) accept the same .rc syntax; we
    # generate one .rc that forwards-references the .manifest and let the
    # active toolchain handle it. windres pukes when handed XML directly,
    # which is why the MinGW path used to fail.
    set(_brain_manifest_rc "${CMAKE_CURRENT_BINARY_DIR}/brain-terminal.rc")
    # Use backslashes in the .rc — windres treats forward slashes as the
    # start of an option in some configurations. file(TO_NATIVE_PATH) gives
    # us OS-correct separators on each platform.
    file(TO_NATIVE_PATH
         "${PROJECT_SOURCE_DIR}/platform/windows/brain-terminal.manifest"
         _brain_manifest_native)
    string(REPLACE "\\" "\\\\" _brain_manifest_escaped "${_brain_manifest_native}")
    file(TO_NATIVE_PATH
         "${PROJECT_SOURCE_DIR}/resources/icons/brain.ico"
         _brain_icon_native)
    string(REPLACE "\\" "\\\\" _brain_icon_escaped "${_brain_icon_native}")
    # Manifest (1 24) + icon (IDI_ICON1 = id 1, type 14 ICON). Putting the
    # app icon FIRST in the .rc makes Windows pick it as the .exe icon
    # shown in Explorer / taskbar pinning / alt-tab. The .ico is multi-
    # resolution (16..256) so no size is upscaled at runtime.
    file(WRITE "${_brain_manifest_rc}"
         "IDI_ICON1 ICON \"${_brain_icon_escaped}\"\n"
         "1 24 \"${_brain_manifest_escaped}\"\n")
    target_sources(brain PRIVATE "${_brain_manifest_rc}")
endif()

if (UNIX AND NOT APPLE)
    target_link_libraries(brain PRIVATE util)
endif()

# config + themes + icons
install(FILES
    resources/config/brain.conf
    DESTINATION share/brain
)

install(DIRECTORY
    resources/themes
    resources/icons
    DESTINATION share/brain
)

# ── Tests (ctest) ──────────────────────────────────────────────────────
# Pure-logic unit tests; no Qt needed. Also the cross-validation oracle for
# the Krypton parser (krypton/linux/brain_ansi.k) — same event format.
# Run with:  cmake --build build-linux --target test_ansi && (cd build-linux && ctest)
option(BUILD_TESTING "Build the tests" ON)
if (BUILD_TESTING)
    enable_testing()
    add_executable(test_ansi
        ${PROJECT_SOURCE_DIR}/tests/test_ansi.cpp
        ${PROJECT_SOURCE_DIR}/src/parser/AnsiParser.cpp
    )
    target_include_directories(test_ansi PRIVATE ${PROJECT_SOURCE_DIR}/include)
    add_test(NAME ansi_parser COMMAND test_ansi)

    add_executable(test_winop
        ${PROJECT_SOURCE_DIR}/tests/test_winop.cpp
        ${PROJECT_SOURCE_DIR}/src/core/Terminal.cpp
        ${PROJECT_SOURCE_DIR}/src/parser/AnsiParser.cpp
        ${PROJECT_SOURCE_DIR}/src/renderer/Grid.cpp
    )
    target_include_directories(test_winop PRIVATE ${PROJECT_SOURCE_DIR}/include)
    add_test(NAME window_query COMMAND test_winop)
endif()
