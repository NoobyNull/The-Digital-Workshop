set(DW_SOURCE_SIZE_EXEMPTIONS
    "src/ui/fonts/fa_solid_900.h"
    "src/ui/fonts/inter_regular.h"
)

function(dw_collect_production_sources repo_root output_var)
    file(GLOB_RECURSE sources LIST_DIRECTORIES false
        "${repo_root}/src/*.c"
        "${repo_root}/src/*.cc"
        "${repo_root}/src/*.cpp"
        "${repo_root}/src/*.cxx"
        "${repo_root}/src/*.h"
        "${repo_root}/src/*.hpp"
    )

    set(filtered)
    foreach(source IN LISTS sources)
        file(RELATIVE_PATH relative "${repo_root}" "${source}")
        list(FIND DW_SOURCE_SIZE_EXEMPTIONS "${relative}" exemption_index)
        if(exemption_index EQUAL -1)
            list(APPEND filtered "${source}")
        endif()
    endforeach()
    list(SORT filtered)
    set(${output_var} "${filtered}" PARENT_SCOPE)
endfunction()

function(dw_count_physical_lines source output_var)
    file(READ "${source}" contents)
    if(contents STREQUAL "")
        set(line_count 0)
    else()
        string(REGEX REPLACE "[^\n]" "" newline_chars "${contents}")
        string(LENGTH "${newline_chars}" line_count)
        if(NOT contents MATCHES "\n$")
            math(EXPR line_count "${line_count} + 1")
        endif()
    endif()
    set(${output_var} "${line_count}" PARENT_SCOPE)
endfunction()

function(dw_cap_for_path relative_path output_cap output_found)
    set(found false)
    set(cap 0)
    foreach(entry IN LISTS DW_SOURCE_SIZE_CAPS)
        string(REPLACE "|" ";" fields "${entry}")
        list(GET fields 0 cap_path)
        list(GET fields 1 cap_value)
        if(cap_path STREQUAL relative_path)
            set(found true)
            set(cap "${cap_value}")
            break()
        endif()
    endforeach()
    set(${output_cap} "${cap}" PARENT_SCOPE)
    set(${output_found} "${found}" PARENT_SCOPE)
endfunction()
