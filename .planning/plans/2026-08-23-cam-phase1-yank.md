# CAM Phase 1: Yank Direct Carve Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove Digital Workshop's internal CAM (Direct Carve toolpath generation, panels, and preparation glue), leaving a green build with a native "CAM rebuild in progress" placeholder and an intact Run/streaming boundary.

**Architecture:** Big-bang deletion per `.planning/CAM-INTEGRATION-DESIGN.md`. The Run effect adapter is rewired to stream the fingerprint-verified persisted G-code file (dropping `CarveJob`), the viewport keeps `model_fitter`/`alignment_validator` (self-contained), and the `direct_carve` window ID is preserved and pointed at a placeholder panel so no config/layout migration is needed in this phase.

**Tech Stack:** C++17, CMake, GoogleTest. Build dir `build/` is already configured.

**Verification commands used throughout:**
- Build: `cmake --build build -j$(nproc) 2>&1 | tail -5` → expect `Built target` lines, no errors
- Full suite: `./build/tests/dw_tests --gtest_brief=1 | tail -3` → expect `PASSED`
- Clean diff: `git diff --check` → expect no output

**Ground truth (from the dependency survey, verified 2026-08-23):**

Keep in `src/core/carve/`: `model_fitter.{h,cpp}` and `alignment_validator.{h,cpp}` only (self-contained; consumed by `viewport_panel.h:10-11`, `viewport_overlays.cpp:39`; `FitParams`/`StockDimensions` used by viewport API). Everything else in that directory is deleted in this phase.

`src/modules/` (including `dw_carve_preparation`, `dw_run_coordination`) references nothing in `core/carve` — modules and their isolated test executables are untouched.

---

### Task 1: Milestone branch and version bump

**Files:**
- Modify: `CMakeLists.txt:12-15`

- [ ] **Step 1: Create the milestone branch**

```bash
cd /home/matthew/Projects/DW
git checkout -b v0.8.0-cam-integration
```

- [ ] **Step 2: Bump the project version**

In `CMakeLists.txt`, change:

```cmake
project(DigitalWorkshop
    VERSION 0.7.0
```

to:

```cmake
project(DigitalWorkshop
    VERSION 0.8.0
```

- [ ] **Step 3: Reconfigure and build to verify**

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Release -DDW_BUILD_TESTS=ON 2>&1 | tail -2 && cmake --build build -j$(nproc) 2>&1 | tail -3`
Expected: configure and build succeed.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "Start v0.8.0 CAM integration milestone"
```

---

### Task 2: Delete dead-in-production CAM leaves

These four units have no production consumers (verified: `heightmap_preview` and `analysis_overlay` are called only by their own tests; `tool_recommender` is consumed only by `tool_recommendation_widget`, which nothing instantiates).

**Files:**
- Delete: `src/core/carve/heightmap_preview.{h,cpp}`, `src/core/carve/analysis_overlay.{h,cpp}`, `src/core/carve/tool_recommender.{h,cpp}`, `src/ui/widgets/tool_recommendation_widget.{h,cpp}`
- Delete: `tests/test_heightmap_preview.cpp`, `tests/test_analysis_overlay.cpp`, `tests/test_tool_recommender.cpp`
- Modify: `src/CMakeLists.txt` (carve block near line 208, widget at ~line 328), `tests/CMakeLists.txt` (`DW_TEST_SOURCES` ~119-153, `DW_TEST_DEPS` ~313-331)

- [ ] **Step 1: Delete the source and test files**

```bash
git rm src/core/carve/heightmap_preview.h src/core/carve/heightmap_preview.cpp \
       src/core/carve/analysis_overlay.h src/core/carve/analysis_overlay.cpp \
       src/core/carve/tool_recommender.h src/core/carve/tool_recommender.cpp \
       src/ui/widgets/tool_recommendation_widget.h src/ui/widgets/tool_recommendation_widget.cpp \
       tests/test_heightmap_preview.cpp tests/test_analysis_overlay.cpp tests/test_tool_recommender.cpp
```

- [ ] **Step 2: Remove the CMake entries**

In `src/CMakeLists.txt` remove the lines naming `core/carve/heightmap_preview.cpp`, `core/carve/analysis_overlay.cpp`, `core/carve/tool_recommender.cpp`, `ui/widgets/tool_recommendation_widget.cpp`. In `tests/CMakeLists.txt` remove the same three `core/carve/*.cpp` entries from `DW_TEST_DEPS`, the widget entry if present, and the three `test_*.cpp` entries from `DW_TEST_SOURCES`.

Verify nothing remains: `grep -rn "heightmap_preview\|analysis_overlay\|tool_recommender\|tool_recommendation_widget" src tests CMakeLists.txt` → expect no output.

- [ ] **Step 3: Build and run the suite**

Run: `cmake --build build -j$(nproc) 2>&1 | tail -3 && ./build/tests/dw_tests --gtest_brief=1 | tail -3`
Expected: build succeeds, suite PASSED.

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "Remove dead CAM analysis and recommendation units"
```

---

### Task 3: Rewire the Run effect adapter off CarveJob

Today `StartStream` fingerprint-verifies the persisted G-code file (`direct_carve_run_effect_adapter.cpp:177-185`) and then ignores it, streaming the in-memory toolpath via `CarveJob::startStreaming` — which itself just drains lines into `CncController::startStream(std::vector<std::string>)`. Stream the verified file instead; `CarveJob`/`CarveStreamer` drop out of the adapter.

**Files:**
- Modify: `src/app/direct_carve_run_effect_adapter.h` (ctor at :85-89, member at :121, forward-decl at :16)
- Modify: `src/app/direct_carve_run_effect_adapter.cpp` (include at :9, StartStream at :192-213, JobRecord cleanup at :200-207, AbortStream at :258-263)
- Modify: `src/app/application_wiring_cnc.cpp:158-164` (adapter construction)
- Modify: `tests/test_direct_carve_run_effect_adapter.cpp` (ctor call sites)
- Modify: `tests/test_run_coordination_application_architecture.cpp:33-44` (reads `carve_job.cpp` as text)

- [ ] **Step 1: Update the adapter test to the new contract**

In `tests/test_direct_carve_run_effect_adapter.cpp`, remove the `carve::CarveJob` fixture member and the `carveJob` constructor argument at every `DirectCarveRunEffectAdapter(...)` call site. The test already writes the persisted G-code file to satisfy the fingerprint check; after this task the streamed line count comes from that file, so where the test asserts stream progress totals, the expectation is the number of runnable (non-blank, non-comment) lines in the fixture file. Read the fixture-writing helper first and count its lines to set exact expectations.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build -j$(nproc) --target dw_tests 2>&1 | tail -5`
Expected: FAILS to compile (adapter still requires `CarveJob&`).

- [ ] **Step 3: Rewire the adapter header**

In `direct_carve_run_effect_adapter.h`: delete the `namespace carve { class CarveJob; }` forward declaration (line 16 area), delete the `carve::CarveJob& carveJob,` constructor parameter, delete the `carve::CarveJob& m_carveJob;` member.

- [ ] **Step 4: Rewire the adapter implementation**

In `direct_carve_run_effect_adapter.cpp`: replace `#include "core/carve/carve_job.h"` with `#include <fstream>`; drop `m_carveJob` from the ctor init list. Add a file-local helper above the handlers (same comment-stripping rules as `gcode_panel.cpp:1036-1050`):

```cpp
namespace {
// Runnable lines only: blanks and comment lines dropped, inline comments and
// trailing whitespace stripped — the same filter the G-code panel streams with.
std::vector<std::string> readRunnableLines(const Path& path) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    if (!in.is_open())
        return lines;
    std::string raw;
    while (std::getline(in, raw)) {
        while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\r'))
            raw.pop_back();
        if (raw.empty() || raw.front() == ';' || raw.front() == '(')
            continue;
        const auto semi = raw.find(';');
        if (semi != std::string::npos)
            raw = raw.substr(0, semi);
        lines.push_back(raw);
    }
    return lines;
}
} // namespace
```

Replace the `StartStream` tail (current lines 192-213) with:

```cpp
    const std::vector<std::string> lines = readRunnableLines(resolvedPath);
    if (lines.empty())
        return rejected(DirectCarveRunEffectError::GCodeFileMissing);

    m_snapshot.package = effect.package;
    m_snapshot.persistedGCodePath = storedPathText;
    m_snapshot.state = DirectCarveRunControlState::Streaming;
    m_snapshot.streamAttempted = true;

    const bool streamStarted = m_cncController.startStream(lines);
    JobRecord record;
    record.fileName = storedPath.filename().string();
    record.filePath = storedPathText;
    record.totalLines = static_cast<int>(lines.size());
    const auto jobId = m_jobRepository.insert(record);
    if (!jobId.has_value()) {
        if (streamStarted) {
            m_cncController.feedHold();
            m_cncController.stopStream();
        }
        m_snapshot.state = DirectCarveRunControlState::Aborting;
        return rejected(DirectCarveRunEffectError::JobRecordInsertFailed);
    }

    m_snapshot.jobId = *jobId;
    (void)m_projectManager.listOpenItems(gcodeRef.project.value);
    if (!streamStarted)
        return rejected(DirectCarveRunEffectError::StreamStartFailed);
    return applied();
```

Note the snapshot-mutation block moves above the read only if the existing order requires it — keep the existing order: verify, read lines, then mutate snapshot exactly as the original did before calling `startStreaming`.

In the `AbortStream` handler (lines 258-263), delete the two `streamer()->abort()` lines and keep `feedHold()` + `stopStream()`; update the safety comment to end at "stop the queued stream" (`stopStream()` already halts the queue).

- [ ] **Step 5: Update the construction site**

In `application_wiring_cnc.cpp:158-164`: remove `*m_carveJob,` from the `make_unique<DirectCarveRunEffectAdapter>` argument list and remove `m_carveJob &&` from the guard condition above it. Leave `m_carveJob` itself alive — the panel still uses it until Task 5.

- [ ] **Step 6: Update the architecture test**

In `tests/test_run_coordination_application_architecture.cpp`: delete line 34 (`readFile(... "carve_job.cpp")`) and line 44 (`EXPECT_NE(carveJob.find("controller->startStream(program)"), ...)`). Add in their place an assertion that the adapter itself reaches the controller boundary:

```cpp
    const auto adapter =
        readFile(root / "app" / "direct_carve_run_effect_adapter.cpp");
    EXPECT_NE(adapter.find("m_cncController.startStream(lines)"),
              std::string::npos);
```

(If an `adapter` variable already exists at line 33, reuse it instead of re-reading.)

- [ ] **Step 7: Build and run the affected tests, then the suite**

Run: `cmake --build build -j$(nproc) --target dw_tests 2>&1 | tail -3 && ./build/tests/dw_tests --gtest_filter='*RunEffect*:*RunCoordinationApplication*' | tail -3 && ./build/tests/dw_tests --gtest_brief=1 | tail -3`
Expected: all PASS.

- [ ] **Step 8: Commit**

```bash
git add -A && git commit -m "Stream verified persisted G-code in Run adapter, drop CarveJob"
```

---

### Task 4: CAM placeholder panel

A minimal panel that owns the `direct_carve` window ID after the Direct Carve panel is deleted, so saved layouts and the persisted `show_direct_carve` config key stay valid without migration. Title becomes "CAM".

**Files:**
- Create: `src/ui/panels/cam_placeholder_panel.h`
- Create: `src/ui/panels/cam_placeholder_panel.cpp`
- Modify: `src/CMakeLists.txt` (panels list)
- Test: `tests/test_cam_placeholder_panel.cpp` (+ `tests/CMakeLists.txt`)

- [ ] **Step 1: Write the failing test**

Create `tests/test_cam_placeholder_panel.cpp`:

```cpp
#include "ui/panels/cam_placeholder_panel.h"
#include <gtest/gtest.h>

namespace dw {

TEST(CamPlaceholderPanel, ReportsRebuildStatusCopy) {
    CamPlaceholderPanel panel;
    const auto& copy = panel.statusCopy();
    EXPECT_NE(copy.find("CAM"), std::string::npos);
    EXPECT_NE(copy.find("rebuilt"), std::string::npos);
}

TEST(CamPlaceholderPanel, VisibilityDefaultsClosedAndToggles) {
    CamPlaceholderPanel panel;
    EXPECT_FALSE(panel.isVisible());
    panel.setVisible(true);
    EXPECT_TRUE(panel.isVisible());
}

} // namespace dw
```

Register it in `tests/CMakeLists.txt`: add `test_cam_placeholder_panel.cpp` to `DW_TEST_SOURCES` and `${CMAKE_SOURCE_DIR}/src/ui/panels/cam_placeholder_panel.cpp` to `DW_TEST_DEPS`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build -j$(nproc) --target dw_tests 2>&1 | tail -3`
Expected: FAILS — header does not exist.

- [ ] **Step 3: Implement the panel**

`src/ui/panels/cam_placeholder_panel.h`:

```cpp
#pragma once

#include <string>

namespace dw {

// Placeholder occupying the "direct_carve" window ID while the CAM
// workspace is rebuilt on the PureCutCNC engine (v0.8.0 milestone).
// See .planning/CAM-INTEGRATION-DESIGN.md.
class CamPlaceholderPanel {
  public:
    void render();
    [[nodiscard]] bool isVisible() const noexcept { return m_visible; }
    void setVisible(bool visible) noexcept { m_visible = visible; }
    [[nodiscard]] const std::string& statusCopy() const noexcept;

  private:
    bool m_visible = false;
};

} // namespace dw
```

`src/ui/panels/cam_placeholder_panel.cpp`:

```cpp
#include "cam_placeholder_panel.h"

#include <imgui.h>

namespace dw {

const std::string& CamPlaceholderPanel::statusCopy() const noexcept {
    static const std::string copy =
        "The CAM workspace is being rebuilt on the PureCutCNC engine.\n"
        "Carve preparation and toolpath generation return when the "
        "rebuild ships.\nExternal G-code workflows are unaffected.";
    return copy;
}

void CamPlaceholderPanel::render() {
    if (!m_visible)
        return;
    if (ImGui::Begin("CAM", &m_visible)) {
        ImGui::TextWrapped("%s", statusCopy().c_str());
    }
    ImGui::End();
}

} // namespace dw
```

Add `ui/panels/cam_placeholder_panel.cpp` to the panels list in `src/CMakeLists.txt`.

Adjust the test copy assertion if wording changes — the test checks "CAM" and "rebuilt" appear.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build -j$(nproc) --target dw_tests 2>&1 | tail -3 && ./build/tests/dw_tests --gtest_filter='CamPlaceholder*' | tail -3`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "Add CAM placeholder panel for the rebuild window"
```

---

### Task 5: Cut the application over to the placeholder

Swap `DirectCarvePanel` ownership for the placeholder in `UIManager`, then strip every panel reference from the app layer. Known reference sites (verified): `src/managers/ui_manager.{h,cpp}` (member :52 area, construction :98, reset :189, catalog row :238-239), `src/managers/ui_manager_menus.cpp:292-293`, `src/app/application_wiring_cnc.cpp` (wiring block :122-170, progress callback :310-317), `src/app/application_wiring_workshop.cpp`, `src/app/application_callbacks.cpp`, `src/app/application_library_picker.cpp`, `src/app/application_project_session.cpp`, `src/app/application_project_resume.cpp` (:14, :33, :330, :361), `src/app/application_ux_capture.cpp`, `src/app/application.cpp:27,390`, `src/app/application.h` (`m_carveJob` member).

- [ ] **Step 1: Swap the UIManager-owned panel**

In `ui_manager.h`/`ui_manager.cpp`: replace the `DirectCarvePanel` include, member, accessor, construction, and reset with `CamPlaceholderPanel` (member name `m_camPlaceholderPanel`, accessor `camPlaceholderPanel()`). Keep the window-catalog row keyed `"direct_carve"` but route its visibility get/set to the placeholder. In `ui_manager_menus.cpp:292-293` keep the menu item, retitled "CAM".

- [ ] **Step 2: Strip the app wiring**

Work file by file; after each file, run `cmake --build build -j$(nproc) 2>&1 | tail -3` and fix what surfaces before moving on:

- `application_wiring_cnc.cpp`: delete the whole `if (dcarvep) { ... }` wiring block (:122-170 area) including `setCarveJob`, the run-effect executor hookup, and the preview callbacks. In the progress callback (:310-317), delete `if (dcarvep) dcarvep->onRunProgress(progress);` and replace the origin ternary with `const auto origin = CncStreamOrigin::ExternalGCode;`.
- `application.cpp` / `application.h`: delete the `carve_job.h` include (:27), the `make_unique<carve::CarveJob>()` construction (:390), and the `m_carveJob` member.
- `application_callbacks.cpp`, `application_wiring_workshop.cpp`, `application_library_picker.cpp`, `application_project_session.cpp`: locate each `direct_carve_panel.h` include and every use with `grep -n "directCarvePanel\|DirectCarvePanel\|dcarvep" <file>`; delete those statements. Where a guided-stage navigation used to focus the panel, call `m_uiManager->openWindow("direct_carve")` (opens the placeholder) instead of touching panel API.
- `application_project_resume.cpp`: delete includes :14 and :33. At :330, the `parseDirectCarveOperationSetup(item)` restoration branch is removed — a resumed Direct Carve operation item now reports resume-pending and opens the placeholder via the existing `openWindow("direct_carve")` at :361. Keep the route/identity guards around it intact.
- `application_ux_capture.cpp` and the River Sign study: delete the study and capture drivers wholesale — `git rm src/app/application_ux_capture.cpp src/app/ux_capture_fixture.{h,cpp} src/app/ux_capture_scenario.{h,cpp} src/app/application_river_sign_study.cpp src/app/river_sign_study_command.{h,cpp} src/app/river_sign_study_fixture.{h,cpp} src/ui/panels/direct_carve_ux_capture.cpp` plus tests `test_river_sign_study_architecture.cpp`, `test_river_sign_study_command.cpp`, `test_river_sign_study_fixture.cpp` and their CMake entries (note `direct_carve_ux_capture.cpp` sits in a conditional block near `src/CMakeLists.txt:353`; remove the block if empty). Remove the CLI flags/entry points that invoked them (grep `ux_capture\|river_sign` across `src/` and `settings/`). The v0.6-era study machinery validated the guided flow this milestone deletes; Phase 6-7 recreate E2E validation on the new CAM path (per design doc). Leave `scripts/*.py` in place — they are not compiled.

- [ ] **Step 3: Fix the Project Plan input adapter**

`src/app/project_plan_input_adapter.h:7` includes `direct_carve_workflow.h` and takes `DirectCarveWorkflowState` at :27. Remove the include and the parameter; carve-stage evidence becomes "absent" (tri-state unknown), which Project Plan already renders as incomplete stages. Update `tests/test_project_plan_input_adapter.cpp`: delete the cases that construct `DirectCarveWorkflowState`, keep the rest, and add one case asserting carve-stage evidence is absent by default.

- [ ] **Step 4: Build everything and run the suite**

Run: `cmake --build build -j$(nproc) 2>&1 | tail -3 && ./build/tests/dw_tests --gtest_brief=1 | tail -3`
Expected: build green; failures only in tests that read deleted files as text (handled next task) — if any such test fails here, list it and defer, do not fix blind.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "Route direct_carve window to CAM placeholder, strip panel wiring"
```

---

### Task 6: Delete the panel family and app preparation glue

**Files:**
- Delete: all 27 `src/ui/panels/direct_carve_*` files (`git rm src/ui/panels/direct_carve_*`)
- Delete: `src/app/application_carve_preparation.cpp`, `src/app/carve_preparation_adapter.{h,cpp}`
- Delete: `tests/test_direct_carve_ui_copy.cpp`, `tests/test_prepare_carve_ui_architecture.cpp`, `tests/test_carve_preparation_adapter.cpp`, `tests/test_carve_preparation_application_architecture.cpp`
- Modify: `src/CMakeLists.txt` (panels block ~:297-318, app entries), `tests/CMakeLists.txt`, `tests/test_project_lifecycle_architecture.cpp:263,448,465`

- [ ] **Step 1: Delete files**

```bash
git rm src/ui/panels/direct_carve_* \
       src/app/application_carve_preparation.cpp \
       src/app/carve_preparation_adapter.h src/app/carve_preparation_adapter.cpp \
       tests/test_direct_carve_ui_copy.cpp tests/test_prepare_carve_ui_architecture.cpp \
       tests/test_carve_preparation_adapter.cpp tests/test_carve_preparation_application_architecture.cpp
```

(`dw_carve_preparation` module and its isolated tests `test_prepare_carve_flow.cpp`, `test_carve_preparation_boundary.cpp`, `test_preparation_identity.cpp`, `test_preparation_step_guidance.cpp` stay — they link only the module.)

- [ ] **Step 2: Remove CMake entries and fix text-reading tests**

Remove all deleted entries from `src/CMakeLists.txt` and `tests/CMakeLists.txt` (both source lists and `DW_TEST_DEPS`). In `tests/test_project_lifecycle_architecture.cpp`, lines 263/448/465 read `direct_carve_panel.h` as text — rewrite those assertions to target the file that now owns the checked behavior (read each assertion; if it guarded panel-specific behavior that no longer exists, delete the assertion; if it guarded resume/session behavior, point it at `application_project_resume.cpp`).

Then sweep for stragglers: `grep -rn "direct_carve\|DirectCarve" src tests --include='*.cpp' --include='*.h' | grep -v cam_placeholder | grep -v "direct_carve\"" | grep -viE "window|layout|config|origin"` — every remaining hit must be a deliberate keep (window ID strings, `CncStreamOrigin` enum, config keys) or it gets removed.

- [ ] **Step 3: Build and run the suite**

Run: `cmake --build build -j$(nproc) 2>&1 | tail -3 && ./build/tests/dw_tests --gtest_brief=1 | tail -3`
Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "Delete Direct Carve panel family and preparation glue"
```

---

### Task 7: Delete the CAM core

**Files:**
- Delete from `src/core/carve/`: `carve_job.{h,cpp}`, `carve_streamer.{h,cpp}`, `toolpath_generator.{h,cpp}`, `toolpath_types.h`, `toolpath_advisor.h`, `toolpath_preview.{h,cpp}`, `gcode_export.{h,cpp}`, `heightmap.{h,cpp}`, `surface_analysis.{h,cpp}`, `island_detector.{h,cpp}`, `roughing_tool_selector.{h,cpp}`, `material_blank_defaults.{h,cpp}`, `direct_carve_workflow.{h,cpp}`, `direct_carve_operation_state.{h,cpp}`, `direct_carve_tool_plan.{h,cpp}`, `direct_carve_probe_tool_diameter.{h,cpp}`, `direct_carve_zeroing_probe.{h,cpp}`
- Keep: `model_fitter.{h,cpp}`, `alignment_validator.{h,cpp}`
- Delete tests: `test_carve_job.cpp`, `test_carve_streamer.cpp`, `test_carve_integration.cpp`, `test_toolpath_generator.cpp`, `test_toolpath_preview.cpp`, `test_gcode_export.cpp`, `test_heightmap.cpp`, `test_surface_analysis.cpp`, `test_island_detector.cpp`, `test_roughing_tool_selector.cpp`, `test_material_blank_defaults.cpp`, `test_direct_carve_workflow.cpp`, `test_direct_carve_operation_state.cpp`, `test_direct_carve_tool_plan.cpp`, `test_direct_carve_probe_tool_diameter.cpp`, `test_direct_carve_zeroing_probe.cpp` (verify each exists before rm; the survey found all but confirm with `ls`)
- Modify: `src/CMakeLists.txt` carve block (~:208-228), `tests/CMakeLists.txt` (`DW_TEST_SOURCES` ~:119-153, `DW_TEST_DEPS` ~:313-331 — note `core/carve/heightmap.cpp` is listed TWICE in tests/CMakeLists.txt, at ~:239 and ~:313; remove both)

- [ ] **Step 1: Delete sources and tests**

```bash
cd /home/matthew/Projects/DW
git rm $(printf 'src/core/carve/%s ' carve_job.h carve_job.cpp carve_streamer.h carve_streamer.cpp \
    toolpath_generator.h toolpath_generator.cpp toolpath_types.h toolpath_advisor.h \
    toolpath_preview.h toolpath_preview.cpp gcode_export.h gcode_export.cpp \
    heightmap.h heightmap.cpp surface_analysis.h surface_analysis.cpp \
    island_detector.h island_detector.cpp roughing_tool_selector.h roughing_tool_selector.cpp \
    material_blank_defaults.h material_blank_defaults.cpp \
    direct_carve_workflow.h direct_carve_workflow.cpp \
    direct_carve_operation_state.h direct_carve_operation_state.cpp \
    direct_carve_tool_plan.h direct_carve_tool_plan.cpp \
    direct_carve_probe_tool_diameter.h direct_carve_probe_tool_diameter.cpp \
    direct_carve_zeroing_probe.h direct_carve_zeroing_probe.cpp)
ls tests/ | grep -E 'carve|toolpath|heightmap|island|surface_analysis|gcode_export|material_blank|roughing'   # confirm test list, then git rm them (KEEP test_prepare_carve_flow.cpp, test_carve_preparation_boundary.cpp)
```

- [ ] **Step 2: Remove CMake entries**

Empty the `# Carve (Direct Carve pipeline)` block in `src/CMakeLists.txt` down to `model_fitter.cpp` and `alignment_validator.cpp`; mirror in `tests/CMakeLists.txt` (both lists, remembering the duplicated `heightmap.cpp`).

- [ ] **Step 3: Verify the keeps are truly self-contained and build**

Run: `grep -n '#include' src/core/carve/model_fitter.cpp src/core/carve/alignment_validator.cpp | grep '"'` — expect only own headers and `core/` headers outside carve. Then:
`cmake --build build -j$(nproc) 2>&1 | tail -3 && ./build/tests/dw_tests --gtest_brief=1 | tail -3`
Expected: PASS. Any missed includer surfaces here as a compile error naming the file to fix.

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "Delete Direct Carve CAM core"
```

---

### Task 8: Source-size caps, gates, and milestone bookkeeping

**Files:**
- Modify: `cmake/SourceSizeCaps.cmake` (remove entries for every deleted file)
- Modify: `.planning/STATE.md`, `README.md`

- [ ] **Step 1: Prune source-size caps**

Remove every deleted path from `cmake/SourceSizeCaps.cmake`. Verify: `cmake --build build --target check_source_sizes 2>&1 | tail -3` → passes with no references to missing files.

- [ ] **Step 2: Run the full gate**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -3
./build/tests/dw_tests --gtest_brief=1 | tail -3
./build/tests/dw_carve_preparation_tests --gtest_brief=1 | tail -2
./build/tests/dw_run_coordination_tests --gtest_brief=1 | tail -2
ctest --test-dir build -j$(nproc) --output-on-failure 2>&1 | tail -5
git diff --check
```
Expected: everything green, no whitespace errors. Also launch once: `timeout 20 ./build/digital_workshop --diagnostic-init 2>&1 | tail -3` (or the project's isolated-profile diagnostic invocation recorded in `.planning/PROJECT-CENTERED-WORKSHOP-RELEASE-VALIDATION.md`) → clean initialization.

- [ ] **Step 3: Update milestone records**

- `.planning/STATE.md`: new frontmatter (milestone v0.8.0 "PureCutCNC CAM Integration", phase 1 of 7 complete), a short Phase 1 completion note (what was deleted, the placeholder, the Run adapter rewire), and Next Action = Phase 2 (sidecar packaging, Bun compile proof-of-concept first).
- `README.md`: in Features, replace the Direct Carve/carve-preparation claims with one line: CAM is being rebuilt on the PureCutCNC engine (v0.8.0); external G-code workflows unaffected. Update the test-count sentence to the new suite total (read it from the Step 2 output).

- [ ] **Step 4: Final commit**

```bash
git add -A && git commit -m "Complete CAM Phase 1: Direct Carve yanked, placeholder live"
```

---

## Explicitly kept (do not touch)

- `src/core/carve/model_fitter.*`, `alignment_validator.*` (viewport support)
- `src/modules/**` — all seven module libraries and their isolated test executables
- `src/core/cnc/**`, `src/core/gcode/**`, `src/ui/panels/gcode_panel*`, `cnc_safety_panel*`
- Window ID `"direct_carve"`, config key `show_direct_carve`, layout presets, `CncStreamOrigin` enum (origin *usage* simplifies to `ExternalGCode` in Task 5)
- `scripts/*.py`, `.planning/` history, `cambridge/`
