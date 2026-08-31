# Digital Workshop - Git Version
# Derives build metadata from git tags without allowing an older repository tag
# to roll the source-declared release version backward.
# Tag format: v0.3.0 → SEMVER "0.3.0", or v0.2.2-14-ge2259ce → "0.2.2+14.e2259ce"
# Falls back to project(VERSION ...) from CMakeLists.txt when not in a git repo

find_package(Git QUIET)

set(DW_GIT_HASH "unknown")
set(DW_BUILD_DATE "")
set(DW_SEMVER "${CMAKE_PROJECT_VERSION}")

# Get current date
string(TIMESTAMP DW_BUILD_DATE "%Y-%m-%d")

if(GIT_FOUND)
    # Get git hash
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=7 HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE DW_GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_RESULT
    )

    if(NOT GIT_RESULT EQUAL 0)
        set(DW_GIT_HASH "nogit")
    endif()

    # Check if working directory is dirty
    execute_process(
        COMMAND ${GIT_EXECUTABLE} diff --quiet
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE GIT_DIRTY_RESULT
    )

    if(NOT GIT_DIRTY_RESULT EQUAL 0)
        set(DW_GIT_HASH "${DW_GIT_HASH}-dirty")
    endif()

    # Derive semantic version from git tags
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --match "v*" --always
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_DESCRIBE_RESULT
    )

    if(GIT_DESCRIBE_RESULT EQUAL 0 AND GIT_DESCRIBE MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)(-([0-9]+)-g([0-9a-f]+))?$")
        set(DW_VERSION_MAJOR ${CMAKE_MATCH_1})
        set(DW_VERSION_MINOR ${CMAKE_MATCH_2})
        set(DW_VERSION_PATCH ${CMAKE_MATCH_3})
        set(DW_TAG_VERSION
            "${DW_VERSION_MAJOR}.${DW_VERSION_MINOR}.${DW_VERSION_PATCH}")

        if(DW_TAG_VERSION VERSION_LESS CMAKE_PROJECT_VERSION)
            # A milestone can be prepared before its tag exists. Keep the
            # explicit project() version authoritative in that case.
            set(DW_SEMVER "${CMAKE_PROJECT_VERSION}")
        elseif(CMAKE_MATCH_5)
            # Not on exact tag: v0.2.2-14-ge2259ce → "0.2.2+14.e2259ce"
            set(DW_SEMVER "${DW_VERSION_MAJOR}.${DW_VERSION_MINOR}.${DW_VERSION_PATCH}+${CMAKE_MATCH_5}.${CMAKE_MATCH_6}")
        else()
            # Exact tag: v0.3.0 → "0.3.0"
            set(DW_SEMVER "${DW_VERSION_MAJOR}.${DW_VERSION_MINOR}.${DW_VERSION_PATCH}")
        endif()

        if(NOT DW_TAG_VERSION VERSION_LESS CMAKE_PROJECT_VERSION)
            # A matching or newer release tag remains authoritative.
            set(CMAKE_PROJECT_VERSION "${DW_TAG_VERSION}")
            set(CMAKE_PROJECT_VERSION_MAJOR "${DW_VERSION_MAJOR}")
            set(CMAKE_PROJECT_VERSION_MINOR "${DW_VERSION_MINOR}")
            set(CMAKE_PROJECT_VERSION_PATCH "${DW_VERSION_PATCH}")
            set(PROJECT_VERSION "${DW_TAG_VERSION}")
            set(PROJECT_VERSION_MAJOR "${DW_VERSION_MAJOR}")
            set(PROJECT_VERSION_MINOR "${DW_VERSION_MINOR}")
            set(PROJECT_VERSION_PATCH "${DW_VERSION_PATCH}")
        endif()
    endif()
else()
    set(DW_GIT_HASH "nogit")
endif()

# Configure version header
configure_file(
    ${CMAKE_SOURCE_DIR}/src/version.h.in
    ${CMAKE_BINARY_DIR}/generated/version.h
    @ONLY
)

# Make generated header available
include_directories(${CMAKE_BINARY_DIR}/generated)

message(STATUS "Version: ${DW_SEMVER} (${DW_GIT_HASH}, ${DW_BUILD_DATE})")
