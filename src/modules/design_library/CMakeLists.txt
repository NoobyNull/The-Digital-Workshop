add_library(dw_design_library STATIC
    library_card_layout.cpp
    library_card_layout.h
    library_picker_flow.cpp
    library_picker_flow_completion.cpp
    library_picker_flow.h
    library_picker_presentation.cpp
    library_picker_presentation.h
)

add_library(dw::design_library ALIAS dw_design_library)

target_include_directories(dw_design_library
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    INTERFACE
        ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(dw_design_library PUBLIC cxx_std_17)
target_link_libraries(dw_design_library PUBLIC dw_workshop_core)
dw_set_warning_flags(dw_design_library)
dw_set_debug_flags(dw_design_library)
dw_set_release_flags(dw_design_library)
