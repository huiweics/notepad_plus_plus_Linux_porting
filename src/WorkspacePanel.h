#pragma once
// Notepad++ Linux port - Workspace file tree panel (Phase 3)

#include <gtk/gtk.h>
#include <string>
#include <functional>

class WorkspacePanel {
public:
    using OpenFileCb = std::function<void(const std::string&)>;

    WorkspacePanel();
    ~WorkspacePanel() = default;

    GtkWidget* getWidget() const { return _box; }
    void setOpenFileCallback(OpenFileCb cb) { _openFileCb = cb; }
    void setRootDirectory(const std::string& path);
    void refresh();

private:
    GtkWidget*    _box      = nullptr;
    GtkWidget*    _treeView = nullptr;
    GtkTreeStore* _store    = nullptr;
    std::string   _rootDir;
    OpenFileCb    _openFileCb;

    enum { COL_NAME = 0, COL_PATH, COL_IS_DIR, COL_COUNT };

    void appendDir(GtkTreeIter* parent, const std::string& dirPath);
    static bool isHidden(const std::string& name);
    static void onRowActivated(GtkTreeView*, GtkTreePath*,
                               GtkTreeViewColumn*, gpointer);
    static void onRowExpanded(GtkTreeView*, GtkTreeIter*,
                              GtkTreePath*, gpointer);
};
