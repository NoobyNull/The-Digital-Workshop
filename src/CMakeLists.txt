# Digital Workshop - Source CMakeLists.txt
# Main executable target

# Collect source files
set(DW_SOURCES
    main.cpp
    app/application.cpp
    app/application_wiring.cpp
    app/application_wiring_cnc.cpp
    app/application_callbacks.cpp
    app/window.cpp
    app/workspace.cpp

    # Managers (extracted from Application)
    managers/ui_manager.cpp
    managers/ui_manager_menus.cpp
    managers/ui_manager_layout.cpp
    managers/file_io_manager.cpp
    managers/config_manager.cpp

    # Core types
    core/types.cpp

    # Core utilities
    core/utils/log.cpp
    core/utils/file_utils.cpp
    core/utils/string_utils.cpp
    core/utils/gemini_http.cpp
    core/utils/unit_conversion.cpp
    core/utils/board_foot.cpp

    # Threading
    core/threading/main_thread_queue.cpp
    core/threading/thread_pool.cpp

    # Core paths
    core/paths/app_paths.cpp
    core/paths/path_resolver.cpp
    core/paths/path_recovery.cpp

    # Core config
    core/config/config.cpp
    core/config/config_watcher.cpp
    core/config/input_binding.cpp
    core/config/layout_preset.cpp
    core/config/settings_archive.cpp

    # Database
    core/database/database.cpp
    core/database/connection_pool.cpp
    core/database/schema.cpp
    core/database/statement_helper.cpp
    core/database/model_repository.cpp
    core/database/material_repository.cpp
    core/database/project_repository.cpp
    core/database/gcode_repository.cpp
    core/database/job_repository.cpp
    core/database/cost_repository.cpp
    core/database/stock_size_repository.cpp
    core/database/rate_category_repository.cpp
    core/database/cut_plan_repository.cpp
    core/database/tool_database.cpp
    core/database/toolbox_repository.cpp

    # Graph
    core/graph/graph_manager.cpp

    # Mesh
    core/mesh/mesh.cpp
    core/mesh/hash.cpp

    # Loaders
    core/loaders/loader_factory.cpp
    core/loaders/stl_loader.cpp
    core/loaders/obj_loader.cpp
    core/loaders/threemf_loader.cpp
    core/loaders/gcode_loader.cpp
    core/loaders/texture_loader.cpp

    # Project
    core/project/project.cpp
    core/project/gcode_project_context.cpp
    core/project/project_open_item_warnings.cpp
    core/project/project_directory.cpp
    core/project/project_costing_io.cpp
    core/project/costing_engine.cpp

    # Storage
    core/storage/storage_manager.cpp

    # Import
    core/import/import_queue.cpp
    core/import/import_log.cpp
    core/import/background_tagger.cpp
    core/import/file_handler.cpp
    core/import/file_validator.cpp
    core/import/filesystem_detector.cpp

    # Library
    core/library/library_manager.cpp

    # Export
    core/export/model_exporter.cpp
    core/export/project_export_manager.cpp
    core/export/project_import.cpp

    # G-code
    core/gcode/gcode_parser.cpp
    core/gcode/gcode_analyzer.cpp
    core/gcode/gcode_modal_scanner.cpp
    core/gcode/machine_profile.cpp

    # CNC Controller (multi-firmware support)
    core/cnc/cnc_tool.cpp
    core/cnc/serial_port.cpp
    core/cnc/tcp_socket.cpp
    core/cnc/cnc_controller.cpp
    core/cnc/preflight_check.cpp
    core/cnc/tool_calculator.cpp
    core/cnc/grbl_settings.cpp
    core/cnc/unified_settings.cpp
    core/cnc/macro_manager.cpp
    core/cnc/gamepad_input.cpp

    # Optimizer
    core/optimizer/cut_optimizer.cpp
    core/optimizer/cut_list_file.cpp
    core/optimizer/bin_packer.cpp
    core/optimizer/guillotine.cpp
    core/optimizer/waste_breakdown.cpp
    core/optimizer/multi_stock_optimizer.cpp
    core/optimizer/clo_result_file.cpp

    # Carve (Direct Carve pipeline)
    core/carve/heightmap.cpp
    core/carve/model_fitter.cpp
    core/carve/alignment_validator.cpp
    core/carve/carve_job.cpp
    core/carve/direct_carve_operation_state.cpp
    core/carve/surface_analysis.cpp
    core/carve/island_detector.cpp
    core/carve/analysis_overlay.cpp
    core/carve/tool_recommender.cpp
    core/carve/toolpath_generator.cpp
    core/carve/gcode_export.cpp
    core/carve/carve_streamer.cpp

    # Archive
    core/archive/archive.cpp

    # Workspace
    core/workspace/workspace_relocator.cpp

    # Materials
    core/materials/default_materials.cpp
    core/materials/gemini_material_service.cpp
    core/materials/gemini_descriptor_service.cpp
    core/materials/material_archive.cpp
    core/materials/material_manager.cpp

    # Render
    render/gl_utils.cpp
    render/shader.cpp
    render/camera.cpp
    render/framebuffer.cpp
    render/renderer.cpp
    render/thumbnail_generator.cpp
    render/texture.cpp

    # UI
    ui/theme.cpp
    ui/context_menu_manager.cpp

    # UI Panels
    ui/panels/viewport_panel.cpp
    ui/panels/library_panel.cpp
    ui/panels/library_panel_items.cpp
    ui/panels/materials_panel.cpp
    ui/panels/materials_panel_dialogs.cpp
    ui/panels/materials_panel_stock.cpp
    ui/panels/properties_panel.cpp
    ui/panels/project_panel.cpp
    ui/panels/gcode_panel.cpp
    ui/panels/cost_panel.cpp
    ui/panels/cut_optimizer_panel.cpp
    ui/panels/start_page.cpp
    ui/panels/tool_browser_panel.cpp
    ui/panels/cnc_status_panel.cpp
    ui/panels/cnc_jog_panel.cpp
    ui/panels/cnc_console_panel.cpp
    ui/panels/cnc_wcs_panel.cpp
    ui/panels/cnc_tool_panel.cpp
    ui/panels/cnc_job_panel.cpp
    ui/panels/cnc_safety_panel.cpp
    ui/panels/cnc_settings_panel.cpp
    ui/panels/cnc_macro_panel.cpp
    ui/panels/direct_carve_panel.cpp
    ui/panels/group_panel.cpp

    # UI Widgets
    ui/widgets/binding_recorder.cpp
    ui/widgets/status_bar.cpp
    ui/widgets/toast.cpp
    ui/widgets/tool_recommendation_widget.cpp

    # UI Dialogs
    ui/dialogs/message_dialog.cpp
    ui/dialogs/settings_import_dialog.cpp
    ui/dialogs/file_dialog.cpp
    ui/dialogs/lighting_dialog.cpp
    ui/dialogs/machine_profile_dialog.cpp
    ui/dialogs/import_summary_dialog.cpp
    ui/dialogs/import_options_dialog.cpp
    ui/dialogs/progress_dialog.cpp
    ui/dialogs/tag_image_dialog.cpp
    ui/dialogs/maintenance_dialog.cpp
    ui/dialogs/tagger_shutdown_dialog.cpp
)

# Windows application icon resource
if(WIN32)
    set(DW_WIN_RESOURCE "${CMAKE_SOURCE_DIR}/resources/app.rc")
endif()

# Create executable
add_executable(digital_workshop ${DW_SOURCES} ${DW_WIN_RESOURCE})

# Apply compiler flags
dw_configure_target(digital_workshop)

# Link dependencies
target_link_libraries(digital_workshop PRIVATE
    imgui
    glad_gl33_core
    OpenGL::GL
    glm::glm
    SQLite::SQLite3
    ZLIB::ZLIB
    miniz_static
    nlohmann_json::nlohmann_json
    CURL::libcurl
    nfd
)

# Include directories
target_include_directories(digital_workshop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/generated
    ${stb_SOURCE_DIR}
)

# Platform-specific linking
if(WIN32)
    target_link_libraries(digital_workshop PRIVATE
        SDL2::SDL2main
    )
endif()

# Set output name
set_target_properties(digital_workshop PROPERTIES
    OUTPUT_NAME "digital_workshop"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# --- Bundle GraphQLite extension (if available) ---
if(DW_GRAPHQLITE_AVAILABLE)
    add_custom_command(TARGET digital_workshop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DW_GRAPHQLITE_LIB_PATH}"
            "$<TARGET_FILE_DIR:digital_workshop>/${GRAPHQLITE_LIB}"
        COMMENT "Copying GraphQLite extension to build directory"
    )
endif()

# --- Bundle resources (materials) ---
# Copy .dwmat files from resources/materials/ to the build output directory.
# Uses a stamp file to avoid issues with special characters in filenames.
file(GLOB DW_BUNDLED_MATERIALS "${CMAKE_SOURCE_DIR}/resources/materials/*.dwmat")
if(DW_BUNDLED_MATERIALS)
    set(DW_MATERIALS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/resources/materials")
    set(DW_MATERIALS_STAMP "${CMAKE_BINARY_DIR}/materials_copied.stamp")

    add_custom_command(
        OUTPUT "${DW_MATERIALS_STAMP}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DW_MATERIALS_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources/materials" "${DW_MATERIALS_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E touch "${DW_MATERIALS_STAMP}"
        DEPENDS ${DW_BUNDLED_MATERIALS}
        COMMENT "Copying bundled materials to build dir"
    )

    add_custom_target(dw_bundle_materials ALL DEPENDS "${DW_MATERIALS_STAMP}")
    add_dependencies(digital_workshop dw_bundle_materials)
endif()

# --- Bundle icons ---
set(DW_ICONS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/resources/icons")
add_custom_command(TARGET digital_workshop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DW_ICONS_OUTPUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources/icons" "${DW_ICONS_OUTPUT_DIR}"
    COMMENT "Copying icons to build dir"
)

# Install target
install(TARGETS digital_workshop RUNTIME DESTINATION bin)

# Install icons
install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources/icons/"
    DESTINATION share/digitalworkshop/resources/icons
    FILES_MATCHING PATTERN "*.ico" PATTERN "*.png"
)

# Install bundled materials if directory exists
if(EXISTS "${CMAKE_SOURCE_DIR}/resources/materials/")
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/resources/materials/"
        DESTINATION share/digitalworkshop/resources/materials
        FILES_MATCHING PATTERN "*.dwmat"
    )
endif()
