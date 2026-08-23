# Existing non-generated production files above the 750-line hard ceiling.
# A cap is a ratchet, not an allowance to grow. When a file shrinks, lower its
# cap in the same session. New oversized files are never added to this list.
set(DW_SOURCE_SIZE_CAPS
    "src/ui/panels/gcode_panel.cpp|1226"
    "src/ui/panels/cost_panel.cpp|1316"
    "src/ui/panels/tool_browser_panel.cpp|1264"
    "src/ui/panels/cut_optimizer_panel.cpp|1223"
    "src/ui/panels/cnc_settings_panel.cpp|1161"
    "src/core/library/library_manager.cpp|1092"
    "src/ui/panels/cnc_safety_panel.cpp|1012"
    "src/core/database/model_repository.cpp|845"
    "src/core/database/schema.cpp|822"
    "src/core/import/import_queue.cpp|777"
)
