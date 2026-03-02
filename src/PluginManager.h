#pragma once
// Notepad++ Linux port - Plugin loader and manager (Phase 4)

#include <gtk/gtk.h>
#include <string>
#include <vector>
#include "npp_plugin_api.h"

// One loaded plugin
struct PluginEntry {
    void*               handle      = nullptr; // dlopen handle
    std::string         name;                  // from getName()
    std::vector<FuncItem> funcs;               // copied from getFuncsArray()

    // Symbol table
    void        (*pfSetInfo)    (NppData)                            = nullptr;
    const char* (*pfGetName)    ()                                   = nullptr;
    FuncItem*   (*pfGetFuncs)   (int*)                               = nullptr;
    void        (*pfBeNotified) (SCNotification*)                    = nullptr;
    long        (*pfMessageProc)(unsigned int, unsigned long, long)  = nullptr;
};

class PluginManager {
public:
    PluginManager() = default;
    ~PluginManager();

    // Scan pluginsDir for *.so files and load each one.
    void loadAll(const std::string& pluginsDir, NppData nppData);

    // Unload all plugins (dlclose).
    void unloadAll();

    // Forward an SCNotification to all loaded plugins.
    void notifyAll(SCNotification* notif);

    // Build and return a GtkMenu populated with each plugin's menu items.
    // The caller is responsible for attaching it to a GtkMenuItem.
    GtkWidget* buildPluginsMenu();

    int pluginCount() const { return (int)_plugins.size(); }

private:
    std::vector<PluginEntry*> _plugins;

    bool loadOne(const std::string& soPath, NppData nppData);
};
