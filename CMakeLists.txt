cmake_minimum_required(VERSION 3.21)
project(terk LANGUAGES CXX)

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
qt_add_executable(terk ${SOURCES} ${HEADERS})

# Include directories (modern style)
target_include_directories(terk PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/collect
    ${PROJECT_SOURCE_DIR}/include/render
    ${PROJECT_SOURCE_DIR}/include/platform
    ${PROJECT_SOURCE_DIR}/include/utils
    ${PROJECT_SOURCE_DIR}/include/branding
)

# Link Qt6
target_link_libraries(terk PRIVATE
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
    target_link_libraries(terk PRIVATE
        Ws2_32
        Iphlpapi
        Kernel32
        User32
    )

    if (MSVC)
        # MSVC: a small .rc file embeds the manifest as a Win32 resource.
        # CMake compiles .rc files via rc.exe automatically.
        set(_brain_manifest_rc "${CMAKE_CURRENT_BINARY_DIR}/brain-terminal.rc")
        file(WRITE "${_brain_manifest_rc}"
            "1 24 \"${PROJECT_SOURCE_DIR}/platform/windows/brain-terminal.manifest\"\n")
        target_sources(terk PRIVATE "${_brain_manifest_rc}")
    else()
        # MinGW / clang-on-Windows: windres compiles the .rc.
        target_sources(terk PRIVATE
            "${PROJECT_SOURCE_DIR}/platform/windows/brain-terminal.manifest")
        set_source_files_properties(
            "${PROJECT_SOURCE_DIR}/platform/windows/brain-terminal.manifest"
            PROPERTIES LANGUAGE RC)
    endif()
endif()

if (UNIX AND NOT APPLE)
    target_link_libraries(terk PRIVATE util)
endif()

# config + themes
install(FILES
    resources/config/terk.conf
    DESTINATION share/terk
)

install(DIRECTORY
    resources/themes
    DESTINATION share/terk
)