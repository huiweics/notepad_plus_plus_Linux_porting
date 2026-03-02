#include "FunctionList.h"
#include <regex>
#include <sstream>
#include <algorithm>
#include <cctype>

FunctionList::FunctionList() {
    _box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(_box, 180, -1);

    // Header label
    GtkWidget* lbl = gtk_label_new("Function List");
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_widget_set_margin_start(lbl, 6);
    gtk_widget_set_margin_top(lbl, 4);
    gtk_widget_set_margin_bottom(lbl, 2);
    gtk_box_pack_start(GTK_BOX(_box), lbl, FALSE, FALSE, 0);

    // Filter entry
    _entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(_entry), "Filter...");
    gtk_widget_set_margin_start(_entry, 4);
    gtk_widget_set_margin_end(_entry, 4);
    gtk_widget_set_margin_bottom(_entry, 4);
    gtk_box_pack_start(GTK_BOX(_box), _entry, FALSE, FALSE, 0);
    g_signal_connect(_entry, "changed", G_CALLBACK(onFilterChanged), this);

    // List store + filter model
    _store = gtk_list_store_new(COL_COUNT, G_TYPE_STRING, G_TYPE_INT);
    _filterModel = gtk_tree_model_filter_new(GTK_TREE_MODEL(_store), nullptr);
    gtk_tree_model_filter_set_visible_func(
        GTK_TREE_MODEL_FILTER(_filterModel), filterFunc, this, nullptr);

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(_box), scroll, TRUE, TRUE, 0);

    _treeView = gtk_tree_view_new_with_model(_filterModel);
    g_object_unref(_filterModel);  // view holds the ref
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(_treeView), FALSE);

    GtkCellRenderer* r = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(_treeView),
        gtk_tree_view_column_new_with_attributes(
            "Function", r, "text", COL_FUNC_NAME, nullptr));

    g_signal_connect(_treeView, "row-activated",
                     G_CALLBACK(onRowActivated), this);
    gtk_container_add(GTK_CONTAINER(scroll), _treeView);

    gtk_widget_show_all(_box);
}

void FunctionList::clear() {
    gtk_list_store_clear(_store);
}

// Returns a regex pattern whose first capture group is the function/method name
std::string FunctionList::patternForLexer(LexerType lexer) {
    switch (lexer) {
        case LexerType::Cpp:
        case LexerType::CSharp:
            // Match: [modifiers] returnType functionName(
            return R"(^\s*(?:(?:public|private|protected|static|virtual|override|inline|explicit|constexpr|async|extern)\s+)*[\w\*&:<>]+\s+(\w+)\s*\()";
        case LexerType::Java:
            return R"(^\s*(?:(?:public|private|protected|static|final|synchronized|abstract|native|default)\s+)*[\w<>\[\]]+\s+(\w+)\s*\()";
        case LexerType::JavaScript:
            // function foo(...) or const foo = (...) =>
            return R"((?:^function\s+(\w+)|^\s*(?:const|let|var)\s+(\w+)\s*=\s*(?:async\s*)?\([^)]*\)\s*=>))";
        case LexerType::Python:
            return R"(^\s*(?:async\s+)?def\s+(\w+)\s*\()";
        case LexerType::Go:
            return R"(^\s*func\s+(?:\(.*?\)\s*)?(\w+)\s*\()";
        case LexerType::Rust:
            return R"(^\s*(?:pub\s+)?(?:async\s+)?fn\s+(\w+)\s*[<\(])";
        case LexerType::Swift:
            return R"(^\s*(?:(?:public|private|internal|open|fileprivate|static|class|override|mutating)\s+)*func\s+(\w+)\s*[<\(])";
        case LexerType::Bash:
            return R"(^(\w+)\s*\(\s*\))";
        case LexerType::Lua:
            return R"(^\s*(?:local\s+)?function\s+([\w.:]+)\s*\()";
        case LexerType::Ruby:
            return R"(^\s*def\s+(\w+))";
        case LexerType::PHP:
            return R"(^\s*(?:(?:public|private|protected|static|abstract|final)\s+)*function\s+(\w+)\s*\()";
        case LexerType::Perl:
            return R"(^\s*sub\s+(\w+))";
        case LexerType::TCL:
            return R"(^\s*proc\s+(\w+))";
        default:
            return "";
    }
}

void FunctionList::update(ScintillaView* view, LexerType lexer) {
    clear();
    if (!view) return;

    std::string pattern = patternForLexer(lexer);
    if (pattern.empty()) return;

    std::string text = view->getText();
    if (text.empty()) return;

    // Only scan files up to 2MB to keep UI responsive
    if (text.size() > 2 * 1024 * 1024) return;

    std::istringstream ss(text);
    std::string line;
    int lineNum = 0;
    try {
        std::regex re(pattern);
        while (std::getline(ss, line)) {
            std::smatch m;
            if (std::regex_search(line, m, re)) {
                // Use first non-empty capture group as the name
                std::string name;
                for (size_t i = 1; i < m.size(); ++i) {
                    if (m[i].matched && !m[i].str().empty()) {
                        name = m[i].str();
                        break;
                    }
                }
                if (!name.empty()) {
                    GtkTreeIter iter;
                    gtk_list_store_append(_store, &iter);
                    gtk_list_store_set(_store, &iter,
                                       COL_FUNC_NAME, name.c_str(),
                                       COL_LINE,      lineNum,
                                       -1);
                }
            }
            ++lineNum;
        }
    } catch (...) {}

    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(_filterModel));
}

void FunctionList::onFilterChanged(GtkEntry* entry, gpointer d) {
    auto* self = static_cast<FunctionList*>(d);
    self->_filterText = gtk_entry_get_text(entry);
    std::transform(self->_filterText.begin(), self->_filterText.end(),
                   self->_filterText.begin(), ::tolower);
    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(self->_filterModel));
}

gboolean FunctionList::filterFunc(GtkTreeModel* model, GtkTreeIter* iter, gpointer d) {
    auto* self = static_cast<FunctionList*>(d);
    if (self->_filterText.empty()) return TRUE;
    gchar* name;
    gtk_tree_model_get(model, iter, COL_FUNC_NAME, &name, -1);
    if (!name) return FALSE;
    std::string lower(name);
    g_free(name);
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower.find(self->_filterText) != std::string::npos ? TRUE : FALSE;
}

void FunctionList::onRowActivated(GtkTreeView* tv, GtkTreePath* path,
                                   GtkTreeViewColumn*, gpointer d) {
    auto* self = static_cast<FunctionList*>(d);
    GtkTreeIter filterIter, storeIter;
    if (!gtk_tree_model_get_iter(gtk_tree_view_get_model(tv), &filterIter, path)) return;

    gtk_tree_model_filter_convert_iter_to_child_iter(
        GTK_TREE_MODEL_FILTER(self->_filterModel), &storeIter, &filterIter);

    gint line;
    gtk_tree_model_get(GTK_TREE_MODEL(self->_store), &storeIter, COL_LINE, &line, -1);
    if (self->_gotoLineCb) self->_gotoLineCb(line);
}
