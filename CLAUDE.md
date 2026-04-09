# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a **Qt Quick application with embedded InspectorServer** for runtime UI inspection and control via MCP tools.

### Key File: `CLAUDE.md`
- Contains project-specific instructions that override default behavior
- Current instruction: "只能运行一个 app 实例，启动前先杀掉已存在的 app 实例" (Only one app instance allowed; kill existing instances before starting)

## MCP Server Configuration

This project has three MCP servers configured in `.mcp.json`:

### 1. playqmlright – Qt UI Automation for QML Apps

```json
{
  "mcpServers": {
    "playqmlright": {
      "type": "stdio",
      "command": "uv",
      "args": ["run", "--project", "playqmlright/playqmlright", "python", "playqmlright/playqmlright/server.py"],
      "env": {"QML_INSPECTOR_SOCKET": "127.0.0.1:37521"}
    }
  }
}
```

Available tools: `dump_qt_tree`, `take_screenshot`, `click`, `mouse_move`, `scroll`, `key_press`, `type_text`, `find_item`, `get_item_properties`, `set_property`

### 2. context7-mcp - Documentation Assistant

Install and configure context7-mcp for reading external documentation (e.g., Qt6 Quick Controls API docs):

```bash
npm install @upstash/context7-mcp
```

Configure in `.mcp.json`:

```json
{
  "context7-mcp": {
    "type": "stdio",
    "command": "npx",
    "args": ["-y", "@upstash/context7-mcp"]
  }
}
```

Usage: When working with Qt Quick Controls, ask Claude to use the context7 MCP server to read Qt documentation for API details and usage examples.

### 3. playwright-mcp - Web Browser Automation

Official Playwright MCP integration for browser automation:

```bash
npm install @playwright/mcp
```

Configure in `.mcp.json`:

```json
{
  "playwright-mcp": {
    "type": "stdio",
    "command": "npx",
    "args": ["-y", "@playwright/mcp"]
  }
}
```

Common options:
- `--browser <chrome|firefox|webkit>` – specify browser
- `--viewport-size <width>x<height>` – set viewport size (e.g., "1280x720")
- `--saved-session <name>` – save session state for reuse

## Code Architecture

```
src/
  main.cpp            ← QQmlApplicationEngine loads main.qml from qrc:/usdSim/
  UsdSimApp.h/cpp     ← App wrapper: register_types, init, exec, findDocument, processEvents
  UsdDocument.h/cpp   ← USD Stage management, prim/attr CRUD, StageCache sharing, undo/redo
  UsdViewportItem.h/cpp ← 3D viewport, mesh building, camera, gizmo
  UndoStack.h/cpp
  UndoCommands.h/cpp
  PrimInfo.h
  AttrInfo.h
  bindings.cpp        ← pybind11 module (pyusdSim): exposes UsdSimApp + UsdDocument to Python
  qml/                ← QML UI files
  shaders/            ← GLSL vertex/fragment shaders (viewport, shadow, shadow_cube, outline_post)
  icons/              ← SVG icon resources

python/examples/     ← Python example scripts
  test_shared_stage/
    main.py          ← Demo: Python-C++ shared USD Stage with periodic random modifications
    sample.usda      ← Simple Cube mesh for testing

CMakeLists.txt
  ├── Links to playqmlright and prism_all submodules
  ├── Shared lib: usdSimLib (Qt6, OpenUSD, prism, qmlinspector)
  ├── Executable: usdSim
  ├── pybind11 module: pyusdSim
  ├── Dependencies: Qt6::Quick, Qt6::QuickControls2, Qt6::Gui, qmlinspector, prism, OpenUSD (pxr incl. usdUtils)
  └── Finds OpenUSD via CMAKE_PREFIX_PATH (passed to build.py)

build.py
  ├── Python build script, accepts <pxr_install_dir> as argument
  └── Generates build/usdsim_env.py (cross-platform environment launcher)

pixi.toml
  ├── Conda-forge environment: Python 3.11, Qt6 6.8.3, PySide6 6.8.3 (shared Qt), OpenGL dev libs, pybind11
  └── Tasks: build, py-example
```

## Build & Run

```bash
# Install pixi environment (first time)
pixi install

# Build (pass OpenUSD install directory)
pixi run build /home/cnf2025581067/source/installed

# Run the C++ application
./build/bin/usdSim

# Run Python examples (uses build/usdsim_env.py to set up paths automatically)
pixi run py-example python/examples/test_shared_stage/main.py
```

The app will expose an inspector server on port 37521 (or from `QML_INSPECTOR_PORT` env var).

## Python-C++ Shared Stage

Python and C++ can share the same USD Stage instance via `UsdUtilsStageCache`:

```
Python: stage = Usd.Stage.Open("file.usda")
        cache_id = UsdUtils.StageCache.Get().Insert(stage)
           ↓ (pass cache_id integer)
C++:    UsdUtilsStageCache::Get().Find(id) → same UsdStageRefPtr
           ↓
Python: modify stage → doc.notify_stage_modified(["/World/Cube"])
           ↓
C++:    emit stageModified(paths) → viewport rebuilds meshes
                                   → attribute panel refreshes (only if selected prim changed)
```

### pybind11 API (pyusdSim module)

- `UsdSimApp()` — create app instance
  - `.register_types()` / `.init(args)` / `.exec()` / `.uninit()` / `.unregister_types()`
  - `.find_document()` → `UsdDocument` (call after `init`)
  - `.process_events()` — pump Qt event loop from Python
- `UsdDocument`
  - `.open(path)` / `.open_from_stage_cache(cache_id)` / `.insert_to_stage_cache()`
  - `.notify_stage_modified(modified_prim_paths=[])` — trigger viewport refresh
  - `.is_open()` / `.file_path()`

### build/usdsim_env.py

Auto-generated by `build.py`. Sets up `sys.path` (pyusdSim + pxr) and `LD_LIBRARY_PATH` (conda libstdc++, USD libs). On Linux, re-execs the process via `os.execv` if `LD_LIBRARY_PATH` was missing.

Usage:
- As launcher: `python build/usdsim_env.py your_script.py [args...]`
- Via pixi task: `pixi run py-example your_script.py`

## Submodule Structure

- `third_party/playqmlright/` – Git submodule containing:
  - `qmlinspector/` – C++ library with InspectorServer for TCP JSON-RPC UI control
  - `playqmlright/playqmlright/` – Python MCP server (FastMCP)
  - `qmlapp/` – Example QML app
- `third_party/prism_all/` – Git submodule containing prism framework (container, qt_core, qt_modular, qt_ui)

## Shadow Mapping

The viewport supports shadow mapping for all light types:

### Directional / Rect / Disk lights
- Single 2048×2048 2D depth shadow map
- Orthographic projection from light
- PCSS (Percentage Closer Soft Shadows) with blocker search + variable-width PCF

### Sphere / Cylinder lights (omnidirectional)
- **Cubemap shadow map**: 6-face R32F cubemap, 2048×2048 per face
- Each face uses a separate UBO per mesh (6 UBOs) to avoid QRhi dynamic buffer reuse issues
- Cube shadow shaders: `shadow_cube.vert` / `shadow_cube.frag`
- `shadow_cube.vert`: expands geometry along normals by ~2 cubemap texels to prevent junction light leaks
- `shadow_cube.frag`: stores exact linear distance `length(worldPos - lightPos) / farPlane`
- Sampling in `viewport.frag`: direction-based cubemap lookup with normal offset bias and PCSS

### Shadow bias strategy
- **Geometry expansion** (shadow_cube.vert): vertices pushed along normals in clip space only (not world pos), covering more cubemap texels at surface edges
- **Normal offset** (viewport.frag): sampling point pushed along surface normal to prevent self-shadowing
- **Depth bias** (viewport.frag): proportional to distance, slope-scaled by NdotL

## Camera Save/Restore

Camera state (yaw, pitch, distance, target) is automatically saved to/restored from USD `customLayerData`:
- **Save**: triggered by "保存" / "另存为" menu actions via `saveCameraToStage()`
- **Restore**: automatic on file open in `buildMeshes()` via `restoreCameraFromStage()`
- Stored as `viewportCameraYaw/Pitch/Dist/TargetX/Y/Z` in root layer custom data

## Usage Pattern

1. Launch the Qt app: it embeds InspectorServer automatically
2. Claude Code can use MCP tools via `.mcp.json` configuration
3. Tools communicate with InspectorServer via TCP JSON-RPC
4. Events are injected as synthetic `QMouseEvent`/`QKeyEvent` — no window focus required
