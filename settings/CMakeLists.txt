# Digital Workshop - Settings Application
# Separate executable for editing application settings

# Source files needed from main project (selective recompilation)
set(SETTINGS_DEPS
    ${CMAKE_SOURCE_DIR}/src/core/types.cpp
    ${CMAKE_SOURCE_DIR}/src/core/utils/log.cpp
    ${CMAKE_SOURCE_DIR}/src/core/utils/file_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/utils/string_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/paths/app_paths.cpp
    ${CMAKE_SOURCE_DIR}/src/core/paths/network_location.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config/config.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config/config_layout.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config/config_project_paths.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config/input_binding.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config/layout_preset.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config/layout_migration.cpp
    ${CMAKE_SOURCE_DIR}/src/core/threading/thread_pool.cpp
    ${CMAKE_SOURCE_DIR}/src/core/gcode/machine_profile.cpp
    ${CMAKE_SOURCE_DIR}/src/core/gcode/machine_rigidity.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/theme.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/binding_recorder.cpp
)

set(SETTINGS_SOURCES
    settings_main.cpp
    settings_app.cpp
)

add_executable(dw_settings ${SETTINGS_SOURCES} ${SETTINGS_DEPS})

dw_configure_target(dw_settings)

target_link_libraries(dw_settings PRIVATE
    imgui
    glad_gl33_core
    OpenGL::GL
    glm::glm
    nlohmann_json::nlohmann_json
)
if(UNIX AND NOT APPLE)
    target_link_libraries(dw_settings PRIVATE PkgConfig::DW_DBUS)
endif()

target_include_directories(dw_settings PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)

set_target_properties(dw_settings PROPERTIES
    OUTPUT_NAME "dw_settings"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Install target
install(TARGETS dw_settings RUNTIME DESTINATION bin)
