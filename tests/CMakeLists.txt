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
    test_gcode_document.cpp
    test_optimizer.cpp
    # Tier 0 — added in first pass
    test_string_utils.cpp
    test_file_utils.cpp
    test_mesh.cpp
    test_mesh_preview.cpp
    test_mesh_repair.cpp
    test_database.cpp
    test_model_repository.cpp
    test_material_repository.cpp
    test_project_repository.cpp
    test_project_open_item_warnings.cpp
    test_project_open_item_repository.cpp
    test_project_open_item_repository_sync.cpp
    test_project_plan.cpp
    test_project_plan_boundary.cpp
    test_project_plan_presentation.cpp
    test_project_plan_input_adapter.cpp
    test_project_plan_run_truth_adapter.cpp
    test_project_plan_ui_architecture.cpp
    test_loader_factory.cpp
    test_project.cpp
    test_project_asset_membership.cpp
    test_project_directory_importer.cpp
    test_named_project_creation.cpp
    test_project_storage.cpp
    test_exporter.cpp
    # Tier 1 — ConnectionPool
    test_connection_pool.cpp
    # Tier 1 — MainThreadQueue
    test_main_thread_queue.cpp
    # Tier 1 — core logic
    test_types.cpp
    test_gcode_analyzer.cpp
    test_gcode_viewport_sampling.cpp
    test_gcode_project_context.cpp
    test_schema.cpp
    test_camera.cpp
    test_status_tips.cpp
    test_window_catalog.cpp
    test_layout_migration.cpp
    test_navigation_style.cpp
    test_view_cube_orientation.cpp
    test_viewport_presentation.cpp
    test_viewport_ui_architecture.cpp
    test_guided_accessibility_architecture.cpp
    test_archive.cpp
    test_library_manager.cpp
    test_library_source_deletion.cpp
    test_threemf_loader.cpp
    test_import_pipeline.cpp
    test_import_mode_policy.cpp
    test_import_path_collector.cpp
    test_thumbnail_sidecar.cpp
    test_config_watcher.cpp
    test_app_paths.cpp
    test_path_resolver.cpp
    test_network_location.cpp
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
    test_machine_profile_customization.cpp
    test_machine_rigidity.cpp
    test_machine_profile_ui_architecture.cpp
    # Import log + tagger
    test_import_log.cpp
    test_background_tagger.cpp
    test_smart_tagger.cpp
    test_lmstudio_descriptor_parser.cpp
    test_ollama_runtime.cpp
    # CNC Controller (multi-firmware support)
    test_serial_port.cpp
    test_tcp_socket.cpp
    test_cnc_controller.cpp
    test_machine_units.cpp
    test_gcode_modal_scanner.cpp
    test_tool_database.cpp
    test_selective_tool_importer.cpp
    test_tool_calculator.cpp
    test_machine_profile_calculator_adapter.cpp
    test_tool_profile.cpp
    test_tool_browser_panel.cpp
    test_supplier_tool_import_presentation.cpp
    test_supplier_tool_import_architecture.cpp
    test_log_viewer_panel.cpp
    test_cam_placeholder_panel.cpp
    test_file_dialog_filters.cpp
    test_modal_dialogs.cpp
    test_grbl_settings.cpp
    test_macro_manager.cpp
    # CAM engine
    test_cam_engine_client.cpp
    test_cam_engine_runtime.cpp
    # Carve (Direct Carve pipeline)
    test_model_fitter.cpp
    test_preparation_identity.cpp
    test_prepare_carve_flow.cpp
    test_preparation_step_guidance.cpp
    test_carve_preparation_boundary.cpp
    test_run_package.cpp
    test_run_coordinator.cpp
    test_run_coordination_boundary.cpp
    test_run_coordination_application_architecture.cpp
    test_direct_carve_run_effect_adapter.cpp
    test_carve_preparation_adapter.cpp
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
    test_home_flow.cpp
    test_library_picker_flow.cpp
    test_library_picker_presentation.cpp
    test_project_resume.cpp
    test_project_resume_file_store.cpp
    test_project_item_load_guard.cpp
    test_workshop_core.cpp
    test_workshop_module_boundary.cpp
    test_project_session.cpp
    test_project_workshop_controller.cpp
    test_project_session_boundary.cpp
    test_project_session_integration.cpp
    test_project_resume_integration.cpp
    test_project_lifecycle_architecture.cpp
    test_project_centered_workshop_end_to_end.cpp
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
    ${CMAKE_SOURCE_DIR}/src/core/mesh/mesh_preview.cpp
    ${CMAKE_SOURCE_DIR}/src/core/mesh/mesh_repair.cpp
    ${CMAKE_SOURCE_DIR}/src/core/mesh/hash.cpp
    ${CMAKE_SOURCE_DIR}/src/core/loaders/stl_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/core/loaders/obj_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/core/loaders/threemf_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/core/loaders/gcode_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/core/loaders/loader_factory.cpp
    ${CMAKE_SOURCE_DIR}/src/core/import/smart_tagger.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ai/ollama_runtime.cpp
    ${CMAKE_SOURCE_DIR}/src/core/materials/lmstudio_descriptor_service.cpp
    ${CMAKE_SOURCE_DIR}/src/core/utils/lmstudio_http.cpp
    ${CMAKE_SOURCE_DIR}/src/core/gcode/gcode_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/core/gcode/gcode_analyzer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/gcode/gcode_document.cpp
    ${CMAKE_SOURCE_DIR}/src/core/gcode/gcode_modal_scanner.cpp
    ${CMAKE_SOURCE_DIR}/src/core/gcode/machine_profile.cpp
    ${CMAKE_SOURCE_DIR}/src/core/gcode/machine_profile_customization.cpp
    ${CMAKE_SOURCE_DIR}/src/core/gcode/machine_rigidity.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/status_tips.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/log_viewer_panel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/panels/cam_placeholder_panel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/file_filter_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/import_options_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/import_summary_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/tagger_shutdown_dialog.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/dialogs/supplier_tool_import_presentation.cpp
    ${CMAKE_SOURCE_DIR}/src/core/viewport/view_cube_orientation.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/cnc_tool.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/serial_port.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/tcp_socket.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/cnc_controller.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/cnc_controller_status_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/cnc_controller_simulator_runtime.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/cnc_controller_simulator_commands.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/cnc_controller_stream_control.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/preflight_check.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/tool_calculator.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/machine_profile_calculator_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/tool_profile.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/grbl_settings.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/unified_settings.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/machine_units.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cnc/macro_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/optimizer/cut_optimizer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/optimizer/bin_packer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/optimizer/guillotine.cpp
    ${CMAKE_SOURCE_DIR}/src/core/optimizer/waste_breakdown.cpp
    ${CMAKE_SOURCE_DIR}/src/core/optimizer/multi_stock_optimizer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/optimizer/clo_result_file.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/database.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/connection_pool.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/schema.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/schema_project_migrations.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/model_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/material_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/project_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/project_repository_mapping.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/project_repository_open_item_detail.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/project_repository_open_item_sync.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/project_repository_open_item_validation.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/project_repository_open_items.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/cost_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/stock_size_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/rate_category_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/gcode_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/job_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/cut_plan_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/tool_database.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/tool_database_machine_material.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/tool_database_schema.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/tool_database_import.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/supplier_tool_catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/selective_tool_importer.cpp
    ${CMAKE_SOURCE_DIR}/src/core/database/toolbox_repository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/project/named_project_creation.cpp
    ${CMAKE_SOURCE_DIR}/src/core/project/project_asset_membership.cpp
    ${CMAKE_SOURCE_DIR}/src/core/project/project.cpp
    ${CMAKE_SOURCE_DIR}/src/core/project/project_storage.cpp
    ${CMAKE_SOURCE_DIR}/src/core/project/project_directory_importer.cpp
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
    ${CMAKE_SOURCE_DIR}/src/core/library/image_thumbnail.cpp
    ${CMAKE_SOURCE_DIR}/src/core/library/library_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/library/library_source_deletion.cpp
    ${CMAKE_SOURCE_DIR}/src/core/graph/graph_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/import/import_queue.cpp
    ${CMAKE_SOURCE_DIR}/src/core/import/import_log.cpp
    ${CMAKE_SOURCE_DIR}/src/core/import/import_path_collector.cpp
    ${CMAKE_SOURCE_DIR}/src/core/import/thumbnail_sidecar.cpp
    ${CMAKE_SOURCE_DIR}/src/core/import/file_handler.cpp
    ${CMAKE_SOURCE_DIR}/src/core/import/file_validator.cpp
    ${CMAKE_SOURCE_DIR}/src/core/import/filesystem_detector.cpp
    ${CMAKE_SOURCE_DIR}/src/core/import/import_mode_policy.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config/config.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config/config_layout.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config/config_project_paths.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config/config_watcher.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config/input_binding.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config/layout_preset.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config/layout_migration.cpp
    ${CMAKE_SOURCE_DIR}/src/core/config/window_catalog.cpp
    ${CMAKE_SOURCE_DIR}/src/core/paths/app_paths.cpp
    ${CMAKE_SOURCE_DIR}/src/core/paths/network_location.cpp
    ${CMAKE_SOURCE_DIR}/src/core/paths/path_resolver.cpp
    ${CMAKE_SOURCE_DIR}/src/core/storage/storage_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/export/project_export_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/export/project_import.cpp
    ${CMAKE_SOURCE_DIR}/src/core/carve/model_fitter.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cam/cam_engine_client.cpp
    ${CMAKE_SOURCE_DIR}/src/core/cam/cam_engine_runtime.cpp
    ${CMAKE_SOURCE_DIR}/src/render/camera.cpp
    ${CMAKE_SOURCE_DIR}/src/app/workspace.cpp
    ${CMAKE_SOURCE_DIR}/src/app/project_session_integration.cpp
    ${CMAKE_SOURCE_DIR}/src/app/project_resume_file_store.cpp
    ${CMAKE_SOURCE_DIR}/src/app/project_plan_input_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/app/project_plan_run_truth_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/app/carve_preparation_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/app/library_workflow_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/app/library_workflow_coordinator.cpp
    ${CMAKE_SOURCE_DIR}/src/app/direct_carve_run_effect_adapter.cpp
)

# Create test executable
add_executable(dw_tests ${DW_TEST_SOURCES} ${DW_TEST_DEPS})

# Apply compiler flags
dw_configure_target(dw_tests)

# Link dependencies
target_link_libraries(dw_tests PRIVATE
    dw_carve_preparation
    dw_viewport_presentation
    dw_run_coordination
    dw_design_library
    dw_project_plan
    dw_project_session
    dw_workshop_core
    GTest::gtest
    imgui
    glm::glm
    ${DW_SQLITE_TARGET}
    ZLIB::ZLIB
    miniz_static
    nlohmann_json::nlohmann_json
    CURL::libcurl
)
if(UNIX AND NOT APPLE)
    target_link_libraries(dw_tests PRIVATE PkgConfig::DW_DBUS)
endif()

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

# Prove the workshop contracts compile and run without the legacy test target's
# ImGui, GLM, database, or application dependency closure.
add_executable(dw_workshop_core_tests
    test_guided_layout_metrics.cpp
    test_home_flow.cpp
    test_project_resume.cpp
    test_project_item_load_guard.cpp
    test_workshop_core.cpp
    test_workshop_module_boundary.cpp
)
target_compile_features(dw_workshop_core_tests PRIVATE cxx_std_17)
dw_set_warning_flags(dw_workshop_core_tests)
dw_set_debug_flags(dw_workshop_core_tests)
dw_set_release_flags(dw_workshop_core_tests)
target_link_libraries(dw_workshop_core_tests PRIVATE
    dw_workshop_core
    GTest::gtest_main
)
target_compile_definitions(dw_workshop_core_tests PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
gtest_discover_tests(dw_workshop_core_tests TEST_PREFIX "isolated.")

# Keep the contextual Library policy independently buildable without the
# legacy application, UI, renderer, database, or persistence dependency graph.
add_executable(dw_design_library_tests
    test_library_picker_flow.cpp
    test_library_picker_presentation.cpp
)
target_compile_features(dw_design_library_tests PRIVATE cxx_std_17)
dw_set_warning_flags(dw_design_library_tests)
dw_set_debug_flags(dw_design_library_tests)
dw_set_release_flags(dw_design_library_tests)
target_link_libraries(dw_design_library_tests PRIVATE
    dw_design_library
    GTest::gtest_main
)
gtest_discover_tests(dw_design_library_tests TEST_PREFIX "isolated.library.")

add_executable(dw_project_session_tests
    test_project_session.cpp
    test_project_workshop_controller.cpp
    test_project_session_boundary.cpp
)
target_compile_features(dw_project_session_tests PRIVATE cxx_std_17)
dw_set_warning_flags(dw_project_session_tests)
dw_set_debug_flags(dw_project_session_tests)
dw_set_release_flags(dw_project_session_tests)
target_link_libraries(dw_project_session_tests PRIVATE
    dw_project_session
    GTest::gtest_main
)
target_compile_definitions(dw_project_session_tests PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
gtest_discover_tests(dw_project_session_tests TEST_PREFIX "isolated.session.")

# Keep the Project Plan projection independently buildable without persistence,
# UI, rendering, application, or machine-control dependencies.
add_executable(dw_project_plan_tests
    test_project_plan.cpp
    test_project_plan_boundary.cpp
    test_project_plan_presentation.cpp
)
target_compile_features(dw_project_plan_tests PRIVATE cxx_std_17)
dw_set_warning_flags(dw_project_plan_tests)
dw_set_debug_flags(dw_project_plan_tests)
dw_set_release_flags(dw_project_plan_tests)
target_link_libraries(dw_project_plan_tests PRIVATE
    dw_project_plan
    GTest::gtest_main
)
target_compile_definitions(dw_project_plan_tests PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
gtest_discover_tests(dw_project_plan_tests TEST_PREFIX "isolated.plan.")

# Keep preparation identity independently buildable without application, UI,
# persistence, rendering, or machine-control dependency graphs.
add_executable(dw_carve_preparation_tests
    test_preparation_identity.cpp
    test_prepare_carve_flow.cpp
    test_preparation_step_guidance.cpp
    test_carve_preparation_boundary.cpp
)
target_compile_features(dw_carve_preparation_tests PRIVATE cxx_std_17)
dw_set_warning_flags(dw_carve_preparation_tests)
dw_set_debug_flags(dw_carve_preparation_tests)
dw_set_release_flags(dw_carve_preparation_tests)
target_link_libraries(dw_carve_preparation_tests PRIVATE
    dw_carve_preparation
    GTest::gtest_main
)
target_compile_definitions(dw_carve_preparation_tests PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
gtest_discover_tests(dw_carve_preparation_tests TEST_PREFIX "isolated.preparation.")

# Keep protected Run policy independently buildable without application, UI,
# persistence, rendering, or hardware/controller dependency graphs.
add_executable(dw_run_coordination_tests
    test_run_package.cpp
    test_run_coordinator.cpp
    test_run_coordination_boundary.cpp
)
target_compile_features(dw_run_coordination_tests PRIVATE cxx_std_17)
dw_set_warning_flags(dw_run_coordination_tests)
dw_set_debug_flags(dw_run_coordination_tests)
dw_set_release_flags(dw_run_coordination_tests)
target_link_libraries(dw_run_coordination_tests PRIVATE
    dw_run_coordination
    GTest::gtest_main
)
target_compile_definitions(dw_run_coordination_tests PRIVATE
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
gtest_discover_tests(dw_run_coordination_tests TEST_PREFIX "isolated.run.")

# Keep viewport identity projection buildable without ImGui, rendering,
# repositories, ProjectSession, or application orchestration.
add_executable(dw_viewport_presentation_tests
    test_viewport_presentation.cpp
)
target_compile_features(dw_viewport_presentation_tests PRIVATE cxx_std_17)
dw_set_warning_flags(dw_viewport_presentation_tests)
dw_set_debug_flags(dw_viewport_presentation_tests)
dw_set_release_flags(dw_viewport_presentation_tests)
target_link_libraries(dw_viewport_presentation_tests PRIVATE
    dw_viewport_presentation
    GTest::gtest_main
)
gtest_discover_tests(dw_viewport_presentation_tests TEST_PREFIX "isolated.viewport.")

# Copy test data
# file(COPY data/ DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/data)
