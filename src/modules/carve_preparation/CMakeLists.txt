add_library(dw_carve_preparation STATIC
    prepare_carve_flow.cpp
    prepare_carve_flow.h
    preparation_identity.cpp
    preparation_identity.h
    preparation_step_guidance.cpp
    preparation_step_guidance.h
)

add_library(dw::carve_preparation ALIAS dw_carve_preparation)

target_include_directories(dw_carve_preparation
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    INTERFACE
        ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(dw_carve_preparation PUBLIC cxx_std_17)
target_link_libraries(dw_carve_preparation PUBLIC dw_workshop_core)
dw_set_warning_flags(dw_carve_preparation)
dw_set_debug_flags(dw_carve_preparation)
dw_set_release_flags(dw_carve_preparation)
