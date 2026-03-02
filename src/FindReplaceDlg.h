#pragma once
// Notepad++ Linux port - Find / Replace dialog (modeless GTK3 window)

#include <gtk/gtk.h>
#include <string>
#include <functional>

class ScintillaView;

enum class FindDirection { Forward, Backward };

struct FindOptions {
    std::string   findText;
    std::string   replaceText;
    bool          matchCase   = false;
    bool          wholeWord   = false;
    bool          regex       = false;
    bool          wrapAround  = true;
    FindDirection direction   = FindDirection::Forward;
};

class FindReplaceDlg {
public:
    using GetViewCb        = std::function<ScintillaView*()>;
    // Returns total replacements made across all docs; called with (find, replace, matchCase, wholeWord, regex)
    using ReplaceAllDocsCb = std::function<int(const std::string&, const std::string&,
                                               bool, bool, bool)>;
    using FindAllCb        = std::function<void(const FindOptions&)>;

    explicit FindReplaceDlg(GtkWindow* parent, GetViewCb getView);
    ~FindReplaceDlg();

    void showFind       (const std::string& selectedText = {});
    void showFindReplace(const std::string& selectedText = {});

    bool isVisible() const;
    void hide();
    void focusFindEntry();

    const FindOptions& getOptions() const { return _opts; }
    void setReplaceAllDocsCb(ReplaceAllDocsCb cb) { _replaceAllDocsCb = cb; }
    void setFindAllCb       (FindAllCb        cb) { _findAllCb        = cb; }

private:
    GtkWindow* _parent = nullptr;
    GtkWidget* _dialog = nullptr;
    GetViewCb  _getView;
    ReplaceAllDocsCb _replaceAllDocsCb;
    FindAllCb        _findAllCb;

    GtkWidget* _findEntry       = nullptr;
    GtkWidget* _replaceEntry    = nullptr;
    GtkWidget* _replaceLabel    = nullptr;
    GtkWidget* _matchCaseCheck  = nullptr;
    GtkWidget* _wholeWordCheck  = nullptr;
    GtkWidget* _regexCheck      = nullptr;
    GtkWidget* _wrapAroundCheck = nullptr;
    GtkWidget* _dirForward      = nullptr;
    GtkWidget* _dirBackward     = nullptr;
    GtkWidget* _replaceBtn      = nullptr;
    GtkWidget* _replaceAllBtn   = nullptr;
    GtkWidget* _replaceInAllBtn = nullptr;   // Phase 2: replace in all docs
    GtkWidget* _statusLabel     = nullptr;

    // Phase 2: action buttons (find mode)
    GtkWidget* _highlightAllBtn = nullptr;
    GtkWidget* _clearHlBtn      = nullptr;
    GtkWidget* _countBtn        = nullptr;
    GtkWidget* _selectAllBtn    = nullptr;
    GtkWidget* _bookmarkAllBtn  = nullptr;
    GtkWidget* _actionsFrame    = nullptr;   // shown in find mode only
    GtkWidget* _findAllBtn      = nullptr;

    FindOptions _opts;
    bool        _replaceMode = false;

    void buildDialog();
    void setReplaceMode(bool on);
    void syncFromWidgets();
    void setStatus(const std::string& msg, bool error = false);

    void doFindNext(bool backward = false);
    void doReplace();
    void doReplaceAll();
    void doHighlightAll();
    void doClearHighlights();
    void doCountAll();
    void doSelectAll();
    void doBookmarkAll();
    void doReplaceInAllDocs();
    void doFindAll();

    static void onFindNext      (GtkButton*, gpointer);
    static void onFindPrev      (GtkButton*, gpointer);
    static void onReplace       (GtkButton*, gpointer);
    static void onReplaceAll    (GtkButton*, gpointer);
    static void onReplaceInAll  (GtkButton*, gpointer);
    static void onClose         (GtkButton*, gpointer);
    static void onActivate      (GtkEntry*,  gpointer);
    static void onHighlightAll  (GtkButton*, gpointer);
    static void onClearHL       (GtkButton*, gpointer);
    static void onCountAll      (GtkButton*, gpointer);
    static void onSelectAll     (GtkButton*, gpointer);
    static void onBookmarkAll   (GtkButton*, gpointer);
    static void onFindAll       (GtkButton*, gpointer);
    static gboolean onKey    (GtkWidget*, GdkEventKey*, gpointer);
    static gboolean onDelete (GtkWidget*, GdkEvent*,    gpointer);
};
