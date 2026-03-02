#include "PluginManager.h"
#include <dlfcn.h>
#include <filesystem>
#include <cstdio>

namespace fs = std::filesystem;

PluginManager::~PluginManager() {
    unloadAll();
}

bool PluginManager::loadOne(const std::string& soPath, NppData nppData) {
    void* handle = dlopen(soPath.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "[PluginManager] dlopen(%s): %s\n",
                soPath.c_str(), dlerror());
        return false;
    }

    auto* pe = new PluginEntry();
    pe->handle = handle;

    // Load mandatory symbols
    pe->pfGetName    = (const char*(*)())       dlsym(handle, "getName");
    pe->pfGetFuncs   = (FuncItem*(*)(int*))     dlsym(handle, "getFuncsArray");
    pe->pfSetInfo    = (void(*)(NppData))        dlsym(handle, "setInfo");
    pe->pfBeNotified = (void(*)(SCNotification*))dlsym(handle, "beNotified");
    pe->pfMessageProc= (long(*)(unsigned int, unsigned long, long))
                        dlsym(handle, "messageProc");

    if (!pe->pfGetName || !pe->pfGetFuncs || !pe->pfSetInfo) {
        fprintf(stderr, "[PluginManager] %s: missing required exports\n",
                soPath.c_str());
        dlclose(handle);
        delete pe;
        return false;
    }

    pe->name = pe->pfGetName();

    // Copy the FuncItem array
    int nbF = 0;
    FuncItem* funcs = pe->pfGetFuncs(&nbF);
    if (funcs && nbF > 0)
        pe->funcs.assign(funcs, funcs + nbF);

    // Initialise the plugin
    pe->pfSetInfo(nppData);

    _plugins.push_back(pe);
    fprintf(stderr, "[PluginManager] Loaded: %s  (%d items)\n",
            pe->name.c_str(), nbF);
    return true;
}

void PluginManager::loadAll(const std::string& pluginsDir, NppData nppData) {
    if (!fs::exists(pluginsDir)) {
        std::error_code ec;
        fs::create_directories(pluginsDir, ec);
        return;   // no plugins yet
    }
    for (auto& e : fs::directory_iterator(pluginsDir)) {
        if (e.path().extension() == ".so")
            loadOne(e.path().string(), nppData);
    }
}

void PluginManager::unloadAll() {
    for (auto* pe : _plugins) {
        if (pe->handle) dlclose(pe->handle);
        delete pe;
    }
    _plugins.clear();
}

void PluginManager::notifyAll(SCNotification* notif) {
    for (auto* pe : _plugins)
        if (pe->pfBeNotified) pe->pfBeNotified(notif);
}

GtkWidget* PluginManager::buildPluginsMenu() {
    GtkWidget* menu = gtk_menu_new();

    if (_plugins.empty()) {
        GtkWidget* none = gtk_menu_item_new_with_label("(no plugins loaded)");
        gtk_widget_set_sensitive(none, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), none);
        gtk_widget_show(none);
        return menu;
    }

    for (auto* pe : _plugins) {
        if (pe->funcs.empty()) continue;

        if (_plugins.size() > 1) {
            // Multiple plugins: put each under its own submenu
            GtkWidget* subItem = gtk_menu_item_new_with_label(pe->name.c_str());
            GtkWidget* subMenu = gtk_menu_new();
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(subItem), subMenu);

            for (const auto& fi : pe->funcs) {
                GtkWidget* item = gtk_menu_item_new_with_label(fi.itemName);
                void (*fn)(void) = fi.pFunc;
                g_signal_connect(item, "activate",
                    G_CALLBACK(+[](GtkMenuItem*, gpointer p) {
                        auto* f = reinterpret_cast<void(*)(void)>(p);
                        if (f) f();
                    }), reinterpret_cast<gpointer>(fn));
                gtk_menu_shell_append(GTK_MENU_SHELL(subMenu), item);
            }
            gtk_widget_show_all(subItem);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), subItem);
        } else {
            // Single plugin: add items directly to the Plugins menu
            for (const auto& fi : pe->funcs) {
                GtkWidget* item = gtk_menu_item_new_with_label(fi.itemName);
                void (*fn)(void) = fi.pFunc;
                g_signal_connect(item, "activate",
                    G_CALLBACK(+[](GtkMenuItem*, gpointer p) {
                        auto* f = reinterpret_cast<void(*)(void)>(p);
                        if (f) f();
                    }), reinterpret_cast<gpointer>(fn));
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
            }
        }
    }
    gtk_widget_show_all(menu);
    return menu;
}
