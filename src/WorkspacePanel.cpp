#include "WorkspacePanel.h"
#include <filesystem>
#include <algorithm>
#include <vector>

namespace fs = std::filesystem;

WorkspacePanel::WorkspacePanel() {
    _box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(_box, 200, -1);

    // Header with label and refresh button
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget* lbl = gtk_label_new("Workspace");
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_widget_set_margin_start(lbl, 6);
    gtk_widget_set_margin_top(lbl, 4);
    gtk_widget_set_margin_bottom(lbl, 4);
    gtk_box_pack_start(GTK_BOX(header), lbl, TRUE, TRUE, 0);

    GtkWidget* refreshBtn = gtk_button_new_from_icon_name(
        "view-refresh", GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_button_set_relief(GTK_BUTTON(refreshBtn), GTK_RELIEF_NONE);
    gtk_box_pack_end(GTK_BOX(header), refreshBtn, FALSE, FALSE, 2);
    g_signal_connect(refreshBtn, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer d) {
                         ((WorkspacePanel*)d)->refresh();
                     }), this);

    gtk_box_pack_start(GTK_BOX(_box), header, FALSE, FALSE, 0);

    // Separator
    gtk_box_pack_start(GTK_BOX(_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                       FALSE, FALSE, 0);

    // Scrolled window containing the tree view
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(_box), scroll, TRUE, TRUE, 0);

    _store = gtk_tree_store_new(COL_COUNT,
                                G_TYPE_STRING,   // name
                                G_TYPE_STRING,   // full path
                                G_TYPE_BOOLEAN); // is_dir

    _treeView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(_store));
    g_object_unref(_store);  // view holds the ref
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(_treeView), FALSE);

    // Single column: icon + name
    GtkTreeViewColumn* col = gtk_tree_view_column_new();
    GtkCellRenderer* iconR = gtk_cell_renderer_pixbuf_new();
    GtkCellRenderer* textR = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col, iconR, FALSE);
    gtk_tree_view_column_pack_start(col, textR, TRUE);

    // Set icon based on is_dir
    gtk_tree_view_column_set_cell_data_func(col, iconR,
        [](GtkTreeViewColumn*, GtkCellRenderer* r, GtkTreeModel* m,
           GtkTreeIter* it, gpointer) {
            gboolean isDir;
            gtk_tree_model_get(m, it, 2, &isDir, -1);
            g_object_set(r, "icon-name",
                         isDir ? "folder" : "text-x-generic", nullptr);
        }, nullptr, nullptr);

    // Set text from name column
    gtk_tree_view_column_set_cell_data_func(col, textR,
        [](GtkTreeViewColumn*, GtkCellRenderer* r, GtkTreeModel* m,
           GtkTreeIter* it, gpointer) {
            gchar* name;
            gtk_tree_model_get(m, it, 0, &name, -1);
            g_object_set(r, "text", name ? name : "", nullptr);
            g_free(name);
        }, nullptr, nullptr);

    gtk_tree_view_append_column(GTK_TREE_VIEW(_treeView), col);

    g_signal_connect(_treeView, "row-activated",
                     G_CALLBACK(onRowActivated), this);
    g_signal_connect(_treeView, "row-expanded",
                     G_CALLBACK(onRowExpanded),  this);

    gtk_container_add(GTK_CONTAINER(scroll), _treeView);
    gtk_widget_show_all(_box);
}

bool WorkspacePanel::isHidden(const std::string& name) {
    return !name.empty() && name[0] == '.';
}

void WorkspacePanel::setRootDirectory(const std::string& path) {
    _rootDir = path;
    refresh();
}

void WorkspacePanel::refresh() {
    if (_rootDir.empty()) return;
    gtk_tree_store_clear(_store);

    // Add root node
    GtkTreeIter root;
    gtk_tree_store_append(_store, &root, nullptr);
    gtk_tree_store_set(_store, &root,
                       COL_NAME,   fs::path(_rootDir).filename().c_str(),
                       COL_PATH,   _rootDir.c_str(),
                       COL_IS_DIR, TRUE,
                       -1);

    // Add dummy child so root gets an expand arrow
    GtkTreeIter dummy;
    gtk_tree_store_append(_store, &dummy, &root);
    gtk_tree_store_set(_store, &dummy,
                       COL_NAME, "...", COL_PATH, "", COL_IS_DIR, FALSE, -1);

    // Expand root
    GtkTreePath* rpath = gtk_tree_model_get_path(GTK_TREE_MODEL(_store), &root);
    gtk_tree_view_expand_row(GTK_TREE_VIEW(_treeView), rpath, FALSE);
    gtk_tree_path_free(rpath);
}

void WorkspacePanel::appendDir(GtkTreeIter* parent, const std::string& dirPath) {
    // Remove all existing children (the dummy or previous real children)
    GtkTreeIter child;
    while (gtk_tree_model_iter_children(GTK_TREE_MODEL(_store), &child, parent))
        gtk_tree_store_remove(_store, &child);

    // Enumerate directory entries
    std::vector<fs::directory_entry> entries;
    try {
        for (auto& e : fs::directory_iterator(dirPath))
            entries.push_back(e);
    } catch (...) { return; }

    // Sort: directories first, then alphabetically
    std::sort(entries.begin(), entries.end(),
              [](const fs::directory_entry& a, const fs::directory_entry& b) {
                  bool aD = a.is_directory(), bD = b.is_directory();
                  if (aD != bD) return aD > bD;
                  return a.path().filename().string() < b.path().filename().string();
              });

    for (auto& e : entries) {
        std::string name = e.path().filename().string();
        if (isHidden(name)) continue;
        bool isDir = e.is_directory();

        GtkTreeIter iter;
        gtk_tree_store_append(_store, &iter, parent);
        gtk_tree_store_set(_store, &iter,
                           COL_NAME,   name.c_str(),
                           COL_PATH,   e.path().string().c_str(),
                           COL_IS_DIR, isDir ? TRUE : FALSE,
                           -1);
        if (isDir) {
            // Add dummy child so folder gets an expand arrow
            GtkTreeIter d;
            gtk_tree_store_append(_store, &d, &iter);
            gtk_tree_store_set(_store, &d,
                               COL_NAME, "...", COL_PATH, "", COL_IS_DIR, FALSE, -1);
        }
    }
}

void WorkspacePanel::onRowActivated(GtkTreeView* tv, GtkTreePath* treePath,
                                     GtkTreeViewColumn*, gpointer d) {
    auto* self = static_cast<WorkspacePanel*>(d);
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(self->_store), &iter, treePath)) return;

    gchar* path;
    gboolean isDir;
    gtk_tree_model_get(GTK_TREE_MODEL(self->_store), &iter,
                       COL_PATH, &path, COL_IS_DIR, &isDir, -1);
    if (path && !isDir && self->_openFileCb)
        self->_openFileCb(std::string(path));
    g_free(path);
}

void WorkspacePanel::onRowExpanded(GtkTreeView*, GtkTreeIter* iter,
                                    GtkTreePath*, gpointer d) {
    auto* self = static_cast<WorkspacePanel*>(d);
    gchar* path;
    gboolean isDir;
    gtk_tree_model_get(GTK_TREE_MODEL(self->_store), iter,
                       COL_PATH, &path, COL_IS_DIR, &isDir, -1);
    if (!path || !isDir) { g_free(path); return; }

    // Check if the first child is the dummy placeholder
    GtkTreeIter firstChild;
    if (gtk_tree_model_iter_children(GTK_TREE_MODEL(self->_store), &firstChild, iter)) {
        gchar* childPath;
        gtk_tree_model_get(GTK_TREE_MODEL(self->_store), &firstChild,
                           COL_PATH, &childPath, -1);
        bool isDummy = (childPath && std::string(childPath).empty());
        g_free(childPath);
        if (isDummy)
            self->appendDir(iter, std::string(path));
    }
    g_free(path);
}
