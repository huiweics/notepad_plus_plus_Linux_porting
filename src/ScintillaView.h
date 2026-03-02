#pragma once
// Notepad++ Linux port - Scintilla GTK widget wrapper

#include <gtk/gtk.h>
#include <Scintilla.h>
#include <ScintillaWidget.h>
#include <string>
#include <vector>
#include <functional>

enum class LexerType {
    None, Cpp, Python, Java, JavaScript, HTML, XML, CSS, Bash,
    Makefile, SQL, Lua, Perl, Ruby, PHP, Rust, Go, CMake, Ini,
    Diff, Markdown, JSON, YAML, TCL, Pascal, Ada, Fortran, Asm,
    VB, CSharp, Swift,
};

class ScintillaView {
public:
    ScintillaView();
    ~ScintillaView();  // releases the GObject ref taken in the constructor

    GtkWidget* getWidget() { return _sci; }

    // ---- Document ----
    std::string getText() const;
    void        setText(const std::string& text);
    void        clearAll();
    int         getLength() const;
    bool        isReadOnly() const;
    void        setReadOnly(bool ro);
    bool        isModified() const;
    void        setSavePoint();
    void        emptyUndoBuffer();

    // ---- Navigation ----
    int  getCurrentLine()       const;   // 0-based
    int  getCurrentColumn()     const;   // 0-based
    int  getLineCount()         const;
    int  getFirstVisibleLine()  const;   // Phase 3: DocumentMap
    int  getLinesOnScreen()     const;   // Phase 3: DocumentMap
    int  getLineLength(int line) const;  // Phase 3: DocumentMap
    void gotoLine(int line);             // 0-based
    void gotoPos(int pos);
    void ensureCaretVisible();

    // ---- Selection / clipboard ----
    std::string getSelectedText() const;
    void selectAll();
    void cut();
    void copy();
    void paste();
    void deleteSel();

    // ---- Undo / redo ----
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;

    // ---- Search ----
    // Find using SCI target API; returns match start pos or -1.
    int  findInTarget(const std::string& text, int startPos, int endPos,
                      bool matchCase, bool wholeWord, bool regex);

    struct MatchResult { int line; std::string text; };
    std::vector<MatchResult> findAllMatches(const std::string& term,
                                             bool matchCase, bool wholeWord, bool regex);
    // Forward search from current caret; wraps if wrapAround.
    int  findNext(const std::string& text,
                  bool matchCase, bool wholeWord, bool regex, bool wrapAround);
    // Replace selection if it matches, then findNext.
    bool replaceAndFindNext(const std::string& findText,
                            const std::string& replaceText,
                            bool matchCase, bool wholeWord,
                            bool regex, bool wrapAround);
    int  replaceAll(const std::string& findText,
                    const std::string& replaceText,
                    bool matchCase, bool wholeWord, bool regex);

    // ---- View options ----
    void setWordWrap(bool wrap);
    void setShowWhitespace(bool show);
    void setShowLineNumbers(bool show);
    void setTabWidth(int width);
    void setUseTabs(bool useTabs);
    void setFont(const std::string& name, int sizePoints);
    void setZoom(int level);
    int  getZoom() const;

    // ---- EOL ----
    void        setEOLMode(int mode);   // SC_EOL_LF / SC_EOL_CRLF / SC_EOL_CR
    int         getEOLMode()       const;
    std::string getEOLModeString() const;
    void        convertEOLs(int mode);

    // ---- Line operations ----
    void duplicateLine();
    void deleteLine();
    void moveSelectedLinesUp();
    void moveSelectedLinesDown();

    // ---- Extended case conversion ----
    void toTitleCase();
    void toCamelCase();
    void toSnakeCase();

    // ---- Whitespace / EOL operations ----
    void trimTrailingWhitespace();
    void trimLeadingWhitespace();
    void trimAllWhitespace();
    void tabsToSpaces();
    void spacesToTabs();

    // ---- Comment / uncomment ----
    void toggleLineComment();

    // ---- Auto-complete ----
    void showAutoComplete();

    // ---- Bookmarks (marker 0, margin 1) ----
    void toggleBookmark();
    void gotoNextBookmark();
    void gotoPrevBookmark();
    void clearAllBookmarks();

    // ---- Info helpers ----
    std::string getLexerName()     const;
    int         getSelectionStart() const;
    int         getSelectionEnd()   const;

    // ---- Phase 2: find enhancements ----
    int  highlightAllMatches(const std::string& text, bool matchCase, bool wholeWord, bool regex);
    void clearAllHighlights();
    int  selectAllMatches  (const std::string& text, bool matchCase, bool wholeWord, bool regex);
    int  countMatches      (const std::string& text, bool matchCase, bool wholeWord, bool regex);
    int  bookmarkAllMatches(const std::string& text, bool matchCase, bool wholeWord, bool regex);

    // ---- Syntax highlighting ----
    void setLexerByFilename(const std::string& filename);
    void setLexerType(LexerType type);
    void applyColorTheme(const std::string& theme);  // "default" | "dark" | "zenburn"

    // ---- Edge column (Phase 6) ----
    void setEdgeMode(bool show);   // show/hide the edge column indicator
    void setEdgeColumn(int col);   // set column position

    // ---- Macro replay (Phase 5) ----
    // Used by MacroManager to replay recorded SCI_* messages without going
    // through the usual callback pipeline.
    void replayMessage(unsigned int msg, uptr_t wp = 0, sptr_t lp = 0);

    // ---- Callbacks ----
    using ModifiedCb    = std::function<void(bool)>;
    using CursorMovedCb = std::function<void()>;
    using NotifyCb      = std::function<void(SCNotification*)>; // Phase 4: plugin dispatch
    using MacroRecordCb = std::function<void(unsigned int, uptr_t, sptr_t)>; // Phase 5
    void setModifiedCallback   (ModifiedCb cb)    { _modifiedCb    = cb; }
    void setCursorMovedCallback(CursorMovedCb cb) { _cursorMovedCb = cb; }
    void setNotifyCallback     (NotifyCb cb)      { _notifyCb      = cb; }
    void setMacroRecordCallback(MacroRecordCb cb) { _macroRecordCb = cb; }

private:
    GtkWidget*    _sci          = nullptr;
    ModifiedCb    _modifiedCb;
    CursorMovedCb _cursorMovedCb;
    NotifyCb      _notifyCb;     // Phase 4: plugin notification dispatch
    MacroRecordCb _macroRecordCb; // Phase 5: macro recording
    LexerType     _currentLexer = LexerType::None;
    std::string   _currentTheme = "default";

    sptr_t send(unsigned int msg, uptr_t wp = 0, sptr_t lp = 0) const;

    static void onNotify(GtkWidget*, gint, SCNotification*, gpointer);

    void applyBaseStyles();
    void applyDefaultTheme();
    void applyDarkTheme();
    void applyZenburnTheme();
    void applyCppStyles();
    void applyPythonStyles();
    void applyBashStyles();
    void applyXmlHtmlStyles();
    void applyMakefileStyles();
    void applyIniStyles();
    void applyDiffStyles();
    void applyLuaStyles();
};
