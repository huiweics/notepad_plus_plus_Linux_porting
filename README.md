# Notepad++ — Linux Port (GTK3)

A native Linux port of [Notepad++](https://notepad-plus-plus.org/) built with GTK3 and the embedded Scintilla/Lexilla libraries.

## Features

- Syntax highlighting for ~100 languages (via Lexilla)
- Multi-tab editing with tab × close buttons
- Find / Replace dialog (regex, match case, whole word)
- Find in Files with results panel
- Workspace / file-tree panel (lazy-loaded)
- Function list panel (regex-based symbol extraction)
- Document map (minimap via cairo)
- Macro record and replay
- Session save/restore (`~/.config/notepad++/session.conf`)
- Edge column guide
- Zenburn dark theme
- Plugin system (dlopen-based, C ABI)
  - **MimeTools** — Base64, URL, ROT-13, Hex encode/decode
  - **NppExport** — Export to HTML or plain text
- Preferences dialog (4 tabs: General, Editor, Appearance, Session)
- Keyboard shortcuts reference dialog
- Man page (`notepad++.1`)
- AppImage and Debian packaging scripts

## Requirements

| Package | Ubuntu/Debian |
|---------|---------------|
| CMake ≥ 3.16 | `cmake` |
| GCC / G++ (C++17) | `build-essential` |
| pkg-config | `pkg-config` |
| GTK3 dev headers | `libgtk-3-dev` |

Install all at once:

```bash
sudo apt install build-essential cmake pkg-config libgtk-3-dev
```

## Building

```bash
cd linux

# Release build (default)
./build.sh

# Debug build
./build.sh --debug

# Clean then build
./build.sh --clean

# Parallel jobs
./build.sh --jobs 8

# Build and install to /usr/local/bin
sudo ./build.sh --install
```

The binary is produced at `linux/build/notepad++`.

### Manual CMake build

```bash
mkdir -p linux/build && cd linux/build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel $(nproc)
```

## Running

```bash
# From the build directory
./build/notepad++

# Open specific files
./build/notepad++ file1.txt file2.cpp
```

Settings are stored in `~/.config/notepad++/`.

## Project Structure

```
linux/
├── build.sh              # Convenience build script
├── CMakeLists.txt        # CMake build definition
├── src/
│   ├── main.cpp          # Entry point; gtk_init, CLI file loading
│   ├── NotepadPlusGtk.cpp/.h     # Main GtkWindow, menus, toolbar, tabs
│   ├── ScintillaView.cpp/.h      # Scintilla GTK3 wrapper
│   ├── FindReplaceDlg.cpp/.h     # Find/Replace dialog
│   ├── FindInFilesDlg.cpp/.h     # Find in Files dialog + results panel
│   ├── WorkspacePanel.cpp/.h     # File-tree panel (GtkTreeView)
│   ├── FunctionList.cpp/.h       # Function list panel
│   ├── DocumentMap.cpp/.h        # Minimap (GtkDrawingArea + cairo)
│   ├── Parameters.cpp/.h         # Settings persistence
│   ├── MacroManager.cpp/.h       # Macro record/replay
│   ├── PluginManager.cpp/.h      # dlopen plugin loader
│   └── npp_plugin_api.h          # C plugin ABI (NppData, FuncItem)
├── plugins/
│   ├── MimeTools/MimeTools.cpp   # Encoding conversion plugin
│   └── NppExport/NppExport.cpp   # HTML/text export plugin
├── resources/
│   ├── notepad++.desktop         # Desktop entry file
│   └── notepad++.1               # Man page
└── packaging/
    ├── build_appimage.sh         # Build a portable AppImage
    └── build_deb.sh              # Build a Debian .deb package
```

## Writing a Plugin

Plugins are shared libraries (`.so`) that export five C symbols:

```c
#include "npp_plugin_api.h"

// Called once on load — fill in your menu items
extern "C" FuncItem* getFuncsArray(int* count);

// Called once on load — receive host window handles
extern "C" void setInfo(NppData data);

// Plugin display name
extern "C" const char* getName();

// Called on every Scintilla/Notepad++ notification
extern "C" void beNotified(SCNotification* notif);

// Called for inter-plugin messages
extern "C" LRESULT messageProc(UINT msg, WPARAM wp, LPARAM lp);
```

Place the compiled `.so` next to the `notepad++` binary inside a `plugins/` subdirectory. The plugin manager discovers and loads it at startup.

## Packaging

```bash
# AppImage (portable, no install needed)
cd linux/packaging
./build_appimage.sh

# Debian package
./build_deb.sh
```

## Relationship to the Windows Version

| Component | Windows | Linux |
|-----------|---------|-------|
| Editor widget | Scintilla Win32 | Scintilla GTK |
| Lexers | Lexilla (built into ScintillaLib) | Lexilla (static lib) |
| GUI framework | Pure Win32 | GTK3 |
| Plugin ABI | Win32 DLL | Linux `.so` (same C ABI) |
| Config location | `%APPDATA%\Notepad++\` | `~/.config/notepad++/` |
| C++ standard | C++20 | C++17 |
