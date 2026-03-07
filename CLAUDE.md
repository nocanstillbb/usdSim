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
main.cpp
  └── QQmlApplicationEngine loads main.qml from qrc:/test_qmlcmp/
  └── InspectorServer starts TCP listener on port 37521 (or QML_INSPECTOR_PORT env)

main.qml – Empty window placeholder (520x660 logical pixels)

CMakeLists.txt
  ├── Links to playqmlright submodule
  ├── Qt6 Quick application: apptest_qmlcmp
  └── Dependencies: Qt6::Quick, qmlinspector
```

## Build & Run

```bash
# From repository root (assuming submodules initialized)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run the application
./build/apptest_qmlcmp.app/Contents/MacOS/apptest_qmlcmp
```

The app will expose an inspector server on port 37521 (or from `QML_INSPECTOR_PORT` env var).

## Submodule Structure

- `playqmlright/` – Git submodule containing:
  - `qmlinspector/` – C++ library with InspectorServer for TCP JSON-RPC UI control
  - `playqmlright/playqmlright/` – Python MCP server (FastMCP)
  - `qmlapp/` – Example QML app

## Usage Pattern

1. Launch the Qt app: it embeds InspectorServer automatically
2. Claude Code can use MCP tools via `.mcp.json` configuration
3. Tools communicate with InspectorServer via TCP JSON-RPC
4. Events are injected as synthetic `QMouseEvent`/`QKeyEvent` — no window focus required
