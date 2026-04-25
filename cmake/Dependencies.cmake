# Digital Workshop - Dependencies
# All dependencies fetched via FetchContent (no manual installation)

include(FetchContent)

# SDL2 - Windowing, input, events
find_package(SDL2 QUIET)
if(SDL2_FOUND)
    message(STATUS "Found system SDL2")
    # System SDL2 uses dynamic library
    set(DW_SDL2_TARGET SDL2::SDL2)
else()
    message(STATUS "SDL2 not found, fetching from GitHub...")
    FetchContent_Declare(
        SDL2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-2.30.0
        GIT_SHALLOW TRUE
    )
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL2)
    set(DW_SDL2_TARGET SDL2::SDL2-static)
endif()

# Dear ImGui with docking branch
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

# Create ImGui library target
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC ${DW_SDL2_TARGET})

# OpenGL
find_package(OpenGL REQUIRED)

# GLAD - OpenGL loader (pre-generated for OpenGL 3.3 Core)
FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v2.0.6
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR cmake
)
FetchContent_MakeAvailable(glad)
glad_add_library(glad_gl33_core STATIC API gl:core=3.3)

# GLM - Math library (header-only)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glm)
if(TARGET glm)
    target_include_directories(glm SYSTEM INTERFACE ${glm_SOURCE_DIR})
endif()

# SQLite3
# macOS system SQLite disables load_extension; always use FetchContent there
if(NOT APPLE)
    find_package(SQLite3 QUIET)
endif()
if(NOT SQLite3_FOUND)
    message(STATUS "SQLite3 not found, fetching from GitHub...")
    FetchContent_Declare(
        sqlite3
        GIT_REPOSITORY https://github.com/azadkuh/sqlite-amalgamation.git
        GIT_TAG 3.38.2
        GIT_SHALLOW TRUE
    )
    set(SQLITE3_SKIP_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(sqlite3)

    # Create SQLite3 target
    add_library(sqlite3 STATIC
        ${sqlite3_SOURCE_DIR}/sqlite3.c
    )
    target_include_directories(sqlite3 PUBLIC ${sqlite3_SOURCE_DIR})
    target_compile_definitions(sqlite3 PRIVATE SQLITE_ENABLE_FTS5 SQLITE_ENABLE_LOAD_EXTENSION)
    add_library(SQLite3::SQLite3 ALIAS sqlite3)
endif()

if(TARGET SQLite3::SQLite3)
    set(DW_SQLITE_TARGET SQLite3::SQLite3)
elseif(TARGET SQLite::SQLite3)
    set(DW_SQLITE_TARGET SQLite::SQLite3)
else()
    message(FATAL_ERROR "SQLite3 target was not created by dependency setup")
endif()

# zlib - Compression (needed for deflate in ZIP/3MF files)
find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
    message(STATUS "ZLIB not found, fetching from GitHub...")
    FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3.1
        GIT_SHALLOW TRUE
    )
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

    # Prevent zlib from installing to system paths during CPack
    # Since zlib is statically linked, we don't need to install its libraries
    set(ZLIB_SKIP_INSTALL_FILES ON CACHE BOOL "" FORCE)
    set(SKIP_INSTALL_ALL ON)

    FetchContent_MakeAvailable(zlib)

    set(SKIP_INSTALL_ALL OFF)

    # Create ZLIB::ZLIB alias if not already defined
    if(NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    endif()
    # Expose include dirs for consumers
    target_include_directories(zlibstatic PUBLIC
        ${zlib_SOURCE_DIR}
        ${zlib_BINARY_DIR}
    )
endif()

# miniz - Compression library for ZIP/3MF files
FetchContent_Declare(
    miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG 3.0.2
    GIT_SHALLOW TRUE
    # Populate the sources only. The pinned upstream CMake requires compatibility
    # removed by CMake 4.x, so Digital Workshop owns the small static target.
    SOURCE_SUBDIR cmake-no-subdir
)
FetchContent_MakeAvailable(miniz)
FetchContent_GetProperties(miniz)
if(NOT TARGET miniz_static)
    # miniz 3.0.2 splits the library into multiple .c files:
    #   miniz.c         - zlib API (deflate/inflate)
    #   miniz_tdef.c    - deflate compressor
    #   miniz_tinfl.c   - inflate decompressor
    #   miniz_zip.c     - ZIP reader/writer
    # All four are required for ZIP operations (mz_zip_*).
    add_library(miniz_static STATIC
        ${miniz_SOURCE_DIR}/miniz.c
        ${miniz_SOURCE_DIR}/miniz_tdef.c
        ${miniz_SOURCE_DIR}/miniz_tinfl.c
        ${miniz_SOURCE_DIR}/miniz_zip.c
    )
    target_include_directories(miniz_static PUBLIC ${miniz_SOURCE_DIR} ${miniz_BINARY_DIR})
    target_compile_definitions(miniz_static PUBLIC MINIZ_EXPORT=)
    if(NOT EXISTS "${miniz_BINARY_DIR}/miniz_export.h")
        file(WRITE "${miniz_BINARY_DIR}/miniz_export.h" "#ifndef MINIZ_EXPORT_H\n#define MINIZ_EXPORT_H\n#endif\n")
    endif()
endif()

# stb - Single-file header libraries (image loading, etc.)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(stb)

# libcurl - HTTP client (needed for Gemini API)
find_package(CURL QUIET)
if(NOT CURL_FOUND)
    message(STATUS "CURL not found, fetching from GitHub...")
    FetchContent_Declare(
        curl
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG curl-8_5_0
        GIT_SHALLOW TRUE
    )
    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(HTTP_ONLY ON CACHE BOOL "" FORCE)
    set(CURL_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(curl)
    # Create the CURL::libcurl target alias if needed
    if(NOT TARGET CURL::libcurl)
        add_library(CURL::libcurl ALIAS libcurl)
    endif()
endif()

# nlohmann/json - JSON library
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(json)

# NFD-extended - Native file dialogs (Linux GTK/portal, Windows, macOS)
FetchContent_Declare(
    nfd
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG v1.2.1
    GIT_SHALLOW TRUE
)
set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nfd)
if(TARGET nfd)
    target_include_directories(nfd SYSTEM INTERFACE ${nfd_SOURCE_DIR}/src/include)
endif()

# GoogleTest (for testing only)
if(DW_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
        GIT_SHALLOW TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# GraphQLite - SQLite extension for Cypher graph queries
# Downloaded as pre-built shared library, loaded at runtime via sqlite3_load_extension()
option(DW_ENABLE_GRAPHQLITE "Download and bundle GraphQLite extension for graph queries" ON)
if(DW_ENABLE_GRAPHQLITE)
    set(GRAPHQLITE_VERSION "0.3.5")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(GRAPHQLITE_ASSET "graphqlite-linux-x86_64.so")
        set(GRAPHQLITE_LIB "graphqlite.so")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(GRAPHQLITE_ASSET "graphqlite-windows-x86_64.dll")
        set(GRAPHQLITE_LIB "graphqlite.dll")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(GRAPHQLITE_ASSET "graphqlite-macos-x86_64.dylib")
        set(GRAPHQLITE_LIB "graphqlite.dylib")
    endif()

    # Download the pre-built shared library directly (not an archive)
    set(GRAPHQLITE_URL "https://github.com/colliery-io/graphqlite/releases/download/v${GRAPHQLITE_VERSION}/${GRAPHQLITE_ASSET}")
    set(GRAPHQLITE_DEST "${CMAKE_BINARY_DIR}/${GRAPHQLITE_LIB}")

    if(NOT EXISTS "${GRAPHQLITE_DEST}")
        message(STATUS "GraphQLite: downloading from ${GRAPHQLITE_URL}")
        file(DOWNLOAD "${GRAPHQLITE_URL}" "${GRAPHQLITE_DEST}" STATUS GRAPHQLITE_DL_STATUS)
        list(GET GRAPHQLITE_DL_STATUS 0 GRAPHQLITE_DL_CODE)
        if(NOT GRAPHQLITE_DL_CODE EQUAL 0)
            list(GET GRAPHQLITE_DL_STATUS 1 GRAPHQLITE_DL_MSG)
            message(WARNING "GraphQLite: download failed (${GRAPHQLITE_DL_MSG}) -- graph queries disabled")
            file(REMOVE "${GRAPHQLITE_DEST}")
        endif()
    endif()

    if(EXISTS "${GRAPHQLITE_DEST}")
        set(DW_GRAPHQLITE_AVAILABLE TRUE)
        set(DW_GRAPHQLITE_LIB_PATH "${GRAPHQLITE_DEST}" CACHE STRING "" FORCE)
        message(STATUS "GraphQLite: found at ${DW_GRAPHQLITE_LIB_PATH}")
    else()
        set(DW_GRAPHQLITE_AVAILABLE FALSE)
        message(WARNING "GraphQLite: library not found -- graph queries disabled")
    endif()
else()
    set(DW_GRAPHQLITE_AVAILABLE FALSE)
    message(STATUS "GraphQLite: disabled via DW_ENABLE_GRAPHQLITE=OFF")
endif()

# Status message
message(STATUS "Dependencies configured:")
message(STATUS "  SDL2:     ${SDL2_VERSION}")
message(STATUS "  ImGui:    docking branch")
message(STATUS "  OpenGL:   ${OPENGL_gl_LIBRARY}")
message(STATUS "  GLM:      1.0.1")
message(STATUS "  SQLite3:  3.38.2")
message(STATUS "  zlib:     ${ZLIB_VERSION_STRING}${zlib_VERSION}")
message(STATUS "  miniz:    3.0.2")
message(STATUS "  stb:      master (header-only)")
message(STATUS "  libcurl:  ${CURL_VERSION_STRING}")
message(STATUS "  nlohmann/json: 3.11.3")
