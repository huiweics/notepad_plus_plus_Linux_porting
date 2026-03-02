#pragma once
// Notepad++ Linux port - main GTK3 application window

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <string>
#include <vector>
#include <memory>
#include "ScintillaView.h"
#include "FindReplaceDlg.h"
#include "FindInFilesDlg.h"
#include "WorkspacePanel.h"
#include "FunctionList.h"
#include "DocumentMap.h"
#include "PluginManager.h"
#include "MacroManager.h"

struct Document {
    ScintillaView* view           = nullptr;
    std::string    filePath;                   // empty = untitled
    std::string    tabTitle;                   // shown in tab
    GtkWidget*     tabLabelWidget = nullptr;   // the GtkLabel inside the tab hbox
    bool           modified       = false;
    LexerType      lexerType      = LexerType::None;
    std::string    encoding       = "UTF-8";   // detected on open
    GFileMonitor*  fileMonitor    = nullptr;   // Phase 3: external change detection
};

class NotepadPlusGtk {
public:
    NotepadPlusGtk();
    ~NotepadPlusGtk();

    void init();
    void openFile(const std::string& path);
    void openFileAtLine(const std::string& path, int line);  // Phase 2

private:
    // === Widgets ===
    GtkWidget* _window   = nullptr;
    GtkWidget* _mainVBox = nullptr;
    GtkWidget* _menuBar  = nullptr;
    GtkWidget* _toolbar  = nullptr;
    GtkWidget* _notebook = nullptr;
    GtkWidget* _statusBar= nullptr;

    // Check-menu items reflecting app state
    GtkWidget* _miWordWrap       = nullptr;
    GtkWidget* _miShowWhitespace = nullptr;
    GtkWidget* _miShowLineNums   = nullptr;
    GtkWidget* _miShowStatusBar  = nullptr;
    GtkWidget* _miShowToolbar    = nullptr;
    GtkWidget* _miDarkTheme      = nullptr;
    // Phase 3 check-menu items
    GtkWidget* _miShowWorkspace  = nullptr;
    GtkWidget* _miShowFuncList   = nullptr;
    GtkWidget* _miShowDocMap     = nullptr;
    GtkWidget* _miSplitView      = nullptr;

    GtkWidget* _recentMenu = nullptr;   // rebuilt on demand

    std::vector<Document> _docs;
    int _untitledCount = 0;

    std::unique_ptr<FindReplaceDlg>   _findDlg;
    std::unique_ptr<FindInFilesDlg>   _findInFilesDlg;

    // Results panel (Find in Files / Find All)
    GtkWidget*    _resultsPanel       = nullptr;  // the collapsible bottom panel
    GtkWidget*    _resultsView        = nullptr;  // GtkTreeView
    GtkListStore* _resultsStore       = nullptr;  // model
    GtkWidget*    _resultsHeaderLabel = nullptr;  // dynamic title label

    // Phase 3 layout panes and panels
    GtkWidget* _outerHPaned  = nullptr;  // workspace | editor+right
    GtkWidget* _editorHPaned = nullptr;  // editor+results | right panels
    GtkWidget* _editorVPaned = nullptr;  // split+notebook | results
    GtkWidget* _splitHPaned  = nullptr;  // notebook | split editor
    GtkWidget* _rightVPaned  = nullptr;  // funclist | docmap

    // Phase 3 split view
    ScintillaView* _splitEditor  = nullptr;

    // Phase 3 panel classes
    std::unique_ptr<WorkspacePanel> _workspacePanel;
    std::unique_ptr<FunctionList>   _funcList;
    std::unique_ptr<DocumentMap>    _docMap;

    // Phase 3 file-change guard
    bool _fileChangePending = false;

    // Phase 4: plugin manager
    std::unique_ptr<PluginManager> _pluginManager;
    GtkWidget* _pluginsMenuItem = nullptr;   // top-level "Plugins" menu bar item

    // Phase 5: macro manager + UI
    std::unique_ptr<MacroManager> _macroManager;
    GtkWidget* _miMacroRecord = nullptr;     // check menu item "Record Macro"

    // === UI construction ===
    GtkWidget* buildMenuBar();
    GtkWidget* buildToolbar();
    GtkWidget* buildStatusBar();
    GtkWidget* buildResultsPanel();
    GtkWidget* makeTabLabel(const std::string& title, int docIdx);

    GtkWidget* appendMenu(GtkWidget* bar,  const char* label);
    GtkWidget* addItem   (GtkWidget* menu, const char* label,
                          GCallback cb, gpointer data = nullptr);
    GtkWidget* addCheck  (GtkWidget* menu, const char* label, bool active,
                          GCallback cb, gpointer data = nullptr);
    void       addSep    (GtkWidget* menu);

    // === Documents ===
    int  newDocument();
    int  pageCount()  const;
    int  currentPage()  const;
    Document*      currentDoc();
    Document*      docAt(int idx);
    ScintillaView* currentView();

    void applySettingsToView(ScintillaView* v);
    void applySettingsToAll();

    bool loadFileIntoDoc(int idx, const std::string& path);
    bool saveDocument(int idx, bool forceDialog = false);
    bool saveAs(int idx);
    bool closeDocument(int idx, bool createIfEmpty = true); // false if user cancels
    bool closeAll();

    void updateTabLabel(int idx);
    void updateTitle();
    void updateStatusBar();
    void updateMenuChecks();
    void rebuildRecentMenu();

    std::string runOpenDialog();
    std::string runSaveDialog(const std::string& hint);

    // === Callbacks – file ===
    static void cbNew      (GtkMenuItem*, gpointer);
    static void cbOpen     (GtkMenuItem*, gpointer);
    static void cbSave     (GtkMenuItem*, gpointer);
    static void cbSaveAs   (GtkMenuItem*, gpointer);
    static void cbSaveAll  (GtkMenuItem*, gpointer);
    static void cbReload   (GtkMenuItem*, gpointer);
    static void cbClose    (GtkMenuItem*, gpointer);
    static void cbCloseAll (GtkMenuItem*, gpointer);
    static void cbExit     (GtkMenuItem*, gpointer);

    // === Callbacks – edit ===
    static void cbUndo      (GtkMenuItem*, gpointer);
    static void cbRedo      (GtkMenuItem*, gpointer);
    static void cbCut       (GtkMenuItem*, gpointer);
    static void cbCopy      (GtkMenuItem*, gpointer);
    static void cbPaste     (GtkMenuItem*, gpointer);
    static void cbDelete    (GtkMenuItem*, gpointer);
    static void cbSelectAll (GtkMenuItem*, gpointer);
    static void cbToUpper   (GtkMenuItem*, gpointer);
    static void cbToLower   (GtkMenuItem*, gpointer);
    static void cbIndent    (GtkMenuItem*, gpointer);
    static void cbUnindent  (GtkMenuItem*, gpointer);

    // Edit → Line operations
    static void cbDuplicateLine (GtkMenuItem*, gpointer);
    static void cbDeleteLine    (GtkMenuItem*, gpointer);
    static void cbMoveLineUp    (GtkMenuItem*, gpointer);
    static void cbMoveLineDown  (GtkMenuItem*, gpointer);

    // Edit → Extended case
    static void cbToTitleCase   (GtkMenuItem*, gpointer);
    static void cbToCamelCase   (GtkMenuItem*, gpointer);
    static void cbToSnakeCase   (GtkMenuItem*, gpointer);

    // Edit → Comment
    static void cbToggleComment (GtkMenuItem*, gpointer);

    // Edit → Whitespace
    static void cbTrimTrailing  (GtkMenuItem*, gpointer);
    static void cbTrimLeading   (GtkMenuItem*, gpointer);
    static void cbTrimAll       (GtkMenuItem*, gpointer);
    static void cbTabsToSpaces  (GtkMenuItem*, gpointer);
    static void cbSpacesToTabs  (GtkMenuItem*, gpointer);

    // Edit → EOL conversion
    static void cbEOLtoCRLF     (GtkMenuItem*, gpointer);
    static void cbEOLtoLF       (GtkMenuItem*, gpointer);
    static void cbEOLtoCR       (GtkMenuItem*, gpointer);

    // Search → Bookmarks
    static void cbToggleBookmark(GtkMenuItem*, gpointer);
    static void cbNextBookmark  (GtkMenuItem*, gpointer);
    static void cbPrevBookmark  (GtkMenuItem*, gpointer);
    static void cbClearBookmarks(GtkMenuItem*, gpointer);

    // === Callbacks – search ===
    static void cbFind         (GtkMenuItem*, gpointer);
    static void cbReplace      (GtkMenuItem*, gpointer);
    static void cbFindNext     (GtkMenuItem*, gpointer);
    static void cbFindPrev     (GtkMenuItem*, gpointer);
    static void cbGoToLine     (GtkMenuItem*, gpointer);
    static void cbFindInFiles  (GtkMenuItem*, gpointer);   // Phase 2

    // Results panel
    void showResultsPanel(bool show);
    void clearResults();
    void addResult   (const std::string& path, int line, const std::string& text);
    void addResultRaw(const std::string& display, const std::string& path, int line);
    void setResultsHeader(const std::string& text);
    static void cbResultRowActivated(GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer);

    // === Callbacks – view ===
    static void cbWordWrap      (GtkCheckMenuItem*, gpointer);
    static void cbShowWhitespace(GtkCheckMenuItem*, gpointer);
    static void cbShowLineNums  (GtkCheckMenuItem*, gpointer);
    static void cbShowStatusBar (GtkCheckMenuItem*, gpointer);
    static void cbShowToolbar   (GtkCheckMenuItem*, gpointer);
    static void cbDarkTheme     (GtkCheckMenuItem*, gpointer);
    static void cbZoomIn        (GtkMenuItem*, gpointer);
    static void cbZoomOut       (GtkMenuItem*, gpointer);
    static void cbZoomReset     (GtkMenuItem*, gpointer);
    // Phase 3 view callbacks
    static void cbShowWorkspace    (GtkCheckMenuItem*, gpointer);
    static void cbShowFuncList     (GtkCheckMenuItem*, gpointer);
    static void cbShowDocMap       (GtkCheckMenuItem*, gpointer);
    static void cbSplitView        (GtkCheckMenuItem*, gpointer);
    static void cbSetWorkspaceDir  (GtkMenuItem*, gpointer);
    static void cbFileChanged      (GFileMonitor*, GFile*, GFile*,
                                    GFileMonitorEvent, gpointer);

    void updateRightPanelVisibility();

    // === Callbacks – language ===
    // Data = pointer to heap-allocated LexerType (deleted after use)
    static void cbSetLang(GtkMenuItem*, gpointer);

    // === Callbacks – help ===
    static void cbAbout(GtkMenuItem*, gpointer);

    // === Window / notebook ===
    static gboolean cbWindowDelete(GtkWidget*, GdkEvent*, gpointer);
    static void     cbTabSwitched (GtkNotebook*, GtkWidget*, guint, gpointer);
    static gboolean cbKeyPress    (GtkWidget*, GdkEventKey*, gpointer);

    // === Drag and drop ===
    static void cbDragData(GtkWidget*, GdkDragContext*, gint, gint,
                           GtkSelectionData*, guint, guint, gpointer);

    // === Phase 4: plugins ===
    void populatePluginsMenu();
    std::string getPluginsDir() const;

    // === Phase 5: macro callbacks ===
    static void cbMacroRecord (GtkCheckMenuItem*, gpointer);
    static void cbMacroPlay   (GtkMenuItem*, gpointer);
    static void cbMacroPlayN  (GtkMenuItem*, gpointer);
    static void cbMacroSave   (GtkMenuItem*, gpointer);
    static void cbMacroManage (GtkMenuItem*, gpointer);

    // === Phase 5: tool callbacks ===
    static void cbRunProgram  (GtkMenuItem*, gpointer);
    static void cbMd5         (GtkMenuItem*, gpointer);
    static void cbSha256      (GtkMenuItem*, gpointer);

    // Helper shared by MD5 / SHA-256
    static void doChecksum(NotepadPlusGtk* npp, GChecksumType type, const char* title);

    // === Phase 6: preferences / session ===
    static void cbPreferences       (GtkMenuItem*, gpointer);
    static void cbKeyboardShortcuts (GtkMenuItem*, gpointer);
    void saveSession();
    void restoreSession();
};

// Heap-allocated data blocks for callbacks that need extra context
struct LangCallbackData {
    NotepadPlusGtk* npp;
    LexerType       type;
    std::string     name;
};

struct RecentCallbackData {
    NotepadPlusGtk* npp;
    std::string     path;
};

struct FileMonitorData {
    NotepadPlusGtk* npp;
    std::string     path;
};
