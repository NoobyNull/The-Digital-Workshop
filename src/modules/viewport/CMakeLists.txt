add_library(dw_viewport_presentation STATIC
    viewport_presentation.cpp
    viewport_presentation.h
)

add_library(dw::viewport_presentation ALIAS dw_viewport_presentation)

target_include_directories(dw_viewport_presentation
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    INTERFACE
        ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(dw_viewport_presentation PUBLIC cxx_std_17)
dw_set_warning_flags(dw_viewport_presentation)
dw_set_debug_flags(dw_viewport_presentation)
dw_set_release_flags(dw_viewport_presentation)
