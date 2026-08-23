add_library(dw_run_coordination STATIC
    run_coordinator.cpp
    run_coordinator.h
    run_package.cpp
    run_package.h
)

add_library(dw::run_coordination ALIAS dw_run_coordination)

target_include_directories(dw_run_coordination
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    INTERFACE
        ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(dw_run_coordination PUBLIC cxx_std_17)
target_link_libraries(dw_run_coordination PUBLIC
    dw_carve_preparation
    dw_workshop_core
)
dw_set_warning_flags(dw_run_coordination)
dw_set_debug_flags(dw_run_coordination)
dw_set_release_flags(dw_run_coordination)
