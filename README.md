# Digital Workshop

A desktop workshop for CNC woodworkers with three jobs: manage your design
library, turn designs into G-code (CAM), and send that G-code to the machine.
Projects tie them together the way a 3D-printing slicer project does: the
designs, the CAM setups made from them, the G-code those setups produced, and
the runs of that G-code.

Built with C++17, OpenGL 3.3, and Dear ImGui. Runs on Linux, Windows, and macOS.

## Why

A typical CNC woodworking workflow touches several disconnected tools: a folder full of STL files with cryptic names, a CAM program, a separate G-code viewer to sanity-check toolpaths, and a sender to run the job. None of them talk to each other, so you re-enter dimensions and mentally stitch together information that should live in one place.

Digital Workshop collapses that into one workspace: find the design, set up
the cut, check the toolpath, and run it, with every piece kept together in the
project.

It's also fully offline and open source. Your files stay on your machine, the database is a single SQLite file, and every dependency uses a permissive license. There's no account to create, no telemetry, and no feature that stops working if a server goes down.

## Features

### Design Library
- Import STL, OBJ, and 3MF files with drag-and-drop (including recursive folders)
- Content-addressable storage with SHA-based deduplication
- Automatic thumbnail generation via offscreen OpenGL rendering
- Full-text search across names, tags, filenames, and categories
- Two-level category hierarchy, batch operations, and multi-select
- Library maintenance, missing-file recovery, and workspace relocation
- Optional AI-assisted tagging via a local LM Studio or Ollama model

### Projects
- Organized like a slicer project: each design, its CAM setups, the G-code each
  setup produced, and the runs of that G-code
- G-code whose design or setup changed since it was generated reads **Out of
  date**; a run cut short reads **Interrupted**
- Home is the one place to create, open, resume, or start a project from a Library design
- Preview Library designs without adding them, then return to the exact prior project selection
- Export projects as portable `.dwproj` ZIP archives with models and G-code included
- Full round-trip: export on one machine, import on another with deduplication

### CAM
- CAM is being rebuilt on the [PureCutCNC](https://github.com/PureCutCNC/purecutcnc)
  toolpath engine, which runs as a local sidecar process. Digital Workshop uses
  it for toolpath math only; the CAM workspace itself is native. See
  `.planning/WORKSHOP-SCOPE.md` for the plan.
- External G-code from any CAM program imports, previews, and runs today

### Sender and Machine Control
- GRBL-family controllers over USB serial or TCP, plus a built-in virtual CNC
- Real-time status (position, feed rate, spindle speed, overrides) and feed, rapid, and spindle overrides
- Jog (keyboard and gamepad), MDI console, work zero and coordinate systems, macros
- Firmware settings, machine profiles with per-axis configuration, preflight checks, and safety controls
- Job progress, history, and resume after interruption
- A protected Run package keyed on the exact setup and G-code fingerprint, ready
  for CAM output; the Sender streams G-code files directly today

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
- Color-coded 3D toolpath rendering with simulation playback

### Materials
- 32 built-in material archives covering hardwoods, softwoods, composites, metals, and plastics
- Material properties used by feeds and speeds: Janka hardness, feed rate, spindle speed, depth of cut, grain direction
- Bundled materials can be hidden but not deleted; user materials are fully removable
- `.dwmat` archive format for sharing materials between users

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

The Linux `.run` installer also includes the CAM engine sidecar, which
accounts for roughly 100 MB of the installer's size.

## Building from source

### Prerequisites

**Linux (Ubuntu/Debian):**
```bash
sudo apt-get install libsdl2-dev libgl-dev zlib1g-dev libcurl4-openssl-dev \
    libdbus-1-dev pkg-config
```

**Windows:** Visual Studio 2019+ with C++ workload. Dependencies are fetched automatically via CMake FetchContent.

**macOS:** Xcode command-line tools. SDL2 can be installed via Homebrew (`brew install sdl2`) or will be fetched automatically.

**CAM engine sidecar:** Bun is only required if you want to build the CAM
engine sidecar payload (`packaging/build-cam-engine.sh build/cam-engine`).
The app builds and runs without it; CAM features simply report the engine
as unavailable.

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

1,258 tests cover loaders, parsers, database repositories,
project lifecycle and restart resume, project contents, contextual Library
navigation, pinned CAM setups, protected Run coordination, layout migration,
viewport identity, the tool calculator, and import/export pipelines.

The render-independent workflow boundaries also have focused test executables:

```bash
./build/tests/dw_workshop_core_tests
./build/tests/dw_project_session_tests
./build/tests/dw_design_library_tests
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

## Architecture

```
src/
  app/          Application shell, window management, workspace wiring
  managers/     UIManager, FileIOManager, ConfigManager
  modules/      UI-free project session, Library, setup identity, run, and viewport policy
  core/
    archive/    ZIP archive wrapper (miniz)
    config/     Configuration, input bindings, config hot-reload
    database/   SQLite (WAL mode), connection pool, schema, repositories
    export/     .dwproj export/import
    cam/        PureCutCNC engine sidecar client and runtime
    cnc/        CNC controller, tool calculator, serial port
    gcode/      G-code parser, analyzer, machine profiles
    import/     Background import queue with thread pool
    library/    Library manager
    loaders/    STL, OBJ, 3MF, G-code, texture loaders
    materials/  Material types, manager, archives, AI generation
    mesh/       Mesh data structures, content hashing
    paths/      Application paths, path resolver
    storage/    Content-addressable blob store
    threading/  Thread pool, main-thread dispatch queue
    utils/      Logging, file utilities, string utilities
  render/       OpenGL 3.3 renderer, camera, shaders, framebuffers
  ui/
    dialogs/    File, message, lighting, machine profile dialogs
    panels/     Viewport, library, materials, properties, project, G-code, CAM, sender, tool browser
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
| nlohmann/json | 3.11.3 | JSON serialization |
| miniz | 3.0.2 | ZIP archives |
| stb | latest | Image loading |
| libcurl | system | HTTP (CAM engine, AI tagging) |
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
