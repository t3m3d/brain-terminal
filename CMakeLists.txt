cmake_minimum_required(VERSION 3.21)
project(brain LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Warnings
if (MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

file(GLOB_RECURSE SOURCES
    "${PROJECT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE HEADERS
    "${PROJECT_SOURCE_DIR}/include/*.hpp"
)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

qt_add_executable(brain ${SOURCES} ${HEADERS}
    ${PROJECT_SOURCE_DIR}/resources/brain.qrc)

target_include_directories(brain PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/branding
)

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
    set_target_properties(brain PROPERTIES WIN32_EXECUTABLE TRUE)

    target_link_libraries(brain PRIVATE
        Ws2_32
        Iphlpapi
        Kernel32
        User32
    )

    set(_brain_manifest_rc "${CMAKE_CURRENT_BINARY_DIR}/brain-terminal.rc")
    file(TO_NATIVE_PATH
         "${PROJECT_SOURCE_DIR}/platform/windows/brain-terminal.manifest"
         _brain_manifest_native)
    string(REPLACE "\\" "\\\\" _brain_manifest_escaped "${_brain_manifest_native}")
    file(TO_NATIVE_PATH
         "${PROJECT_SOURCE_DIR}/resources/icons/brain.ico"
         _brain_icon_native)
    string(REPLACE "\\" "\\\\" _brain_icon_escaped "${_brain_icon_native}")
    # Manifest (1 24) + icon (IDI_ICON1 = id 1, type 14 ICON). Putting the
    # resolution (16..256) so no size is upscaled at runtime.
    file(WRITE "${_brain_manifest_rc}"
         "IDI_ICON1 ICON \"${_brain_icon_escaped}\"\n"
         "1 24 \"${_brain_manifest_escaped}\"\n")
    target_sources(brain PRIVATE "${_brain_manifest_rc}")
endif()

if (UNIX AND NOT APPLE)
    target_link_libraries(brain PRIVATE util)
endif()

install(FILES
    resources/config/brain.conf
    DESTINATION share/brain
)

install(DIRECTORY
    resources/themes
    resources/icons
    DESTINATION share/brain
)

if (UNIX AND NOT APPLE)
    install(FILES resources/brain.desktop
            DESTINATION share/applications)
    foreach(_sz 16 24 32 48 64 128 256)
        install(FILES resources/icons/hicolor/${_sz}x${_sz}/apps/brain.png
                DESTINATION share/icons/hicolor/${_sz}x${_sz}/apps)
    endforeach()
endif()

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
        ${PROJECT_SOURCE_DIR}/src/core/Sixel.cpp
        ${PROJECT_SOURCE_DIR}/src/parser/AnsiParser.cpp
        ${PROJECT_SOURCE_DIR}/src/renderer/Grid.cpp
    )
    target_include_directories(test_winop PRIVATE ${PROJECT_SOURCE_DIR}/include)
    add_test(NAME window_query COMMAND test_winop)
endif()
