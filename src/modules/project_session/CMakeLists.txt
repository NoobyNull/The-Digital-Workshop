add_library(dw_project_session STATIC
    project_session.cpp
    project_session.h
)

add_library(dw::project_session ALIAS dw_project_session)

target_include_directories(dw_project_session
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    INTERFACE
        ${CMAKE_SOURCE_DIR}/src
)

target_compile_features(dw_project_session PUBLIC cxx_std_17)
target_link_libraries(dw_project_session PUBLIC dw_workshop_core)
dw_set_warning_flags(dw_project_session)
dw_set_debug_flags(dw_project_session)
dw_set_release_flags(dw_project_session)
