if(NOT DEFINED DW_REPO_ROOT)
    message(FATAL_ERROR "DW_REPO_ROOT is required")
endif()

if(NOT DEFINED DW_SOURCE_HARD_LIMIT)
    set(DW_SOURCE_HARD_LIMIT 750)
endif()
if(NOT DEFINED DW_SOURCE_TARGET)
    set(DW_SOURCE_TARGET 500)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/SourceSizeCaps.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/SourceSizePolicy.cmake")
dw_collect_production_sources("${DW_REPO_ROOT}" sources)

set(session_entries)
if(DEFINED DW_SESSION_SNAPSHOT)
    if(NOT EXISTS "${DW_SESSION_SNAPSHOT}")
        message(FATAL_ERROR "Session snapshot not found: ${DW_SESSION_SNAPSHOT}")
    endif()
    file(STRINGS "${DW_SESSION_SNAPSHOT}" session_entries REGEX "^[^#].*\\|.*\\|.*$")
endif()

set(failures)
set(warning_count 0)
set(checked_count 0)

foreach(source IN LISTS sources)
    math(EXPR checked_count "${checked_count} + 1")
    file(RELATIVE_PATH relative "${DW_REPO_ROOT}" "${source}")
    dw_count_physical_lines("${source}" line_count)
    dw_cap_for_path("${relative}" cap cap_found)

    if(cap_found)
        if(line_count GREATER cap)
            list(APPEND failures "${relative}: ${line_count} lines exceeds legacy cap ${cap}")
        elseif(line_count LESS cap)
            list(APPEND failures
                "${relative}: ${line_count} lines is below stale cap ${cap}; lower SourceSizeCaps.cmake")
        endif()
    elseif(line_count GREATER DW_SOURCE_HARD_LIMIT)
        list(APPEND failures
            "${relative}: ${line_count} lines exceeds hard limit ${DW_SOURCE_HARD_LIMIT}")
    elseif(line_count GREATER DW_SOURCE_TARGET)
        math(EXPR warning_count "${warning_count} + 1")
        message(STATUS
            "Source-size target warning: ${relative} has ${line_count} lines (target ${DW_SOURCE_TARGET})")
    endif()

    if(session_entries)
        set(start_found false)
        foreach(entry IN LISTS session_entries)
            string(REPLACE "|" ";" fields "${entry}")
            list(GET fields 0 start_hash)
            list(GET fields 1 start_lines)
            list(GET fields 2 start_path)
            if(start_path STREQUAL relative)
                set(start_found true)
                file(SHA256 "${source}" current_hash)
                if(NOT current_hash STREQUAL start_hash)
                    if(start_lines GREATER DW_SOURCE_HARD_LIMIT AND
                       line_count GREATER_EQUAL start_lines)
                        list(APPEND failures
                            "${relative}: changed legacy file must shrink below session start ${start_lines}, now ${line_count}")
                    elseif(start_lines LESS_EQUAL DW_SOURCE_HARD_LIMIT AND
                           line_count GREATER DW_SOURCE_HARD_LIMIT)
                        list(APPEND failures
                            "${relative}: changed file crossed hard limit ${DW_SOURCE_HARD_LIMIT}")
                    endif()
                endif()
                break()
            endif()
        endforeach()

        if(NOT start_found AND line_count GREATER DW_SOURCE_HARD_LIMIT)
            list(APPEND failures
                "${relative}: new session file exceeds hard limit ${DW_SOURCE_HARD_LIMIT}")
        endif()
    endif()
endforeach()

if(failures)
    list(JOIN failures "\n  - " failure_text)
    message(FATAL_ERROR "Source-size policy failed:\n  - ${failure_text}")
endif()

message(STATUS
    "Source-size policy passed: ${checked_count} files checked, ${warning_count} target warnings")
