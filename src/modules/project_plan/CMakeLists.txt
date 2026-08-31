add_library(dw_project_plan STATIC
    project_plan.cpp
    project_plan.h
    project_plan_detail.cpp
    project_plan_detail.h
    project_plan_presentation.cpp
    project_plan_presentation.h
)

add_library(dw::project_plan ALIAS dw_project_plan)

target_include_directories(dw_project_plan
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    INTERFACE
        ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(dw_project_plan PUBLIC cxx_std_17)
target_link_libraries(dw_project_plan PUBLIC dw_workshop_core)
dw_set_warning_flags(dw_project_plan)
dw_set_debug_flags(dw_project_plan)
dw_set_release_flags(dw_project_plan)
