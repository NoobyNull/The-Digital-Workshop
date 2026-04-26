# Digital Workshop - Tests CMakeLists.txt
# Test suite using GoogleTest

# Collect test sources
set(DW_TEST_SOURCES
    test_main.cpp
    # Tier 0 — existing tests
    test_stl_loader.cpp
    test_obj_loader.cpp
    test_hash.cpp
    test_gcode_parser.cpp
    test_optimizer.cpp
    # Tier 0 — added in first pass
    test_string_utils.cpp
    test_file_utils.cpp
    test_mesh.cpp
    test_mesh_repair.cpp
    test_database.cpp
    test_model_repository.cpp
    test_material_repository.cpp
    test_project_repository.cpp
    test_project_open_item_warnings.cpp
    test_project_open_item_repository.cpp
    test_loader_factory.cpp
    test_project.cpp
    test_exporter.cpp
    # Tier 1 — ConnectionPool
    test_connection_pool.cpp
    # Tier 1 — MainThreadQueue
    test_main_thread_queue.cpp
    # Tier 1 — core logic
    test_types.cpp
    test_gcode_analyzer.cpp
    test_gcode_project_context.cpp
    test_schema.cpp
    test_camera.cpp
    test_status_tips.cpp
    test_window_catalog.cpp
    test_view_cube_orientation.cpp
    test_archive.cpp
    test_library_manager.cpp
    test_threemf_loader.cpp
    test_import_pipeline.cpp
    test_import_mode_policy.cpp
    test_config_watcher.cpp
    test_app_paths.cpp
    test_path_resolver.cpp
    test_cost_repository.cpp
    test_stock_size_repository.cpp
    test_rate_category_repository.cpp
    test_project_costing_io.cpp
    test_cut_plan_repository.cpp
    test_gcode_repository.cpp
    test_material_archive.cpp
    test_material_manager.cpp
    test_mesh_uv.cpp
    # Storage
    test_storage_manager.cpp
    # Import - Filesystem detection
    test_filesystem_detector.cpp
    # Export - Project export/import
    test_project_export_manager.cpp
    # Motion planner
    test_motion_planner.cpp
    # Import log + tagger
    test_import_log.cpp
    test_background_tagger.cpp
    # CNC Controller (multi-firmware support)
    test_serial_port.cpp
    test_tcp_socket.cpp
    test_cnc_controller.cpp
    test_machine_units.cpp
    test_gcode_modal_scanner.cpp
    test_tool_database.cpp
    test_tool_calculator.cpp
    test_tool_profile.cpp
    test_tool_browser_panel.cpp
    test_grbl_settings.cpp
    test_macro_manager.cpp
    # Carve (Direct Carve pipeline)
    test_heightmap.cpp
    test_model_fitter.cpp
    test_material_blank_defaults.cpp
    test_carve_job.cpp
    test_direct_carve_operation_state.cpp
    test_direct_carve_zeroing_probe.cpp
    test_direct_carve_workflow.cpp
    test_surface_analysis.cpp
    test_island_detector.cpp
    test_analysis_overlay.cpp
    test_heightmap_preview.cpp
    test_roughing_tool_selector.cpp
    test_tool_recommender.cpp
    test_toolpath_generator.cpp
    test_gcode_export.cpp
    test_carve_streamer.cpp
    test_carve_integration.cpp
    test_direct_carve_ui_copy.cpp
    test_unit_conversion.cpp
    test_costing_engine.cpp
    test_board_foot.cpp
    test_waste_breakdown.cpp
    test_multi_stock_optimizer.cpp
    test_clo_result_file.cpp
    # Tier 2 — lint/format compliance (no GL/SDL required)
    test_lint_compliance.cpp
    # Tier 3 — workspace tests (no GL/SDL required)
    test_workspace.cpp
    # Tier 4 — integration tests (import → render → export pipeline, no GL/SDL required)
    test_integration_pipeline.cpp
    # Stubs for GL-dependent classes
    stub_thumbnail_generator.cpp
)

# Source files needed by the tests (compiled from src/)
set(DW_TEST_DEPS
    ${CMAKE_SOURCE_DIR}/src/core/types.cpp
    ${CMAKE_SOURCE_DIR}/src/core/threading/main_thread_queue.cpp
    ${CMAKE_SOURCE_DIR}/src/core/threading/thread_pool.cpp
    ${CMAKE_SOURCE_DIR}/src/core/utils/log.cpp
    ${CMAKE_SOURCE_DIR}/src/core/utils/string_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/utils/file_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/core/utils/unit_conversion.cpp
    ${CMAKE_SOURCE_DIR}/src/core/utils/board_foot.cpp
    ${CMAKE_SOURCE_DIR}/src/core/mesh/mesh.cpp
    ${CMAKE_SOURCE_DIR}/src/core/mesh/mesh_repair.cpp
    ${CMAKE_SOURCE_DIR}/src/core/mesh/hash.cpp
    ${CMAKE_SOURCE_DIR}/src/core/loaders/stl_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/core/loaders/obj_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/core/loaders/threemf_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/core/loaders/gcode_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/core/loaders/loader_factory.cpp
    ${CMAKE_SOURCE_DIR}/src/core/gcode/gcode_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/core/gcode/gcode_analyzer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/gcode/gcode_modal_scanner.cpp
    ${CMAKE_SOURCE_DIR}/src/core/gcode/machine_profile.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/status_tips.cpp
    ${CMAKE_SOURCE_DIR}/src/core/viewport/view_cube_orientation.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/cnc_tool.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/serial_port.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/tcp_socket.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/cnc_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/preflight_check.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/tool_calculator.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/tool_profile.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/grbl_settings.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/unified_settings.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/machine_units.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/macro_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/carve/heightmap.cpp
    ${CMAKE_SOURCE_DIR}/src/core/optimizer/cut_optimizer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/optimizer/bin_packer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/optimizer/guillotine.cpp
    ${CMAKE_SOURCE_DIR}/src/core/optimizer/waste_breakdown.cpp
    ${CMAKE_SOURCE_DIR}/src/core/optimizer/multi_stock_optimizer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/optimizer/clo_result_file.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/database.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/connection_pool.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/schema.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/model_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/material_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/project_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/cost_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/stock_size_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/rate_category_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/gcode_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/job_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/cut_plan_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/tool_database.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/toolbox_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/project/project.cpp
    ${CMAKE_SOURCE_DIR}/src/core/project/gcode_project_context.cpp
    ${CMAKE_SOURCE_DIR}/src/core/project/project_open_item_warnings.cpp
    ${CMAKE_SOURCE_DIR}/src/core/project/project_directory.cpp
    ${CMAKE_SOURCE_DIR}/src/core/project/project_costing_io.cpp
    ${CMAKE_SOURCE_DIR}/src/core/project/costing_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/core/export/model_exporter.cpp
    ${CMAKE_SOURCE_DIR}/src/core/archive/archive.cpp
    ${CMAKE_SOURCE_DIR}/src/core/materials/material_archive.cpp
    ${CMAKE_SOURCE_DIR}/src/core/materials/default_materials.cpp
    ${CMAKE_SOURCE_DIR}/src/core/materials/material_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/loaders/texture_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/core/library/library_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/graph/graph_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/import/import_queue.cpp
    ${CMAKE_SOURCE_DIR}/src/core/import/import_log.cpp
    ${CMAKE_SOURCE_DIR}/src/core/import/file_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/core/import/file_validator.cpp
    ${CMAKE_SOURCE_DIR}/src/core/import/filesystem_detector.cpp
    ${CMAKE_SOURCE_DIR}/src/core/import/import_mode_policy.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config/config.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config/config_watcher.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config/input_binding.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config/layout_preset.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config/window_catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/core/paths/app_paths.cpp
    ${CMAKE_SOURCE_DIR}/src/core/paths/path_resolver.cpp
    ${CMAKE_SOURCE_DIR}/src/core/storage/storage_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/export/project_export_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/export/project_import.cpp
    ${CMAKE_SOURCE_DIR}/src/core/carve/heightmap.cpp
    ${CMAKE_SOURCE_DIR}/src/core/carve/model_fitter.cpp
    ${CMAKE_SOURCE_DIR}/src/core/carve/material_blank_defaults.cpp
    ${CMAKE_SOURCE_DIR}/src/core/carve/carve_job.cpp
    ${CMAKE_SOURCE_DIR}/src/core/carve/direct_carve_operation_state.cpp
    ${CMAKE_SOURCE_DIR}/src/core/carve/direct_carve_zeroing_probe.cpp
    ${CMAKE_SOURCE_DIR}/src/core/carve/direct_carve_workflow.cpp
    ${CMAKE_SOURCE_DIR}/src/core/carve/surface_analysis.cpp
    ${CMAKE_SOURCE_DIR}/src/core/carve/island_detector.cpp
    ${CMAKE_SOURCE_DIR}/src/core/carve/analysis_overlay.cpp
    ${CMAKE_SOURCE_DIR}/src/core/carve/heightmap_preview.cpp
    ${CMAKE_SOURCE_DIR}/src/core/carve/roughing_tool_selector.cpp
    ${CMAKE_SOURCE_DIR}/src/core/carve/tool_recommender.cpp
    ${CMAKE_SOURCE_DIR}/src/core/carve/toolpath_generator.cpp
    ${CMAKE_SOURCE_DIR}/src/core/carve/gcode_export.cpp
    ${CMAKE_SOURCE_DIR}/src/core/carve/carve_streamer.cpp
    ${CMAKE_SOURCE_DIR}/src/render/camera.cpp
    ${CMAKE_SOURCE_DIR}/src/app/workspace.cpp
)

# Create test executable
add_executable(dw_tests ${DW_TEST_SOURCES} ${DW_TEST_DEPS})

# Apply compiler flags
dw_configure_target(dw_tests)

# Link dependencies
target_link_libraries(dw_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    imgui
    glm::glm
    ${DW_SQLITE_TARGET}
    ZLIB::ZLIB
    miniz_static
    nlohmann_json::nlohmann_json
)

# Include source directories for testing internal modules
target_include_directories(dw_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)
target_include_directories(dw_tests SYSTEM PRIVATE
    ${stb_SOURCE_DIR}
)

# Pass source dir to lint compliance tests
target_compile_definitions(dw_tests PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(dw_tests)

# Copy test data
# file(COPY data/ DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data)
