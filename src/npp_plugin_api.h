#pragma once
// Notepad++ Linux port - Plugin API (Phase 4)
// Plugins are shared libraries (.so) that export the C functions listed at the
// bottom of this file.

#include <gtk/gtk.h>
#include <Scintilla.h>
#include <ScintillaWidget.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- Data types passed between host and plugins ----

// NppData: passed to setInfo(); gives the plugin access to the main window
// and the current editor widget.
typedef struct {
    GtkWidget* nppHandle;      // GtkWindow* of the Notepad++ main window
    GtkWidget* scintillaMain;  // Current primary Scintilla widget (ScintillaObject*)
    GtkWidget* scintillaSub;   // Split-view secondary editor (may be NULL)
} NppData;

// ShortcutKey: optional keyboard shortcut for a FuncItem
typedef struct {
    bool isCtrl;
    bool isAlt;
    bool isShift;
    char key;
} ShortcutKey;

// FuncItem: one menu entry exported by a plugin
#define FUNC_ITEM_NAME_LEN 64
typedef struct {
    char         itemName[FUNC_ITEM_NAME_LEN]; // Menu label
    void       (*pFunc)(void);                 // Called when the item is activated
    int          cmdID;        // Assigned by the host; not set by the plugin
    bool         checked;      // TRUE for a check-menu item that starts checked
    ShortcutKey* shortcut;     // NULL if no default shortcut
} FuncItem;

// ---- Host-provided helper (available from the host binary at runtime) ----
// Plugins call this to get the GtkWidget* of the currently active Scintilla
// editor.  The returned pointer is always valid while a document is open.
GtkWidget* npp_get_current_scintilla(void);

// ---- Mandatory exports that every plugin .so must implement ----
//
//   void         setInfo(NppData nppData);
//       Called once at load time.  Store nppData for later use.
//
//   const char*  getName();
//       Return a pointer to the plugin's display name (static string).
//
//   FuncItem*    getFuncsArray(int* nbF);
//       Return pointer to an array of FuncItem; set *nbF to its length.
//       The array must remain valid for the lifetime of the plugin.
//
//   void         beNotified(SCNotification* notif);
//       Called for every Scintilla notification on the active editor.
//
//   long         messageProc(unsigned int msg, unsigned long wParam, long lParam);
//       Custom IPC channel (return 0 if unhandled).

#ifdef __cplusplus
}  // extern "C"
#endif
