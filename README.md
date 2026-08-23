# Digital Workshop

A project-centered desktop workshop for CNC hobbyists. It keeps the active
project, reusable Design Library, preparation steps, toolpath preview, and
protected machine run connected in one offline application.

Built with C++17, OpenGL 3.3, and Dear ImGui. Runs on Linux, Windows, and macOS.

## Why

A typical CNC woodworking workflow touches half a dozen disconnected tools: a slicer for CAM, a separate G-code viewer to sanity-check toolpaths, a spreadsheet to track material costs, a folder full of STL files with cryptic names, and maybe a cut-list optimizer that only runs on Windows. None of them talk to each other. You end up context-switching between apps, re-entering dimensions, and mentally stitching together information that should live in one place.

Digital Workshop exists to collapse that into a single workspace. Import your
models, preview the toolpaths your CAM software generated, figure out how to cut
the parts from the stock you have on hand, and keep it organized by project.
The Design Library stays a reusable catalog; adding or previewing a design is an
explicit choice, so browsing cannot silently change the project.

It's also fully offline and open source. Your files stay on your machine, the database is a single SQLite file, and every dependency uses a permissive license. There's no account to create, no telemetry, and no feature that stops working if a server goes down.

## Features

### Project-Centered Workshop
- Home is the one place to create, open, resume, or start a project from a Library design
- A persistent context bar shows the active project, selected item or Library preview, current area, and Back to Project
- One six-stage Project Plan leads through Design & Size, Material & Blank, Choose Tool, Carve Preview, Machine Setup, and Review & Run
- Prepare Carve is pinned to the exact project, model, and operation; it cannot silently create or switch projects
- Run CNC accepts an immutable, preflight-checked package and protects the active project with pause, resume, abort, and emergency-stop priority
- Guided Workshop and Advanced Workbench share the same project data and can be selected from the Experience menu

Guided Workshop is opt-in for v0.7.0. The automated suite, packaging smoke, and
72-state responsive/keyboard visual matrix pass; the
[documented five-person novice field test](.planning/PROJECT-CENTERED-WORKSHOP-USABILITY.md)
is the remaining release gate. Advanced Workbench remains the default until
that real human study passes.

### 3D Model Library
- Import STL, OBJ, and 3MF files with drag-and-drop (including recursive folders)
- Content-addressable storage with SHA-based deduplication
- Automatic thumbnail generation via offscreen OpenGL rendering
- Full-text search across names, tags, filenames, and categories
- Two-level category hierarchy, batch operations, and multi-select
- Graph-based relationship queries between models, materials, and projects

### G-code Analysis
- Parses G0/G1/G2/G3 (linear, rapid, arc), coordinate modes, tool changes, and spindle commands
- Computes total path length, cutting vs. rapid distance, estimated machining time, and bounding box
- Trapezoidal motion planning with per-axis velocity and acceleration limits
- Machine profiles for popular CNCs (Shapeoko 4, LongMill MK2) with GRBL `$`-parameter mapping
- 2D and 3D color-coded toolpath visualization (cutting moves vs. rapids)

### 3D Viewport
- OpenGL 3.3 Core Profile renderer with orbit camera and ViewCube navigation
- Adjustable directional lighting, wireframe mode, grid, and axis indicator
- Material texture mapping with planar UV projection
- Color-coded 3D toolpath rendering as extruded quads

### Materials System
- 32 built-in material archives covering hardwoods, softwoods, composites, metals, and plastics
- Two-source loading: user materials override bundled defaults, bundled materials load directly from the install directory
- Material properties: Janka hardness, feed rate, spindle speed, depth of cut, cost per board-foot, grain direction
- Bundled materials can be hidden but not deleted; user materials are fully removable
- `.dwmat` archive format for sharing materials between users
- Optional AI-assisted tagging and material profile generation via local LM Studio

### 2D Cut Optimization
- First-Fit Decreasing and Guillotine packing algorithms
- Per-part rotation and grain direction constraints
- Cost breakdown estimation with efficiency metrics
- Cut plans saved to database and linked to projects

### Project Management
- Associate models, G-code, materials, cut plans, cost estimates, and notes with durable project/item identity
- Hierarchical Project Plan with one deterministic Continue action and visible blockers
- Preview Library assets without adding them, then return to the exact prior project selection
- Export projects as portable `.dwproj` ZIP archives with all assets included
- Full round-trip: export on one machine, import on another with deduplication

### CNC Tool Database
- Vectric-compatible `.vtdb` format — tools can be shared with Aspire/VCarve
- Hierarchical tool tree with groups and drag-and-drop organization
- Tool geometries: end mill, ball nose, V-bit, drill, radiused, tapered, and more
- Per-material, per-machine cutting data (feed rate, plunge rate, spindle speed, stepdown, stepover)
- Import tools from existing `.vtdb` files

### Feeds & Speeds Calculator
- Beginner-safe conservative calculations based on Janka hardness, tool geometry, and machine specs
- 7 material classifications: soft/medium/hard/very-hard wood, composite, metal, plastic
- Machine rigidity derating by drive type: belt (80%), lead screw (90%), ball screw/rack & pinion (100%)
- Automatic power limiting — reduces depth of cut when spindle wattage is exceeded
- One-click "Apply to Cutting Data" writes calculated values directly to the tool database

### GRBL CNC Controller
- Serial port communication with RX buffer accounting (128-byte buffer)
- Feed, rapid, and spindle override commands
- Real-time machine status polling (position, feed rate, spindle speed, overrides)
- Machine profile dialog for per-axis configuration
- Protected Run CNC handoff with exact toolpath fingerprint, preflight facts, authoritative run lock, progress, history, and terminal cleanup

### Settings
- Companion `dw_settings` application with hot-reload (no restart needed)
- Customizable input bindings, machine profiles, and theme selection
- Dark, light, and high-contrast themes

## Installing

Pre-built installers are attached to each [GitHub Release](https://github.com/NoobyNull/The-Digital-Workshop/releases). Download the one for your platform:

| Platform | Format | How to install |
|----------|--------|----------------|
| Linux | `.run` | `chmod +x DigitalWorkshop-*.run && ./DigitalWorkshop-*.run` |
| Windows | `.msi` | Double-click the installer |
| Windows | `.zip` | Extract and run `digital_workshop.exe` |
| macOS | `.dmg` | Open the disk image and drag to Applications |

### Linux install details

The `.run` installer is a self-extracting archive (makeself). It supports two modes:

```bash
# User install (default) — installs to ~/.local/bin
./DigitalWorkshop-0.7.0-linux.run

# System install — installs to /usr/local/bin
sudo ./DigitalWorkshop-0.7.0-linux.run -- --system
```

Both modes install `digital_workshop` and `dw_settings` binaries and create a
`.desktop` entry so the app appears in your application launcher. The installer
prints the persistent uninstaller path when it finishes. For a user install,
run `~/.local/share/digitalworkshop/uninstall.sh`; for a system install, run
`sudo /usr/local/share/digitalworkshop/uninstall.sh --system`.

## Building from source

### Prerequisites

**Linux (Ubuntu/Debian):**
```bash
sudo apt-get install libsdl2-dev libgl-dev zlib1g-dev libcurl4-openssl-dev \
    libdbus-1-dev pkg-config
```

**Windows:** Visual Studio 2019+ with C++ workload. Dependencies are fetched automatically via CMake FetchContent.

**macOS:** Xcode command-line tools. SDL2 can be installed via Homebrew (`brew install sdl2`) or will be fetched automatically.

### Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DDW_BUILD_TESTS=ON
cmake --build build -j$(nproc)
```

### Install from build

```bash
# User install (to ~/.local/bin)
cmake --install build --prefix ~/.local

# System install (to /usr/local)
sudo cmake --install build --prefix /usr/local
```

Or use the packaging scripts to create a distributable installer:

| Platform | Command | Output |
|----------|---------|--------|
| Linux | `./packaging/linux/make-installer.sh build <version>` | Self-extracting `.run` installer |
| Windows | `cd build && cpack -G WIX -C Release` | `.msi` installer |
| Windows | `cd build && cpack -G ZIP -C Release` | `.zip` portable |
| macOS | `cd build && cpack -G DragNDrop -C Release` | `.dmg` disk image |

### Run

```bash
./build/digital_workshop
```

### Test

```bash
./build/tests/dw_tests
```

1,587 tests cover loaders, parsers, database repositories, project
lifecycle and restart resume, contextual Library navigation, Project Plan
derivation, pinned preparation, protected Run coordination, layout migration,
viewport identity, the optimizer, tool calculator, and import/export pipelines.

The render-independent workflow boundaries also have focused test executables:

```bash
./build/tests/dw_workshop_core_tests
./build/tests/dw_project_session_tests
./build/tests/dw_design_library_tests
./build/tests/dw_project_plan_tests
./build/tests/dw_carve_preparation_tests
./build/tests/dw_run_coordination_tests
./build/tests/dw_viewport_presentation_tests
cmake --build build --target check_source_sizes
```

Before a refactoring session, capture production-file hashes and line counts so
pre-existing dirty files are distinguishable from that session's edits:

```bash
cmake -DDW_REPO_ROOT="$PWD" \
  -DDW_OUTPUT="$PWD/build/source-size/session-start.tsv" \
  -P cmake/CaptureSourceSizes.cmake
```

Pass that file back to `cmake/CheckSourceSizes.cmake` at the session gate with
`-DDW_SESSION_SNAPSHOT=...`. Changed legacy monoliths must shrink; new files may
not cross the 750-line hard ceiling.

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `DW_BUILD_TESTS` | `ON` | Build the test suite |
| `DW_BUILD_TOOLS` | `OFF` | Build developer tools (matgen, texgen) |
| `DW_ENABLE_GRAPHQLITE` | `ON` | Enable Cypher graph queries via GraphQLite |

## Architecture

```
src/
  app/          Application shell, window management, workspace wiring
  managers/     UIManager, FileIOManager, ConfigManager
  modules/      UI-free project, Library, plan, preparation, run, and viewport policy
  core/
    archive/    ZIP archive wrapper (miniz)
    config/     Configuration, input bindings, config hot-reload
    database/   SQLite (WAL mode), connection pool, schema, repositories
    export/     .dwproj export/import
    cnc/        CNC controller, tool calculator, serial port
    gcode/      G-code parser, analyzer, machine profiles
    graph/      Cypher queries via GraphQLite
    import/     Background import queue with thread pool
    library/    Library manager
    loaders/    STL, OBJ, 3MF, G-code, texture loaders
    materials/  Material types, manager, archives, AI generation
    mesh/       Mesh data structures, content hashing
    optimizer/  Bin packing + guillotine cut optimization
    paths/      Application paths, path resolver
    storage/    Content-addressable blob store
    threading/  Thread pool, main-thread dispatch queue
    utils/      Logging, file utilities, string utilities
  render/       OpenGL 3.3 renderer, camera, shaders, framebuffers
  ui/
    dialogs/    File, message, lighting, machine profile dialogs
    panels/     Viewport, library, materials, properties, project, G-code, cost, cut optimizer, tool browser
    widgets/    Status bar, toast notifications, binding recorder
```

The core layer has zero dependencies on UI or rendering code. The `Application` class is a thin coordinator that wires managers and panels together. Worker threads communicate with the UI via `MainThreadQueue`. SQLite runs in WAL mode with a connection pool for thread-safe access.

## Dependencies

All dependencies are fetched automatically via CMake FetchContent (no vcpkg or Conan required):

| Library | Version | Purpose |
|---------|---------|---------|
| SDL2 | 2.30.0 | Window, input, events |
| Dear ImGui | docking | Immediate-mode GUI |
| GLAD | 2.0.6 | OpenGL 3.3 Core loader |
| GLM | 1.0.1 | Math |
| SQLite3 | 3.38.2 | Database |
| GraphQLite | 0.3.5 | Cypher queries over SQLite |
| nlohmann/json | 3.11.3 | JSON serialization |
| miniz | 3.0.2 | ZIP archives |
| stb | latest | Image loading |
| libcurl | system | HTTP (AI features) |
| D-Bus | system (Linux) | Reconnect durable network locations through KIO-FUSE |
| GoogleTest | 1.14.0 | Testing |

## CI/CD

GitHub Actions runs on every push to `main` and on pull requests:

- **Linux:** GCC + Clang matrix on Ubuntu 24.04
- **Windows:** MSVC on windows-latest
- **macOS:** Apple Clang on macos-latest
- **Static analysis:** clang-tidy + cppcheck
- **Coverage:** lcov HTML report

Tagging `v*` triggers automatic release builds with platform installers attached.

## License

MIT
