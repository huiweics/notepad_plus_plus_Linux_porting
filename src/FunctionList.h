#pragma once
// Notepad++ Linux port - Function list panel (Phase 3)

#include <gtk/gtk.h>
#include <string>
#include <functional>
#include "ScintillaView.h"

class FunctionList {
public:
    using GotoLineCb = std::function<void(int)>;

    FunctionList();
    ~FunctionList() = default;

    GtkWidget* getWidget() const { return _box; }
    void setGotoLineCallback(GotoLineCb cb) { _gotoLineCb = cb; }
    void update(ScintillaView* view, LexerType lexer);
    void clear();

private:
    GtkWidget*    _box         = nullptr;
    GtkWidget*    _entry       = nullptr;
    GtkWidget*    _treeView    = nullptr;
    GtkListStore* _store       = nullptr;
    GtkTreeModel* _filterModel = nullptr;
    GotoLineCb    _gotoLineCb;
    std::string   _filterText;

    enum { COL_FUNC_NAME = 0, COL_LINE, COL_COUNT };

    static std::string patternForLexer(LexerType lexer);
    static void        onFilterChanged(GtkEntry*, gpointer);
    static void        onRowActivated(GtkTreeView*, GtkTreePath*,
                                      GtkTreeViewColumn*, gpointer);
    static gboolean    filterFunc(GtkTreeModel*, GtkTreeIter*, gpointer);
};
