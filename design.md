# Notepad++ Linux 移植架构说明

> 本文档描述 Notepad++ 从 Windows (Win32 API) 移植到 Linux (GTK3) 的整体架构设计。
> 当前移植代码位于 `linux/` 目录，基础框架已完成约 30-40% 的功能。

---

## 目录

1. [移植策略概述](#1-移植策略概述)
2. [技术选型](#2-技术选型)
3. [目录结构](#3-目录结构)
4. [组件对照表](#4-组件对照表-win32--gtk3)
5. [架构分层](#5-架构分层)
6. [已实现功能](#6-已实现功能)
7. [待实现功能](#7-待实现功能)
8. [跨平台共享组件](#8-跨平台共享组件)
9. [构建系统](#9-构建系统)
10. [设置与数据存储](#10-设置与数据存储)
11. [插件系统设计](#11-插件系统设计)
12. [关键 API 映射](#12-关键-api-映射)

---

## 1. 移植策略概述

### 不兼容性分析

原 Windows Notepad++ 代码（`PowerEditor/src/`）深度依赖 Win32 API：

- **GUI 层**：自定义 WNDCLASS + SendMessage 消息循环，无 MFC/Qt/GTK 框架
- **编辑控件**：ScintillaWin（Win32 版 Scintilla）
- **DLL 插件**：LoadLibrary/GetProcAddress
- **注册表**：HKCU/HKLM 配置存储
- **字符串**：TCHAR/wstring（UTF-16）
- **文件监控**：ReadDirectoryChangesW

原代码 **无法** 在 Linux 上直接编译，需要完全重写 UI 层。

### 移植方案：新 GTK3 应用 + 复用 Scintilla/Lexilla

采用"外壳重写、引擎复用"策略：

```
┌────────────────────────────────────────────────┐
│           Windows Notepad++ (原代码)            │
│  Win32 UI ──────────────────────── 不复用      │
│  ScintillaWin ──── 替换为 ScintillaGTK  ←──┐  │
│  Lexilla ──────────────────────────────── 复用 │
└────────────────────────────────────────────────┘

┌────────────────────────────────────────────────┐
│           Linux 移植 (linux/ 目录)             │
│  GTK3 UI (NotepadPlusGtk) ─── 全新编写        │
│  ScintillaGTK (scintilla/gtk/) ─── 原仓库      │
│  Lexilla (lexilla/) ─────────── 原仓库        │
└────────────────────────────────────────────────┘
```

---

## 2. 技术选型

| 组件 | Windows | Linux 移植 | 说明 |
|------|---------|------------|------|
| UI 框架 | Win32 API (原生) | **GTK3** | `libgtk-3-dev` |
| 编辑器控件 | ScintillaWin | **ScintillaGTK** | `scintilla/gtk/` 已有完整实现 |
| 词法分析 | Lexilla (DLL) | **Lexilla (静态库)** | 完全跨平台，直接复用 |
| 正则表达式 | PCRE / Boost.Regex | **Boost.Regex** | `libboost-regex-dev` |
| 配置存储 | Windows 注册表 + XML | **INI 文件** (~/.config/notepad++/) | |
| 字符串 | TCHAR / wchar_t (UTF-16) | **std::string (UTF-8)** | |
| 文件监控 | ReadDirectoryChangesW | **GIO GFileMonitor** | GLib 内置 |
| 插件加载 | LoadLibrary | **dlopen / dlsym** | Linux 共享库 |
| 构建系统 | MSVC / GCC Makefile | **CMake 3.16+** | 自动检测依赖 |
| C++ 标准 | C++20 | **C++17** | GTK3 兼容 |
| 字符编码转换 | MultiByteToWideChar | **libiconv / ICU** | 待实现 |
| 拼写检查 | Aspell COM | **GtkSpell / Enchant** | 待实现 |
| 停靠面板 | 自定义 DockingManager | **GDL / GtkPaned** | 待实现 |

---

## 3. 目录结构

### 当前结构（已实现）

```
linux/
├── CMakeLists.txt              # CMake 构建脚本
├── build.sh                    # 构建辅助脚本（含依赖检查）
├── src/
│   ├── main.cpp                # GTK 入口，命令行参数，gtk_main()
│   ├── NotepadPlusGtk.h        # 主窗口类声明
│   ├── NotepadPlusGtk.cpp      # 主窗口实现（~1121 行）
│   ├── ScintillaView.h         # Scintilla 封装类声明
│   ├── ScintillaView.cpp       # Scintilla 封装（语法高亮、查找、主题）
│   ├── FindReplaceDlg.h        # 查找/替换对话框声明
│   ├── FindReplaceDlg.cpp      # 查找/替换实现（正则、前进/后退）
│   ├── Parameters.h            # 应用设置单例声明
│   └── Parameters.cpp          # 设置加载/保存（INI 格式）
└── resources/
    └── notepad++.desktop       # XDG 桌面集成文件
```

### 目标结构（完整移植）

```
linux/
├── CMakeLists.txt
├── build.sh
├── src/
│   ├── main.cpp
│   ├── NotepadPlusGtk.h/.cpp       # 主窗口（持续扩展）
│   ├── ScintillaView.h/.cpp         # 编辑器视图
│   ├── FindReplaceDlg.h/.cpp        # 查找/替换
│   ├── Parameters.h/.cpp            # 设置存储
│   ├── PluginManager.h/.cpp         # 插件加载器 [TODO]
│   ├── MacroManager.h/.cpp          # 宏录制/回放 [TODO]
│   ├── DockingPanel.h/.cpp          # GDL 停靠面板框架 [TODO]
│   ├── FunctionList.h/.cpp          # 函数列表面板 [TODO]
│   ├── DocumentMap.h/.cpp           # 文档地图面板 [TODO]
│   ├── FileMonitor.h/.cpp           # GFileMonitor 文件监控 [TODO]
│   ├── EncodingHelper.h/.cpp        # 编码检测与转换 [TODO]
│   ├── PreferencesDlg.h/.cpp        # 首选项对话框 [TODO]
│   └── GoToLineDlg.h/.cpp           # 跳转行对话框 [TODO]
└── resources/
    ├── notepad++.desktop
    ├── icons/                        # 应用图标（多尺寸 PNG） [TODO]
    └── themes/                       # 主题配置文件 [TODO]
```

---

## 4. 组件对照表 (Win32 → GTK3)

### UI 控件

| Win32 组件 | GTK3 对应 | 当前状态 | 复杂度 |
|-----------|----------|---------|--------|
| HWND + WndProc | GtkWidget + g_signal_connect | ✅ 基础完成 | — |
| 多标签页 (自定义 TabBar.cpp) | GtkNotebook | ✅ 完成 | 中 |
| 菜单栏 (WinMenu.cpp) | GtkMenuBar + GtkMenuItem | ✅ 完成 | 中 |
| 工具栏 (ToolBar.cpp) | GtkToolbar | ✅ 基础完成 | 中 |
| 状态栏 (StatusBar.cpp) | GtkStatusbar | ✅ 完成 | 低 |
| 停靠管理器 (DockingManager.cpp) | GDL (GNOME Docking Library) | ❌ 未开始 | **极高** |
| 弹出对话框 | GtkDialog / GtkWindow | ✅ FindReplace 完成 | — |
| 文件对话框 | GtkFileChooserDialog | ✅ 使用中 | 低 |

### 编辑器与语言

| Win32 组件 | GTK3 对应 | 当前状态 |
|-----------|----------|---------|
| ScintillaWin (scintilla/win32/) | ScintillaGTK (scintilla/gtk/) | ✅ 完成 |
| Lexilla.dll | Lexilla 静态库 (lexilla/) | ✅ 完成，127 个 Lexer |
| SCI_* 消息 (SendMessage) | scintilla_send_message() | ✅ 完成 |
| SCN_* 通知 (WM_NOTIFY) | SCINTILLA_NOTIFY 信号 | ✅ 完成 |

### 系统服务

| Win32 组件 | Linux 对应 | 当前状态 |
|-----------|----------|---------|
| 注册表 (RegSetValueEx) | INI 文件 (~/.config/notepad++/) | ✅ 完成 |
| ReadDirectoryChangesW | GIO GFileMonitor | ❌ 未实现 |
| LoadLibrary / GetProcAddress | dlopen / dlsym | ❌ 未实现 |
| MultiByteToWideChar | libiconv / iconv_open | ❌ 未实现 |
| CreateProcess / ShellExecute | g_spawn_command_line_async | ❌ 未实现 |
| HBITMAP / GDI+ 绘图 | Cairo / GdkPixbuf | ❌ 未实现 |
| COM Aspell | GtkSpell / Enchant | ❌ 未实现 |
| Clipboard (Win32 API) | GtkClipboard | ✅ 通过 GTK 自动处理 |

---

## 5. 架构分层

```
┌──────────────────────────────────────────────────────────────────┐
│                      用户界面层 (GTK3)                            │
│                                                                    │
│  ┌─────────────┐  ┌────────────┐  ┌──────────────────────────┐   │
│  │ GtkMenuBar  │  │ GtkToolbar │  │     GtkStatusbar         │   │
│  └─────────────┘  └────────────┘  └──────────────────────────┘   │
│                                                                    │
│  ┌──────────────────────────────────────────────────────────────┐ │
│  │                   GtkNotebook (标签页)                       │ │
│  │  ┌──────────────────────────────────────────────────────┐   │ │
│  │  │              ScintillaView (编辑器)                   │   │ │
│  │  │  scintilla_new() + scintilla_send_message()          │   │ │
│  │  └──────────────────────────────────────────────────────┘   │ │
│  └──────────────────────────────────────────────────────────────┘ │
│                                                                    │
│  ┌─────────────────┐  ┌──────────────────┐  ┌────────────────┐   │
│  │  FindReplaceDlg │  │  PreferencesDlg  │  │  GoToLineDlg   │   │
│  │  (GtkWindow)    │  │  (GtkDialog)     │  │  (GtkDialog)   │   │
│  └─────────────────┘  └──────────────────┘  └────────────────┘   │
│                                                                    │
│  ┌──────────────────────────────────────────────────────────────┐ │
│  │              GDL 停靠框架 (TODO)                             │ │
│  │  ┌────────────┐ ┌────────────┐ ┌──────────┐ ┌──────────┐   │ │
│  │  │ 文件夹树   │ │  函数列表  │ │ 文档地图 │ │ 搜索结果 │   │ │
│  │  │(GtkTreeView│ │(GtkTreeView│ │(GdkDraw) │ │(GtkList) │   │ │
│  │  └────────────┘ └────────────┘ └──────────┘ └──────────┘   │ │
│  └──────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────┐
│                      应用逻辑层                                    │
│                                                                    │
│  NotepadPlusGtk          — 主控制器，文档管理，菜单回调           │
│  Parameters              — 设置单例，INI 持久化                   │
│  MacroManager (TODO)     — 宏录制/回放                           │
│  PluginManager (TODO)    — dlopen 插件加载                       │
│  FileMonitor (TODO)      — GFileMonitor 文件监控                  │
│  EncodingHelper (TODO)   — libiconv 编码转换                     │
└──────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────┐
│                   编辑器引擎层（跨平台复用）                       │
│                                                                    │
│  ScintillaGTK             scintilla/gtk/ScintillaGTK.cxx         │
│  PlatGTK                  scintilla/gtk/PlatGTK.cxx              │
│  Lexilla (127 lexers)     lexilla/lexers/*.cxx                   │
│  Boost.Regex              查找/替换正则引擎                       │
└──────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────┐
│                   系统/平台层                                      │
│                                                                    │
│  GLib / GIO               文件 I/O、GFileMonitor、spawn           │
│  GDK                      窗口系统、输入事件、剪贴板               │
│  Cairo                    2D 绘图（GTK3 默认渲染后端）             │
│  Pango                    文本布局与字体渲染                       │
│  XDG Base Dirs            ~/.config/notepad++/ 配置目录           │
└──────────────────────────────────────────────────────────────────┘
```

---

## 6. 已实现功能

### `NotepadPlusGtk` 主窗口（~1121 行）

- [x] GTK3 主窗口创建、标题栏
- [x] GtkNotebook 多标签页，自定义标签标题（含关闭按钮 ×）
- [x] GtkMenuBar：File / Edit / Search / View / Language / Help 六个菜单
- [x] GtkToolbar：新建、打开、保存、撤销、重做、查找
- [x] GtkStatusbar：行/列/字符数/编码/语言显示
- [x] 文档管理：`std::vector<Document> _docs`，与 GtkNotebook 页索引对齐
- [x] 新建文档 (Ctrl+N)
- [x] 打开文件 (Ctrl+O)，支持命令行传入文件
- [x] 保存 (Ctrl+S)、另存为
- [x] 关闭标签，未保存时提示确认
- [x] 拖拽文件（drag-and-drop 打开）
- [x] 最近文件菜单（动态重建，最多 10 条）
- [x] 全局设置应用：`applySettingsToView()` / `applySettingsToAll()`
- [x] 深色/浅色主题切换
- [x] 语言菜单（30 种语言手动切换）
- [x] 键盘快捷键：Ctrl+N/O/S/W/Z/Y/X/C/V/A/F/H/G，F3，Ctrl+Tab
- [x] 关于对话框

### `ScintillaView` 编辑器封装（~800 行）

- [x] `scintilla_new()` 创建编辑器控件
- [x] 读写文本（`getText()` / `setText()`）
- [x] 从文件加载、保存到文件（UTF-8）
- [x] 语法高亮：30 种语言，`CreateLexer()` + `SCI_SETILEXER`
- [x] 颜色主题：default / dark（通过 SCI_STYLESETFORE/BACK 设置）
- [x] 字体设置（`SCI_STYLESETFONT` / `SCI_STYLESETSIZ`）
- [x] 缩放（`SCI_SETZOOM`）
- [x] 行号显示 (`SCI_SETMARGINTYPEN`)
- [x] 自动缩进（`SCN_CHARADDED` → 插入前一行缩进）
- [x] 查找下一个（`SCI_SEARCHINTARGET` 前向）
- [x] 查找/替换（正则支持 `SCFIND_REGEXP | SCFIND_POSIX`）
- [x] 全部替换
- [x] 修改状态检测（`SCN_SAVEPOINTREACHED` / `SCN_SAVEPOINTLEFT`）

### `FindReplaceDlg` 查找替换（~279 行）

- [x] 模态/非模态 GtkWindow（`gtk_window_set_transient_for`）
- [x] 查找 / 查找替换 两种模式
- [x] 区分大小写、全词匹配、正则表达式、环绕搜索
- [x] 方向：向前 / 向后（`SCI_SETTARGETSTART` > `SCI_SETTARGETEND`）
- [x] Escape 关闭，F3 / Shift+F3 快速查找
- [x] 状态消息（"未找到"用红色 Pango markup 显示）

### `Parameters` 设置（~180 行）

- [x] 单例模式（`getInstance()`）
- [x] XDG 感知路径（`$XDG_CONFIG_HOME/notepad++/settings.conf`）
- [x] INI 格式 key=value 解析，容错处理
- [x] 字段：wordWrap, showLineNumbers, fontName, fontSize, theme, showToolbar, showStatusBar, recentFiles[], window geometry, search options

---

## 7. 待实现功能

### 阶段 1 — 核心编辑增强（高优先级）

| 功能 | 实现方式 |
|------|---------|
| 跳转到行/列对话框 | `GoToLineDlg` + `SCI_GOTOLINE` / `SCI_GOTOPOS` |
| 全部保存 | 遍历 `_docs`，对 `modified==true` 的调用 `saveDocument()` |
| 全部关闭 | 遍历标签页，每个调用 `closeDocument()` |
| 从磁盘重新加载 | 对当前文档重新调用 `loadFileIntoDoc()` |
| 行操作（复制行/删除行/上移下移） | `SCI_LINEDUPLICATE`, `SCI_LINEDELETE`, `SCI_MOVESELECTEDLINESUP/DOWN` |
| 大小写转换 | `SCI_UPPERCASE`, `SCI_LOWERCASE`，自定义 title/camel/snake |
| 修剪空白 | 遍历行，用 `SCI_REPLACESEL` 修剪 |
| EOL 转换 | `SCI_CONVERTEOLS` + `SC_EOL_CRLF/LF/CR` |
| Tab↔空格转换 | `SCI_SETTABINDENTS` + `SCI_SETUSETABS` |
| 书签 | `SCI_MARKERADD` / `SCI_MARKERNEXT/PREV`，边距 Marker 类型 1 |
| 多光标 | `SCI_ADDSELECTION`，Alt+Click 事件 |
| 注释/取消注释 | 根据 `LexerType` 插入 `//` 或 `/* */` |
| 自动完成（当前文档词） | 扫描文档词汇 → `SCI_AUTOCSHOW` |
| 编码检测与转换 | libiconv，`BOM` 检测，`SCI_SETCODEPAGE(SC_CP_UTF8)` |

### 阶段 2 — 搜索增强（高优先级）

| 功能 | 实现方式 |
|------|---------|
| 高亮所有匹配 | `SCI_SETINDICATORCURRENT` + `SCI_INDICATORFILLRANGE` |
| 全选匹配 | `SCI_ADDSELECTION` 遍历所有匹配 |
| 统计匹配数 | 遍历 `SCI_SEARCHINTARGET` 计数 |
| 在文件中查找 | 递归目录 + `g_file_enumerate_children()` + 搜索结果面板 |
| 在所有打开文档中替换 | 遍历 `_docs`，对每个调用 `replaceAll()` |

### 阶段 3 — 视图面板（中优先级）

| 功能 | 实现方式 |
|------|---------|
| 文档地图 | `GtkDrawingArea`，缩放渲染 Scintilla 内容，可点击滚动 |
| 函数列表 | `GtkTreeView` + 按语言 regex 解析函数签名 |
| 文件夹工作区 | `GtkTreeView` + `GFile` 递归目录 |
| 文件监控 | `g_file_monitor_file()` → `changed` 信号 → 提示重载 |
| 分屏视图 | `GtkPaned` 分割，两个独立 `ScintillaView` |
| GDL 停靠 | 引入 `libgdl-3-dev`，替换 `GtkPaned` 固定布局 |

### 阶段 4 — 插件系统（中优先级）

| 功能 | 实现方式 |
|------|---------|
| 插件 API 头文件 | `npp_plugin_api.h`（C ABI，对应 NppFuncs） |
| 插件加载 | `dlopen()` → `getName()` / `getFuncsArray()` / `beNotified()` |
| 插件菜单集成 | 动态创建 GtkMenu 条目 |
| 插件通知 | 映射 `SCN_*` 通知 → 插件 `beNotified()` |

### 阶段 5 — 宏与工具（低优先级）

| 功能 | 实现方式 |
|------|---------|
| 宏录制 | 拦截 `key-press-event` / `SCI_ADDTEXT`，存入动作列表 |
| 宏回放 | 重放动作列表，调用对应 SCI_* 消息 |
| 宏保存/加载 | XML 或 INI 序列化 |
| 运行外部程序 | `g_spawn_command_line_async`，变量 `$(FULL_CURRENT_PATH)` |
| MD5/SHA-256 | OpenSSL / GLib `g_checksum_new()` |
| 拼写检查 | `GtkSpell` 或 `Enchant` |

### 阶段 6 — 完善与发布（低优先级）

| 功能 | 实现方式 |
|------|---------|
| 首选项对话框 | 多标签 GtkDialog（通用/样式/编辑/备份） |
| 快捷键管理器 | `GtkTreeView` 可编辑，保存到 `shortcuts.conf` |
| 主题管理 | 主题列表 + 实时预览，应用到 ScintillaView |
| 会话保存/恢复 | 保存打开文件列表 + 光标位置到 `session.conf` |
| 打包 | AppImage / Flatpak / `.deb` |
| Man 页 | `notepad++.1` |

---

## 8. 跨平台共享组件

以下组件来自原仓库，**无需修改**，直接在 Linux 上使用：

| 组件 | 路径 | 说明 |
|------|------|------|
| ScintillaGTK | `scintilla/gtk/ScintillaGTK.cxx` (106K) | GTK3 Scintilla 完整实现 |
| PlatGTK | `scintilla/gtk/PlatGTK.cxx` (76K) | GTK3 平台层（字体、绘图） |
| ScintillaGTK Accessible | `scintilla/gtk/ScintillaGTKAccessible.cxx` (46K) | 辅助功能支持 |
| Scintilla Core | `scintilla/src/*.cxx` | 核心编辑器逻辑（平台无关） |
| Lexilla | `lexilla/lexers/*.cxx` (127 个) | 所有语言词法分析器 |
| Lexilla Lib | `lexilla/lexlib/*.cxx` | 词法辅助库 |
| Lexilla API | `lexilla/src/Lexilla.cxx` | `CreateLexer(name)` 入口 |

**接口示例（ScintillaGTK）：**
```c
GtkWidget* scintilla_new(void);
sptr_t scintilla_send_message(ScintillaObject* sci, uint msg, uptr_t wp, sptr_t lp);
GtkWidget* SCINTILLA(ScintillaObject* sci);      // 类型转换宏
// 信号名称：
#define SCINTILLA_NOTIFY "sci-notify"
```

**接口示例（Lexilla 静态库）：**
```cpp
// lexilla/include/Lexilla.h
ILexer5* CreateLexer(const char* name);   // 按名称创建词法分析器
// 使用方式：
scintilla_send_message(SCINTILLA(w), SCI_SETILEXER, 0, (sptr_t)CreateLexer("cpp"));
```

---

## 9. 构建系统

### CMakeLists.txt 结构

```cmake
# 1. 查找 GTK3 依赖
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

# 2. 构建 Scintilla 静态库
#    Sources: scintilla/src/*.cxx + scintilla/gtk/*.cxx
#    Flags: -DGTK -DSCI_LEXER -fPIC -std=c++17

# 3. 构建 Lexilla 静态库
#    Sources: lexilla/lexers/*.cxx + lexilla/lexlib/*.cxx + lexilla/src/Lexilla.cxx
#    Flags: -DLEXILLA_STATIC -fPIC -std=c++17

# 4. 构建主程序
#    Sources: linux/src/*.cpp
#    Link: scintilla_gtk + lexilla + GTK3 libs + boost_regex
```

### 构建步骤

```bash
# 安装依赖
sudo apt install build-essential cmake pkg-config \
    libgtk-3-dev libboost-regex-dev

# 构建
cd linux/
bash build.sh          # 默认 Release 构建
bash build.sh --debug  # Debug 构建
bash build.sh --clean  # 清理重建
bash build.sh --install # 安装到 /usr/local/bin

# 直接 CMake
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

---

## 10. 设置与数据存储

### 路径规范（XDG Base Directory）

```
~/.config/notepad++/
├── settings.conf        # 主配置（INI 格式）
├── shortcuts.conf       # 键盘快捷键 [TODO]
├── session.conf         # 上次会话文件列表 [TODO]
├── stylers.conf         # 颜色主题 [TODO]
├── userDefineLang/      # 用户自定义语言 [TODO]
└── plugins/             # 插件数据目录 [TODO]

~/.local/share/notepad++/
└── plugins/             # 用户安装的插件 .so 文件 [TODO]
```

### settings.conf 格式（INI）

```ini
wordWrap=0
showLineNumbers=1
showToolbar=1
showStatusBar=1
fontName=Monospace
fontSize=11
theme=default
windowX=100
windowY=100
windowW=1024
windowH=768
windowMaximized=0
findMatchCase=0
findWholeWord=0
findRegex=0
findWrapAround=1
recent0=/home/user/example.cpp
recent1=/home/user/readme.md
```

---

## 11. 插件系统设计

### Linux 插件 ABI（设计目标）

```c
// npp_plugin_api.h（待创建）
typedef struct {
    GtkWidget* nppHandle;      // 主窗口 GtkWindow*（替代 Win32 HWND）
    GtkWidget* scintillaMain;  // 主编辑器 ScintillaObject*
    GtkWidget* scintillaSub;   // 副编辑器（分屏时）
} NppData;

// 插件必须导出的函数（extern "C"）：
void        setInfo(NppData nppData);
const char* getName();                      // 插件名称（UTF-8）
FuncItem*   getFuncsArray(int* nbF);        // 菜单项列表
void        beNotified(SCNotification*);    // Scintilla 通知
long        messageProc(unsigned msg, unsigned long wp, long lp);
int         isUnicode();                    // 返回 1
```

### 插件目录扫描

```cpp
// PluginManager.cpp
void PluginManager::loadPlugins() {
    std::string pluginDir = getXdgDataHome() + "/notepad++/plugins/";
    // 扫描 *.so 文件
    // 对每个 .so: dlopen() → dlsym("getName") → dlsym("getFuncsArray") → 添加菜单
}
```

---

## 12. 关键 API 映射

### Scintilla 常用消息

| 功能 | Win32 (SendMessage) | GTK3 (scintilla_send_message) |
|------|---------------------|-------------------------------|
| 设置文本 | `SCI_SETTEXT, 0, (LPARAM)text` | `SCI_SETTEXT, 0, (sptr_t)text` |
| 获取文本 | `SCI_GETTEXT, len, (LPARAM)buf` | `SCI_GETTEXT, len, (sptr_t)buf` |
| 设置词法器 | `SCI_SETILEXER, 0, (LPARAM)lexer` | `SCI_SETILEXER, 0, (sptr_t)lexer` |
| 查找 | `SCI_SEARCHINTARGET, len, (LPARAM)text` | 同左（sptr_t 替代 LPARAM） |
| 多选 | `SCI_ADDSELECTION, anchor, caret` | 同左 |
| 折叠 | `SCI_FOLDALL, SC_FOLDACTION_CONTRACT` | 同左 |
| 跳转行 | `SCI_GOTOLINE, line, 0` | 同左 |

### GTK3 事件信号

| 事件 | Win32 消息 | GTK3 信号 |
|------|-----------|----------|
| 键盘输入 | WM_KEYDOWN | `key-press-event` |
| 窗口关闭 | WM_CLOSE | `delete-event` |
| 标签切换 | (自定义) | `GtkNotebook::switch-page` |
| Scintilla 通知 | WM_NOTIFY (SCNotification) | `sci-notify` |
| 拖拽文件 | WM_DROPFILES | `drag-data-received` |
| 窗口尺寸变化 | WM_SIZE | `configure-event` |

---

*文档版本：1.0 | 生成时间：2026-02-28*
