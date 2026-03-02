# Notepad++ Linux 移植计划

> 本文档定义将 Windows Notepad++ 移植到 Linux (GTK3) 的分阶段实施计划。
> 代码位于 `linux/` 目录，基于 GTK3 + ScintillaGTK + Lexilla 实现。
> 详细架构见 `linux_arch.md`，功能参考见 `windows_guide.md`。

---

## 当前进度概览

| 阶段 | 名称 | 状态 | 功能覆盖 |
|------|------|------|---------|
| Phase 0 | 基础框架 | ✅ 完成 | ~30% |
| Phase 1 | 核心编辑 | ✅ 完成 | +15% |
| Phase 2 | 搜索增强 | ✅ 完成 | +8% |
| Phase 3 | 视图面板 | ✅ 完成 | +15% |
| Phase 4 | 插件系统 | ✅ 完成 | +12% |
| Phase 5 | 宏与工具 | ✅ 完成 | +8% |
| Phase 6 | 完善与发布 | ✅ 完成 | +12% |

---

## Phase 0 — 基础框架（已完成）

> **目标**：建立可运行的基础 GTK3 应用，集成 Scintilla + Lexilla 编辑器引擎。

### 完成项

- [x] **CMake 构建系统** (`linux/CMakeLists.txt`)
  - Scintilla GTK3 静态库（`scintilla/gtk/` + `scintilla/src/`）
  - Lexilla 静态库（127 个 Lexer，`lexilla/`）
  - GTK3 pkg-config 自动检测
  - C++17，`-DGTK -DSCI_LEXER -fPIC`

- [x] **构建脚本** (`linux/build.sh`)
  - `--clean` / `--debug` / `--jobs N` / `--install` 选项
  - 依赖检查（cmake, pkg-config, g++, gtk+-3.0）

- [x] **应用入口** (`linux/src/main.cpp`)
  - `gtk_init()` + `gtk_main()`
  - 命令行参数：`-n LINE`、`-c COL`、`--help`、`--version`、文件路径

- [x] **主窗口** (`NotepadPlusGtk.h/.cpp`)
  - GtkWindow + GtkVBox 布局
  - GtkNotebook 多标签页（含自定义关闭按钮 ×）
  - GtkMenuBar：File / Edit / Search / View / Language / Help
  - GtkToolbar（新建/打开/保存/撤销/重做/查找）
  - GtkStatusbar（行/列/字符/编码/语言）
  - 文档管理：`std::vector<Document>` 与 Notebook 页同步
  - 最近文件菜单（最多 10 条，动态重建）
  - 拖拽文件打开（`gtk_drag_dest_set` + URI targets）
  - 键盘快捷键（Ctrl+N/O/S/W/Z/Y/X/C/V/A/F/H/G，F3，Ctrl+Tab）
  - 深色/浅色主题切换

- [x] **编辑器封装** (`ScintillaView.h/.cpp`)
  - `scintilla_new()` + `scintilla_send_message()` 封装
  - 30 种语言语法高亮（`CreateLexer()` + `SCI_SETILEXER`）
  - 字体/缩放/行号/自动缩进/空白显示
  - 查找/替换（正则支持，前向/后向，环绕）
  - 修改状态检测（savepoint 信号）

- [x] **查找替换对话框** (`FindReplaceDlg.h/.cpp`)
  - 模态 GtkWindow（非 GtkDialog），Find / Find+Replace 两种模式
  - 区分大小写、全词匹配、正则表达式、环绕搜索、方向选择
  - Escape 关闭，F3/Shift+F3 快捷键

- [x] **设置持久化** (`Parameters.h/.cpp`)
  - XDG 感知路径（`~/.config/notepad++/settings.conf`）
  - INI key=value 格式，启动加载/退出保存

- [x] **XDG 桌面集成** (`linux/resources/notepad++.desktop`)

---

## Phase 1 — 核心编辑增强

> **目标**：补全日常文本编辑所需的核心功能，达到可日常使用的水平。
> **预估工时**：10-14 天

### 文件操作

- [x] **全部保存**
  - 遍历 `_docs`，对 `modified == true` 的调用 `saveDocument(idx)`
  - 菜单项：File → Save All（已有占位，需实现）

- [x] **全部关闭**
  - 遍历所有标签，依次调用 `closeDocument(idx)`（含脏检查）
  - 菜单项：File → Close All

- [x] **从磁盘重新加载**
  - 弹出确认对话框，确认后重调 `loadFileIntoDoc(idx, path)`
  - 菜单项：File → Reload from Disk

- [x] **编码检测与显示**
  - 打开文件时检测 BOM（UTF-8 BOM / UTF-16 LE / UTF-16 BE）
  - 在状态栏显示检测到的编码（动态显示）

- [ ] **编码转换（基础）**
  - UTF-8 ↔ Latin-1（libiconv）—— 推迟到 Phase 6
  - 菜单项：Edit → Encoding → 转换为 UTF-8

### 编辑操作

- [x] **行操作**

  | 功能 | Scintilla 消息 |
  |------|---------------|
  | 复制当前行 | `SCI_LINEDUPLICATE` |
  | 删除当前行 | `SCI_LINEDELETE` |
  | 将选区行上移 | `SCI_MOVESELECTEDLINESUP` |
  | 将选区行下移 | `SCI_MOVESELECTEDLINESDOWN` |
  | 合并选区行 | 读取行内容，用空格合并，`SCI_REPLACESEL` |

- [x] **大小写转换**
  - 转为大写：`SCI_UPPERCASE`
  - 转为小写：`SCI_LOWERCASE`
  - 首字母大写：自定义实现（遍历单词边界）
  - 驼峰命名：自定义实现
  - 蛇形命名：自定义实现

- [x] **空白处理**
  - 修剪行尾/行首/全部空白
  - EOL 转换：`SCI_CONVERTEOLS(SC_EOL_CRLF/LF/CR)`
  - Tab↔空格（target API 搜索替换）

- [x] **注释/取消注释**
  - 根据 `LexerType` 判断注释符号（`//` 或 `#` 等）
  - 行注释：在缩进位置插入/删除注释符号
  - 菜单项：Edit → Comment/Uncomment Line（Ctrl+/）

- [x] **自动完成（文档内词汇）**
  - 扫描当前文档所有单词构建词表（≤512KB 文档）
  - 输入 2+ 字符时调用 `SCI_AUTOCSHOW`

### 导航

- [x] **跳转到行/列对话框**
  - 扩展现有对话框，支持行号 + 列号
  - 使用 `SCI_GOTOLINE` + `SCI_FINDCOLUMN`
  - 菜单项：Search → Go to Line (Ctrl+G)

### 书签

- [x] **书签系统**
  - Margin 1：`SC_MARGIN_SYMBOL`，蓝色圆点
  - Ctrl+F2 切换书签，F2/Shift+F2 前后跳转
  - 菜单项：Search → Bookmark → Toggle/Next/Prev/Clear All

### 多光标

- [ ] **列模式选择**（Alt+拖拽）—— 推迟到 Phase 2/3
  - GDK `alt` modifier + 拖拽 → `SCI_SETSELECTIONMODE(SC_SEL_RECTANGLE)`

- [ ] **多光标（基础）**—— 推迟到 Phase 2/3
  - Alt+Click → `SCI_ADDSELECTION`
  - Ctrl+Alt+Down/Up → 在下/上行同列添加光标

---

## Phase 2 — 搜索增强

> **目标**：补全查找/替换的高级功能，支持在文件中查找。
> **预估工时**：5-7 天

### 查找对话框增强

- [x] **高亮所有匹配**
  - Indicator 0，黄色 INDIC_ROUNDBOX，查找对话框 "Highlight All" 按钮

- [x] **全选匹配**
  - `SCI_SETSEL` + `SCI_ADDSELECTION` 遍历所有匹配，查找对话框 "Select All" 按钮

- [x] **统计匹配数**
  - 查找对话框 "Count" 按钮，状态栏显示 "N match(es) found."

- [x] **书签模式搜索**
  - 查找对话框 "Bookmark All" 按钮，对每行调用 `SCI_MARKERADD`

### 在文件中查找 (Ctrl+Shift+F)

- [x] **新建 FindInFilesDlg 对话框**（`linux/src/FindInFilesDlg.h/.cpp`）
  - 搜索文本、GtkFileChooserButton 目录、文件过滤器、递归/大小写/正则选项
  - "Find All" / "Stop" 切换按钮

- [x] **主线程 idle 搜索**（无额外线程依赖）
  - `g_idle_add` 每次处理 5 个文件，UI 保持响应
  - 二进制文件自动跳过（前 512 字节空字节检测）

- [x] **搜索结果面板**
  - 底部可隐藏 GtkTreeView，显示：文件名:行号  匹配内容
  - 双击跳转到对应文件和行（`openFileAtLine`）

### 在所有文档中替换

- [x] 查找替换对话框新增 "In All Docs" 按钮
- [x] 遍历 `_docs`，调用 `view->replaceAll()`，汇总替换总数

---

## Phase 3 — 视图面板

> **目标**：实现停靠面板体系，包括文档地图、函数列表、文件夹工作区。
> **预估工时**：15-21 天

### 停靠框架

- [x] **GtkPaned 固定布局**（备选方案，无需 GDL 依赖）
  - 嵌套 GtkPaned：`_outerHPaned`（工作区 | 编辑器+右面板）
  - `_editorHPaned`（编辑器 | 函数列表+文档地图）
  - `_editorVPaned`（分屏编辑器 | 搜索结果面板）
  - `_splitHPaned`（主编辑器 | 分屏编辑器）
  - View 菜单切换各面板可见性

### 文件夹工作区面板

- [x] **WorkspacePanel 类**（`linux/src/WorkspacePanel.h/.cpp`）
  - `std::filesystem::directory_iterator` 递归枚举目录
  - 目录节点懒加载（dummy child + row-expanded 信号）
  - 隐藏文件过滤（以 `.` 开头的文件/目录）
  - 双击文件 → `npp->openFile(path)`
  - View → Set Workspace Directory... 选择根目录

### 函数列表面板

- [x] **FunctionList 类**（`linux/src/FunctionList.h/.cpp`）
  - 针对 C++/Java/Python/JS/Go/Rust/Swift/Bash/Lua/Ruby/PHP/Perl/TCL 定义 regex 规则
  - 切换标签时自动重新解析当前文档
  - GtkTreeView 显示函数名（行号存储于隐藏列）
  - 点击 → `SCI_GOTOLINE(line)` 跳转
  - 顶部过滤输入框（GtkTreeModelFilter 实时过滤）

### 文档地图面板

- [x] **DocumentMap 类**（`linux/src/DocumentMap.h/.cpp`）
  - GtkDrawingArea + cairo 绘制行条形图
  - 用矩形框标注当前可见区域（firstVisibleLine / linesOnScreen）
  - 鼠标点击 → 主编辑区滚动到对应行
  - 切换标签/滚动时自动刷新

### 文件监控

- [x] **GFileMonitor 集成**（内嵌于 NotepadPlusGtk）
  - 对每个打开文件调用 `g_file_monitor_file()`
  - `G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT` → 弹出重载确认对话框
  - 防重入标志（`_fileChangePending`）避免多次弹窗
  - 关闭文档时自动取消监控

### 分屏视图

- [x] **双视图（GtkPaned）**
  - View → Split View 切换
  - `_splitEditor`（第二个 ScintillaView）通过 `SCI_SETDOCPOINTER` 共享文档
  - 切换标签时自动同步分屏编辑器的文档指针
  - 关闭时解除文档共享

---

## Phase 4 — 插件系统

> **目标**：实现可加载 `.so` 插件的插件系统，兼容基础 Notepad++ 插件 API。
> **预估工时**：10-14 天

### 插件 API

- [x] **创建 `linux/src/npp_plugin_api.h`**
  ```c
  typedef struct { GtkWidget *nppHandle, *scintillaMain, *scintillaSub; } NppData;
  // 插件导出：setInfo / getName / getFuncsArray / beNotified / messageProc
  ```

- [x] **FuncItem 结构**（`FUNC_ITEM_NAME_LEN = 64`，含 ShortcutKey* 字段）

### PluginManager 实现

- [x] **`linux/src/PluginManager.h/.cpp`**
  - 扫描 `<exe_dir>/plugins/*.so`（回退 `~/.config/notepad++/plugins/`）
  - 对每个 `.so`：`dlopen(RTLD_LAZY|RTLD_LOCAL)` → 解析5个符号 → `setInfo(nppData)`
  - `unloadAll()` → `dlclose()`

- [x] **通知分发**
  - `ScintillaView::onNotify()` 末尾调用 `_notifyCb(n)` → `_pluginManager->notifyAll(n)`

- [x] **插件菜单**
  - 菜单栏添加 "Plugins" 菜单，`populatePluginsMenu()` 调用 `PluginManager::buildPluginsMenu()`
  - 单插件：直接列出菜单项；多插件：子菜单分组

### 插件适配

- [x] 移植 **NppExport**（`linux/plugins/NppExport/NppExport.cpp`）
  - 导出到 HTML（样式感知，≤512KB 全着色）
  - 导出为纯文本（文件保存对话框）
- [x] 移植 **MimeTools**（`linux/plugins/MimeTools/MimeTools.cpp`）
  - Base64 编码/解码、URL 编码/解码、ROT-13、ASCII↔Hex

---

## Phase 5 — 宏与工具

> **目标**：实现宏录制/回放，以及 MD5、外部程序运行等工具。
> **预估工时**：5-7 天

### 宏系统

- [x] **MacroManager 类** (`linux/src/MacroManager.h/.cpp`)
  - `struct Action { unsigned msg; uptr_t wParam; sptr_t lParam; std::string text; }`
  - `startRecord()` / `stopRecord()` + `appendAction()` (called via SCN_MACRORECORD)
  - `play(view, times)` wraps replay in `SCI_BEGINUNDOACTION` / `SCI_ENDUNDOACTION`
  - Named macro persistence: `~/.config/notepad++/macros.conf` (hex-encoded text params)

- [x] **录制集成**
  - `ScintillaView::setMacroRecordCallback()` + `SCN_MACRORECORD` in `onNotify()`
  - `ScintillaView::replayMessage()` public method for playback
  - `cbMacroRecord` starts/stops `SCI_STARTRECORD`/`SCI_STOPRECORD` on all open views
  - Tab-switch and new-document hooks keep `SCI_STARTRECORD` in sync
  - Status bar shows "● REC" when recording is active

- [x] **菜单集成**：Macro → Record Macro (Ctrl+Shift+R) / Play Back (Ctrl+Shift+P) / Play N Times... / Save Macro... / Manage Macros...

### 工具

- [x] **运行外部程序**（Ctrl+F5）
  - GtkDialog with command entry + variable reference hint label
  - Substitutes: `$(FULL_CURRENT_PATH)`, `$(FILE_NAME)`, `$(CURRENT_DIRECTORY)`, `$(CURRENT_WORD)`
  - Executes via `g_spawn_command_line_async()`; shows error dialog on failure

- [x] **MD5 生成器**
  - `Tools → MD5 of Selection...`
  - Uses `g_checksum_new(G_CHECKSUM_MD5)` on selection (or whole doc if no selection)
  - Dialog shows result + "Copy to Clipboard" button

- [x] **SHA-256 生成器**
  - `Tools → SHA-256 of Selection...`
  - Same pattern as MD5 using `G_CHECKSUM_SHA256`

- [ ] **拼写检查（可选）** — 推迟 (GtkSpell 不兼容 ScintillaGTK)

---

## Phase 6 — 完善与发布

> **目标**：完善 UI 体验，实现首选项/快捷键管理器，打包发布。
> **预估工时**：5-7 天

### 首选项对话框

- [x] **PreferencesDlg**（多标签 GtkDialog）
  - 通用标签：Tab 宽度、使用空格、自动缩进、最大最近文件数、恢复会话
  - 视图标签：自动换行、行号、空白符、状态栏、工具栏、边缘线列
  - 样式标签：字体选择器（GtkFontButton）、主题 combo（default/dark/zenburn）
  - 搜索标签：区分大小写/全词/正则/环绕搜索默认值
  - 应用设置后立即调用 `applySettingsToAll()`

### 快捷键管理器

- [x] **Keyboard Shortcuts 对话框**（GtkTreeView 只读参考列表）
  - 列出所有命令名称和当前快捷键（35 条）
  - Edit → Keyboard Shortcuts... 菜单项

### 主题管理

- [x] 主题列表（`GtkComboBox`）—— 首选项对话框样式标签
- [x] 内置主题：default（已有）、dark（已有）、Zenburn 新增

### 会话管理

- [x] 退出时将 `_docs` 列表（文件路径 + 光标位置）保存到 `~/.config/notepad++/session.conf`
- [x] 启动时（若 `restoreSession` 设置开启）读取 `session.conf` 恢复上次会话

### 打包与发布

- [x] **AppImage** — `linux/packaging/build_appimage.sh`
- [x] **Debian 包** — `linux/packaging/build_deb.sh`
- [x] **Man 页面** — `linux/resources/notepad++.1`（含安装规则于 CMakeLists.txt）

### 边缘线（Edge Column）

- [x] `ScintillaView::setEdgeMode()` / `setEdgeColumn()` 新增
- [x] `AppSettings` 新增 `edgeEnabled` / `edgeColumn` 字段（默认启用，120 列）
- [x] `applySettingsToView()` 应用边缘线设置

---

## 关键文件速查

| 文件 | 路径 | 说明 |
|------|------|------|
| 主窗口 | `linux/src/NotepadPlusGtk.cpp` | 主控制器，持续扩展 |
| 编辑器封装 | `linux/src/ScintillaView.cpp` | Scintilla GTK3 API 封装 |
| 查找替换 | `linux/src/FindReplaceDlg.cpp` | 查找/替换对话框 |
| 设置 | `linux/src/Parameters.cpp` | INI 设置持久化 |
| CMake | `linux/CMakeLists.txt` | 构建脚本 |
| ScintillaGTK | `scintilla/gtk/ScintillaGTK.cxx` | 编辑器控件（勿修改） |
| Lexilla | `lexilla/src/Lexilla.cxx` | `CreateLexer()` 入口 |
| 功能参考 | `windows_guide.md` | Windows 版 300+ 功能说明 |
| 架构说明 | `linux_arch.md` | 移植架构详情 |

---

## 依赖安装（Ubuntu/Debian）

```bash
# 基础构建依赖
sudo apt install build-essential cmake pkg-config g++

# GTK3 和 GLib
sudo apt install libgtk-3-dev

# Boost.Regex（查找/替换正则）
sudo apt install libboost-regex-dev

# GDL 停靠框架（Phase 3）
sudo apt install libgdl-3-dev

# 编码转换（Phase 1）
sudo apt install libiconv-hook-dev
# 或使用系统内置 iconv（glibc）

# 拼写检查（Phase 5，可选）
sudo apt install libgtkspell3-3-dev libenchant-2-dev

# AppImage 打包工具（Phase 6）
wget https://github.com/AppImage/AppImageKit/releases/latest/download/appimagetool-x86_64.AppImage
chmod +x appimagetool-x86_64.AppImage
```

---

## 构建与测试

```bash
# 快速构建
cd /media/1T_S_E/images/notepad-plus-plus-notepad-plus-plus-53cb350/linux
bash build.sh

# 运行
./build/notepad++

# 带参数运行
./build/notepad++ -n 42 /path/to/file.cpp

# 调试构建
bash build.sh --debug
gdb ./build/notepad++
```

---

## 功能覆盖预测

| 完成阶段 | 预计功能覆盖率 |
|---------|-------------|
| Phase 0（当前） | ~30% |
| Phase 0 + 1 | ~45% |
| Phase 0 + 1 + 2 | ~53% |
| Phase 0 + 1 + 2 + 3 | ~68% |
| Phase 0 ~ 4 | ~80% |
| Phase 0 ~ 5 | ~88% |
| Phase 0 ~ 6 | ~95% |

> 注：5% 的功能（如 Windows 注册表文件关联、Windows 打印 API）不适用于 Linux，无法移植。

---

*文档版本：1.0 | 生成时间：2026-02-28*
