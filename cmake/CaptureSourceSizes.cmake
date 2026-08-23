if(NOT DEFINED DW_REPO_ROOT)
    message(FATAL_ERROR "DW_REPO_ROOT is required")
endif()
if(NOT DEFINED DW_OUTPUT)
    message(FATAL_ERROR "DW_OUTPUT is required")
endif()
if(EXISTS "${DW_OUTPUT}" AND NOT DW_FORCE)
    message(FATAL_ERROR
        "Snapshot already exists: ${DW_OUTPUT}. Choose a new session file or pass -DDW_FORCE=ON intentionally.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/SourceSizePolicy.cmake")
dw_collect_production_sources("${DW_REPO_ROOT}" sources)

get_filename_component(output_dir "${DW_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")
file(WRITE "${DW_OUTPUT}" "# sha256|lines|path\n")

foreach(source IN LISTS sources)
    file(RELATIVE_PATH relative "${DW_REPO_ROOT}" "${source}")
    dw_count_physical_lines("${source}" line_count)
    file(SHA256 "${source}" source_hash)
    file(APPEND "${DW_OUTPUT}" "${source_hash}|${line_count}|${relative}\n")
endforeach()

list(LENGTH sources source_count)
message(STATUS "Captured ${source_count} production source files in ${DW_OUTPUT}")
