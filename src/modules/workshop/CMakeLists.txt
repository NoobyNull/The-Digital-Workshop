add_library(dw_workshop_core STATIC
    experience_router.cpp
    experience_router.h
    home_flow.cpp
    home_flow.h
    project_item_load_guard.cpp
    project_item_load_guard.h
    project_workshop_controller.cpp
    project_workshop_controller.h
    project_resume.cpp
    project_resume.h
    workshop_contract.h
)

add_library(dw::workshop_core ALIAS dw_workshop_core)

target_include_directories(dw_workshop_core
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    INTERFACE
        ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(dw_workshop_core PUBLIC cxx_std_17)
dw_set_warning_flags(dw_workshop_core)
dw_set_debug_flags(dw_workshop_core)
dw_set_release_flags(dw_workshop_core)
