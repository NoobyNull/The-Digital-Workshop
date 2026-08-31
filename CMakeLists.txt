# Digital Workshop - Root CMakeLists.txt
# Cross-platform C++17 desktop application
# Stack: SDL2 + Dear ImGui (docking) + OpenGL 3.3 + SQLite3

cmake_minimum_required(VERSION 3.20)

# Keep vendored FetchContent projects with older cmake_minimum_required()
# declarations from emitting compatibility warnings under newer CMake releases.
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)

# Project definition (C needed for SQLite3)
project(DigitalWorkshop
    VERSION 0.8.0
    DESCRIPTION "Project-centered CNC workshop for hobbyists"
    LANGUAGES CXX C
)

# C++17 required
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for clang tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Static CRT on MSVC so users don't need VC++ redistributable
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Options
option(DW_BUILD_TESTS "Build test suite" ON)
option(DW_BUILD_TOOLS "Build developer tools (matgen, etc.)" OFF)
option(DW_ENABLE_GRAPHQLITE "Enable GraphQLite extension for graph queries" ON)

# Explicit, non-ALL architecture ratchet. Source archives without Git metadata
# can still build normally; developers and CI invoke this target deliberately.
add_custom_target(check_source_sizes
    COMMAND ${CMAKE_COMMAND}
        -DDW_REPO_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/CheckSourceSizes.cmake
    COMMENT "Checking production source-size caps"
    VERBATIM
)

# Include CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerFlags)
include(Dependencies)
include(GitVersion)

# Main executable sources will be in src/
add_subdirectory(src)

# Settings application (separate process)
add_subdirectory(settings)

# Developer tools (material generator, etc.)
if(DW_BUILD_TOOLS)
    add_subdirectory(tools/matgen)
    add_subdirectory(tools/texgen)
endif()

# Tests
if(DW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Packaging (CPack)
set(CPACK_PACKAGE_NAME "DigitalWorkshop")
set(CPACK_PACKAGE_VENDOR "Digital Workshop")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "DigitalWorkshop")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt")

if(WIN32)
    # Windows: WiX installer + ZIP portable
    set(CPACK_GENERATOR "WIX;ZIP")

    # WiX installer settings
    set(CPACK_WIX_UPGRADE_GUID "8F4E2A1B-3C5D-4E6F-A7B8-9C0D1E2F3A4B")
    # set(CPACK_WIX_PRODUCT_ICON "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/icon.ico")
    set(CPACK_WIX_PROGRAM_MENU_FOLDER "Digital Workshop")
    set(CPACK_WIX_PATCH_FILE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/wix_patch.xml")
    set(CPACK_WIX_PROPERTY_ARPHELPLINK "https://github.com/NoobyNull/The-Digital-Workshop")

elseif(APPLE)
    # macOS: DMG disk image
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "DigitalWorkshop")
    set(CPACK_BUNDLE_NAME "Digital Workshop")

else()
    # TGZ archive for Linux
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)

# Summary
message(STATUS "")
message(STATUS "Digital Workshop Configuration:")
message(STATUS "  Version:      ${PROJECT_VERSION}")
message(STATUS "  Git Hash:     ${DW_GIT_HASH}")
message(STATUS "  Build Type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Tests:        ${DW_BUILD_TESTS}")
message(STATUS "  Tools:        ${DW_BUILD_TOOLS}")
message(STATUS "")
