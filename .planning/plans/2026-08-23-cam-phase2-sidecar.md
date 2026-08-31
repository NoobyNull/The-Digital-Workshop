# CAM Phase 2: Sidecar Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Package the dw-bridge CAM engine as a shippable sidecar, give DW a native runtime that spawns/health-checks/stops it and a client that talks to it, and surface engine status in the CAM placeholder — ending with the sidecar in the Linux install payload and its smoke test green.

**Architecture:** The sidecar ships as the payload shape proven by the committed proof of concept (`a2a7ac3`): Bun runtime binary + `dw-cam-engine.js` bundle (`bun build --target=bun --external manifold-3d`) + `node_modules/manifold-3d` beside it. Single-file `--compile` is known-broken (bundler mangles manifold's emscripten glue; compiled binaries cannot resolve external packages) — do not revisit it. On the C++ side, `CamEngineRuntime` mirrors the existing `OllamaRuntime` house pattern (typed status, `ensureReady()`, `stopOwnedProcess()`, POSIX `fork`/`execlp`, Windows reports unmanaged), and `CamEngineClient` splits into pure JSON-parsing functions (unit-tested) over a small libcurl transport (covered end-to-end by the sidecar smoke script).

**Tech Stack:** Bun 1.3.11 (`/usr/bin/bun`, build-time only), bash packaging scripts, C++17, libcurl (existing dep), nlohmann/json, GoogleTest.

**Verification commands:**
- Build: `cmake --build build -j$(nproc) 2>&1 | tail -3`
- Suite: `./build/tests/dw_tests --gtest_brief=1 | tail -3` (baseline: 1485 passed / 2 env-gated skips)
- Shell is fish: run compound commands via `bash -c` or scripts. Bash timeout 600000 ms for builds.

**Ground truth (verified 2026-08-23):**
- Bridge endpoints: `GET /api/health` → `{"ok":true,"service":"dw-bridge","version":1}`; `GET /api/machines` → array; `POST /api/job` (JobSpec) → `{ok,gcode,...}` or `{ok:false,error}` (422/400/500). Port: `DW_BRIDGE_PORT` env, default 8973, binds 127.0.0.1 only.
- Working payload recipe (from `cambridge/`): `bun build --target=bun --external manifold-3d bridge/server.ts --outfile <out>/dw-cam-engine.js` (~5 MB) + copy `node_modules/manifold-3d` + copy the `bun` binary → run `./bun dw-cam-engine.js` with cwd = payload dir. Total ~103 MB.
- House process pattern: `src/core/ai/ollama_runtime.{h,cpp}` — config struct, `OllamaRuntimeStatus` with typed factory reasons, `ensureReady()`, `waitUntilReachable()`, `stopOwnedProcess()`, `fork`/`execlp` under `#ifndef _WIN32`, Windows branch returns false (unmanaged).
- HTTP helper: `src/core/utils/lmstudio_http.h` has `curlPost(url, body)` only; a GET helper must be added.
- Resource lookup: `dw::findBundledResourceDirForExe(exeDir, leafDir)` in `src/core/paths/app_paths.h:77` resolves both build-tree and installed Linux layouts.
- Installer staging: `packaging/linux/make-installer.sh` stages canonical source `resources/` into `$STAGING_DIR/resources`; `install.sh`/`uninstall.sh` handle user/system modes; `packaging/linux/smoke-packages.sh` smoke-tests installed payloads.
- Windows/macOS payloads need per-platform Bun binaries; this phase wires the script for it (`--platform` flag) but only Linux is built and smoke-verified locally. Windows process-spawn support follows the house pattern's current scope (unmanaged) and is deferred to the pre-Windows-release phase.

---

### Task 1: Sidecar build script

**Files:**
- Create: `packaging/build-cam-engine.sh`

- [ ] **Step 1: Write the script**

```bash
#!/bin/bash
# Build the dw-cam-engine sidecar payload from the cambridge environment.
# Usage: packaging/build-cam-engine.sh <out-dir> [--platform linux-x64]
# Output layout (proven by the Phase 2 PoC, commit a2a7ac3):
#   <out-dir>/bun                      runtime binary
#   <out-dir>/dw-cam-engine.js        bundled bridge (+engine), manifold external
#   <out-dir>/node_modules/manifold-3d/  wasm + glue, external because the
#                                        bundler breaks its emscripten glue
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(realpath "$SCRIPT_DIR/..")"
CAMBRIDGE="$REPO_ROOT/cambridge"
OUT="${1:?usage: build-cam-engine.sh <out-dir> [--platform linux-x64]}"
PLATFORM="${3:-linux-x64}"
[ "${2:-}" = "--platform" ] && PLATFORM="$3"

command -v bun >/dev/null || { echo "bun is required (https://bun.sh)"; exit 1; }
[ -d "$CAMBRIDGE/node_modules/manifold-3d" ] || { echo "run npm install in cambridge/ first"; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT/node_modules"
(cd "$CAMBRIDGE" && bun build --target=bun --external manifold-3d \
    bridge/server.ts --outfile "$OUT/dw-cam-engine.js")
cp -a "$CAMBRIDGE/node_modules/manifold-3d" "$OUT/node_modules/"

if [ "$PLATFORM" = "linux-x64" ]; then
    cp "$(command -v bun)" "$OUT/bun"
else
    # ponytail: non-Linux runtimes are fetched in CI, not here.
    echo "NOTE: $PLATFORM runtime not staged; download bun-$PLATFORM in CI." >&2
fi

echo "cam-engine payload: $OUT ($(du -sh "$OUT" | cut -f1))"
```

`chmod +x packaging/build-cam-engine.sh`.

- [ ] **Step 2: Run it and verify the payload works**

```bash
packaging/build-cam-engine.sh build/cam-engine
bash -c 'cd build/cam-engine && DW_BRIDGE_PORT=18980 ./bun dw-cam-engine.js & sleep 1.5
  curl -sf http://127.0.0.1:18980/api/health
  curl -sf -X POST http://127.0.0.1:18980/api/job -H "Content-Type: application/json" \
    --data-binary @cambridge/bridge/examples/dome-fluidnc.json | head -c 120
  kill %1'
```
Expected: health JSON with `"service":"dw-bridge"`, then G-code output beginning `{"ok":true,"gcode":"; dome-example`.

- [ ] **Step 3: Commit**

```bash
git add packaging/build-cam-engine.sh && git commit -m "Add cam-engine sidecar payload build script"
```

---

### Task 2: Sidecar smoke script

**Files:**
- Create: `packaging/smoke-cam-engine.sh`

- [ ] **Step 1: Write the smoke script**

A standalone check usable against any payload dir (build tree or installed location). Same shape as the Step-2 verification above, hardened: waits for health with retries (20 × 0.25 s), asserts `"service":"dw-bridge"` in health, asserts `"ok":true` and `"gcode"` in the job response, always kills the spawned process (trap), prints PASS/FAIL lines, exits nonzero on failure. Takes the payload dir as `$1` and an optional port as `$2` (default 18981). Reuse the retry/trap structure from `packaging/linux/smoke-installed-app.sh` for consistency.

- [ ] **Step 2: Run it against the Task 1 payload**

Run: `packaging/smoke-cam-engine.sh build/cam-engine`
Expected: `PASS` lines for health, machines, job; exit 0.

- [ ] **Step 3: Commit**

```bash
git add packaging/smoke-cam-engine.sh && git commit -m "Add cam-engine sidecar smoke script"
```

---

### Task 3: HTTP GET helper

**Files:**
- Modify: `src/core/utils/lmstudio_http.{h,cpp}`

- [ ] **Step 1: Add `curlGet` beside `curlPost`**

Header (below the `curlPost` declaration):

```cpp
// GET a URL, return response body. Empty string on failure.
// timeoutSeconds bounds the whole transfer (connect + response).
std::string curlGet(const std::string& url, long timeoutSeconds = 5);
```

Implementation mirrors `curlPost`'s handle setup and `writeCallback` usage minus the POST fields, with `CURLOPT_TIMEOUT` set from the parameter. Read `curlPost`'s body first and copy its cleanup/error style exactly.

- [ ] **Step 2: Build**

Run: `cmake --build build -j$(nproc) 2>&1 | tail -3` — green. (Transport is exercised for real in Task 6's live check; no socket stub tests — same coverage split the ollama runtime uses.)

- [ ] **Step 3: Commit**

```bash
git add src/core/utils/lmstudio_http.h src/core/utils/lmstudio_http.cpp
git commit -m "Add curlGet helper for local service clients"
```

---

### Task 4: CamEngineClient — pure parsing + thin transport

**Files:**
- Create: `src/core/cam/cam_engine_client.h`
- Create: `src/core/cam/cam_engine_client.cpp`
- Test: `tests/test_cam_engine_client.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing tests (pure parsing only)**

`tests/test_cam_engine_client.cpp`:

```cpp
#include "core/cam/cam_engine_client.h"
#include <gtest/gtest.h>

namespace dw::cam {

TEST(CamEngineClient, ParsesHealthyResponse) {
    const auto h = parseHealth(R"({"ok":true,"service":"dw-bridge","version":1})");
    ASSERT_TRUE(h.has_value());
    EXPECT_TRUE(h->ok);
    EXPECT_EQ(h->service, "dw-bridge");
    EXPECT_EQ(h->version, 1);
}

TEST(CamEngineClient, RejectsWrongServiceAndGarbage) {
    const auto wrong = parseHealth(R"({"ok":true,"service":"other","version":1})");
    ASSERT_TRUE(wrong.has_value());
    EXPECT_NE(wrong->service, "dw-bridge");
    EXPECT_FALSE(parseHealth("not json").has_value());
    EXPECT_FALSE(parseHealth("").has_value());
}

TEST(CamEngineClient, ParsesMachineList) {
    const auto machines = parseMachines(
        R"([{"id":"grbl","name":"GRBL 1.1","description":"d","fileExtension":"nc"}])");
    ASSERT_EQ(machines.size(), 1u);
    EXPECT_EQ(machines[0].id, "grbl");
    EXPECT_EQ(machines[0].name, "GRBL 1.1");
    EXPECT_EQ(machines[0].fileExtension, "nc");
}

TEST(CamEngineClient, ParsesJobSuccessAndFailure) {
    const auto ok = parseJobResult(R"({"ok":true,"gcode":"G21\nG90\n"})");
    EXPECT_TRUE(ok.ok);
    EXPECT_EQ(ok.gcode, "G21\nG90\n");
    const auto bad = parseJobResult(R"({"ok":false,"error":"boom"})");
    EXPECT_FALSE(bad.ok);
    EXPECT_EQ(bad.error, "boom");
    const auto garbage = parseJobResult("not json");
    EXPECT_FALSE(garbage.ok);
    EXPECT_FALSE(garbage.error.empty());
}

} // namespace dw::cam
```

Register in `tests/CMakeLists.txt` (`DW_TEST_SOURCES` + `${CMAKE_SOURCE_DIR}/src/core/cam/cam_engine_client.cpp` in `DW_TEST_DEPS`; the deps list already links curl for lmstudio_http — verify, and mirror whatever it does).

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build -j$(nproc) --target dw_tests 2>&1 | tail -3` — fails: header missing.

- [ ] **Step 3: Implement**

`src/core/cam/cam_engine_client.h`:

```cpp
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace dw::cam {

struct EngineHealth {
    bool ok = false;
    std::string service;
    int version = 0;
};

struct EngineMachine {
    std::string id;
    std::string name;
    std::string description;
    std::string fileExtension;
};

struct EngineJobResult {
    bool ok = false;
    std::string gcode;
    std::string error;
};

// Pure parsers — unit-testable without a live engine.
[[nodiscard]] std::optional<EngineHealth> parseHealth(const std::string& json);
[[nodiscard]] std::vector<EngineMachine> parseMachines(const std::string& json);
[[nodiscard]] EngineJobResult parseJobResult(const std::string& json);

// Thin transport over the local sidecar (loopback HTTP, libcurl).
// Covered end-to-end by packaging/smoke-cam-engine.sh and the runtime's
// reachability checks rather than socket-stub unit tests.
class CamEngineClient {
  public:
    explicit CamEngineClient(std::string baseUrl);

    [[nodiscard]] std::optional<EngineHealth> health() const;
    [[nodiscard]] std::vector<EngineMachine> machines() const;
    [[nodiscard]] EngineJobResult submitJob(const std::string& jobSpecJson) const;
    [[nodiscard]] const std::string& baseUrl() const noexcept { return m_baseUrl; }

  private:
    std::string m_baseUrl;
};

} // namespace dw::cam
```

`.cpp`: parsers use `nlohmann::json::parse(s, nullptr, false)` + `is_discarded()` guards, tolerate missing fields (default values), never throw; `parseJobResult` on unparseable input returns `{ok=false, error="engine returned unparseable response"}`. Transport methods: `curlGet(m_baseUrl + "/api/health")`, `curlGet(m_baseUrl + "/api/machines")`, `curlPost(m_baseUrl + "/api/job", jobSpecJson)` (from `core/utils/lmstudio_http.h`), each feeding the corresponding parser; empty transport response → nullopt/empty/`{ok=false,error="engine unreachable"}`. Add the new `.cpp` to `src/CMakeLists.txt` (new `# CAM engine` block after the carve block).

- [ ] **Step 4: Run tests**

Run: `cmake --build build -j$(nproc) --target dw_tests 2>&1 | tail -3 && ./build/tests/dw_tests --gtest_filter='CamEngineClient*' | tail -3` — 4 tests pass. Then full suite — 1489 passed / 2 skipped.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "Add CamEngineClient with pure response parsers"
```

---

### Task 5: CamEngineRuntime — process lifecycle

**Files:**
- Create: `src/core/cam/cam_engine_runtime.h`
- Create: `src/core/cam/cam_engine_runtime.cpp`
- Test: `tests/test_cam_engine_runtime.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

Mirror `src/core/ai/ollama_runtime.{h,cpp}` — read both files fully before writing. Same shape, adapted:

- [ ] **Step 1: Write failing tests for the pure parts**

`tests/test_cam_engine_runtime.cpp` — test what needs no process: config → base URL (`http://127.0.0.1:8973`), payload-dir validation (`payloadLooksComplete(dir)` false for a dir missing `bun`/`dw-cam-engine.js`, true for a temp dir containing both — create fixture files with `std::ofstream`), and status factories (`CamEngineStatus::payloadMissing(...)` carries a non-empty reason). Model the assertions on `tests/test_ollama_runtime.cpp`'s style (read it first).

- [ ] **Step 2: Verify failure, then implement**

`cam_engine_runtime.h` sketch (final signatures may follow ollama's exactly):

```cpp
#pragma once

#include <cstdint>
#include <string>
#include "core/types.h"

namespace dw::cam {

struct CamEngineConfig {
    std::string host = "127.0.0.1";
    uint16_t port = 8973;           // bridge default; DW_BRIDGE_PORT overrides in dev
    Path payloadDir;                // dir holding bun + dw-cam-engine.js
    bool manageProcess = true;
};

struct CamEngineStatus {
    bool ready = false;
    std::string reason;
    std::string endpoint;

    static CamEngineStatus ok(std::string endpoint);
    static CamEngineStatus payloadMissing(const Path& dir);
    static CamEngineStatus engineUnavailable();
    static CamEngineStatus unmanagedPlatform();
};

[[nodiscard]] std::string baseUrl(const CamEngineConfig& config);
[[nodiscard]] bool payloadLooksComplete(const Path& dir);
// Locates the payload: DW_CAM_ENGINE_DIR env override first (dev), then
// findBundledResourceDirForExe(exeDir, "cam-engine").
[[nodiscard]] Path locatePayloadDir(const Path& exeDir);

class CamEngineRuntime {
  public:
    explicit CamEngineRuntime(CamEngineConfig config = {});
    ~CamEngineRuntime();

    CamEngineRuntime(const CamEngineRuntime&) = delete;
    CamEngineRuntime& operator=(const CamEngineRuntime&) = delete;

    CamEngineStatus ensureReady();
    void stopOwnedProcess();
    [[nodiscard]] bool ownsProcess() const;
    [[nodiscard]] const CamEngineConfig& config() const { return m_config; }

  private:
    bool startOwnedProcess();
    bool waitUntilReachable();
    [[nodiscard]] bool isReachable() const;

    CamEngineConfig m_config;
    // pid member + platform guards exactly as ollama_runtime does
};

} // namespace dw::cam
```

Implementation notes (follow ollama's corresponding functions line-for-line where they match):
- `isReachable()` = `CamEngineClient(baseUrl(m_config)).health()` returns a value with `service == "dw-bridge"` — the service check prevents declaring victory over some other local server on the port.
- `startOwnedProcess()` POSIX: `fork()`; child does `chdir(payloadDir)`, `setenv("DW_BRIDGE_PORT", ...)`, `execl("./bun", "bun", "dw-cam-engine.js", nullptr)` — cwd MUST be the payload dir so `node_modules/manifold-3d` resolves. `_WIN32` branch returns false and `ensureReady()` maps that to `unmanagedPlatform()` when unreachable (matching ollama's current Windows scope).
- `stopOwnedProcess()` / destructor: SIGTERM + waitpid as ollama does.
- `waitUntilReachable()`: same retry cadence as ollama's.

- [ ] **Step 3: Run tests, then the live check**

Unit: `./build/tests/dw_tests --gtest_filter='CamEngineRuntime*' | tail -3` — pass. Live (real spawn, ad-hoc, not a registered test): with the Task 1 payload present, a one-off `bash -c 'DW_CAM_ENGINE_DIR=$PWD/build/cam-engine ./build/tests/dw_tests --gtest_filter=CamEngineRuntime*'` still passes (env must not break unit tests), and a manual scratch program is unnecessary — Task 6's in-app check covers real spawn. Full suite green.

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "Add CamEngineRuntime process lifecycle mirroring OllamaRuntime"
```

---

### Task 6: App wiring + placeholder status surface

**Files:**
- Modify: `src/app/application.h` (own a `cam::CamEngineRuntime` + accessor)
- Modify: `src/app/application_wiring_workshop.cpp` or `application_wiring.cpp` (wire status provider into the placeholder — pick the file that already wires the placeholder/UIManager and say which in the report)
- Modify: `src/ui/panels/cam_placeholder_panel.{h,cpp}`
- Modify: `tests/test_cam_placeholder_panel.cpp`

- [ ] **Step 1: Extend the placeholder test first**

Add to `tests/test_cam_placeholder_panel.cpp`: the panel accepts a status provider and exposes the latest status line:

```cpp
TEST(CamPlaceholderPanel, ReportsEngineStatusFromProvider) {
    CamPlaceholderPanel panel;
    panel.setEngineStatusProvider(
        [] { return std::string("Engine ready at http://127.0.0.1:8973"); });
    EXPECT_EQ(panel.engineStatusLine(), "Engine ready at http://127.0.0.1:8973");
}

TEST(CamPlaceholderPanel, EngineStatusDefaultsToNotStarted) {
    CamPlaceholderPanel panel;
    EXPECT_EQ(panel.engineStatusLine(), "Engine not started");
}
```

- [ ] **Step 2: Implement**

Panel: `std::function<std::string()> m_engineStatusProvider;` + `setEngineStatusProvider(...)` + `engineStatusLine()` (returns provider() or the default string); `render()` adds a separator and two elements below the status copy: the status line (`ImGui::TextWrapped`) and a `Start engine` button that invokes an `std::function<void()> m_onStartEngine` when set (`setOnStartEngine(...)`). The button is the Phase 2 manual verification surface; Phase 3 replaces it.

Application: own `std::unique_ptr<cam::CamEngineRuntime> m_camEngineRuntime;` created lazily inside the start callback (locate payload via `cam::locatePayloadDir(exeDir)` — find how the app derives its exe dir today by reading `app_paths` usage, e.g. wherever `findBundledResourceDirForExe` or the materials dir is resolved). Wire in the chosen wiring file:
- status provider → if runtime absent: "Engine not started"; else its last `ensureReady()` status (`ready` → "Engine ready at <endpoint>", else the reason).
- `setOnStartEngine` → create runtime if needed, call `ensureReady()` on a background thread or synchronously (synchronous is acceptable: the wait is bounded by `waitUntilReachable`'s timeout; state that choice in the report), store the returned status for the provider.
- App shutdown already destroys Application members — confirm the runtime destructor stops the child (ollama pattern) and note where.

- [ ] **Step 3: Verify — unit, suite, and the real thing**

Unit + full suite green. Then the real check: `packaging/build-cam-engine.sh build/cam-engine` (if not present), then run the app (`timeout 30 ./build/digital_workshop 2>&1 | tail -5` with a display), open the CAM window, click Start engine, confirm the status line reaches "Engine ready at http://127.0.0.1:8973", and confirm the `bun` process terminates when the app exits (`pgrep -f dw-cam-engine.js` empty after). If no display is available, run `./build/digital_workshop --diagnostic` for init cleanliness and verify spawn/stop via the DW_CAM_ENGINE_DIR-pointed unit run instead; report which path was used.

- [ ] **Step 4: Commit**

```bash
git add -A && git commit -m "Wire CAM engine runtime with placeholder status surface"
```

---

### Task 7: Linux packaging integration

**Files:**
- Modify: `packaging/linux/make-installer.sh` (stage `build/cam-engine` → `$STAGING_DIR/cam-engine`; build it via `packaging/build-cam-engine.sh` if absent)
- Modify: `packaging/linux/install.sh` / `uninstall.sh` (install/remove the `cam-engine/` dir alongside existing resources; preserve the `bun` binary's execute bit)
- Modify: `packaging/linux/smoke-packages.sh` (after install, run `packaging/smoke-cam-engine.sh <installed cam-engine dir>`)
- Modify: `src/core/cam/cam_engine_runtime.cpp` only if `locatePayloadDir` needs the installed location added (check what `findBundledResourceDirForExe` already covers for the installed layout — it was built for exactly this dual layout; extend only if its search roots miss the chosen install path)

- [ ] **Step 1: Read the three packaging scripts fully, then wire the payload through**

Follow the existing staging/install patterns exactly (the scripts are disciplined about staging only canonical sources — mirror the `resources/` handling). Choose the installed location consistent with where `findBundledResourceDirForExe` searches so the runtime finds it without special-casing; state the chosen path in the report.

- [ ] **Step 2: Build the installer and smoke it**

```bash
./packaging/linux/make-installer.sh build 0.8.0-dev
bash packaging/linux/smoke-packages.sh   # or its documented invocation — read the header
```
Expected: installer builds; smoke passes including the new cam-engine check (health + real job through the installed payload). `bash -n` clean on every edited script.

- [ ] **Step 3: Commit**

```bash
git add -A && git commit -m "Bundle cam-engine sidecar in Linux packaging with smoke"
```

---

### Task 8: Gates and records

**Files:**
- Modify: `.planning/STATE.md`, `README.md`, `cmake/SourceSizeCaps.cmake` (only if the ratchet flags a touched file)

- [ ] **Step 1: Full gate**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -3
./build/tests/dw_tests --gtest_brief=1 | tail -3
cmake --build build --target check_source_sizes 2>&1 | tail -3
ctest --test-dir build -j1 --output-on-failure 2>&1 | tail -4   # serial: parallel ctest has pre-existing temp-dir flakiness
git diff --check
packaging/smoke-cam-engine.sh build/cam-engine
```
All green.

- [ ] **Step 2: Records**

- `.planning/STATE.md`: progress 2/7 phases; Phase 2 summary (payload shape + why compile was rejected, runtime/client/status surface, packaging + smoke); Next Action: Phase 3 — bridge session API + parity checklist derivation + schema-export endpoint for generated parameter forms.
- `README.md`: Building-from-source gains a short "CAM engine sidecar" note (Bun required only for building the sidecar payload; `packaging/build-cam-engine.sh`), and the install-size implication (~100 MB payload) in the install section. Surgical edits only.

- [ ] **Step 3: Commit**

```bash
git add -A && git commit -m "Complete CAM Phase 2: sidecar packaged, runtime wired"
```

---

## Explicitly out of scope (Phase 3+)

- Any endpoint beyond health/machines/job; CAMJ session API; parity checklist; schema export.
- Windows/macOS sidecar spawn and payload smoke (script takes `--platform` but only linux-x64 is staged/verified here).
- MSI/DMG payload wiring (CI-side; follows the same staging recipe).
- Replacing the placeholder's Start-engine button with real CAM UI.
