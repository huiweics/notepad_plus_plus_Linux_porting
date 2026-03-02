cmake_minimum_required(VERSION 3.16)
project(notepad-plus-plus VERSION 8.7.5 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

set(REPO_ROOT      ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(SCINTILLA_ROOT ${REPO_ROOT}/scintilla)
set(LEXILLA_ROOT   ${REPO_ROOT}/lexilla)

# ============================================================
# Scintilla static library (core + GTK platform layer)
# ============================================================
file(GLOB SCINTILLA_CORE_SRC CONFIGURE_DEPENDS ${SCINTILLA_ROOT}/src/*.cxx)

set(SCINTILLA_GTK_SRC
    ${SCINTILLA_ROOT}/gtk/ScintillaGTK.cxx
    ${SCINTILLA_ROOT}/gtk/ScintillaGTKAccessible.cxx
    ${SCINTILLA_ROOT}/gtk/PlatGTK.cxx
    ${SCINTILLA_ROOT}/gtk/scintilla-marshal.c
)

add_library(scintilla_gtk STATIC
    ${SCINTILLA_CORE_SRC}
    ${SCINTILLA_GTK_SRC}
)
target_include_directories(scintilla_gtk PUBLIC
    ${SCINTILLA_ROOT}/include
    ${SCINTILLA_ROOT}/src
    ${SCINTILLA_ROOT}/gtk
    ${GTK3_INCLUDE_DIRS}
)
target_compile_definitions(scintilla_gtk PUBLIC GTK)
target_compile_options(scintilla_gtk PRIVATE
    ${GTK3_CFLAGS_OTHER}
    -fPIC
    $<$<COMPILE_LANGUAGE:CXX>:-std=c++17 -Wno-unused-parameter>
    $<$<COMPILE_LANGUAGE:C>:-w>
)

# ============================================================
# Lexilla static library (all language lexers)
# ============================================================
file(GLOB LEXILLA_LEXERS_SRC CONFIGURE_DEPENDS ${LEXILLA_ROOT}/lexers/*.cxx)
file(GLOB LEXILLA_LEXLIB_SRC CONFIGURE_DEPENDS ${LEXILLA_ROOT}/lexlib/*.cxx)

add_library(lexilla_static STATIC
    ${LEXILLA_ROOT}/src/Lexilla.cxx
    ${LEXILLA_LEXERS_SRC}
    ${LEXILLA_LEXLIB_SRC}
)
target_include_directories(lexilla_static PUBLIC
    ${LEXILLA_ROOT}/include
    ${LEXILLA_ROOT}/lexlib
    ${LEXILLA_ROOT}/src
    ${SCINTILLA_ROOT}/include
)
target_compile_options(lexilla_static PRIVATE
    -fPIC
    -std=c++17
    -Wno-unused-parameter
)

# ============================================================
# Notepad++ Linux application
# ============================================================
set(NPP_LINUX_SOURCES
    src/main.cpp
    src/NotepadPlusGtk.cpp
    src/ScintillaView.cpp
    src/FindReplaceDlg.cpp
    src/FindInFilesDlg.cpp
    src/WorkspacePanel.cpp
    src/FunctionList.cpp
    src/DocumentMap.cpp
    src/Parameters.cpp
    src/PluginManager.cpp
    src/MacroManager.cpp
)

add_executable(notepad++ ${NPP_LINUX_SOURCES})
target_include_directories(notepad++ PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${SCINTILLA_ROOT}/include
    ${LEXILLA_ROOT}/include
    ${GTK3_INCLUDE_DIRS}
)
target_compile_definitions(notepad++ PRIVATE GTK)
target_compile_options(notepad++ PRIVATE
    ${GTK3_CFLAGS_OTHER}
    -Wall
    -Wno-unused-parameter
)
target_link_libraries(notepad++ PRIVATE
    scintilla_gtk
    lexilla_static
    ${GTK3_LIBRARIES}
)
# -rdynamic: export host symbols so dlopen'd plugins can call
# npp_get_current_scintilla() and other host functions.
target_link_options(notepad++ PRIVATE -rdynamic)

# ============================================================
# Bundled plugins (Phase 4)  —  built as shared libraries
# ============================================================
set(PLUGIN_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/plugins)

# --- MimeTools ---
add_library(MimeTools MODULE plugins/MimeTools/MimeTools.cpp)
set_target_properties(MimeTools PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${PLUGIN_OUTPUT_DIR}
)
target_include_directories(MimeTools PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${SCINTILLA_ROOT}/include
    ${GTK3_INCLUDE_DIRS}
)
target_compile_definitions(MimeTools PRIVATE GTK)
target_compile_options(MimeTools PRIVATE ${GTK3_CFLAGS_OTHER} -Wno-unused-parameter)
target_link_libraries(MimeTools PRIVATE ${GTK3_LIBRARIES})

# --- NppExport ---
add_library(NppExport MODULE plugins/NppExport/NppExport.cpp)
set_target_properties(NppExport PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${PLUGIN_OUTPUT_DIR}
)
target_include_directories(NppExport PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${SCINTILLA_ROOT}/include
    ${GTK3_INCLUDE_DIRS}
)
target_compile_definitions(NppExport PRIVATE GTK)
target_compile_options(NppExport PRIVATE ${GTK3_CFLAGS_OTHER} -Wno-unused-parameter)
target_link_libraries(NppExport PRIVATE ${GTK3_LIBRARIES})

# ============================================================
# Install rules
# ============================================================
install(TARGETS notepad++ DESTINATION bin)
install(FILES resources/notepad++.desktop DESTINATION share/applications)
install(FILES resources/notepad++.1       DESTINATION share/man/man1)
