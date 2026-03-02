#include "NotepadPlusGtk.h"
#include "Parameters.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <unistd.h>

namespace fs = std::filesystem;

// Escape a plain string for use inside Pango markup
static std::string pangoEscape(const std::string& s) {
    std::string r;
    r.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
        case '&':  r += "&amp;";  break;
        case '<':  r += "&lt;";   break;
        case '>':  r += "&gt;";   break;
        case '"':  r += "&quot;"; break;
        default:   r += (char)c;  break;
        }
    }
    return r;
}

// ---------------------------------------------------------------------------
// Phase 4: plugin host globals
// ---------------------------------------------------------------------------

// Tracks the currently active Scintilla GtkWidget*; updated on every tab
// switch and new-document creation so that npp_get_current_scintilla() is
// always up-to-date without needing access to private NotepadPlusGtk members.
static GtkWidget* g_currentSciWidget = nullptr;

extern "C" GtkWidget* npp_get_current_scintilla() {
    return g_currentSciWidget;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

NotepadPlusGtk::NotepadPlusGtk() = default;

NotepadPlusGtk::~NotepadPlusGtk() {
    // Phase 6: save current session before settings
    saveSession();
    Parameters::getInstance().save();
    // Stop all file monitors
    for (auto& doc : _docs) {
        if (doc.fileMonitor) {
            g_file_monitor_cancel(doc.fileMonitor);
            g_object_unref(doc.fileMonitor);
            doc.fileMonitor = nullptr;
        }
    }
    // Clean up split editor
    if (_splitEditor) {
        if (_splitHPaned)
            gtk_container_remove(GTK_CONTAINER(_splitHPaned),
                                 _splitEditor->getWidget());
        delete _splitEditor;
    }
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

void NotepadPlusGtk::init() {
    auto& cfg = Parameters::getInstance().getSettings();

    _window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(_window), "Notepad++");
    gtk_window_set_default_size(GTK_WINDOW(_window),
                                cfg.windowWidth, cfg.windowHeight);
    gtk_window_move(GTK_WINDOW(_window), cfg.windowX, cfg.windowY);
    if (cfg.windowMaximized)
        gtk_window_maximize(GTK_WINDOW(_window));

    // Drag-and-drop support (files dropped onto the window)
    gtk_drag_dest_set(_window, GTK_DEST_DEFAULT_ALL, nullptr, 0, GDK_ACTION_COPY);
    gtk_drag_dest_add_uri_targets(_window);
    g_signal_connect(_window, "drag-data-received", G_CALLBACK(cbDragData), this);
    g_signal_connect(_window, "delete-event",        G_CALLBACK(cbWindowDelete), this);
    g_signal_connect(_window, "key-press-event",     G_CALLBACK(cbKeyPress), this);

    // Save geometry on resize
    g_signal_connect(_window, "size-allocate",
                     G_CALLBACK(+[](GtkWidget* w, GtkAllocation*, gpointer) {
                         auto& s = Parameters::getInstance().getSettings();
                         if (!s.windowMaximized)
                             gtk_window_get_size(GTK_WINDOW(w),
                                                 &s.windowWidth, &s.windowHeight);
                     }), this);

    _mainVBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(_window), _mainVBox);

    _menuBar  = buildMenuBar();
    _toolbar  = buildToolbar();
    _notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(_notebook), TRUE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(_notebook), FALSE);
    _statusBar = buildStatusBar();

    _resultsPanel = buildResultsPanel();   // hidden by default

    // --- Phase 3 panels ---
    _workspacePanel = std::make_unique<WorkspacePanel>();
    _workspacePanel->setOpenFileCallback([this](const std::string& p) { openFile(p); });

    _funcList = std::make_unique<FunctionList>();
    _funcList->setGotoLineCallback([this](int line) {
        if (auto* v = currentView()) { v->gotoLine(line); v->ensureCaretVisible(); }
    });

    _docMap = std::make_unique<DocumentMap>();

    // Split editor: created now, hidden until split view is activated
    _splitEditor = new ScintillaView();
    applySettingsToView(_splitEditor);

    // Layout: notebook | split editor (horizontal)
    _splitHPaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(_splitHPaned), _notebook, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(_splitHPaned), _splitEditor->getWidget(), TRUE, TRUE);
    gtk_widget_hide(_splitEditor->getWidget());

    // Layout: (notebook | split) on top, results panel on bottom (vertical)
    _editorVPaned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_pack1(GTK_PANED(_editorVPaned), _splitHPaned, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(_editorVPaned), _resultsPanel, FALSE, TRUE);

    // Right side panels: function list (top) | document map (bottom)
    _rightVPaned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_pack1(GTK_PANED(_rightVPaned), _funcList->getWidget(), TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(_rightVPaned), _docMap->getWidget(),   TRUE, FALSE);
    gtk_widget_set_size_request(_rightVPaned, 200, -1);
    gtk_widget_hide(_rightVPaned);

    // Editor area (left) | right panels (right) — horizontal
    _editorHPaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(_editorHPaned), _editorVPaned, TRUE, FALSE);
    gtk_paned_pack2(GTK_PANED(_editorHPaned), _rightVPaned,  FALSE, FALSE);

    // Workspace tree (left) | editor+right area — horizontal (outermost)
    _outerHPaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(_outerHPaned), _workspacePanel->getWidget(), FALSE, FALSE);
    gtk_paned_pack2(GTK_PANED(_outerHPaned), _editorHPaned, TRUE, FALSE);
    gtk_widget_hide(_workspacePanel->getWidget());

    gtk_box_pack_start(GTK_BOX(_mainVBox), _menuBar,     FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(_mainVBox), _toolbar,     FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(_mainVBox), _outerHPaned, TRUE,  TRUE,  0);
    gtk_box_pack_end  (GTK_BOX(_mainVBox), _statusBar,   FALSE, FALSE, 0);

    g_signal_connect(_notebook, "switch-page",
                     G_CALLBACK(cbTabSwitched), this);

    // Find/Replace dialog
    _findDlg = std::make_unique<FindReplaceDlg>(
        GTK_WINDOW(_window),
        [this]() -> ScintillaView* { return currentView(); }
    );
    // Phase 2: Replace in All Docs callback
    _findDlg->setReplaceAllDocsCb(
        [this](const std::string& find, const std::string& repl,
               bool mc, bool ww, bool rx) -> int {
            int total = 0;
            for (auto& d : _docs)
                if (d.view) total += d.view->replaceAll(find, repl, mc, ww, rx);
            return total;
        });

    _findDlg->setFindAllCb([this](const FindOptions& opts) {
        auto* d = currentDoc();
        auto* v = currentView();
        if (!v) return;

        auto matches = v->findAllMatches(opts.findText,
                                          opts.matchCase, opts.wholeWord, opts.regex);

        clearResults();

        std::string filename = (d && !d->tabTitle.empty()) ? d->tabTitle : "current file";
        std::string filePath = d ? d->filePath : "";

        // Dynamic header in the panel title bar (plain text, uses gtk_label_set_text)
        setResultsHeader("Find \"" + opts.findText + "\"  —  "
                         + std::to_string(matches.size()) + " match(es) in \""
                         + filename + "\"");

        // Header row in the list (line=-1 → not clickable) — Pango markup, bold
        addResultRaw("<b>  Find \""
                     + pangoEscape(opts.findText) + "\" in \""
                     + pangoEscape(filename) + "\"  \xe2\x80\x94  "
                     + std::to_string(matches.size()) + " match(es)</b>",
                     "", -1);

        for (auto& m : matches) {
            std::string prefix   = "    Line " + std::to_string(m.line + 1) + ":  ";
            std::string lineText = m.text;

            // Build Pango markup for the line, highlighting every occurrence of the keyword
            std::string markupBody;
            if (!opts.regex && !opts.findText.empty()) {
                std::string needle   = opts.findText;
                std::string haystack = lineText;
                if (!opts.matchCase) {
                    std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
                    std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::tolower);
                }
                size_t pos = 0, found;
                while ((found = haystack.find(needle, pos)) != std::string::npos) {
                    markupBody += pangoEscape(lineText.substr(pos, found - pos));
                    markupBody += "<b><span foreground=\"#FF8C00\">"
                                + pangoEscape(lineText.substr(found, needle.size()))
                                + "</span></b>";
                    pos = found + needle.size();
                }
                markupBody += pangoEscape(lineText.substr(pos));
            } else {
                // For regex results just escape (exact match bounds not available here)
                markupBody = pangoEscape(lineText);
            }

            addResultRaw(pangoEscape(prefix) + markupBody, filePath, m.line);
        }

        showResultsPanel(true);
    });

    // Find in Files dialog
    _findInFilesDlg = std::make_unique<FindInFilesDlg>(
        GTK_WINDOW(_window),
        [this](const FindInFilesResult& r) { addResult(r.path, r.line, r.text); },
        [this](int total, bool /*cancelled*/) {
            if (total > 0) showResultsPanel(true);
        }
    );

    gtk_widget_show_all(_window);

    // Apply stored visibility
    if (!cfg.showToolbar)    gtk_widget_hide(_toolbar);
    if (!cfg.showStatusBar)  gtk_widget_hide(_statusBar);

    // Open an initial empty document
    newDocument();

    // Phase 5: macro manager (persists named macros to ~/.config/notepad++/macros.conf)
    _macroManager = std::make_unique<MacroManager>();

    // Phase 4: load plugins and rebuild the Plugins menu
    _pluginManager = std::make_unique<PluginManager>();
    NppData nppData;
    nppData.nppHandle      = _window;
    nppData.scintillaMain  = g_currentSciWidget;
    nppData.scintillaSub   = _splitEditor ? _splitEditor->getWidget() : nullptr;
    _pluginManager->loadAll(getPluginsDir(), nppData);
    populatePluginsMenu();

    // Phase 6: restore previous session if the setting is enabled
    if (cfg.restoreSession)
        restoreSession();
}

// ---------------------------------------------------------------------------
// Menu bar
// ---------------------------------------------------------------------------

GtkWidget* NotepadPlusGtk::appendMenu(GtkWidget* bar, const char* label) {
    GtkWidget* item = gtk_menu_item_new_with_mnemonic(label);
    GtkWidget* menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), item);
    return menu;
}

GtkWidget* NotepadPlusGtk::addItem(GtkWidget* menu, const char* label,
                                    GCallback cb, gpointer data) {
    GtkWidget* item = gtk_menu_item_new_with_mnemonic(label);
    if (cb) g_signal_connect(item, "activate", cb, data ? data : this);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    return item;
}

GtkWidget* NotepadPlusGtk::addCheck(GtkWidget* menu, const char* label, bool active,
                                     GCallback cb, gpointer data) {
    GtkWidget* item = gtk_check_menu_item_new_with_mnemonic(label);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), active);
    if (cb) g_signal_connect(item, "toggled", cb, data ? data : this);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    return item;
}

void NotepadPlusGtk::addSep(GtkWidget* menu) {
    gtk_menu_shell_append(GTK_MENU_SHELL(menu),
                          gtk_separator_menu_item_new());
}

GtkWidget* NotepadPlusGtk::buildMenuBar() {
    GtkWidget* bar  = gtk_menu_bar_new();
    auto& cfg = Parameters::getInstance().getSettings();

    // === File ===
    GtkWidget* file = appendMenu(bar, "_File");
    addItem(file, "_New",          G_CALLBACK(cbNew),     this);
    addItem(file, "_Open...",      G_CALLBACK(cbOpen),    this);
    addSep(file);
    addItem(file, "_Save",         G_CALLBACK(cbSave),    this);
    addItem(file, "Save _As...",   G_CALLBACK(cbSaveAs),  this);
    addItem(file, "Save A_ll",     G_CALLBACK(cbSaveAll), this);
    addSep(file);
    addItem(file, "_Reload",       G_CALLBACK(cbReload),  this);
    addSep(file);
    addItem(file, "_Close",        G_CALLBACK(cbClose),   this);
    addItem(file, "Close All",     G_CALLBACK(cbCloseAll),this);
    addSep(file);

    // Recent files submenu
    GtkWidget* recentItem = gtk_menu_item_new_with_mnemonic("Recent _Files");
    _recentMenu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(recentItem), _recentMenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(file), recentItem);
    rebuildRecentMenu();
    addSep(file);
    addItem(file, "E_xit",         G_CALLBACK(cbExit),    this);

    // === Edit ===
    GtkWidget* edit = appendMenu(bar, "_Edit");
    addItem(edit, "_Undo\tCtrl+Z",      G_CALLBACK(cbUndo),     this);
    addItem(edit, "_Redo\tCtrl+Y",      G_CALLBACK(cbRedo),     this);
    addSep(edit);
    addItem(edit, "Cu_t\tCtrl+X",       G_CALLBACK(cbCut),      this);
    addItem(edit, "_Copy\tCtrl+C",      G_CALLBACK(cbCopy),     this);
    addItem(edit, "_Paste\tCtrl+V",     G_CALLBACK(cbPaste),    this);
    addItem(edit, "_Delete",            G_CALLBACK(cbDelete),   this);
    addSep(edit);
    addItem(edit, "Select _All\tCtrl+A",G_CALLBACK(cbSelectAll),this);
    addSep(edit);
    addItem(edit, "UPPER CASE\tCtrl+Shift+U", G_CALLBACK(cbToUpper), this);
    addItem(edit, "lower case\tCtrl+U",       G_CALLBACK(cbToLower), this);
    addSep(edit);
    addItem(edit, "Increase _Indent\tTab",    G_CALLBACK(cbIndent),  this);
    addItem(edit, "Decrease Indent\tShift+Tab",G_CALLBACK(cbUnindent),this);

    // --- Line operations submenu ---
    addSep(edit);
    {
        GtkWidget* lineItem = gtk_menu_item_new_with_mnemonic("_Line Operations");
        GtkWidget* lineMenu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(lineItem), lineMenu);
        gtk_menu_shell_append(GTK_MENU_SHELL(edit), lineItem);
        addItem(lineMenu, "Duplicate Line\tCtrl+D",        G_CALLBACK(cbDuplicateLine), this);
        addItem(lineMenu, "Delete Line\tCtrl+Shift+L",     G_CALLBACK(cbDeleteLine),    this);
        addItem(lineMenu, "Move Line Up\tCtrl+Shift+Up",   G_CALLBACK(cbMoveLineUp),    this);
        addItem(lineMenu, "Move Line Down\tCtrl+Shift+Down",G_CALLBACK(cbMoveLineDown), this);
    }

    // --- Extended case submenu ---
    {
        GtkWidget* caseItem = gtk_menu_item_new_with_mnemonic("_Case Convert");
        GtkWidget* caseMenu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(caseItem), caseMenu);
        gtk_menu_shell_append(GTK_MENU_SHELL(edit), caseItem);
        addItem(caseMenu, "Title Case", G_CALLBACK(cbToTitleCase), this);
        addItem(caseMenu, "camelCase",  G_CALLBACK(cbToCamelCase), this);
        addItem(caseMenu, "snake_case", G_CALLBACK(cbToSnakeCase), this);
    }

    // --- Comment / Uncomment ---
    addSep(edit);
    addItem(edit, "Comment/Uncomment\tCtrl+/", G_CALLBACK(cbToggleComment), this);

    // --- Whitespace submenu ---
    addSep(edit);
    {
        GtkWidget* wsItem = gtk_menu_item_new_with_mnemonic("_Whitespace");
        GtkWidget* wsMenu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(wsItem), wsMenu);
        gtk_menu_shell_append(GTK_MENU_SHELL(edit), wsItem);
        addItem(wsMenu, "Trim Trailing Whitespace", G_CALLBACK(cbTrimTrailing), this);
        addItem(wsMenu, "Trim Leading Whitespace",  G_CALLBACK(cbTrimLeading),  this);
        addItem(wsMenu, "Trim All Whitespace",      G_CALLBACK(cbTrimAll),      this);
        gtk_menu_shell_append(GTK_MENU_SHELL(wsMenu), gtk_separator_menu_item_new());
        addItem(wsMenu, "Tab to Spaces",            G_CALLBACK(cbTabsToSpaces), this);
        addItem(wsMenu, "Spaces to Tab",            G_CALLBACK(cbSpacesToTabs), this);
    }

    // --- EOL Conversion submenu ---
    {
        GtkWidget* eolItem = gtk_menu_item_new_with_mnemonic("_EOL Conversion");
        GtkWidget* eolMenu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(eolItem), eolMenu);
        gtk_menu_shell_append(GTK_MENU_SHELL(edit), eolItem);
        addItem(eolMenu, "Convert to Windows (CRLF)", G_CALLBACK(cbEOLtoCRLF), this);
        addItem(eolMenu, "Convert to Unix (LF)",      G_CALLBACK(cbEOLtoLF),   this);
        addItem(eolMenu, "Convert to Old Mac (CR)",   G_CALLBACK(cbEOLtoCR),   this);
    }

    // --- Phase 6: Preferences ---
    addSep(edit);
    addItem(edit, "_Preferences...",       G_CALLBACK(cbPreferences),       this);
    addItem(edit, "_Keyboard Shortcuts...",G_CALLBACK(cbKeyboardShortcuts), this);

    // === Search ===
    GtkWidget* srch = appendMenu(bar, "_Search");
    addItem(srch, "_Find...\tCtrl+F",             G_CALLBACK(cbFind),       this);
    addItem(srch, "Find / _Replace...\tCtrl+H",   G_CALLBACK(cbReplace),    this);
    addItem(srch, "Find _Next\tF3",               G_CALLBACK(cbFindNext),   this);
    addItem(srch, "Find Pre_vious\tShift+F3",     G_CALLBACK(cbFindPrev),   this);
    addItem(srch, "Find in _Files...\tCtrl+Shift+F", G_CALLBACK(cbFindInFiles), this);
    addSep(srch);
    addItem(srch, "_Go to Line...\tCtrl+G",       G_CALLBACK(cbGoToLine),   this);
    addSep(srch);
    // --- Bookmarks submenu ---
    {
        GtkWidget* bkItem = gtk_menu_item_new_with_mnemonic("_Bookmarks");
        GtkWidget* bkMenu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(bkItem), bkMenu);
        gtk_menu_shell_append(GTK_MENU_SHELL(srch), bkItem);
        addItem(bkMenu, "Toggle Bookmark\tCtrl+F2",    G_CALLBACK(cbToggleBookmark), this);
        addItem(bkMenu, "Next Bookmark\tF2",           G_CALLBACK(cbNextBookmark),   this);
        addItem(bkMenu, "Previous Bookmark\tShift+F2", G_CALLBACK(cbPrevBookmark),   this);
        gtk_menu_shell_append(GTK_MENU_SHELL(bkMenu), gtk_separator_menu_item_new());
        addItem(bkMenu, "Clear All Bookmarks",         G_CALLBACK(cbClearBookmarks), this);
    }

    // === View ===
    GtkWidget* view = appendMenu(bar, "_View");
    _miWordWrap       = addCheck(view, "_Word Wrap",        cfg.wordWrap,
                                 G_CALLBACK(cbWordWrap),      this);
    _miShowWhitespace = addCheck(view, "Show _Whitespace",  cfg.showWhitespace,
                                 G_CALLBACK(cbShowWhitespace), this);
    _miShowLineNums   = addCheck(view, "Show _Line Numbers",cfg.showLineNumbers,
                                 G_CALLBACK(cbShowLineNums),   this);
    addSep(view);
    _miShowStatusBar  = addCheck(view, "Show _Status Bar",  cfg.showStatusBar,
                                 G_CALLBACK(cbShowStatusBar),  this);
    _miShowToolbar    = addCheck(view, "Show _Toolbar",     cfg.showToolbar,
                                 G_CALLBACK(cbShowToolbar),    this);
    addSep(view);
    _miDarkTheme      = addCheck(view, "_Dark Theme",       cfg.theme == "dark",
                                 G_CALLBACK(cbDarkTheme),      this);
    addSep(view);
    addItem(view, "Zoom _In\tCtrl++",    G_CALLBACK(cbZoomIn),   this);
    addItem(view, "Zoom _Out\tCtrl+-",   G_CALLBACK(cbZoomOut),  this);
    addItem(view, "Restore _Zoom\tCtrl+0",G_CALLBACK(cbZoomReset),this);
    // --- Phase 3: Panels ---
    addSep(view);
    _miShowWorkspace = addCheck(view, "Show _Workspace",     false,
                                G_CALLBACK(cbShowWorkspace), this);
    _miShowFuncList  = addCheck(view, "Show _Function List", false,
                                G_CALLBACK(cbShowFuncList),  this);
    _miShowDocMap    = addCheck(view, "Show _Document Map",  false,
                                G_CALLBACK(cbShowDocMap),    this);
    addSep(view);
    _miSplitView     = addCheck(view, "Split _View",         false,
                                G_CALLBACK(cbSplitView),     this);
    addSep(view);
    addItem(view, "Set Workspace _Directory...",
            G_CALLBACK(cbSetWorkspaceDir), this);

    // === Language ===
    GtkWidget* lang = appendMenu(bar, "_Language");

    // Helper lambda to add a language item
    auto addLang = [&](const char* label, LexerType type) {
        auto* ld  = new LangCallbackData{this, type, label};
        GtkWidget* item = gtk_menu_item_new_with_label(label);
        g_signal_connect(item, "activate", G_CALLBACK(cbSetLang), ld);
        gtk_menu_shell_append(GTK_MENU_SHELL(lang), item);
    };

    addLang("Normal Text",   LexerType::None);
    addSep(lang);
    addLang("C / C++",       LexerType::Cpp);
    addLang("C#",            LexerType::CSharp);
    addLang("Java",          LexerType::Java);
    addLang("JavaScript",    LexerType::JavaScript);
    addLang("Python",        LexerType::Python);
    addLang("Rust",          LexerType::Rust);
    addLang("Go",            LexerType::Go);
    addLang("Swift",         LexerType::Swift);
    addSep(lang);
    addLang("HTML",          LexerType::HTML);
    addLang("XML",           LexerType::XML);
    addLang("CSS",           LexerType::CSS);
    addLang("PHP",           LexerType::PHP);
    addSep(lang);
    addLang("Bash / Shell",  LexerType::Bash);
    addLang("Makefile",      LexerType::Makefile);
    addLang("CMake",         LexerType::CMake);
    addSep(lang);
    addLang("SQL",           LexerType::SQL);
    addLang("Lua",           LexerType::Lua);
    addLang("Perl",          LexerType::Perl);
    addLang("Ruby",          LexerType::Ruby);
    addSep(lang);
    addLang("JSON",          LexerType::JSON);
    addLang("YAML",          LexerType::YAML);
    addLang("Markdown",      LexerType::Markdown);
    addLang("Diff / Patch",  LexerType::Diff);
    addLang("INI / Config",  LexerType::Ini);
    addSep(lang);
    addLang("Pascal",        LexerType::Pascal);
    addLang("VB",            LexerType::VB);
    addLang("Assembly",      LexerType::Asm);
    addLang("TCL",           LexerType::TCL);

    // === Macro (Phase 5) ===
    GtkWidget* macroMenu = appendMenu(bar, "_Macro");
    _miMacroRecord = addCheck(macroMenu, "_Record Macro\tCtrl+Shift+R", false,
                              G_CALLBACK(cbMacroRecord), this);
    addSep(macroMenu);
    addItem(macroMenu, "_Play Back\tCtrl+Shift+P",  G_CALLBACK(cbMacroPlay),   this);
    addItem(macroMenu, "Play _N Times...",           G_CALLBACK(cbMacroPlayN),  this);
    addSep(macroMenu);
    addItem(macroMenu, "_Save Macro...",             G_CALLBACK(cbMacroSave),   this);
    addItem(macroMenu, "_Manage Macros...",          G_CALLBACK(cbMacroManage), this);

    // === Tools (Phase 5) ===
    GtkWidget* toolsMenu = appendMenu(bar, "_Tools");
    addItem(toolsMenu, "_Run Program...\tCtrl+F5",  G_CALLBACK(cbRunProgram), this);
    addSep(toolsMenu);
    addItem(toolsMenu, "_MD5 of Selection...",      G_CALLBACK(cbMd5),        this);
    addItem(toolsMenu, "_SHA-256 of Selection...",  G_CALLBACK(cbSha256),     this);

    // === Plugins (Phase 4) ===
    // The item is created here with an empty menu; populatePluginsMenu() fills
    // it after the PluginManager has loaded all .so files.
    _pluginsMenuItem = gtk_menu_item_new_with_mnemonic("_Plugins");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(_pluginsMenuItem), gtk_menu_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), _pluginsMenuItem);

    // === Help ===
    GtkWidget* help = appendMenu(bar, "_Help");
    addItem(help, "_About Notepad++", G_CALLBACK(cbAbout), this);

    gtk_widget_show_all(bar);
    return bar;
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------

GtkWidget* NotepadPlusGtk::buildToolbar() {
    GtkWidget* tb = gtk_toolbar_new();
    gtk_toolbar_set_style(GTK_TOOLBAR(tb), GTK_TOOLBAR_ICONS);

    auto addBtn = [&](const char* stockId, const char* tooltip, GCallback cb) {
        GtkToolItem* btn = gtk_tool_button_new(
            gtk_image_new_from_icon_name(stockId, GTK_ICON_SIZE_SMALL_TOOLBAR),
            nullptr);
        gtk_tool_item_set_tooltip_text(btn, tooltip);
        g_signal_connect(btn, "clicked", cb, this);
        gtk_toolbar_insert(GTK_TOOLBAR(tb), btn, -1);
        return btn;
    };

    addBtn("document-new",       "New (Ctrl+N)",       G_CALLBACK(cbNew));
    addBtn("document-open",      "Open (Ctrl+O)",      G_CALLBACK(cbOpen));
    addBtn("document-save",      "Save (Ctrl+S)",      G_CALLBACK(cbSave));
    addBtn("document-save-as",   "Save As",            G_CALLBACK(cbSaveAs));

    gtk_toolbar_insert(GTK_TOOLBAR(tb), gtk_separator_tool_item_new(), -1);

    addBtn("edit-undo",          "Undo (Ctrl+Z)",      G_CALLBACK(cbUndo));
    addBtn("edit-redo",          "Redo (Ctrl+Y)",      G_CALLBACK(cbRedo));

    gtk_toolbar_insert(GTK_TOOLBAR(tb), gtk_separator_tool_item_new(), -1);

    addBtn("edit-cut",           "Cut (Ctrl+X)",       G_CALLBACK(cbCut));
    addBtn("edit-copy",          "Copy (Ctrl+C)",      G_CALLBACK(cbCopy));
    addBtn("edit-paste",         "Paste (Ctrl+V)",     G_CALLBACK(cbPaste));

    gtk_toolbar_insert(GTK_TOOLBAR(tb), gtk_separator_tool_item_new(), -1);

    addBtn("edit-find",          "Find (Ctrl+F)",      G_CALLBACK(cbFind));
    addBtn("edit-find-replace",  "Replace (Ctrl+H)",   G_CALLBACK(cbReplace));

    gtk_toolbar_insert(GTK_TOOLBAR(tb), gtk_separator_tool_item_new(), -1);

    addBtn("zoom-in",            "Zoom In (Ctrl++)",   G_CALLBACK(cbZoomIn));
    addBtn("zoom-out",           "Zoom Out (Ctrl+-)",  G_CALLBACK(cbZoomOut));
    addBtn("zoom-original",      "Reset Zoom (Ctrl+0)",G_CALLBACK(cbZoomReset));

    gtk_widget_show_all(tb);
    return tb;
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------

GtkWidget* NotepadPlusGtk::buildStatusBar() {
    GtkWidget* sb = gtk_statusbar_new();
    gtk_statusbar_push(GTK_STATUSBAR(sb), 0, "Ready");
    gtk_widget_show(sb);
    return sb;
}

// ---------------------------------------------------------------------------
// Tab label with close button
// ---------------------------------------------------------------------------

GtkWidget* NotepadPlusGtk::makeTabLabel(const std::string& title, int docIdx) {
    GtkWidget* hbox  = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget* label = gtk_label_new(title.c_str());
    // Set width_chars to the actual title length (capped at 128) so the
    // label's minimum allocated width equals its content.  This is the only
    // reliable way to make GTK3 scrollable notebooks size tabs dynamically:
    // without it, ellipsized labels have a near-zero minimum and collapse to
    // "..." regardless of the actual text.
    int wchars = std::max(1, std::min((int)title.size(), 128));
    gtk_label_set_width_chars(GTK_LABEL(label), wchars);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 128);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);

    GtkWidget* closeBtn = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(closeBtn), GTK_RELIEF_NONE);
    gtk_widget_set_focus_on_click(closeBtn, FALSE);
    GtkWidget* closeImg = gtk_image_new_from_icon_name("window-close",
                                                        GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(closeBtn), closeImg);

    // Style the button to be smaller
    GtkStyleContext* ctx = gtk_widget_get_style_context(closeBtn);
    gtk_style_context_add_class(ctx, "flat");
    gtk_widget_set_size_request(closeBtn, 16, 16);

    gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), closeBtn, FALSE, FALSE, 0);

    // Store a direct pointer to the label so updateTabLabel can find it
    // without relying on gtk_container_get_children ordering.
    g_object_set_data(G_OBJECT(hbox), "npp-tab-label", label);

    gtk_widget_show_all(hbox);

    // We close the tab identified by page index
    g_signal_connect(closeBtn, "clicked",
                     G_CALLBACK(+[](GtkWidget* btn, gpointer ud) {
                         auto* npp = static_cast<NotepadPlusGtk*>(ud);
                         // Find which page this button's label belongs to.
                         // gtk_notebook_get_tab_label() returns the widget
                         // directly set as the tab label (hbox), which is
                         // the immediate parent of the close button.
                         GtkWidget* label_box = gtk_widget_get_parent(btn);
                         int n = gtk_notebook_get_n_pages(
                             GTK_NOTEBOOK(npp->_notebook));
                         for (int i = 0; i < n; ++i) {
                             if (gtk_notebook_get_tab_label(
                                     GTK_NOTEBOOK(npp->_notebook),
                                     gtk_notebook_get_nth_page(
                                         GTK_NOTEBOOK(npp->_notebook), i))
                                 == label_box) {
                                 npp->closeDocument(i);
                                 return;
                             }
                         }
                     }), this);

    return hbox;
}

// ---------------------------------------------------------------------------
// Document management
// ---------------------------------------------------------------------------

int NotepadPlusGtk::newDocument() {
    auto* view = new ScintillaView();
    applySettingsToView(view);

    view->setModifiedCallback([this](bool /*mod*/) {
        int idx = currentPage();
        if (idx < 0 || idx >= (int)_docs.size()) return;
        bool m = _docs[idx].view->isModified();
        if (_docs[idx].modified != m) {
            _docs[idx].modified = m;
            updateTabLabel(idx);
            updateTitle();
        }
    });
    view->setCursorMovedCallback([this]() {
        updateStatusBar();
        if (_docMap) _docMap->scrollUpdate(currentView());
    });
    // Phase 4: forward Scintilla notifications to all loaded plugins
    view->setNotifyCallback([this](SCNotification* n) {
        if (_pluginManager) _pluginManager->notifyAll(n);
    });
    // Phase 5: forward SCN_MACRORECORD to macro manager
    view->setMacroRecordCallback([this](unsigned int msg, uptr_t wp, sptr_t lp) {
        if (_macroManager && _macroManager->isRecording())
            _macroManager->appendAction(msg, wp, lp);
    });
    // If recording is already active when a new tab is opened, enable it here too
    if (_macroManager && _macroManager->isRecording())
        view->replayMessage(SCI_STARTRECORD);

    Document doc;
    doc.view     = view;
    doc.modified = false;
    ++_untitledCount;
    doc.tabTitle = "new " + std::to_string(_untitledCount);
    _docs.push_back(doc);

    int idx = (int)_docs.size() - 1;
    GtkWidget* tabBox = makeTabLabel(doc.tabTitle, idx);
    // Store the GtkLabel pointer directly so updateTabLabel never has to
    // traverse GTK internals to find it.
    _docs[idx].tabLabelWidget =
        GTK_WIDGET(g_object_get_data(G_OBJECT(tabBox), "npp-tab-label"));

    gtk_notebook_append_page(GTK_NOTEBOOK(_notebook),
                              view->getWidget(), tabBox);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(_notebook),
                                     view->getWidget(), TRUE);
    gtk_widget_show(view->getWidget());
    gtk_notebook_set_current_page(GTK_NOTEBOOK(_notebook), idx);
    g_currentSciWidget = view->getWidget();  // Phase 4: keep global in sync

    updateTitle();
    return idx;
}

int  NotepadPlusGtk::pageCount()   const {
    return gtk_notebook_get_n_pages(GTK_NOTEBOOK(_notebook));
}

int  NotepadPlusGtk::currentPage() const {
    return gtk_notebook_get_current_page(GTK_NOTEBOOK(_notebook));
}

Document* NotepadPlusGtk::currentDoc() {
    int idx = currentPage();
    return docAt(idx);
}

Document* NotepadPlusGtk::docAt(int idx) {
    if (idx < 0 || idx >= (int)_docs.size()) return nullptr;
    return &_docs[idx];
}

ScintillaView* NotepadPlusGtk::currentView() {
    auto* d = currentDoc();
    return d ? d->view : nullptr;
}

void NotepadPlusGtk::applySettingsToView(ScintillaView* v) {
    auto& cfg = Parameters::getInstance().getSettings();
    v->setWordWrap(cfg.wordWrap);
    v->setShowWhitespace(cfg.showWhitespace);
    v->setShowLineNumbers(cfg.showLineNumbers);
    v->setTabWidth(cfg.tabWidth);
    v->setUseTabs(!cfg.tabUseSpaces);
    v->setFont(cfg.fontName, cfg.fontSize);
    v->setZoom(cfg.zoomLevel);
    v->applyColorTheme(cfg.theme);
    v->setEOLMode(SC_EOL_LF);
    v->setEdgeMode(cfg.edgeEnabled);
    v->setEdgeColumn(cfg.edgeColumn);
}

void NotepadPlusGtk::applySettingsToAll() {
    for (auto& d : _docs)
        if (d.view) applySettingsToView(d.view);
}

// ---------------------------------------------------------------------------
// File open / save
// ---------------------------------------------------------------------------

void NotepadPlusGtk::openFile(const std::string& path) {
    // Check if already open
    for (int i = 0; i < (int)_docs.size(); ++i) {
        if (_docs[i].filePath == path) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(_notebook), i);
            return;
        }
    }
    // Reuse current tab if empty and unmodified
    int idx = currentPage();
    Document* d = docAt(idx);
    bool reuseTab = (d && d->filePath.empty() && !d->modified &&
                     d->view->getLength() == 0);
    if (!reuseTab) idx = newDocument();

    loadFileIntoDoc(idx, path);
}

bool NotepadPlusGtk::loadFileIntoDoc(int idx, const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        GtkWidget* dlg = gtk_message_dialog_new(
            GTK_WINDOW(_window), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK, "Cannot open file:\n%s", path.c_str());
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        return false;
    }

    std::ostringstream ss;
    ss << f.rdbuf();
    std::string content = ss.str();

    // Detect encoding from BOM
    std::string enc = "UTF-8";
    if (content.size() >= 3 &&
        (unsigned char)content[0] == 0xEF &&
        (unsigned char)content[1] == 0xBB &&
        (unsigned char)content[2] == 0xBF) {
        enc = "UTF-8 BOM";
        content = content.substr(3);
    } else if (content.size() >= 2 &&
               (unsigned char)content[0] == 0xFF &&
               (unsigned char)content[1] == 0xFE) {
        enc = "UTF-16 LE";
    } else if (content.size() >= 2 &&
               (unsigned char)content[0] == 0xFE &&
               (unsigned char)content[1] == 0xFF) {
        enc = "UTF-16 BE";
    }

    auto& doc = _docs[idx];
    doc.filePath  = path;
    doc.tabTitle  = fs::path(path).filename().string();
    doc.lexerType = LexerType::None;
    doc.encoding  = enc;

    doc.view->setText(content);
    doc.view->setLexerByFilename(path);
    doc.modified = false;

    // Recalculate line-number margin width based on the actual line count now
    // that the file content is loaded (may be much larger than the default).
    if (Parameters::getInstance().getSettings().showLineNumbers)
        doc.view->setShowLineNumbers(true);

    Parameters::getInstance().addRecentFile(path);
    rebuildRecentMenu();

    // Phase 3: (re)start file monitor
    if (doc.fileMonitor) {
        g_file_monitor_cancel(doc.fileMonitor);
        g_object_unref(doc.fileMonitor);
        doc.fileMonitor = nullptr;
    }
    if (!path.empty()) {
        GFile*  gfile = g_file_new_for_path(path.c_str());
        GError* err   = nullptr;
        doc.fileMonitor = g_file_monitor_file(gfile, G_FILE_MONITOR_NONE, nullptr, &err);
        if (doc.fileMonitor) {
            auto* md = new FileMonitorData{this, path};
            g_signal_connect_data(doc.fileMonitor, "changed",
                G_CALLBACK(cbFileChanged), md,
                [](gpointer p, GClosure*) { delete static_cast<FileMonitorData*>(p); },
                G_CONNECT_AFTER);
        }
        if (err) g_error_free(err);
        g_object_unref(gfile);
    }

    updateTabLabel(idx);
    updateTitle();
    updateStatusBar();

    // Phase 3: refresh function list / document map for the current tab
    if (idx == currentPage()) {
        if (_funcList) _funcList->update(doc.view, doc.lexerType);
        if (_docMap)   _docMap->update(doc.view);
    }
    return true;
}

bool NotepadPlusGtk::saveDocument(int idx, bool forceDialog) {
    auto* d = docAt(idx);
    if (!d) return false;

    if (d->filePath.empty() || forceDialog)
        return saveAs(idx);

    std::ofstream f(d->filePath, std::ios::binary);
    if (!f.is_open()) {
        GtkWidget* dlg = gtk_message_dialog_new(
            GTK_WINDOW(_window), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK, "Cannot save file:\n%s", d->filePath.c_str());
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        return false;
    }

    std::string text = d->view->getText();
    f.write(text.data(), text.size());
    d->view->setSavePoint();
    d->modified = false;
    updateTabLabel(idx);
    updateTitle();
    return true;
}

bool NotepadPlusGtk::saveAs(int idx) {
    auto* d = docAt(idx);
    if (!d) return false;

    std::string path = runSaveDialog(d->filePath.empty()
                                     ? d->tabTitle : d->filePath);
    if (path.empty()) return false;

    d->filePath = path;
    d->tabTitle = fs::path(path).filename().string();
    d->view->setLexerByFilename(path);

    Parameters::getInstance().addRecentFile(path);
    rebuildRecentMenu();

    return saveDocument(idx);
}

bool NotepadPlusGtk::closeDocument(int idx, bool createIfEmpty) {
    auto* d = docAt(idx);
    if (!d) return true;

    if (d->modified) {
        std::string msg = "Save changes to \"" + d->tabTitle + "\"?";
        GtkWidget* dlg = gtk_message_dialog_new(
            GTK_WINDOW(_window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s", msg.c_str());
        gtk_dialog_add_buttons(GTK_DIALOG(dlg),
                               "_Save",    GTK_RESPONSE_YES,
                               "_Discard", GTK_RESPONSE_NO,
                               "_Cancel",  GTK_RESPONSE_CANCEL,
                               nullptr);
        int resp = gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        if (resp == GTK_RESPONSE_YES) {
            if (!saveDocument(idx)) return false;
        } else if (resp == GTK_RESPONSE_CANCEL ||
                   resp == GTK_RESPONSE_DELETE_EVENT) {
            return false;
        }
    }

    // Phase 3: stop file monitor
    if (d->fileMonitor) {
        g_file_monitor_cancel(d->fileMonitor);
        g_object_unref(d->fileMonitor);
        d->fileMonitor = nullptr;
    }

    // Save pointers before erasing so we can clean up after.
    // Erase from _docs FIRST so that cbTabSwitched (fired synchronously by
    // gtk_notebook_remove_page) sees a consistent _docs vector.
    ScintillaView* viewToDelete = d->view;
    _docs.erase(_docs.begin() + idx);
    gtk_notebook_remove_page(GTK_NOTEBOOK(_notebook), idx);
    delete viewToDelete;  // we own it (ref_sink'd in ScintillaView ctor)

    if (_docs.empty() && createIfEmpty) newDocument();
    return true;
}

bool NotepadPlusGtk::closeAll() {
    // Pass createIfEmpty=false to prevent newDocument() from firing every
    // iteration — that would cause an infinite loop.
    while (!_docs.empty()) {
        if (!closeDocument(0, false)) return false;
    }
    return true;  // _docs is now empty; caller decides whether to quit or create a new doc
}

// ---------------------------------------------------------------------------
// Tab / title / status helpers
// ---------------------------------------------------------------------------

void NotepadPlusGtk::updateTabLabel(int idx) {
    auto* d = docAt(idx);
    if (!d) return;
    std::string title = (d->modified ? "\u25cf " : "") + d->tabTitle;

    GtkWidget* page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(_notebook), idx);
    if (!page) return;

    // Replace the entire tab-label widget instead of just updating the text.
    // gtk_label_set_text alone does NOT cause GTK3 scrollable notebooks to
    // recalculate the tab's allocated width, so a short initial title ("new 1")
    // would leave the tab permanently narrow and collapse the filename to "...".
    // Replacing the widget forces a fresh size negotiation every time.
    GtkWidget* newTabBox = makeTabLabel(title, idx);
    d->tabLabelWidget =
        GTK_WIDGET(g_object_get_data(G_OBJECT(newTabBox), "npp-tab-label"));
    gtk_notebook_set_tab_label(GTK_NOTEBOOK(_notebook), page, newTabBox);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(_notebook), page, TRUE);
}

void NotepadPlusGtk::updateTitle() {
    auto* d = currentDoc();
    std::string title;
    if (d) {
        if (d->modified) title += "\u25cf ";
        title += d->tabTitle;
        if (!d->filePath.empty()) title += " - " + d->filePath;
        title += " - ";
    }
    title += "Notepad++";
    gtk_window_set_title(GTK_WINDOW(_window), title.c_str());
}

void NotepadPlusGtk::updateStatusBar() {
    auto* d = currentDoc();
    if (!d || !d->view) return;

    int line = d->view->getCurrentLine() + 1;  // 1-based for display
    int col  = d->view->getCurrentColumn() + 1;
    int total= d->view->getLineCount();
    std::string eol = d->view->getEOLModeString();

    std::string msg = "Line " + std::to_string(line) +
                      "/" + std::to_string(total) +
                      "  Col " + std::to_string(col) +
                      "  |  " + eol +
                      "  |  " + d->view->getLexerName() +
                      "  |  " + d->encoding;
    // Phase 5: recording indicator
    if (_macroManager && _macroManager->isRecording())
        msg += "  |  \u25cf REC";

    gtk_statusbar_pop (GTK_STATUSBAR(_statusBar), 0);
    gtk_statusbar_push(GTK_STATUSBAR(_statusBar), 0, msg.c_str());
}

void NotepadPlusGtk::updateMenuChecks() {
    auto& cfg = Parameters::getInstance().getSettings();
    if (_miWordWrap)
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(_miWordWrap),
                                       cfg.wordWrap);
    if (_miShowWhitespace)
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(_miShowWhitespace),
                                       cfg.showWhitespace);
    if (_miShowLineNums)
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(_miShowLineNums),
                                       cfg.showLineNumbers);
    if (_miShowStatusBar)
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(_miShowStatusBar),
                                       cfg.showStatusBar);
    if (_miShowToolbar)
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(_miShowToolbar),
                                       cfg.showToolbar);
    if (_miDarkTheme)
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(_miDarkTheme),
                                       cfg.theme == "dark");
}

void NotepadPlusGtk::rebuildRecentMenu() {
    if (!_recentMenu) return;
    // Remove all existing items
    GList* kids = gtk_container_get_children(GTK_CONTAINER(_recentMenu));
    for (GList* l = kids; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(kids);

    auto& recents = Parameters::getInstance().getSettings().recentFiles;
    if (recents.empty()) {
        GtkWidget* none = gtk_menu_item_new_with_label("(empty)");
        gtk_widget_set_sensitive(none, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(_recentMenu), none);
    } else {
        for (auto& rf : recents) {
            auto* rd = new RecentCallbackData{this, rf.path};
            GtkWidget* item = gtk_menu_item_new_with_label(rf.path.c_str());
            g_signal_connect_data(item, "activate",
                G_CALLBACK(+[](GtkMenuItem*, gpointer d) {
                    auto* rd = static_cast<RecentCallbackData*>(d);
                    rd->npp->openFile(rd->path);
                }),
                rd, [](gpointer d, GClosure*) { delete static_cast<RecentCallbackData*>(d); },
                G_CONNECT_AFTER);
            gtk_menu_shell_append(GTK_MENU_SHELL(_recentMenu), item);
        }
    }
    gtk_widget_show_all(_recentMenu);
}

// ---------------------------------------------------------------------------
// File dialogs
// ---------------------------------------------------------------------------

std::string NotepadPlusGtk::runOpenDialog() {
    GtkWidget* dlg = gtk_file_chooser_dialog_new(
        "Open File", GTK_WINDOW(_window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open",   GTK_RESPONSE_ACCEPT,
        nullptr);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dlg), FALSE);

    std::string result;
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char* fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (fn) { result = fn; g_free(fn); }
    }
    gtk_widget_destroy(dlg);
    return result;
}

std::string NotepadPlusGtk::runSaveDialog(const std::string& hint) {
    GtkWidget* dlg = gtk_file_chooser_dialog_new(
        "Save File As", GTK_WINDOW(_window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save",   GTK_RESPONSE_ACCEPT,
        nullptr);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
    if (!hint.empty()) {
        if (hint.find('/') != std::string::npos) {
            gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dlg), hint.c_str());
        } else {
            gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg),
                                               hint.c_str());
        }
    }
    std::string result;
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char* fn = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (fn) { result = fn; g_free(fn); }
    }
    gtk_widget_destroy(dlg);
    return result;
}

// ---------------------------------------------------------------------------
// File menu callbacks
// ---------------------------------------------------------------------------

void NotepadPlusGtk::cbNew (GtkMenuItem*, gpointer d) {
    static_cast<NotepadPlusGtk*>(d)->newDocument();
}

void NotepadPlusGtk::cbOpen(GtkMenuItem*, gpointer d) {
    auto* npp  = static_cast<NotepadPlusGtk*>(d);
    std::string path = npp->runOpenDialog();
    if (!path.empty()) npp->openFile(path);
}

void NotepadPlusGtk::cbSave(GtkMenuItem*, gpointer d) {
    auto* npp = static_cast<NotepadPlusGtk*>(d);
    npp->saveDocument(npp->currentPage());
}

void NotepadPlusGtk::cbSaveAs(GtkMenuItem*, gpointer d) {
    auto* npp = static_cast<NotepadPlusGtk*>(d);
    npp->saveAs(npp->currentPage());
}

void NotepadPlusGtk::cbSaveAll(GtkMenuItem*, gpointer d) {
    auto* npp = static_cast<NotepadPlusGtk*>(d);
    for (int i = 0; i < (int)npp->_docs.size(); ++i)
        npp->saveDocument(i);
}

void NotepadPlusGtk::cbReload(GtkMenuItem*, gpointer d) {
    auto* npp = static_cast<NotepadPlusGtk*>(d);
    int   idx = npp->currentPage();
    auto* doc = npp->docAt(idx);
    if (!doc || doc->filePath.empty()) return;

    if (doc->modified) {
        GtkWidget* dlg = gtk_message_dialog_new(
            GTK_WINDOW(npp->_window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
            "Discard changes and reload \"%s\"?",
            doc->tabTitle.c_str());
        int r = gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        if (r != GTK_RESPONSE_YES) return;
    }
    npp->loadFileIntoDoc(idx, doc->filePath);
}

void NotepadPlusGtk::cbClose(GtkMenuItem*, gpointer d) {
    auto* npp = static_cast<NotepadPlusGtk*>(d);
    npp->closeDocument(npp->currentPage());
}

void NotepadPlusGtk::cbCloseAll(GtkMenuItem*, gpointer d) {
    auto* npp = static_cast<NotepadPlusGtk*>(d);
    if (npp->closeAll())
        npp->newDocument();  // leave one empty tab after "Close All"
}

void NotepadPlusGtk::cbExit(GtkMenuItem*, gpointer d) {
    auto* npp = static_cast<NotepadPlusGtk*>(d);
    if (npp->closeAll()) gtk_main_quit();
}

// ---------------------------------------------------------------------------
// Edit menu callbacks
// ---------------------------------------------------------------------------

void NotepadPlusGtk::cbUndo     (GtkMenuItem*, gpointer d) { auto* v = ((NotepadPlusGtk*)d)->currentView(); if (v) v->undo(); }
void NotepadPlusGtk::cbRedo     (GtkMenuItem*, gpointer d) { auto* v = ((NotepadPlusGtk*)d)->currentView(); if (v) v->redo(); }
void NotepadPlusGtk::cbCut      (GtkMenuItem*, gpointer d) { auto* v = ((NotepadPlusGtk*)d)->currentView(); if (v) v->cut(); }
void NotepadPlusGtk::cbCopy     (GtkMenuItem*, gpointer d) { auto* v = ((NotepadPlusGtk*)d)->currentView(); if (v) v->copy(); }
void NotepadPlusGtk::cbPaste    (GtkMenuItem*, gpointer d) { auto* v = ((NotepadPlusGtk*)d)->currentView(); if (v) v->paste(); }
void NotepadPlusGtk::cbDelete   (GtkMenuItem*, gpointer d) { auto* v = ((NotepadPlusGtk*)d)->currentView(); if (v) v->deleteSel(); }
void NotepadPlusGtk::cbSelectAll(GtkMenuItem*, gpointer d) { auto* v = ((NotepadPlusGtk*)d)->currentView(); if (v) v->selectAll(); }

void NotepadPlusGtk::cbToUpper(GtkMenuItem*, gpointer d) {
    auto* v = ((NotepadPlusGtk*)d)->currentView();
    if (!v) return;
    std::string sel = v->getSelectedText();
    if (sel.empty()) return;
    std::transform(sel.begin(), sel.end(), sel.begin(), ::toupper);
    scintilla_send_message(SCINTILLA(v->getWidget()),
                            SCI_REPLACESEL, 0, (sptr_t)sel.c_str());
}

void NotepadPlusGtk::cbToLower(GtkMenuItem*, gpointer d) {
    auto* v = ((NotepadPlusGtk*)d)->currentView();
    if (!v) return;
    std::string sel = v->getSelectedText();
    if (sel.empty()) return;
    std::transform(sel.begin(), sel.end(), sel.begin(), ::tolower);
    scintilla_send_message(SCINTILLA(v->getWidget()),
                            SCI_REPLACESEL, 0, (sptr_t)sel.c_str());
}

void NotepadPlusGtk::cbIndent(GtkMenuItem*, gpointer d) {
    auto* v = ((NotepadPlusGtk*)d)->currentView();
    if (v) scintilla_send_message(SCINTILLA(v->getWidget()), SCI_TAB, 0, 0);
}

void NotepadPlusGtk::cbUnindent(GtkMenuItem*, gpointer d) {
    auto* v = ((NotepadPlusGtk*)d)->currentView();
    if (v) scintilla_send_message(SCINTILLA(v->getWidget()), SCI_BACKTAB, 0, 0);
}

// ---------------------------------------------------------------------------
// Search menu callbacks
// ---------------------------------------------------------------------------

void NotepadPlusGtk::cbFind(GtkMenuItem*, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    auto* v   = npp->currentView();
    std::string sel = v ? v->getSelectedText() : "";
    npp->_findDlg->showFind(sel);
}

void NotepadPlusGtk::cbReplace(GtkMenuItem*, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    auto* v   = npp->currentView();
    std::string sel = v ? v->getSelectedText() : "";
    npp->_findDlg->showFindReplace(sel);
}

void NotepadPlusGtk::cbFindNext(GtkMenuItem*, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    auto* v   = npp->currentView();
    if (!v) return;
    const auto& opts = npp->_findDlg->getOptions();
    if (!opts.findText.empty())
        v->findNext(opts.findText, opts.matchCase, opts.wholeWord,
                    opts.regex, opts.wrapAround);
}

void NotepadPlusGtk::cbFindPrev(GtkMenuItem*, gpointer d) {
    // Invoke dialog in backward mode
    auto* npp = (NotepadPlusGtk*)d;
    if (npp->_findDlg->isVisible())
        npp->_findDlg->focusFindEntry();
    else
        npp->_findDlg->showFind();
}

void NotepadPlusGtk::cbGoToLine(GtkMenuItem*, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    auto* v   = npp->currentView();
    if (!v) return;

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Go to Line", GTK_WINDOW(npp->_window),
        GTK_DIALOG_MODAL,
        "_OK",     GTK_RESPONSE_OK,
        "_Cancel", GTK_RESPONSE_CANCEL,
        nullptr);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);

    GtkWidget* lineLabel  = gtk_label_new("Line:");
    GtkWidget* lineEntry  = gtk_entry_new();
    GtkWidget* colLabel   = gtk_label_new("Column:");
    GtkWidget* colEntry   = gtk_entry_new();

    gtk_label_set_xalign(GTK_LABEL(lineLabel), 1.0f);
    gtk_label_set_xalign(GTK_LABEL(colLabel),  1.0f);
    gtk_widget_set_size_request(lineEntry, 120, -1);
    gtk_widget_set_size_request(colEntry,  120, -1);

    gtk_entry_set_text(GTK_ENTRY(lineEntry),
                       std::to_string(v->getCurrentLine() + 1).c_str());
    gtk_entry_set_text(GTK_ENTRY(colEntry),
                       std::to_string(v->getCurrentColumn() + 1).c_str());
    gtk_entry_set_activates_default(GTK_ENTRY(lineEntry), TRUE);
    gtk_entry_set_activates_default(GTK_ENTRY(colEntry),  TRUE);

    gtk_grid_attach(GTK_GRID(grid), lineLabel, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), lineEntry, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), colLabel,  0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), colEntry,  1, 1, 1, 1);

    gtk_container_add(GTK_CONTAINER(content), grid);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        const char* lineTxt = gtk_entry_get_text(GTK_ENTRY(lineEntry));
        const char* colTxt  = gtk_entry_get_text(GTK_ENTRY(colEntry));
        try {
            int line = std::stoi(lineTxt) - 1;
            if (line >= 0) {
                v->gotoLine(line);
                // Move caret to requested column if specified
                const char* p = colTxt;
                while (*p == ' ') ++p;
                if (*p != '\0') {
                    int col = std::stoi(colTxt) - 1;
                    if (col > 0) {
                        int targetPos = (int)scintilla_send_message(
                            SCINTILLA(v->getWidget()), SCI_FINDCOLUMN, line, col);
                        scintilla_send_message(SCINTILLA(v->getWidget()),
                                               SCI_GOTOPOS, targetPos, 0);
                    }
                }
                v->ensureCaretVisible();
            }
        } catch (...) {}
    }
    gtk_widget_destroy(dlg);
}

// ---------------------------------------------------------------------------
// View menu callbacks
// ---------------------------------------------------------------------------

void NotepadPlusGtk::cbWordWrap(GtkCheckMenuItem* mi, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    bool on   = gtk_check_menu_item_get_active(mi);
    Parameters::getInstance().getSettings().wordWrap = on;
    for (auto& doc : npp->_docs)
        if (doc.view) doc.view->setWordWrap(on);
}

void NotepadPlusGtk::cbShowWhitespace(GtkCheckMenuItem* mi, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    bool on   = gtk_check_menu_item_get_active(mi);
    Parameters::getInstance().getSettings().showWhitespace = on;
    for (auto& doc : npp->_docs)
        if (doc.view) doc.view->setShowWhitespace(on);
}

void NotepadPlusGtk::cbShowLineNums(GtkCheckMenuItem* mi, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    bool on   = gtk_check_menu_item_get_active(mi);
    Parameters::getInstance().getSettings().showLineNumbers = on;
    for (auto& doc : npp->_docs)
        if (doc.view) doc.view->setShowLineNumbers(on);
}

void NotepadPlusGtk::cbShowStatusBar(GtkCheckMenuItem* mi, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    bool on   = gtk_check_menu_item_get_active(mi);
    Parameters::getInstance().getSettings().showStatusBar = on;
    if (on) gtk_widget_show(npp->_statusBar);
    else    gtk_widget_hide(npp->_statusBar);
}

void NotepadPlusGtk::cbShowToolbar(GtkCheckMenuItem* mi, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    bool on   = gtk_check_menu_item_get_active(mi);
    Parameters::getInstance().getSettings().showToolbar = on;
    if (on) gtk_widget_show(npp->_toolbar);
    else    gtk_widget_hide(npp->_toolbar);
}

void NotepadPlusGtk::cbDarkTheme(GtkCheckMenuItem* mi, gpointer d) {
    auto* npp  = (NotepadPlusGtk*)d;
    bool dark  = gtk_check_menu_item_get_active(mi);
    std::string theme = dark ? "dark" : "default";
    Parameters::getInstance().getSettings().theme = theme;
    for (auto& doc : npp->_docs)
        if (doc.view) doc.view->applyColorTheme(theme);
}

void NotepadPlusGtk::cbZoomIn(GtkMenuItem*, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    auto* v   = npp->currentView();
    if (!v) return;
    int z = v->getZoom() + 1;
    v->setZoom(z);
    Parameters::getInstance().getSettings().zoomLevel = z;
}

void NotepadPlusGtk::cbZoomOut(GtkMenuItem*, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    auto* v   = npp->currentView();
    if (!v) return;
    int z = v->getZoom() - 1;
    v->setZoom(z);
    Parameters::getInstance().getSettings().zoomLevel = z;
}

void NotepadPlusGtk::cbZoomReset(GtkMenuItem*, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    auto* v   = npp->currentView();
    if (!v) return;
    v->setZoom(0);
    Parameters::getInstance().getSettings().zoomLevel = 0;
}

// ---------------------------------------------------------------------------
// Language callback
// ---------------------------------------------------------------------------

void NotepadPlusGtk::cbSetLang(GtkMenuItem*, gpointer d) {
    auto* ld  = static_cast<LangCallbackData*>(d);
    auto* v   = ld->npp->currentView();
    if (v) {
        v->setLexerType(ld->type);
        if (auto* doc = ld->npp->currentDoc())
            doc->lexerType = ld->type;
    }
    delete ld;
}

// ---------------------------------------------------------------------------
// Help
// ---------------------------------------------------------------------------

void NotepadPlusGtk::cbAbout(GtkMenuItem*, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    GtkWidget* dlg = gtk_about_dialog_new();
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dlg), "Notepad++");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dlg), "8.7.5 (Linux port)");
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dlg),
        "Linux port using GTK3 and Scintilla.\n"
        "Original Notepad++ by Don HO (Windows).\n"
        "Licensed under GPL v3+.");
    gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(dlg),
                                  "https://notepad-plus-plus.org");
    gtk_window_set_transient_for(GTK_WINDOW(dlg), GTK_WINDOW(npp->_window));
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

// ---------------------------------------------------------------------------
// Window / notebook callbacks
// ---------------------------------------------------------------------------

gboolean NotepadPlusGtk::cbWindowDelete(GtkWidget*, GdkEvent*, gpointer d) {
    auto* npp = static_cast<NotepadPlusGtk*>(d);
    if (npp->closeAll()) {
        gtk_main_quit();
        return FALSE;
    }
    return TRUE;   // cancel close
}

void NotepadPlusGtk::cbTabSwitched(GtkNotebook*, GtkWidget*, guint page, gpointer d) {
    auto* npp = static_cast<NotepadPlusGtk*>(d);
    if (page >= npp->_docs.size()) return;
    npp->updateTitle();
    npp->updateStatusBar();
    // Give keyboard focus to the editor
    if (npp->_docs[page].view)
        gtk_widget_grab_focus(npp->_docs[page].view->getWidget());

    // Phase 3: refresh panels for new tab
    auto& doc = npp->_docs[page];
    if (npp->_funcList) npp->_funcList->update(doc.view, doc.lexerType);
    if (npp->_docMap)   npp->_docMap->update(doc.view);

    // Phase 3: sync split editor document
    if (npp->_splitEditor &&
        gtk_widget_get_visible(npp->_splitEditor->getWidget()) && doc.view) {
        sptr_t docPtr = scintilla_send_message(
            SCINTILLA(doc.view->getWidget()), SCI_GETDOCPOINTER, 0, 0);
        scintilla_send_message(
            SCINTILLA(npp->_splitEditor->getWidget()), SCI_SETDOCPOINTER, 0, docPtr);
    }
    // Phase 4: keep global current-scintilla pointer in sync
    if (doc.view) g_currentSciWidget = doc.view->getWidget();
    // Phase 5: if recording, ensure new tab's view also fires SCN_MACRORECORD
    if (npp->_macroManager && npp->_macroManager->isRecording() && doc.view)
        doc.view->replayMessage(SCI_STARTRECORD);
}

gboolean NotepadPlusGtk::cbKeyPress(GtkWidget*, GdkEventKey* ev, gpointer d) {
    auto* npp  = static_cast<NotepadPlusGtk*>(d);
    bool  ctrl = (ev->state & GDK_CONTROL_MASK) != 0;
    bool  shft = (ev->state & GDK_SHIFT_MASK) != 0;

    if (ctrl) {
        switch (ev->keyval) {
            case GDK_KEY_n: cbNew(nullptr, npp);      return TRUE;
            case GDK_KEY_o: cbOpen(nullptr, npp);     return TRUE;
            case GDK_KEY_s: shft ? cbSaveAs(nullptr, npp)
                                 : cbSave  (nullptr, npp); return TRUE;
            case GDK_KEY_w: cbClose(nullptr, npp);    return TRUE;
            case GDK_KEY_z: cbUndo(nullptr, npp);     return TRUE;
            case GDK_KEY_y: cbRedo(nullptr, npp);     return TRUE;
            case GDK_KEY_f:
                if (shft) { cbFindInFiles(nullptr, npp); return TRUE; }
                cbFind(nullptr, npp); return TRUE;
            case GDK_KEY_h: cbReplace(nullptr, npp);  return TRUE;
            case GDK_KEY_g: cbGoToLine(nullptr, npp); return TRUE;
            case GDK_KEY_a: cbSelectAll(nullptr, npp);return TRUE;
            case GDK_KEY_d: cbDuplicateLine(nullptr, npp); return TRUE;
            case GDK_KEY_slash: cbToggleComment(nullptr, npp); return TRUE;
            case GDK_KEY_F2:    cbToggleBookmark(nullptr, npp); return TRUE;
            case GDK_KEY_equal:
            case GDK_KEY_plus:  cbZoomIn(nullptr, npp);   return TRUE;
            case GDK_KEY_minus: cbZoomOut(nullptr, npp);  return TRUE;
            case GDK_KEY_0:     cbZoomReset(nullptr, npp);return TRUE;
            // Ctrl+Shift combinations
            case GDK_KEY_L: case GDK_KEY_l:
                if (shft) { cbDeleteLine(nullptr, npp); return TRUE; }
                break;
            case GDK_KEY_Up:
                if (shft) { cbMoveLineUp(nullptr, npp); return TRUE; }
                break;
            case GDK_KEY_Down:
                if (shft) { cbMoveLineDown(nullptr, npp); return TRUE; }
                break;
            // Phase 5: Ctrl+Shift+R → toggle macro record
            case GDK_KEY_R: case GDK_KEY_r:
                if (shft && npp->_miMacroRecord) {
                    gtk_check_menu_item_set_active(
                        GTK_CHECK_MENU_ITEM(npp->_miMacroRecord),
                        !gtk_check_menu_item_get_active(
                             GTK_CHECK_MENU_ITEM(npp->_miMacroRecord)));
                    return TRUE;
                }
                break;
            // Phase 5: Ctrl+Shift+P → play back macro
            case GDK_KEY_P: case GDK_KEY_p:
                if (shft) { cbMacroPlay(nullptr, npp); return TRUE; }
                break;
            // Phase 5: Ctrl+F5 → run program
            case GDK_KEY_F5:
                cbRunProgram(nullptr, npp); return TRUE;
            // Tab navigation
            case GDK_KEY_Tab:
                if (shft) {
                    int p = npp->currentPage();
                    gtk_notebook_set_current_page(GTK_NOTEBOOK(npp->_notebook),
                                                   p > 0 ? p - 1 : npp->pageCount() - 1);
                } else {
                    int p = npp->currentPage();
                    gtk_notebook_set_current_page(GTK_NOTEBOOK(npp->_notebook),
                                                   (p + 1) % npp->pageCount());
                }
                return TRUE;
            default: break;
        }
    }
    if (ev->keyval == GDK_KEY_F3) {
        if (shft) cbFindPrev(nullptr, npp);
        else      cbFindNext(nullptr, npp);
        return TRUE;
    }
    // F2 / Shift+F2: bookmark navigation (only when find dialog not focused)
    if (ev->keyval == GDK_KEY_F2 && !ctrl) {
        if (shft) cbPrevBookmark(nullptr, npp);
        else      cbNextBookmark(nullptr, npp);
        return TRUE;
    }
    if (ev->keyval == GDK_KEY_Escape && npp->_findDlg->isVisible()) {
        npp->_findDlg->hide();
        return TRUE;
    }
    return FALSE;
}

// ---------------------------------------------------------------------------
// Drag and drop (files dropped onto window)
// ---------------------------------------------------------------------------

void NotepadPlusGtk::cbDragData(GtkWidget*, GdkDragContext*, gint, gint,
                                  GtkSelectionData* selData,
                                  guint, guint time, gpointer d)
{
    auto* npp = static_cast<NotepadPlusGtk*>(d);
    gchar** uris = gtk_selection_data_get_uris(selData);
    if (!uris) return;
    for (int i = 0; uris[i]; ++i) {
        gchar* path = g_filename_from_uri(uris[i], nullptr, nullptr);
        if (path) {
            npp->openFile(path);
            g_free(path);
        }
    }
    g_strfreev(uris);
}

// ---------------------------------------------------------------------------
// Phase 1 callbacks — Line operations
// ---------------------------------------------------------------------------

void NotepadPlusGtk::cbDuplicateLine(GtkMenuItem*, gpointer d) {
    auto* v = ((NotepadPlusGtk*)d)->currentView(); if (v) v->duplicateLine();
}
void NotepadPlusGtk::cbDeleteLine(GtkMenuItem*, gpointer d) {
    auto* v = ((NotepadPlusGtk*)d)->currentView(); if (v) v->deleteLine();
}
void NotepadPlusGtk::cbMoveLineUp(GtkMenuItem*, gpointer d) {
    auto* v = ((NotepadPlusGtk*)d)->currentView(); if (v) v->moveSelectedLinesUp();
}
void NotepadPlusGtk::cbMoveLineDown(GtkMenuItem*, gpointer d) {
    auto* v = ((NotepadPlusGtk*)d)->currentView(); if (v) v->moveSelectedLinesDown();
}

// ---------------------------------------------------------------------------
// Phase 1 callbacks — Extended case conversions
// ---------------------------------------------------------------------------

void NotepadPlusGtk::cbToTitleCase(GtkMenuItem*, gpointer d) {
    auto* v = ((NotepadPlusGtk*)d)->currentView(); if (v) v->toTitleCase();
}
void NotepadPlusGtk::cbToCamelCase(GtkMenuItem*, gpointer d) {
    auto* v = ((NotepadPlusGtk*)d)->currentView(); if (v) v->toCamelCase();
}
void NotepadPlusGtk::cbToSnakeCase(GtkMenuItem*, gpointer d) {
    auto* v = ((NotepadPlusGtk*)d)->currentView(); if (v) v->toSnakeCase();
}

// ---------------------------------------------------------------------------
// Phase 1 callbacks — Comment / Uncomment
// ---------------------------------------------------------------------------

void NotepadPlusGtk::cbToggleComment(GtkMenuItem*, gpointer d) {
    auto* v = ((NotepadPlusGtk*)d)->currentView(); if (v) v->toggleLineComment();
}

// ---------------------------------------------------------------------------
// Phase 1 callbacks — Whitespace
// ---------------------------------------------------------------------------

void NotepadPlusGtk::cbTrimTrailing(GtkMenuItem*, gpointer d) {
    auto* v = ((NotepadPlusGtk*)d)->currentView(); if (v) v->trimTrailingWhitespace();
}
void NotepadPlusGtk::cbTrimLeading(GtkMenuItem*, gpointer d) {
    auto* v = ((NotepadPlusGtk*)d)->currentView(); if (v) v->trimLeadingWhitespace();
}
void NotepadPlusGtk::cbTrimAll(GtkMenuItem*, gpointer d) {
    auto* v = ((NotepadPlusGtk*)d)->currentView(); if (v) v->trimAllWhitespace();
}
void NotepadPlusGtk::cbTabsToSpaces(GtkMenuItem*, gpointer d) {
    auto* v = ((NotepadPlusGtk*)d)->currentView(); if (v) v->tabsToSpaces();
}
void NotepadPlusGtk::cbSpacesToTabs(GtkMenuItem*, gpointer d) {
    auto* v = ((NotepadPlusGtk*)d)->currentView(); if (v) v->spacesToTabs();
}

// ---------------------------------------------------------------------------
// Phase 1 callbacks — EOL conversion
// ---------------------------------------------------------------------------

void NotepadPlusGtk::cbEOLtoCRLF(GtkMenuItem*, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    auto* v   = npp->currentView();
    if (!v) return;
    v->convertEOLs(SC_EOL_CRLF);
    npp->updateStatusBar();
}
void NotepadPlusGtk::cbEOLtoLF(GtkMenuItem*, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    auto* v   = npp->currentView();
    if (!v) return;
    v->convertEOLs(SC_EOL_LF);
    npp->updateStatusBar();
}
void NotepadPlusGtk::cbEOLtoCR(GtkMenuItem*, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    auto* v   = npp->currentView();
    if (!v) return;
    v->convertEOLs(SC_EOL_CR);
    npp->updateStatusBar();
}

// ---------------------------------------------------------------------------
// Phase 1 callbacks — Bookmarks
// ---------------------------------------------------------------------------

void NotepadPlusGtk::cbToggleBookmark(GtkMenuItem*, gpointer d) {
    auto* v = ((NotepadPlusGtk*)d)->currentView(); if (v) v->toggleBookmark();
}
void NotepadPlusGtk::cbNextBookmark(GtkMenuItem*, gpointer d) {
    auto* v = ((NotepadPlusGtk*)d)->currentView(); if (v) v->gotoNextBookmark();
}
void NotepadPlusGtk::cbPrevBookmark(GtkMenuItem*, gpointer d) {
    auto* v = ((NotepadPlusGtk*)d)->currentView(); if (v) v->gotoPrevBookmark();
}
void NotepadPlusGtk::cbClearBookmarks(GtkMenuItem*, gpointer d) {
    auto* v = ((NotepadPlusGtk*)d)->currentView(); if (v) v->clearAllBookmarks();
}

// ---------------------------------------------------------------------------
// Phase 2: Find in Files — results panel
// ---------------------------------------------------------------------------

// Column indices for the results GtkListStore
enum { RES_COL_DISPLAY = 0, RES_COL_PATH, RES_COL_LINE, RES_COL_COUNT };

GtkWidget* NotepadPlusGtk::buildResultsPanel() {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(box, -1, 180);

    // Header bar: label + close button
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_name(header, "results-header");
    gtk_box_pack_start(GTK_BOX(box), header, FALSE, FALSE, 0);

    _resultsHeaderLabel = gtk_label_new("Search Results");
    gtk_widget_set_margin_start(_resultsHeaderLabel, 6);
    gtk_box_pack_start(GTK_BOX(header), _resultsHeaderLabel, TRUE, TRUE, 0);
    gtk_widget_set_halign(_resultsHeaderLabel, GTK_ALIGN_START);

    GtkWidget* closeBtn = gtk_button_new_with_label("✕");
    gtk_button_set_relief(GTK_BUTTON(closeBtn), GTK_RELIEF_NONE);
    gtk_box_pack_end(GTK_BOX(header), closeBtn, FALSE, FALSE, 2);
    g_signal_connect(closeBtn, "clicked",
                     G_CALLBACK(+[](GtkButton*, gpointer d) {
                         ((NotepadPlusGtk*)d)->showResultsPanel(false);
                     }), this);

    // Scrolled window + tree view
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

    _resultsStore = gtk_list_store_new(RES_COL_COUNT,
                                        G_TYPE_STRING,   // display
                                        G_TYPE_STRING,   // path
                                        G_TYPE_INT);     // line (0-based)

    _resultsView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(_resultsStore));
    g_object_unref(_resultsStore);   // view holds the ref now
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(_resultsView), FALSE);
    gtk_tree_view_set_activate_on_single_click(GTK_TREE_VIEW(_resultsView), FALSE);

    GtkCellRenderer* cell = gtk_cell_renderer_text_new();
    GtkTreeViewColumn* col = gtk_tree_view_column_new_with_attributes(
        "Result", cell, "markup", RES_COL_DISPLAY, nullptr);
    gtk_tree_view_append_column(GTK_TREE_VIEW(_resultsView), col);

    g_signal_connect(_resultsView, "row-activated",
                     G_CALLBACK(cbResultRowActivated), this);

    gtk_container_add(GTK_CONTAINER(scroll), _resultsView);

    gtk_widget_show_all(box);
    gtk_widget_hide(box);   // hidden by default
    return box;
}

void NotepadPlusGtk::showResultsPanel(bool show) {
    if (show) gtk_widget_show(_resultsPanel);
    else      gtk_widget_hide(_resultsPanel);
}

void NotepadPlusGtk::clearResults() {
    gtk_list_store_clear(_resultsStore);
}

void NotepadPlusGtk::addResult(const std::string& path, int line,
                                 const std::string& text) {
    // Build display markup: "filename:line+1  content" (plain, escaped for Pango)
    std::string display = pangoEscape(fs::path(path).filename().string() +
                          ":" + std::to_string(line + 1) +
                          "  " + text);
    GtkTreeIter iter;
    gtk_list_store_append(_resultsStore, &iter);
    gtk_list_store_set(_resultsStore, &iter,
                       RES_COL_DISPLAY, display.c_str(),
                       RES_COL_PATH,    path.c_str(),
                       RES_COL_LINE,    line,
                       -1);
}

void NotepadPlusGtk::addResultRaw(const std::string& display,
                                   const std::string& path, int line) {
    GtkTreeIter iter;
    gtk_list_store_append(_resultsStore, &iter);
    gtk_list_store_set(_resultsStore, &iter,
                       RES_COL_DISPLAY, display.c_str(),
                       RES_COL_PATH,    path.c_str(),
                       RES_COL_LINE,    line,
                       -1);
}

void NotepadPlusGtk::setResultsHeader(const std::string& text) {
    if (_resultsHeaderLabel)
        gtk_label_set_text(GTK_LABEL(_resultsHeaderLabel), text.c_str());
}

void NotepadPlusGtk::cbResultRowActivated(GtkTreeView* tv, GtkTreePath* path,
                                            GtkTreeViewColumn*, gpointer d) {
    auto* npp   = static_cast<NotepadPlusGtk*>(d);
    auto* model = gtk_tree_view_get_model(tv);
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter(model, &iter, path)) return;

    gchar* filePath = nullptr;
    gint   line     = 0;
    gtk_tree_model_get(model, &iter,
                       RES_COL_PATH, &filePath,
                       RES_COL_LINE, &line, -1);
    if (line >= 0) {
        if (filePath && filePath[0] != '\0') {
            npp->openFileAtLine(filePath, line);
        } else {
            // Unsaved file: jump to line in the currently active view
            ScintillaView* v = npp->currentView();
            if (v) { v->gotoLine(line); v->ensureCaretVisible(); }
        }
    }
    g_free(filePath);
}

void NotepadPlusGtk::openFileAtLine(const std::string& path, int line) {
    openFile(path);
    ScintillaView* v = currentView();
    if (v) {
        v->gotoLine(line);
        v->ensureCaretVisible();
    }
}

// ---------------------------------------------------------------------------
// Phase 2: Find in Files callback
// ---------------------------------------------------------------------------

void NotepadPlusGtk::cbFindInFiles(GtkMenuItem*, gpointer d) {
    auto* npp = static_cast<NotepadPlusGtk*>(d);
    npp->clearResults();
    npp->setResultsHeader("Find in Files Results");
    npp->_findInFilesDlg->show();
}

// ---------------------------------------------------------------------------
// Phase 3: Panel visibility helpers
// ---------------------------------------------------------------------------

void NotepadPlusGtk::updateRightPanelVisibility() {
    bool fl = _funcList && gtk_widget_get_visible(_funcList->getWidget());
    bool dm = _docMap   && gtk_widget_get_visible(_docMap->getWidget());
    if (fl || dm) gtk_widget_show(_rightVPaned);
    else          gtk_widget_hide(_rightVPaned);
}

// ---------------------------------------------------------------------------
// Phase 3: View callbacks — panels
// ---------------------------------------------------------------------------

void NotepadPlusGtk::cbShowWorkspace(GtkCheckMenuItem* mi, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    bool on = gtk_check_menu_item_get_active(mi);
    if (on) gtk_widget_show(npp->_workspacePanel->getWidget());
    else    gtk_widget_hide(npp->_workspacePanel->getWidget());
}

void NotepadPlusGtk::cbShowFuncList(GtkCheckMenuItem* mi, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    bool on = gtk_check_menu_item_get_active(mi);
    if (on) {
        gtk_widget_show(npp->_funcList->getWidget());
        // Populate with current document
        if (auto* doc = npp->currentDoc())
            npp->_funcList->update(doc->view, doc->lexerType);
    } else {
        gtk_widget_hide(npp->_funcList->getWidget());
    }
    npp->updateRightPanelVisibility();
}

void NotepadPlusGtk::cbShowDocMap(GtkCheckMenuItem* mi, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    bool on = gtk_check_menu_item_get_active(mi);
    if (on) {
        gtk_widget_show(npp->_docMap->getWidget());
        npp->_docMap->update(npp->currentView());
    } else {
        gtk_widget_hide(npp->_docMap->getWidget());
    }
    npp->updateRightPanelVisibility();
}

void NotepadPlusGtk::cbSplitView(GtkCheckMenuItem* mi, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    bool on = gtk_check_menu_item_get_active(mi);
    if (on) {
        gtk_widget_show(npp->_splitEditor->getWidget());
        // Share the current document with the split editor
        auto* v = npp->currentView();
        if (v) {
            sptr_t docPtr = scintilla_send_message(
                SCINTILLA(v->getWidget()), SCI_GETDOCPOINTER, 0, 0);
            scintilla_send_message(
                SCINTILLA(npp->_splitEditor->getWidget()),
                SCI_SETDOCPOINTER, 0, docPtr);
        }
    } else {
        gtk_widget_hide(npp->_splitEditor->getWidget());
        // Detach from shared document (give split editor its own empty document)
        scintilla_send_message(
            SCINTILLA(npp->_splitEditor->getWidget()), SCI_SETDOCPOINTER, 0, 0);
    }
}

void NotepadPlusGtk::cbSetWorkspaceDir(GtkMenuItem*, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    GtkWidget* dlg = gtk_file_chooser_dialog_new(
        "Select Workspace Directory", GTK_WINDOW(npp->_window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Select", GTK_RESPONSE_ACCEPT,
        nullptr);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char* dir = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (dir) {
            npp->_workspacePanel->setRootDirectory(dir);
            // Auto-show workspace panel
            gtk_widget_show(npp->_workspacePanel->getWidget());
            if (npp->_miShowWorkspace)
                gtk_check_menu_item_set_active(
                    GTK_CHECK_MENU_ITEM(npp->_miShowWorkspace), TRUE);
            g_free(dir);
        }
    }
    gtk_widget_destroy(dlg);
}

// ---------------------------------------------------------------------------
// Phase 3: File monitor callback
// ---------------------------------------------------------------------------

void NotepadPlusGtk::cbFileChanged(GFileMonitor*, GFile*, GFile*,
                                    GFileMonitorEvent ev, gpointer d) {
    // Only react when the write is complete (not on every intermediate change)
    if (ev != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT) return;

    auto* md  = static_cast<FileMonitorData*>(d);
    auto* npp = md->npp;

    // Guard against re-entrant dialogs
    if (npp->_fileChangePending) return;
    npp->_fileChangePending = true;

    // Find the document with this path
    for (int i = 0; i < (int)npp->_docs.size(); ++i) {
        if (npp->_docs[i].filePath != md->path) continue;
        auto* doc = &npp->_docs[i];

        std::string msg = "\"" + doc->tabTitle +
                          "\" was modified externally.\nReload?";
        GtkWidget* dlg = gtk_message_dialog_new(
            GTK_WINDOW(npp->_window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
            "%s", msg.c_str());
        int resp = gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);

        if (resp == GTK_RESPONSE_YES)
            npp->loadFileIntoDoc(i, doc->filePath);
        break;
    }
    npp->_fileChangePending = false;
}

// ---------------------------------------------------------------------------
// Phase 4: plugin helpers
// ---------------------------------------------------------------------------

std::string NotepadPlusGtk::getPluginsDir() const {
    // Plugins are expected in <executable_dir>/plugins/  or
    // ~/.config/notepad++/plugins/ (whichever exists first).
    // For the installed/development build, put them next to the binary.
    std::string exeDir;
    {
        char buf[4096] = {};
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = '\0';
            std::string p(buf);
            auto slash = p.rfind('/');
            if (slash != std::string::npos) exeDir = p.substr(0, slash);
        }
    }
    if (!exeDir.empty()) {
        std::string candidate = exeDir + "/plugins";
        if (fs::exists(candidate)) return candidate;
    }
    // Fallback: ~/.config/notepad++/plugins
    const char* home = getenv("HOME");
    if (home) return std::string(home) + "/.config/notepad++/plugins";
    return "/tmp/notepad++_plugins";
}

void NotepadPlusGtk::populatePluginsMenu() {
    if (!_pluginsMenuItem || !_pluginManager) return;

    // Replace the submenu with a freshly built one
    GtkWidget* newMenu = _pluginManager->buildPluginsMenu();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(_pluginsMenuItem), newMenu);
    gtk_widget_show(_pluginsMenuItem);
}

// ---------------------------------------------------------------------------
// Phase 5: Macro callbacks
// ---------------------------------------------------------------------------

void NotepadPlusGtk::cbMacroRecord(GtkCheckMenuItem* item, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    bool rec  = gtk_check_menu_item_get_active(item);

    if (rec) {
        npp->_macroManager->startRecord();
        // Enable SCN_MACRORECORD on all open views
        for (auto& doc : npp->_docs)
            if (doc.view) doc.view->replayMessage(SCI_STARTRECORD);
    } else {
        npp->_macroManager->stopRecord();
        // Disable SCN_MACRORECORD on all open views
        for (auto& doc : npp->_docs)
            if (doc.view) doc.view->replayMessage(SCI_STOPRECORD);
    }
    npp->updateStatusBar();
}

void NotepadPlusGtk::cbMacroPlay(GtkMenuItem*, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    if (!npp->_macroManager->hasRecording()) {
        GtkWidget* dlg = gtk_message_dialog_new(
            GTK_WINDOW(npp->_window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
            "No macro has been recorded yet.");
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        return;
    }
    npp->_macroManager->play(npp->currentView(), 1);
}

void NotepadPlusGtk::cbMacroPlayN(GtkMenuItem*, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    if (!npp->_macroManager->hasRecording()) {
        GtkWidget* dlg = gtk_message_dialog_new(
            GTK_WINDOW(npp->_window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
            "No macro has been recorded yet.");
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        return;
    }

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Play Macro N Times", GTK_WINDOW(npp->_window), GTK_DIALOG_MODAL,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Play",   GTK_RESPONSE_ACCEPT,
        nullptr);
    GtkWidget* box  = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("Repeat count:"), FALSE, FALSE, 0);
    GtkWidget* spin = gtk_spin_button_new_with_range(1, 9999, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), 1);
    gtk_box_pack_start(GTK_BOX(hbox), spin, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), hbox, FALSE, FALSE, 0);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        int n = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
        npp->_macroManager->play(npp->currentView(), n);
    }
    gtk_widget_destroy(dlg);
}

void NotepadPlusGtk::cbMacroSave(GtkMenuItem*, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    if (!npp->_macroManager->hasRecording()) {
        GtkWidget* dlg = gtk_message_dialog_new(
            GTK_WINDOW(npp->_window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
            "No macro has been recorded yet.");
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        return;
    }

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Save Macro", GTK_WINDOW(npp->_window), GTK_DIALOG_MODAL,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save",   GTK_RESPONSE_ACCEPT,
        nullptr);
    GtkWidget* box  = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new("Macro name:"), FALSE, FALSE, 0);
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "e.g. FormatCode");
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), hbox, FALSE, FALSE, 0);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        const char* name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (name && *name)
            npp->_macroManager->saveNamed(name);
    }
    gtk_widget_destroy(dlg);
}

void NotepadPlusGtk::cbMacroManage(GtkMenuItem*, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Manage Macros", GTK_WINDOW(npp->_window), GTK_DIALOG_MODAL,
        "_Close", GTK_RESPONSE_CLOSE,
        nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 350, 300);
    GtkWidget* box = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(box), 8);

    // List of saved macros
    GtkListStore* store = gtk_list_store_new(1, G_TYPE_STRING);
    for (const auto& name : npp->_macroManager->getNamedMacros()) {
        GtkTreeIter it;
        gtk_list_store_append(store, &it);
        gtk_list_store_set(store, &it, 0, name.c_str(), -1);
    }
    GtkWidget* tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    GtkCellRenderer* rend = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
        gtk_tree_view_column_new_with_attributes(
            "Macro Name", rend, "text", 0, nullptr));

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), tree);

    GtkWidget* btnBox  = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget* btnPlay = gtk_button_new_with_mnemonic("_Play");
    GtkWidget* btnDel  = gtk_button_new_with_mnemonic("_Delete");
    gtk_box_pack_start(GTK_BOX(btnBox), btnPlay, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btnBox), btnDel,  FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE,  TRUE,  0);
    gtk_box_pack_start(GTK_BOX(box), btnBox, FALSE, FALSE, 4);

    // Callback data owned by the dialog
    struct ManageData { NotepadPlusGtk* npp; GtkWidget* tree; GtkListStore* store; };
    auto* md = new ManageData{npp, tree, store};
    g_object_set_data_full(G_OBJECT(dlg), "manage-data", md,
        (GDestroyNotify)+[](gpointer p){ delete static_cast<ManageData*>(p); });

    g_signal_connect(btnPlay, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer p) {
            auto* md  = static_cast<ManageData*>(p);
            GtkTreeSelection* sel = gtk_tree_view_get_selection(
                GTK_TREE_VIEW(md->tree));
            GtkTreeIter it; GtkTreeModel* mdl;
            if (!gtk_tree_selection_get_selected(sel, &mdl, &it)) return;
            gchar* name;
            gtk_tree_model_get(mdl, &it, 0, &name, -1);
            md->npp->_macroManager->playNamed(name, md->npp->currentView(), 1);
            g_free(name);
        }), md);

    g_signal_connect(btnDel, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer p) {
            auto* md  = static_cast<ManageData*>(p);
            GtkTreeSelection* sel = gtk_tree_view_get_selection(
                GTK_TREE_VIEW(md->tree));
            GtkTreeIter it; GtkTreeModel* mdl;
            if (!gtk_tree_selection_get_selected(sel, &mdl, &it)) return;
            gchar* name;
            gtk_tree_model_get(mdl, &it, 0, &name, -1);
            md->npp->_macroManager->deleteNamed(name);
            gtk_list_store_remove(md->store, &it);
            g_free(name);
        }), md);

    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

// ---------------------------------------------------------------------------
// Phase 5: Tool callbacks
// ---------------------------------------------------------------------------

void NotepadPlusGtk::cbRunProgram(GtkMenuItem*, gpointer d) {
    auto* npp = (NotepadPlusGtk*)d;
    auto* doc = npp->currentDoc();
    auto* v   = npp->currentView();

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Run Program", GTK_WINDOW(npp->_window), GTK_DIALOG_MODAL,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Run",    GTK_RESPONSE_ACCEPT,
        nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 520, 160);
    GtkWidget* box = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);

    GtkWidget* cmdLabel = gtk_label_new("Command:");
    gtk_widget_set_halign(cmdLabel, GTK_ALIGN_START);
    GtkWidget* cmdEntry = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(cmdEntry), TRUE);

    const char* varHelp =
        "Variables: $(FULL_CURRENT_PATH)  $(FILE_NAME)"
        "  $(CURRENT_DIRECTORY)  $(CURRENT_WORD)";
    GtkWidget* helpLabel = gtk_label_new(varHelp);
    gtk_label_set_line_wrap(GTK_LABEL(helpLabel), TRUE);
    gtk_widget_set_halign(helpLabel, GTK_ALIGN_START);
    // Small italic font for the hint
    PangoAttrList* attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_scale_new(PANGO_SCALE_SMALL));
    pango_attr_list_insert(attrs, pango_attr_style_new(PANGO_STYLE_ITALIC));
    gtk_label_set_attributes(GTK_LABEL(helpLabel), attrs);
    pango_attr_list_unref(attrs);

    gtk_box_pack_start(GTK_BOX(box), cmdLabel,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), cmdEntry,  FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(box), helpLabel, FALSE, FALSE, 4);

    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        std::string cmd = gtk_entry_get_text(GTK_ENTRY(cmdEntry));

        // Variable substitution
        auto replaceVar = [&](const char* var, const std::string& val) {
            std::string::size_type pos;
            while ((pos = cmd.find(var)) != std::string::npos)
                cmd.replace(pos, std::strlen(var), val);
        };

        std::string fullPath = doc ? doc->filePath : "";
        std::string fileName = fullPath.empty() ? ""
                             : fs::path(fullPath).filename().string();
        std::string curDir   = fullPath.empty() ? ""
                             : fs::path(fullPath).parent_path().string();
        std::string curWord  = (v && !v->getSelectedText().empty())
                             ? v->getSelectedText() : "";

        replaceVar("$(FULL_CURRENT_PATH)", fullPath);
        replaceVar("$(FILE_NAME)",         fileName);
        replaceVar("$(CURRENT_DIRECTORY)", curDir);
        replaceVar("$(CURRENT_WORD)",      curWord);

        if (!cmd.empty()) {
            GError* err = nullptr;
            g_spawn_command_line_async(cmd.c_str(), &err);
            if (err) {
                GtkWidget* edlg = gtk_message_dialog_new(
                    GTK_WINDOW(npp->_window), GTK_DIALOG_MODAL,
                    GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                    "Failed to run:\n%s\n\n%s", cmd.c_str(), err->message);
                gtk_dialog_run(GTK_DIALOG(edlg));
                gtk_widget_destroy(edlg);
                g_error_free(err);
            }
        }
    }
    gtk_widget_destroy(dlg);
}

// Shared implementation for MD5 and SHA-256 dialogs
void NotepadPlusGtk::doChecksum(NotepadPlusGtk* npp,
                                 GChecksumType type, const char* title) {
    auto* v = npp->currentView();
    std::string input;
    bool usedSelection = false;
    if (v) {
        input = v->getSelectedText();
        if (!input.empty()) {
            usedSelection = true;
        } else {
            input = v->getText();
        }
    }

    GChecksum* cs = g_checksum_new(type);
    g_checksum_update(cs, reinterpret_cast<const guchar*>(input.data()),
                      static_cast<gssize>(input.size()));
    std::string digest(g_checksum_get_string(cs));
    g_checksum_free(cs);

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        title, GTK_WINDOW(npp->_window), GTK_DIALOG_MODAL,
        "_Close", GTK_RESPONSE_CLOSE,
        nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 480, 120);
    GtkWidget* box = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);

    std::string labelText = std::string(title) + " of " +
                            (usedSelection ? "selection" : "document") + ":";
    gtk_box_pack_start(GTK_BOX(box),
        gtk_label_new(labelText.c_str()), FALSE, FALSE, 0);

    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), digest.c_str());
    gtk_editable_set_editable(GTK_EDITABLE(entry), FALSE);
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 4);

    GtkWidget* btnCopy = gtk_button_new_with_mnemonic("_Copy to Clipboard");
    g_signal_connect_data(btnCopy, "clicked",
        G_CALLBACK(+[](GtkButton*, gpointer p) {
            GtkClipboard* cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
            gtk_clipboard_set_text(cb, static_cast<const char*>(p), -1);
        }),
        g_strdup(digest.c_str()),
        (GClosureNotify)g_free, (GConnectFlags)0);
    gtk_box_pack_start(GTK_BOX(box), btnCopy, FALSE, FALSE, 4);

    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

void NotepadPlusGtk::cbMd5(GtkMenuItem*, gpointer d) {
    doChecksum(static_cast<NotepadPlusGtk*>(d), G_CHECKSUM_MD5, "MD5");
}

void NotepadPlusGtk::cbSha256(GtkMenuItem*, gpointer d) {
    doChecksum(static_cast<NotepadPlusGtk*>(d), G_CHECKSUM_SHA256, "SHA-256");
}

// ---------------------------------------------------------------------------
// Phase 6: Preferences dialog
// ---------------------------------------------------------------------------

void NotepadPlusGtk::cbPreferences(GtkMenuItem*, gpointer d) {
    auto* npp = static_cast<NotepadPlusGtk*>(d);
    auto& cfg  = Parameters::getInstance().getSettings();

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Preferences",
        GTK_WINDOW(npp->_window),
        GTK_DIALOG_MODAL,
        "_OK",     GTK_RESPONSE_OK,
        "_Cancel", GTK_RESPONSE_CANCEL,
        nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 480, 420);

    GtkWidget* nb = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
                       nb, TRUE, TRUE, 8);

    // ---- Helper macros ----
    auto addTab = [&](const char* label) -> GtkWidget* {
        GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);
        GtkWidget* tabLabel = gtk_label_new(label);
        gtk_notebook_append_page(GTK_NOTEBOOK(nb), vbox, tabLabel);
        return vbox;
    };
    auto addRow = [](GtkWidget* grid, int row, const char* lbl, GtkWidget* w) {
        GtkWidget* label = gtk_label_new(lbl);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
        gtk_widget_set_hexpand(w, TRUE);
        gtk_grid_attach(GTK_GRID(grid), w, 1, row, 1, 1);
    };
    auto mkSpin = [](double mn, double mx, double val) -> GtkWidget* {
        GtkWidget* w = gtk_spin_button_new_with_range(mn, mx, 1.0);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), val);
        return w;
    };

    // ==========================================================
    // Tab 1: General
    // ==========================================================
    GtkWidget* genVBox = addTab("General");
    GtkWidget* genGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(genGrid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(genGrid), 16);
    gtk_box_pack_start(GTK_BOX(genVBox), genGrid, FALSE, FALSE, 0);

    GtkWidget* wTabWidth  = mkSpin(1, 16, cfg.tabWidth);
    GtkWidget* wUseSpaces = gtk_check_button_new_with_label("Use spaces instead of Tab");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wUseSpaces), cfg.tabUseSpaces);
    GtkWidget* wAutoInd   = gtk_check_button_new_with_label("Auto-indent");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wAutoInd), cfg.autoIndent);
    GtkWidget* wMaxRecent = mkSpin(1, 30, cfg.maxRecentFiles);
    GtkWidget* wRestore   = gtk_check_button_new_with_label("Restore previous session on startup");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wRestore), cfg.restoreSession);

    addRow(genGrid, 0, "Tab width:", wTabWidth);
    gtk_grid_attach(GTK_GRID(genGrid), wUseSpaces,  0, 1, 2, 1);
    gtk_grid_attach(GTK_GRID(genGrid), wAutoInd,    0, 2, 2, 1);
    addRow(genGrid, 3, "Max recent files:", wMaxRecent);
    gtk_grid_attach(GTK_GRID(genGrid), wRestore,    0, 4, 2, 1);

    // ==========================================================
    // Tab 2: View
    // ==========================================================
    GtkWidget* viewVBox = addTab("View");
    GtkWidget* viewGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(viewGrid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(viewGrid), 16);
    gtk_box_pack_start(GTK_BOX(viewVBox), viewGrid, FALSE, FALSE, 0);

    GtkWidget* wWordWrap   = gtk_check_button_new_with_label("Word wrap");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wWordWrap), cfg.wordWrap);
    GtkWidget* wLineNums   = gtk_check_button_new_with_label("Show line numbers");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wLineNums), cfg.showLineNumbers);
    GtkWidget* wWhitespace = gtk_check_button_new_with_label("Show whitespace characters");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wWhitespace), cfg.showWhitespace);
    GtkWidget* wStatusBar  = gtk_check_button_new_with_label("Show status bar");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wStatusBar), cfg.showStatusBar);
    GtkWidget* wToolbar    = gtk_check_button_new_with_label("Show toolbar");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wToolbar), cfg.showToolbar);
    GtkWidget* wEdge       = gtk_check_button_new_with_label("Show edge column");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wEdge), cfg.edgeEnabled);
    GtkWidget* wEdgeCol    = mkSpin(1, 500, cfg.edgeColumn);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(wEdgeCol), cfg.edgeColumn);

    gtk_grid_attach(GTK_GRID(viewGrid), wWordWrap,   0, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(viewGrid), wLineNums,   0, 1, 2, 1);
    gtk_grid_attach(GTK_GRID(viewGrid), wWhitespace, 0, 2, 2, 1);
    gtk_grid_attach(GTK_GRID(viewGrid), wStatusBar,  0, 3, 2, 1);
    gtk_grid_attach(GTK_GRID(viewGrid), wToolbar,    0, 4, 2, 1);
    gtk_grid_attach(GTK_GRID(viewGrid), wEdge,       0, 5, 2, 1);
    addRow(viewGrid, 6, "Edge column position:", wEdgeCol);

    // ==========================================================
    // Tab 3: Style
    // ==========================================================
    GtkWidget* styleVBox = addTab("Style");
    GtkWidget* styleGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(styleGrid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(styleGrid), 16);
    gtk_box_pack_start(GTK_BOX(styleVBox), styleGrid, FALSE, FALSE, 0);

    // Build font descriptor string for GtkFontButton (e.g. "Monospace 11")
    std::string fontDesc = cfg.fontName + " " + std::to_string(cfg.fontSize);
    GtkWidget* wFont = gtk_font_button_new_with_font(fontDesc.c_str());
    gtk_font_button_set_use_size(GTK_FONT_BUTTON(wFont), TRUE);
    gtk_font_button_set_show_style(GTK_FONT_BUTTON(wFont), FALSE);

    GtkWidget* wTheme = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(wTheme), "default", "Default (light)");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(wTheme), "dark",    "Dark");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(wTheme), "zenburn", "Zenburn");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(wTheme), cfg.theme.c_str());

    addRow(styleGrid, 0, "Font:", wFont);
    addRow(styleGrid, 1, "Theme:", wTheme);

    // ==========================================================
    // Tab 4: Search
    // ==========================================================
    GtkWidget* srchVBox = addTab("Search");
    GtkWidget* srchGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(srchGrid), 8);
    gtk_box_pack_start(GTK_BOX(srchVBox), srchGrid, FALSE, FALSE, 0);

    GtkWidget* wMatchCase   = gtk_check_button_new_with_label("Match case by default");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wMatchCase), cfg.searchMatchCase);
    GtkWidget* wWholeWord   = gtk_check_button_new_with_label("Whole word by default");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wWholeWord), cfg.searchWholeWord);
    GtkWidget* wRegex       = gtk_check_button_new_with_label("Regular expression by default");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wRegex), cfg.searchRegex);
    GtkWidget* wWrapAround  = gtk_check_button_new_with_label("Wrap around by default");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wWrapAround), cfg.searchWrapAround);

    gtk_grid_attach(GTK_GRID(srchGrid), wMatchCase,  0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(srchGrid), wWholeWord,  0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(srchGrid), wRegex,      0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(srchGrid), wWrapAround, 0, 3, 1, 1);

    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
        // --- General ---
        cfg.tabWidth     = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(wTabWidth));
        cfg.tabUseSpaces = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wUseSpaces));
        cfg.autoIndent   = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wAutoInd));
        cfg.maxRecentFiles = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(wMaxRecent));
        cfg.restoreSession = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wRestore));

        // --- View ---
        cfg.wordWrap       = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wWordWrap));
        cfg.showLineNumbers= gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wLineNums));
        cfg.showWhitespace = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wWhitespace));
        cfg.showStatusBar  = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wStatusBar));
        cfg.showToolbar    = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wToolbar));
        cfg.edgeEnabled    = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wEdge));
        cfg.edgeColumn     = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(wEdgeCol));

        // --- Style: font ---
        const gchar* fdesc = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(wFont));
        if (fdesc) {
            std::string fd = fdesc;
            // Font descriptor format: "Family Size" e.g. "Monospace 11"
            auto spacePos = fd.rfind(' ');
            if (spacePos != std::string::npos) {
                cfg.fontName = fd.substr(0, spacePos);
                try { cfg.fontSize = std::stoi(fd.substr(spacePos + 1)); } catch (...) {}
            }
            g_free(const_cast<gchar*>(fdesc));
        }
        // --- Style: theme ---
        const gchar* tid = gtk_combo_box_get_active_id(GTK_COMBO_BOX(wTheme));
        if (tid) cfg.theme = tid;

        // --- Search ---
        cfg.searchMatchCase  = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wMatchCase));
        cfg.searchWholeWord  = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wWholeWord));
        cfg.searchRegex      = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wRegex));
        cfg.searchWrapAround = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wWrapAround));

        // Apply to all views
        npp->applySettingsToAll();

        // Sync check-menu items and widgets
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(npp->_miWordWrap),
                                       cfg.wordWrap);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(npp->_miShowLineNums),
                                       cfg.showLineNumbers);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(npp->_miShowWhitespace),
                                       cfg.showWhitespace);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(npp->_miShowStatusBar),
                                       cfg.showStatusBar);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(npp->_miShowToolbar),
                                       cfg.showToolbar);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(npp->_miDarkTheme),
                                       cfg.theme == "dark");
        // Apply status/toolbar visibility
        if (cfg.showStatusBar) gtk_widget_show(npp->_statusBar);
        else                   gtk_widget_hide(npp->_statusBar);
        if (cfg.showToolbar)   gtk_widget_show(npp->_toolbar);
        else                   gtk_widget_hide(npp->_toolbar);
    }
    gtk_widget_destroy(dlg);
}

// ---------------------------------------------------------------------------
// Phase 6: Keyboard shortcuts reference dialog
// ---------------------------------------------------------------------------

void NotepadPlusGtk::cbKeyboardShortcuts(GtkMenuItem*, gpointer d) {
    auto* npp = static_cast<NotepadPlusGtk*>(d);

    GtkWidget* dlg = gtk_dialog_new_with_buttons(
        "Keyboard Shortcuts",
        GTK_WINDOW(npp->_window),
        GTK_DIALOG_MODAL,
        "_Close", GTK_RESPONSE_CLOSE,
        nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 450, 520);

    // Scrolled window + tree view
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
                       scroll, TRUE, TRUE, 4);

    GtkListStore* store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);

    auto addCol = [&](const char* title, int colIdx) {
        GtkCellRenderer* r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* c = gtk_tree_view_column_new_with_attributes(
            title, r, "text", colIdx, nullptr);
        gtk_tree_view_column_set_resizable(c, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree), c);
    };
    addCol("Action", 0);
    addCol("Shortcut", 1);

    // Shortcut reference table
    static const std::pair<const char*, const char*> shortcuts[] = {
        { "New",                   "Ctrl+N"         },
        { "Open",                  "Ctrl+O"         },
        { "Save",                  "Ctrl+S"         },
        { "Save As",               "Ctrl+Shift+S"   },
        { "Save All",              "Ctrl+Shift+S"   },
        { "Close",                 "Ctrl+W"         },
        { "Exit",                  "Alt+F4"         },
        { "Undo",                  "Ctrl+Z"         },
        { "Redo",                  "Ctrl+Y"         },
        { "Cut",                   "Ctrl+X"         },
        { "Copy",                  "Ctrl+C"         },
        { "Paste",                 "Ctrl+V"         },
        { "Select All",            "Ctrl+A"         },
        { "Duplicate Line",        "Ctrl+D"         },
        { "Delete Line",           "Ctrl+Shift+L"   },
        { "Move Line Up",          "Ctrl+Shift+Up"  },
        { "Move Line Down",        "Ctrl+Shift+Down"},
        { "Toggle Comment",        "Ctrl+/"         },
        { "Toggle Uppercase",      "Ctrl+Shift+U"   },
        { "Toggle Lowercase",      "Ctrl+U"         },
        { "Preferences",           "(menu)"         },
        { "Find",                  "Ctrl+F"         },
        { "Find & Replace",        "Ctrl+H"         },
        { "Find Next",             "F3"             },
        { "Find Previous",         "Shift+F3"       },
        { "Find in Files",         "Ctrl+Shift+F"   },
        { "Go to Line",            "Ctrl+G"         },
        { "Toggle Bookmark",       "Ctrl+F2"        },
        { "Next Bookmark",         "F2"             },
        { "Previous Bookmark",     "Shift+F2"       },
        { "Zoom In",               "Ctrl++"         },
        { "Zoom Out",              "Ctrl+-"         },
        { "Reset Zoom",            "Ctrl+0"         },
        { "Switch Tab (next)",     "Ctrl+Tab"       },
        { "Record Macro",          "Ctrl+Shift+R"   },
        { "Play Back Macro",       "Ctrl+Shift+P"   },
        { "Run Program",           "Ctrl+F5"        },
    };

    for (const auto& [action, key] : shortcuts) {
        GtkTreeIter it;
        gtk_list_store_append(store, &it);
        gtk_list_store_set(store, &it, 0, action, 1, key, -1);
    }

    gtk_container_add(GTK_CONTAINER(scroll), tree);
    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

// ---------------------------------------------------------------------------
// Phase 6: Session save / restore
// ---------------------------------------------------------------------------

void NotepadPlusGtk::saveSession() {
    if (!Parameters::getInstance().getSettings().restoreSession) return;
    std::vector<SessionFile> files;
    for (const auto& doc : _docs)
        if (!doc.filePath.empty()) {
            SessionFile sf;
            sf.path       = doc.filePath;
            sf.cursorLine = doc.view ? doc.view->getCurrentLine() : 0;
            files.push_back(sf);
        }
    Parameters::getInstance().saveSession(files, currentPage());
}

void NotepadPlusGtk::restoreSession() {
    auto data = Parameters::getInstance().loadSession();
    if (data.files.empty()) return;

    for (const auto& sf : data.files) {
        if (!fs::exists(sf.path)) continue;  // silently skip missing files
        openFileAtLine(sf.path, sf.cursorLine);
    }

    int active = data.activeIdx;
    if (active >= 0 && active < (int)_docs.size())
        gtk_notebook_set_current_page(GTK_NOTEBOOK(_notebook), active);
}
