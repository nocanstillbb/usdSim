# USD 浏览器 (USD Browser)

基于 Qt Quick 和 OpenUSD 的轻量级 3D 场景浏览与编辑器，内置 MCP Inspector 支持运行时 UI 自动化。

![Qt](https://img.shields.io/badge/Qt-6-green) ![USD](https://img.shields.io/badge/OpenUSD-24.x-blue) ![C++](https://img.shields.io/badge/C%2B%2B-17-orange)

## 功能特性

- **3D 视口**：基于 Qt RHI 的实时渲染，支持旋转/平移/缩放相机
- **变换操控**：移动、旋转、缩放 Gizmo，支持多选操作
- **网格与吸附**：可切换地面网格（主/次线 + 坐标轴），移动吸附到 1cm 网格
- **Prim 层级树**：浏览和管理 USD 场景层级，支持可见性切换
- **属性编辑**：查看和修改 Prim 属性，实时同步到视口
- **撤销/重做**：完整的操作历史记录
- **文件支持**：打开/保存 `.usd`、`.usda`、`.usdc`、`.usdz` 格式

## 项目结构

```
├── main.cpp                 # 应用入口，注册 QML 类型，启动 InspectorServer
├── main.qml                 # 主窗口布局（三栏分割）
├── UsdViewportItem.h/cpp    # 3D 视口渲染（RHI）、Gizmo、网格、相机
├── UsdDocument.h/cpp        # USD 文件 I/O、Prim/属性操作
├── UndoStack.h              # 撤销/重做栈
├── UndoCommands.h/cpp       # 撤销/重做命令实现
├── ViewportPanel.qml        # 视口面板（工具栏、方位指示器、cm 标签）
├── PrimTreePanel.qml        # Prim 层级树面板
├── AttributePanel.qml       # 属性编辑面板
├── HistoryPanel.qml         # 操作历史面板
├── shaders/                 # GLSL 顶点/片段着色器
├── icons/                   # SVG 图标资源
├── playqmlright/            # Git 子模块：Qt Inspector + MCP Server
└── prism_all/               # Git 子模块：UI 框架库
```

## 构建与运行

### 依赖

- Qt 6（Core, Gui, Widgets, Quick, Qml, QuickControls2）
- OpenUSD（usd, usdGeom, sdf, tf, vt, gf）
- CMake ≥ 3.16
- C++17 编译器

### 构建

```bash
# 初始化子模块
git submodule update --init --recursive

# 构建
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### 运行

```bash
./build/apptest_qmlcmp
```

应用启动后会在端口 **37521** 上开启 Inspector Server（可通过 `QML_INSPECTOR_PORT` 环境变量修改）。

## MCP 集成

项目配置了三个 MCP 服务器（见 `.mcp.json`）：

| 服务器 | 用途 |
|--------|------|
| **playqmlright** | Qt UI 自动化：截图、点击、输入、树结构查询 |
| **context7-mcp** | 查阅 Qt/USD 等外部文档 |
| **playwright-mcp** | Web 浏览器自动化 |

### 使用方式

1. 启动应用（自动嵌入 InspectorServer）
2. Claude Code 通过 `.mcp.json` 配置的 MCP 工具与应用交互
3. 工具通过 TCP JSON-RPC 与 InspectorServer 通信
4. 所有交互为合成事件注入，无需窗口前台焦点

## 操作说明

| 操作 | 方式 |
|------|------|
| 旋转视角 | 拖拽 |
| 平移视角 | Alt + 拖拽 / 中键拖拽 |
| 缩放 | 滚轮 |
| 选择 | 左键点击 |
| 多选 | Ctrl + 点击 |
| 撤销/重做 | Ctrl+Z / Ctrl+Y |

## 许可证

私有项目。
