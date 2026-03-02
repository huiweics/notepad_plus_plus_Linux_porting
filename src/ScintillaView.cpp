#include "ScintillaView.h"
#include <ILexer.h>
#include <Lexilla.h>
#include <SciLexer.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <set>
#include <sstream>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return s;
}

static std::string fileExt(const std::string& filename) {
    auto p = filename.rfind('.');
    return p == std::string::npos ? std::string{} : toLower(filename.substr(p + 1));
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ScintillaView::ScintillaView() {
    _sci = scintilla_new();
    g_object_ref_sink(_sci);
    g_signal_connect(_sci, SCINTILLA_NOTIFY, G_CALLBACK(onNotify), this);
    applyBaseStyles();
    applyDefaultTheme();
}

ScintillaView::~ScintillaView() {
    // Release the GObject reference taken by g_object_ref_sink() in the
    // constructor.  Without this the Scintilla widget leaks on every tab close.
    if (_sci) {
        g_object_unref(_sci);
        _sci = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Internal send
// ---------------------------------------------------------------------------

sptr_t ScintillaView::send(unsigned int msg, uptr_t wp, sptr_t lp) const {
    return scintilla_send_message(SCINTILLA(_sci), msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Notifications
// ---------------------------------------------------------------------------

void ScintillaView::onNotify(GtkWidget*, gint, SCNotification* n, gpointer ud) {
    auto* self = static_cast<ScintillaView*>(ud);
    switch (n->nmhdr.code) {
        case SCN_SAVEPOINTREACHED:
            if (self->_modifiedCb) self->_modifiedCb(false);
            break;
        case SCN_SAVEPOINTLEFT:
            if (self->_modifiedCb) self->_modifiedCb(true);
            break;
        case SCN_MODIFIED:
            if (n->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))
                if (self->_modifiedCb) self->_modifiedCb(self->isModified());
            break;
        case SCN_UPDATEUI:
            if (self->_cursorMovedCb) self->_cursorMovedCb();
            break;
        case SCN_CHARADDED:
            if (n->ch == '\n') {
                int cur  = (int)self->send(SCI_LINEFROMPOSITION,
                                           self->send(SCI_GETCURRENTPOS));
                int prev = cur - 1;
                if (prev >= 0) {
                    int indent = (int)self->send(SCI_GETLINEINDENTATION, prev);
                    self->send(SCI_SETLINEINDENTATION, cur, indent);
                    self->send(SCI_GOTOPOS,
                               self->send(SCI_GETLINEINDENTPOSITION, cur));
                }
            } else if (std::isalnum((unsigned char)n->ch) || n->ch == '_') {
                self->showAutoComplete();
            }
            break;
        case SCN_MACRORECORD:
            // Phase 5: forward to macro manager for recording
            if (self->_macroRecordCb)
                self->_macroRecordCb(n->message, n->wParam, n->lParam);
            break;
        default: break;
    }
    // Phase 4: forward every notification to registered plugin dispatch callback
    if (self->_notifyCb) self->_notifyCb(n);
}

// ---------------------------------------------------------------------------
// Document
// ---------------------------------------------------------------------------

std::string ScintillaView::getText() const {
    int len = (int)send(SCI_GETLENGTH);
    std::string buf(len + 1, '\0');
    send(SCI_GETTEXT, len + 1, (sptr_t)buf.data());
    buf.resize(len);
    return buf;
}

void ScintillaView::setText(const std::string& text) {
    send(SCI_SETREADONLY, 0);
    send(SCI_SETTEXT, 0, (sptr_t)text.c_str());
    send(SCI_EMPTYUNDOBUFFER);
    send(SCI_SETSAVEPOINT);
    send(SCI_GOTOPOS, 0);
}

void ScintillaView::clearAll() {
    send(SCI_SETREADONLY, 0);
    send(SCI_CLEARALL);
    send(SCI_EMPTYUNDOBUFFER);
    send(SCI_SETSAVEPOINT);
}

int  ScintillaView::getLength()    const { return (int)send(SCI_GETLENGTH); }
bool ScintillaView::isReadOnly()   const { return send(SCI_GETREADONLY) != 0; }
void ScintillaView::setReadOnly(bool ro) { send(SCI_SETREADONLY, ro); }
bool ScintillaView::isModified()   const { return send(SCI_GETMODIFY) != 0; }
void ScintillaView::setSavePoint()       { send(SCI_SETSAVEPOINT); }
void ScintillaView::emptyUndoBuffer()    { send(SCI_EMPTYUNDOBUFFER); }

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

int  ScintillaView::getCurrentLine()   const {
    return (int)send(SCI_LINEFROMPOSITION, send(SCI_GETCURRENTPOS));
}
int  ScintillaView::getCurrentColumn() const {
    return (int)send(SCI_GETCOLUMN, send(SCI_GETCURRENTPOS));
}
int  ScintillaView::getLineCount()     const { return (int)send(SCI_GETLINECOUNT); }

void ScintillaView::gotoLine(int line) {
    send(SCI_GOTOLINE, line);
    send(SCI_ENSUREVISIBLEENFORCEPOLICY, line);
}
void ScintillaView::gotoPos(int pos)     { send(SCI_GOTOPOS, pos); }
void ScintillaView::ensureCaretVisible() { send(SCI_SCROLLCARET); }

// ---------------------------------------------------------------------------
// Selection / clipboard
// ---------------------------------------------------------------------------

std::string ScintillaView::getSelectedText() const {
    int len = (int)send(SCI_GETSELTEXT, 0, 0);
    if (len <= 0) return {};
    std::string buf(len, '\0');
    send(SCI_GETSELTEXT, 0, (sptr_t)buf.data());
    if (!buf.empty() && buf.back() == '\0') buf.pop_back();
    return buf;
}

void ScintillaView::selectAll() { send(SCI_SELECTALL); }
void ScintillaView::cut()       { send(SCI_CUT); }
void ScintillaView::copy()      { send(SCI_COPY); }
void ScintillaView::paste()     { send(SCI_PASTE); }
void ScintillaView::deleteSel() { send(SCI_CLEAR); }

// ---------------------------------------------------------------------------
// Undo / redo
// ---------------------------------------------------------------------------

void ScintillaView::undo()             { send(SCI_UNDO); }
void ScintillaView::redo()             { send(SCI_REDO); }
bool ScintillaView::canUndo()    const { return send(SCI_CANUNDO) != 0; }
bool ScintillaView::canRedo()    const { return send(SCI_CANREDO) != 0; }

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

int ScintillaView::findInTarget(const std::string& text, int startPos, int endPos,
                                 bool matchCase, bool wholeWord, bool regex)
{
    int flags = 0;
    if (matchCase) flags |= SCFIND_MATCHCASE;
    if (wholeWord) flags |= SCFIND_WHOLEWORD;
    if (regex)     flags |= SCFIND_REGEXP | SCFIND_POSIX;
    send(SCI_SETTARGETSTART, startPos);
    send(SCI_SETTARGETEND,   endPos);
    send(SCI_SETSEARCHFLAGS, flags);
    return (int)send(SCI_SEARCHINTARGET, text.size(), (sptr_t)text.c_str());
}

int ScintillaView::findNext(const std::string& text,
                             bool matchCase, bool wholeWord,
                             bool regex, bool wrapAround)
{
    int docLen   = getLength();
    int selEnd   = (int)send(SCI_GETSELECTIONEND);
    int startPos = selEnd;

    int pos = findInTarget(text, startPos, docLen, matchCase, wholeWord, regex);
    if (pos == -1 && wrapAround)
        pos = findInTarget(text, 0, docLen, matchCase, wholeWord, regex);
    if (pos >= 0) {
        int tEnd = (int)send(SCI_GETTARGETEND);
        send(SCI_SETSEL, pos, tEnd);
        send(SCI_SCROLLCARET);
    }
    return pos;
}

std::vector<ScintillaView::MatchResult>
ScintillaView::findAllMatches(const std::string& term,
                               bool matchCase, bool wholeWord, bool regex)
{
    std::vector<MatchResult> results;
    if (term.empty()) return results;

    int docLen = getLength();
    int start  = 0;

    while (start <= docLen) {
        int pos = findInTarget(term, start, docLen, matchCase, wholeWord, regex);
        if (pos < 0) break;

        int matchEnd = (int)send(SCI_GETTARGETEND);
        int lineNum  = (int)send(SCI_LINEFROMPOSITION, pos);

        // Fetch the full line text
        int lineLen = (int)send(SCI_LINELENGTH, lineNum);
        std::string lineText;
        if (lineLen > 0) {
            lineText.resize(lineLen + 1, '\0');
            send(SCI_GETLINE, lineNum, (sptr_t)lineText.data());
            lineText.resize(lineLen);
        }
        // Strip trailing CR/LF
        while (!lineText.empty() &&
               (lineText.back() == '\r' || lineText.back() == '\n'))
            lineText.pop_back();
        // Strip leading whitespace for display
        size_t nonSpace = lineText.find_first_not_of(" \t");
        if (nonSpace != std::string::npos) lineText = lineText.substr(nonSpace);

        results.push_back({lineNum, lineText});

        // Advance past the match (guard against zero-width matches)
        start = (matchEnd > pos) ? matchEnd : pos + 1;
    }

    return results;
}

bool ScintillaView::replaceAndFindNext(const std::string& findText,
                                        const std::string& replaceText,
                                        bool matchCase, bool wholeWord,
                                        bool regex, bool wrapAround)
{
    int selStart = (int)send(SCI_GETSELECTIONSTART);
    int selEnd   = (int)send(SCI_GETSELECTIONEND);
    if (selStart != selEnd) {
        std::string sel = getSelectedText();
        bool matches = matchCase ? (sel == findText)
                                 : (toLower(sel) == toLower(findText));
        if (matches) {
            if (regex) {
                int flags = SCFIND_REGEXP | SCFIND_POSIX;
                if (matchCase) flags |= SCFIND_MATCHCASE;
                if (wholeWord) flags |= SCFIND_WHOLEWORD;
                send(SCI_SETTARGETSTART, selStart);
                send(SCI_SETTARGETEND,   selEnd);
                send(SCI_SETSEARCHFLAGS, flags);
                send(SCI_REPLACETARGET, replaceText.size(), (sptr_t)replaceText.c_str());
            } else {
                send(SCI_REPLACESEL, 0, (sptr_t)replaceText.c_str());
            }
        }
    }
    return findNext(findText, matchCase, wholeWord, regex, wrapAround) >= 0;
}

int ScintillaView::replaceAll(const std::string& findText,
                               const std::string& replaceText,
                               bool matchCase, bool wholeWord, bool regex)
{
    int flags = 0;
    if (matchCase) flags |= SCFIND_MATCHCASE;
    if (wholeWord) flags |= SCFIND_WHOLEWORD;
    if (regex)     flags |= SCFIND_REGEXP | SCFIND_POSIX;

    send(SCI_BEGINUNDOACTION);
    int count = 0;
    int pos   = 0;
    int docLen = getLength();

    while (true) {
        send(SCI_SETTARGETSTART, pos);
        send(SCI_SETTARGETEND,   docLen);
        send(SCI_SETSEARCHFLAGS, flags);
        int found = (int)send(SCI_SEARCHINTARGET,
                              findText.size(), (sptr_t)findText.c_str());
        if (found < 0) break;
        ++count;
        if (regex)
            pos = (int)send(SCI_REPLACETARGETRE,
                            replaceText.size(), (sptr_t)replaceText.c_str());
        else {
            send(SCI_REPLACETARGET,
                 replaceText.size(), (sptr_t)replaceText.c_str());
            pos = (int)send(SCI_GETTARGETEND);
        }
        docLen = getLength();
        if (pos >= docLen) break;
    }
    send(SCI_ENDUNDOACTION);
    return count;
}

// ---------------------------------------------------------------------------
// View options
// ---------------------------------------------------------------------------

void ScintillaView::setWordWrap(bool wrap) {
    send(SCI_SETWRAPMODE, wrap ? SC_WRAP_WORD : SC_WRAP_NONE);
}

void ScintillaView::setShowWhitespace(bool show) {
    send(SCI_SETVIEWWS, show ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
}

void ScintillaView::setShowLineNumbers(bool show) {
    if (show) {
        // Use the actual line count but enforce a minimum of 5 digits so the
        // margin doesn't look too narrow for small/empty files.
        int lines = std::max((int)getLineCount(), 99999);
        char buf[24];
        snprintf(buf, sizeof(buf), "_%d", lines);
        int w = (int)send(SCI_TEXTWIDTH, STYLE_LINENUMBER, (sptr_t)buf);
        send(SCI_SETMARGINWIDTHN, 0, w);
    } else {
        send(SCI_SETMARGINWIDTHN, 0, 0);
    }
}

void ScintillaView::setTabWidth(int w) {
    send(SCI_SETTABWIDTH, w);
    send(SCI_SETINDENT, w);
}

void ScintillaView::setUseTabs(bool use) { send(SCI_SETUSETABS, use ? 1 : 0); }

void ScintillaView::setFont(const std::string& name, int sizePoints) {
    for (int s = 0; s <= STYLE_MAX; ++s) {
        send(SCI_STYLESETFONT, s, (sptr_t)name.c_str());
        send(SCI_STYLESETSIZEFRACTIONAL, s, sizePoints * SC_FONT_SIZE_MULTIPLIER);
    }
}

void ScintillaView::setZoom(int level) { send(SCI_SETZOOM, level); }
int  ScintillaView::getZoom()    const { return (int)send(SCI_GETZOOM); }

// ---------------------------------------------------------------------------
// EOL
// ---------------------------------------------------------------------------

void        ScintillaView::setEOLMode(int m)  { send(SCI_SETEOLMODE, m); }
int         ScintillaView::getEOLMode()  const { return (int)send(SCI_GETEOLMODE); }
void        ScintillaView::convertEOLs(int m) { send(SCI_CONVERTEOLS, m); setEOLMode(m); }

std::string ScintillaView::getEOLModeString() const {
    switch (getEOLMode()) {
        case SC_EOL_CRLF: return "Windows (CRLF)";
        case SC_EOL_CR:   return "Old Mac (CR)";
        default:          return "Unix (LF)";
    }
}

// ---------------------------------------------------------------------------
// Lexer assignment by file extension
// ---------------------------------------------------------------------------

void ScintillaView::setLexerByFilename(const std::string& filename) {
    std::string ext = fileExt(filename);

    if (ext=="c"||ext=="cpp"||ext=="cxx"||ext=="cc"||ext=="h"||
        ext=="hpp"||ext=="hxx"||ext=="hh")              setLexerType(LexerType::Cpp);
    else if (ext=="cs")                                 setLexerType(LexerType::CSharp);
    else if (ext=="java")                               setLexerType(LexerType::Java);
    else if (ext=="swift")                              setLexerType(LexerType::Swift);
    else if (ext=="py"||ext=="pyw")                     setLexerType(LexerType::Python);
    else if (ext=="js"||ext=="ts"||ext=="jsx"||ext=="tsx") setLexerType(LexerType::JavaScript);
    else if (ext=="html"||ext=="htm"||ext=="xhtml")     setLexerType(LexerType::HTML);
    else if (ext=="xml"||ext=="svg"||ext=="xsl")        setLexerType(LexerType::XML);
    else if (ext=="css"||ext=="scss"||ext=="less")      setLexerType(LexerType::CSS);
    else if (ext=="sh"||ext=="bash"||ext=="zsh")        setLexerType(LexerType::Bash);
    else if (ext=="makefile"||ext=="mk"||ext=="mak")    setLexerType(LexerType::Makefile);
    else if (ext=="sql")                                setLexerType(LexerType::SQL);
    else if (ext=="lua")                                setLexerType(LexerType::Lua);
    else if (ext=="pl"||ext=="pm")                      setLexerType(LexerType::Perl);
    else if (ext=="rb"||ext=="rbw")                     setLexerType(LexerType::Ruby);
    else if (ext=="php"||ext=="phtml")                  setLexerType(LexerType::PHP);
    else if (ext=="rs")                                 setLexerType(LexerType::Rust);
    else if (ext=="go")                                 setLexerType(LexerType::Go);
    else if (ext=="cmake")                              setLexerType(LexerType::CMake);
    else if (ext=="ini"||ext=="cfg"||ext=="conf")       setLexerType(LexerType::Ini);
    else if (ext=="diff"||ext=="patch")                 setLexerType(LexerType::Diff);
    else if (ext=="md"||ext=="markdown")                setLexerType(LexerType::Markdown);
    else if (ext=="json")                               setLexerType(LexerType::JSON);
    else if (ext=="yaml"||ext=="yml")                   setLexerType(LexerType::YAML);
    else if (ext=="tcl")                                setLexerType(LexerType::TCL);
    else if (ext=="pas"||ext=="pp")                     setLexerType(LexerType::Pascal);
    else if (ext=="asm"||ext=="s")                      setLexerType(LexerType::Asm);
    else if (ext=="vb"||ext=="bas")                     setLexerType(LexerType::VB);
    else                                                setLexerType(LexerType::None);
}

void ScintillaView::setLexerType(LexerType type) {
    _currentLexer = type;

    const char* name = nullptr;
    switch (type) {
        case LexerType::Cpp:        name = "cpp";        break;
        case LexerType::CSharp:     name = "cpp";        break;
        case LexerType::Java:       name = "cpp";        break;
        case LexerType::Swift:      name = "cpp";        break;
        case LexerType::Python:     name = "python";     break;
        case LexerType::JavaScript: name = "cpp";        break;
        case LexerType::HTML:       name = "hypertext";  break;
        case LexerType::PHP:        name = "hypertext";  break;
        case LexerType::XML:        name = "xml";        break;
        case LexerType::CSS:        name = "css";        break;
        case LexerType::Bash:       name = "bash";       break;
        case LexerType::Makefile:   name = "makefile";   break;
        case LexerType::SQL:        name = "sql";        break;
        case LexerType::Lua:        name = "lua";        break;
        case LexerType::Perl:       name = "perl";       break;
        case LexerType::Ruby:       name = "ruby";       break;
        case LexerType::Rust:       name = "rust";       break;
        case LexerType::Go:         name = "cpp";        break;
        case LexerType::CMake:      name = "cmake";      break;
        case LexerType::Ini:        name = "properties"; break;
        case LexerType::Diff:       name = "diff";       break;
        case LexerType::Markdown:   name = "markdown";   break;
        case LexerType::JSON:       name = "json";       break;
        case LexerType::YAML:       name = "yaml";       break;
        case LexerType::TCL:        name = "tcl";        break;
        case LexerType::Pascal:     name = "pascal";     break;
        case LexerType::Ada:        name = "ada";        break;
        case LexerType::Fortran:    name = "fortran";    break;
        case LexerType::Asm:        name = "asm";        break;
        case LexerType::VB:         name = "vb";         break;
        default:                    name = nullptr;       break;
    }

    if (name) {
        ILexer5* lexer = CreateLexer(name);
        if (lexer) send(SCI_SETILEXER, 0, (sptr_t)lexer);
    } else {
        send(SCI_SETILEXER, 0, 0);
    }

    applyColorTheme(_currentTheme);

    // Keyword sets
    switch (type) {
        case LexerType::Cpp: case LexerType::CSharp:
        case LexerType::Java: case LexerType::Swift:
            send(SCI_SETKEYWORDS, 0, (sptr_t)
                "alignas alignof and and_eq asm auto bitand bitor bool break case catch "
                "char char8_t char16_t char32_t class compl concept const consteval "
                "constexpr constinit const_cast continue co_await co_return co_yield "
                "decltype default delete do double dynamic_cast else enum explicit export "
                "extern false float for friend goto if inline int long mutable namespace "
                "new noexcept not not_eq nullptr operator or or_eq private protected "
                "public register reinterpret_cast requires return short signed sizeof "
                "static static_assert static_cast struct switch template this thread_local "
                "throw true try typedef typeid typename union unsigned using virtual void "
                "volatile wchar_t while xor xor_eq override final import module");
            break;
        case LexerType::Python:
            send(SCI_SETKEYWORDS, 0, (sptr_t)
                "False None True and as assert async await break class continue def del "
                "elif else except finally for from global if import in is lambda nonlocal "
                "not or pass raise return try while with yield");
            break;
        case LexerType::JavaScript: case LexerType::Go:
            send(SCI_SETKEYWORDS, 0, (sptr_t)
                "abstract arguments async await boolean break byte case catch char class "
                "const continue debugger default delete do double else enum eval export "
                "extends false final finally float for function goto if implements import "
                "in instanceof int interface let long native new null package private "
                "protected public return short static super switch synchronized this "
                "throw throws transient true try typeof var void volatile while with yield");
            break;
        case LexerType::Bash:
            send(SCI_SETKEYWORDS, 0, (sptr_t)
                "alias bg bind break builtin case cd command compgen complete continue "
                "declare dirs disown echo enable eval exec exit export false fc fg "
                "function getopts hash help history if jobs kill let local logout "
                "mapfile popd printf pushd pwd read readarray readonly return select "
                "set shift shopt source test then time times trap true type typeset "
                "ulimit umask unalias unset until wait while");
            break;
        case LexerType::Rust:
            send(SCI_SETKEYWORDS, 0, (sptr_t)
                "as async await break const continue crate dyn else enum extern false "
                "fn for if impl in let loop match mod move mut pub ref return self Self "
                "static struct super trait true type union unsafe use where while");
            break;
        case LexerType::SQL:
            send(SCI_SETKEYWORDS, 0, (sptr_t)
                "ADD ALL ALTER AND AS ASC BACKUP BETWEEN BY CASCADE CASE CHECK COLUMN "
                "CONSTRAINT CREATE CROSS DATABASE DEFAULT DELETE DESC DISTINCT DROP ELSE "
                "END EXEC EXISTS FOREIGN FROM FULL GROUP HAVING IDENTITY IN INDEX INNER "
                "INSERT INTO IS JOIN KEY LEFT LIKE LIMIT NOT NULL ON OR ORDER OUTER "
                "PRIMARY PROCEDURE RIGHT ROWNUM SELECT SET TABLE TOP TRIGGER TRUNCATE "
                "UNION UNIQUE UPDATE VALUES VIEW WHERE WITH");
            break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// Base styles (font, margins, caret, folding)
// ---------------------------------------------------------------------------

void ScintillaView::applyBaseStyles() {
    const char* font = "Monospace";
    int sz = 11 * SC_FONT_SIZE_MULTIPLIER;
    for (int s = 0; s <= STYLE_MAX; ++s) {
        send(SCI_STYLESETFONT, s, (sptr_t)font);
        send(SCI_STYLESETSIZEFRACTIONAL, s, sz);
    }

    // Margins
    send(SCI_SETMARGINTYPEN,  0, SC_MARGIN_NUMBER);
    send(SCI_SETMARGINWIDTHN, 0, 40);
    // Margin 1: bookmarks
    send(SCI_SETMARGINTYPEN,  1, SC_MARGIN_SYMBOL);
    send(SCI_SETMARGINWIDTHN, 1, 14);
    send(SCI_SETMARGINMASKN,  1, ~SC_MASK_FOLDERS);
    send(SCI_MARKERDEFINE,    0, SC_MARK_CIRCLE);
    send(SCI_MARKERSETBACK,   0, 0x007ACC);   // blue dot
    send(SCI_MARKERSETFORE,   0, 0xFFFFFF);
    send(SCI_SETMARGINSENSITIVEN, 1, 1);
    send(SCI_SETMARGINTYPEN,  2, SC_MARGIN_SYMBOL);
    send(SCI_SETMARGINWIDTHN, 2, 14);
    send(SCI_SETMARGINMASKN,  2, SC_MASK_FOLDERS);
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDER,     SC_MARK_ARROWDOWN);
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPEN, SC_MARK_ARROWDOWN);
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEREND,  SC_MARK_ARROW);
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERSUB,  SC_MARK_VLINE);
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNER);
    send(SCI_SETFOLDFLAGS, 16);
    send(SCI_SETAUTOMATICFOLD,
         SC_AUTOMATICFOLD_SHOW | SC_AUTOMATICFOLD_CLICK | SC_AUTOMATICFOLD_CHANGE);

    send(SCI_SETTABWIDTH, 4);
    send(SCI_SETUSETABS,  1);
    send(SCI_SETINDENT,   4);
    send(SCI_SETEOLMODE,  SC_EOL_LF);
    send(SCI_SETCARETLINEVISIBLE, 1);
    send(SCI_SETCARETLINEVISIBLEALWAYS, 1);
    send(SCI_SETEDGEMODE, EDGE_BACKGROUND);
    send(SCI_SETEDGECOLUMN, 120);
    send(SCI_SETMULTIPLESELECTION, 1);
    send(SCI_SETADDITIONALSELECTIONTYPING, 1);
    send(SCI_SETSCROLLWIDTHTRACKING, 1);
    send(SCI_SETSCROLLWIDTH, 1);
}

// ---------------------------------------------------------------------------
// Color themes
// ---------------------------------------------------------------------------

void ScintillaView::applyColorTheme(const std::string& theme) {
    _currentTheme = theme;
    if (theme == "dark")    applyDarkTheme();
    else if (theme == "zenburn") applyZenburnTheme();
    else                    applyDefaultTheme();
}

void ScintillaView::applyDefaultTheme() {
    send(SCI_STYLESETBACK, STYLE_DEFAULT, 0xFFFFFF);
    send(SCI_STYLESETFORE, STYLE_DEFAULT, 0x000000);
    send(SCI_STYLECLEARALL);
    send(SCI_STYLESETBACK, STYLE_LINENUMBER, 0xE8E8E8);
    send(SCI_STYLESETFORE, STYLE_LINENUMBER, 0x808080);
    send(SCI_STYLESETBACK, STYLE_BRACELIGHT, 0x99FF99);
    send(SCI_STYLESETFORE, STYLE_BRACELIGHT, 0x000000);
    send(SCI_STYLESETBACK, STYLE_BRACEBAD,   0xFF9999);
    send(SCI_SETSELBACK, 1,  0x3399FF);
    send(SCI_SETSELFORE, 1,  0xFFFFFF);
    send(SCI_SETCARETLINEBACK, 0xF0F0F0);
    send(SCI_SETCARETFORE,     0x000000);
    send(SCI_SETEDGECOLOUR,    0xF0E8D0);

    switch (_currentLexer) {
        case LexerType::Cpp: case LexerType::CSharp:
        case LexerType::Java: case LexerType::Swift:
        case LexerType::JavaScript: case LexerType::Go: applyCppStyles();     break;
        case LexerType::Python:                         applyPythonStyles();  break;
        case LexerType::Bash:                           applyBashStyles();    break;
        case LexerType::HTML: case LexerType::XML:      applyXmlHtmlStyles(); break;
        case LexerType::Makefile:                       applyMakefileStyles();break;
        case LexerType::Ini:                            applyIniStyles();     break;
        case LexerType::Diff:                           applyDiffStyles();    break;
        case LexerType::Lua:                            applyLuaStyles();     break;
        default: break;
    }
}

void ScintillaView::applyDarkTheme() {
    send(SCI_STYLESETBACK, STYLE_DEFAULT, 0x1E1E1E);
    send(SCI_STYLESETFORE, STYLE_DEFAULT, 0xD4D4D4);
    send(SCI_STYLECLEARALL);
    send(SCI_STYLESETBACK, STYLE_LINENUMBER, 0x252526);
    send(SCI_STYLESETFORE, STYLE_LINENUMBER, 0x858585);
    send(SCI_SETSELBACK, 1,  0x264F78);
    send(SCI_SETSELFORE, 1,  0xD4D4D4);
    send(SCI_SETCARETLINEBACK, 0x2A2D2E);
    send(SCI_SETCARETFORE,     0xAEAFAD);
    send(SCI_SETEDGECOLOUR,    0x404040);

    switch (_currentLexer) {
        case LexerType::Cpp: case LexerType::CSharp:
        case LexerType::Java: case LexerType::Swift:
        case LexerType::JavaScript: case LexerType::Go:
            send(SCI_STYLESETFORE, SCE_C_COMMENT,      0x6A9955);
            send(SCI_STYLESETFORE, SCE_C_COMMENTLINE,  0x6A9955);
            send(SCI_STYLESETFORE, SCE_C_COMMENTDOC,   0x6A9955);
            send(SCI_STYLESETFORE, SCE_C_NUMBER,        0xB5CEA8);
            send(SCI_STYLESETFORE, SCE_C_WORD,          0x569CD6);
            send(SCI_STYLESETFORE, SCE_C_STRING,        0xCE9178);
            send(SCI_STYLESETFORE, SCE_C_CHARACTER,     0xCE9178);
            send(SCI_STYLESETFORE, SCE_C_PREPROCESSOR,  0x9B9B9B);
            send(SCI_STYLESETFORE, SCE_C_OPERATOR,      0xD4D4D4);
            break;
        case LexerType::Python:
            send(SCI_STYLESETFORE, SCE_P_COMMENTLINE,   0x6A9955);
            send(SCI_STYLESETFORE, SCE_P_NUMBER,        0xB5CEA8);
            send(SCI_STYLESETFORE, SCE_P_WORD,          0x569CD6);
            send(SCI_STYLESETFORE, SCE_P_STRING,        0xCE9178);
            send(SCI_STYLESETFORE, SCE_P_TRIPLE,        0xCE9178);
            send(SCI_STYLESETFORE, SCE_P_TRIPLEDOUBLE,  0xCE9178);
            break;
        default: break;
    }
}

// Zenburn: warm dark theme (bg=#3f3f3f, fg=#dcdccc)
void ScintillaView::applyZenburnTheme() {
    // bg=0x3f3f3f (#3f3f3f), fg=0xdcdccc (#dcdccc) — stored as BGR for Scintilla
    send(SCI_STYLESETBACK, STYLE_DEFAULT, 0x3f3f3f);
    send(SCI_STYLESETFORE, STYLE_DEFAULT, 0xccdcdc);  // #dcdccc (BGR)
    send(SCI_STYLECLEARALL);
    send(SCI_STYLESETBACK, STYLE_LINENUMBER, 0x2f2f2f);
    send(SCI_STYLESETFORE, STYLE_LINENUMBER, 0x8f8f8f);
    send(SCI_SETSELBACK, 1, 0x7f5f2f);    // warm selection
    send(SCI_SETSELFORE, 1, 0xdcdccc);
    send(SCI_SETCARETLINEBACK, 0x4f4f4f);
    send(SCI_SETCARETFORE, 0xdcdccc);
    send(SCI_SETEDGECOLOUR, 0x5f5f5f);

    switch (_currentLexer) {
        case LexerType::Cpp: case LexerType::CSharp:
        case LexerType::Java: case LexerType::Swift:
        case LexerType::JavaScript: case LexerType::Go:
            // comments=#7f9f7f, numbers=#afd8af, keywords=#f0dfaf, strings=#cc9393
            send(SCI_STYLESETFORE, SCE_C_COMMENT,      0x7f9f7f);
            send(SCI_STYLESETFORE, SCE_C_COMMENTLINE,  0x7f9f7f);
            send(SCI_STYLESETFORE, SCE_C_COMMENTDOC,   0x7f9f7f);
            send(SCI_STYLESETFORE, SCE_C_NUMBER,        0xafd8af);
            send(SCI_STYLESETFORE, SCE_C_WORD,          0xf0dfaf);
            send(SCI_STYLESETFORE, SCE_C_STRING,        0xcc9393);
            send(SCI_STYLESETFORE, SCE_C_CHARACTER,     0xcc9393);
            send(SCI_STYLESETFORE, SCE_C_PREPROCESSOR,  0xdfaf8f);
            send(SCI_STYLESETFORE, SCE_C_OPERATOR,      0xdcdccc);
            break;
        case LexerType::Python:
            send(SCI_STYLESETFORE, SCE_P_COMMENTLINE,   0x7f9f7f);
            send(SCI_STYLESETFORE, SCE_P_NUMBER,        0xafd8af);
            send(SCI_STYLESETFORE, SCE_P_WORD,          0xf0dfaf);
            send(SCI_STYLESETFORE, SCE_P_STRING,        0xcc9393);
            send(SCI_STYLESETFORE, SCE_P_TRIPLE,        0xcc9393);
            send(SCI_STYLESETFORE, SCE_P_TRIPLEDOUBLE,  0xcc9393);
            break;
        case LexerType::Bash:
            send(SCI_STYLESETFORE, SCE_SH_COMMENTLINE, 0x7f9f7f);
            send(SCI_STYLESETFORE, SCE_SH_NUMBER,       0xafd8af);
            send(SCI_STYLESETFORE, SCE_SH_WORD,         0xf0dfaf);
            send(SCI_STYLESETFORE, SCE_SH_STRING,       0xcc9393);
            break;
        default: break;
    }
}

void ScintillaView::applyCppStyles() {
    send(SCI_STYLESETFORE, SCE_C_COMMENT,      0x008000);
    send(SCI_STYLESETFORE, SCE_C_COMMENTLINE,  0x008000);
    send(SCI_STYLESETFORE, SCE_C_COMMENTDOC,   0x008000);
    send(SCI_STYLESETFORE, SCE_C_NUMBER,        0xFF8000);
    send(SCI_STYLESETFORE, SCE_C_WORD,          0x0000FF);
    send(SCI_STYLESETBOLD, SCE_C_WORD,          1);
    send(SCI_STYLESETFORE, SCE_C_STRING,        0xA31515);
    send(SCI_STYLESETFORE, SCE_C_CHARACTER,     0xA31515);
    send(SCI_STYLESETFORE, SCE_C_PREPROCESSOR,  0x800080);
    send(SCI_STYLESETFORE, SCE_C_OPERATOR,      0x000000);
    send(SCI_STYLESETFORE, SCE_C_WORD2,         0x2B91AF);
}

void ScintillaView::applyPythonStyles() {
    send(SCI_STYLESETFORE, SCE_P_COMMENTLINE,   0x008000);
    send(SCI_STYLESETFORE, SCE_P_NUMBER,        0xFF8000);
    send(SCI_STYLESETFORE, SCE_P_STRING,        0xA31515);
    send(SCI_STYLESETFORE, SCE_P_TRIPLE,        0xA31515);
    send(SCI_STYLESETFORE, SCE_P_TRIPLEDOUBLE,  0xA31515);
    send(SCI_STYLESETFORE, SCE_P_WORD,          0x0000FF);
    send(SCI_STYLESETBOLD, SCE_P_WORD,          1);
    send(SCI_STYLESETFORE, SCE_P_DEFNAME,       0x795E26);
    send(SCI_STYLESETFORE, SCE_P_CLASSNAME,     0x267F99);
    send(SCI_STYLESETFORE, SCE_P_DECORATOR,     0x795E26);
}

void ScintillaView::applyBashStyles() {
    send(SCI_STYLESETFORE, SCE_SH_COMMENTLINE, 0x008000);
    send(SCI_STYLESETFORE, SCE_SH_NUMBER,       0xFF8000);
    send(SCI_STYLESETFORE, SCE_SH_WORD,         0x0000FF);
    send(SCI_STYLESETBOLD, SCE_SH_WORD,         1);
    send(SCI_STYLESETFORE, SCE_SH_STRING,       0xA31515);
    send(SCI_STYLESETFORE, SCE_SH_BACKTICKS,    0x795E26);
    send(SCI_STYLESETFORE, SCE_SH_PARAM,        0x800080);
}

void ScintillaView::applyXmlHtmlStyles() {
    send(SCI_STYLESETFORE, SCE_H_COMMENT,       0x008000);
    send(SCI_STYLESETFORE, SCE_H_TAG,           0x800000);
    send(SCI_STYLESETBOLD, SCE_H_TAG,           1);
    send(SCI_STYLESETFORE, SCE_H_TAGUNKNOWN,    0xFF0000);
    send(SCI_STYLESETFORE, SCE_H_ATTRIBUTE,     0xFF0000);
    send(SCI_STYLESETFORE, SCE_H_DOUBLESTRING,  0x0000FF);
    send(SCI_STYLESETFORE, SCE_H_SINGLESTRING,  0x0000FF);
    send(SCI_STYLESETFORE, SCE_H_NUMBER,        0xFF8000);
    send(SCI_STYLESETFORE, SCE_H_ENTITY,        0xFF0000);
}

void ScintillaView::applyMakefileStyles() {
    send(SCI_STYLESETFORE, SCE_MAKE_COMMENT,    0x008000);
    send(SCI_STYLESETFORE, SCE_MAKE_TARGET,     0x800000);
    send(SCI_STYLESETBOLD, SCE_MAKE_TARGET,     1);
    send(SCI_STYLESETFORE, SCE_MAKE_IDENTIFIER, 0x000080);
    send(SCI_STYLESETFORE, SCE_MAKE_PREPROCESSOR,0x800080);
}

void ScintillaView::applyIniStyles() {
    send(SCI_STYLESETFORE, SCE_PROPS_COMMENT,   0x008000);
    send(SCI_STYLESETFORE, SCE_PROPS_SECTION,   0x800000);
    send(SCI_STYLESETBOLD, SCE_PROPS_SECTION,   1);
    send(SCI_STYLESETFORE, SCE_PROPS_ASSIGNMENT,0x0000FF);
}

void ScintillaView::applyDiffStyles() {
    send(SCI_STYLESETFORE, SCE_DIFF_COMMENT,    0x008000);
    send(SCI_STYLESETFORE, SCE_DIFF_COMMAND,    0x000080);
    send(SCI_STYLESETFORE, SCE_DIFF_HEADER,     0x800080);
    send(SCI_STYLESETFORE, SCE_DIFF_DELETED,    0xAA0000);
    send(SCI_STYLESETBACK, SCE_DIFF_DELETED,    0xFFE8E8);
    send(SCI_STYLESETFORE, SCE_DIFF_ADDED,      0x006600);
    send(SCI_STYLESETBACK, SCE_DIFF_ADDED,      0xE8FFE8);
}

void ScintillaView::applyLuaStyles() {
    send(SCI_STYLESETFORE, SCE_LUA_COMMENT,     0x008000);
    send(SCI_STYLESETFORE, SCE_LUA_COMMENTLINE, 0x008000);
    send(SCI_STYLESETFORE, SCE_LUA_NUMBER,      0xFF8000);
    send(SCI_STYLESETFORE, SCE_LUA_WORD,        0x0000FF);
    send(SCI_STYLESETBOLD, SCE_LUA_WORD,        1);
    send(SCI_STYLESETFORE, SCE_LUA_STRING,      0xA31515);
}

// ---------------------------------------------------------------------------
// Line operations
// ---------------------------------------------------------------------------

void ScintillaView::duplicateLine()         { send(SCI_LINEDUPLICATE); }
void ScintillaView::deleteLine()            { send(SCI_LINEDELETE); }
void ScintillaView::moveSelectedLinesUp()   { send(SCI_MOVESELECTEDLINESUP); }
void ScintillaView::moveSelectedLinesDown() { send(SCI_MOVESELECTEDLINESDOWN); }

// ---------------------------------------------------------------------------
// Extended case conversions
// ---------------------------------------------------------------------------

void ScintillaView::toTitleCase() {
    std::string sel = getSelectedText();
    if (sel.empty()) return;
    bool newWord = true;
    for (char& c : sel) {
        if (std::isspace((unsigned char)c) || c == '_' || c == '-') {
            newWord = true;
        } else if (newWord) {
            c = (char)std::toupper((unsigned char)c);
            newWord = false;
        } else {
            c = (char)std::tolower((unsigned char)c);
        }
    }
    send(SCI_REPLACESEL, 0, (sptr_t)sel.c_str());
}

void ScintillaView::toCamelCase() {
    std::string sel = getSelectedText();
    if (sel.empty()) return;
    std::string out;
    bool nextUpper = false;
    bool first = true;
    for (char c : sel) {
        if (c == '_' || c == '-' || c == ' ') {
            nextUpper = !first;
        } else {
            if (nextUpper) {
                out += (char)std::toupper((unsigned char)c);
                nextUpper = false;
            } else if (first) {
                out += (char)std::tolower((unsigned char)c);
            } else {
                out += c;
            }
            first = false;
        }
    }
    send(SCI_REPLACESEL, 0, (sptr_t)out.c_str());
}

void ScintillaView::toSnakeCase() {
    std::string sel = getSelectedText();
    if (sel.empty()) return;
    std::string out;
    for (size_t i = 0; i < sel.size(); ++i) {
        char c = sel[i];
        if (std::isupper((unsigned char)c)) {
            if (i > 0 && !std::isupper((unsigned char)sel[i-1]) && sel[i-1] != '_')
                out += '_';
            out += (char)std::tolower((unsigned char)c);
        } else if (c == ' ' || c == '-') {
            out += '_';
        } else {
            out += c;
        }
    }
    send(SCI_REPLACESEL, 0, (sptr_t)out.c_str());
}

// ---------------------------------------------------------------------------
// Whitespace / EOL operations
// ---------------------------------------------------------------------------

// Helper: get text of range [rangeStart..rangeEnd) from Scintilla
static std::string getLineRange(ScintillaObject* sci, int rangeStart, int rangeEnd) {
    int len = rangeEnd - rangeStart;
    if (len <= 0) return {};
    std::string text(len + 1, '\0');  // +1 for NUL terminator
    Sci_TextRangeFull tr;
    tr.chrg.cpMin = rangeStart;
    tr.chrg.cpMax = rangeEnd;
    tr.lpstrText  = &text[0];
    scintilla_send_message(sci, SCI_GETTEXTRANGEFULL, 0, (sptr_t)&tr);
    text.resize(len);
    return text;
}

// Helper: trim lines. mode: 0=trailing, 1=leading, 2=both
static void trimLinesHelper(ScintillaView* sv, int mode) {
    ScintillaObject* sci = SCINTILLA(sv->getWidget());
    auto S = [sci](unsigned int m, uptr_t w = 0, sptr_t l = 0) {
        return scintilla_send_message(sci, m, w, l);
    };
    int lineCount = (int)S(SCI_GETLINECOUNT);
    S(SCI_BEGINUNDOACTION);
    for (int line = 0; line < lineCount; ++line) {
        int lineStart = (int)S(SCI_POSITIONFROMLINE, line);
        int lineEnd   = (int)S(SCI_GETLINEENDPOSITION, line);
        if (lineEnd <= lineStart) continue;

        std::string text = getLineRange(sci, lineStart, lineEnd);

        // Compute leading/trailing trim bounds within the text string
        int trimFront = 0;
        int trimBack  = 0;

        if (mode == 1 || mode == 2) {
            while (trimFront < (int)text.size() &&
                   (text[trimFront] == ' ' || text[trimFront] == '\t'))
                ++trimFront;
        }
        if (mode == 0 || mode == 2) {
            int i = (int)text.size() - 1;
            while (i >= 0 && (text[i] == ' ' || text[i] == '\t')) { ++trimBack; --i; }
        }

        // Remove trailing first (so positions are stable), then leading
        if (trimBack > 0) {
            S(SCI_SETTARGETSTART, lineEnd - trimBack);
            S(SCI_SETTARGETEND,   lineEnd);
            S(SCI_REPLACETARGET, 0, (sptr_t)"");
        }
        if (trimFront > 0) {
            // Recompute lineStart (position may not have changed for leading)
            int ls2 = (int)S(SCI_POSITIONFROMLINE, line);
            S(SCI_SETTARGETSTART, ls2);
            S(SCI_SETTARGETEND,   ls2 + trimFront);
            S(SCI_REPLACETARGET, 0, (sptr_t)"");
        }
    }
    S(SCI_ENDUNDOACTION);
}

void ScintillaView::trimTrailingWhitespace() { trimLinesHelper(this, 0); }
void ScintillaView::trimLeadingWhitespace()  { trimLinesHelper(this, 1); }
void ScintillaView::trimAllWhitespace()      { trimLinesHelper(this, 2); }

void ScintillaView::tabsToSpaces() {
    int tabW = (int)send(SCI_GETTABWIDTH);
    if (tabW <= 0) tabW = 4;
    std::string spaces(tabW, ' ');
    // Use replaceAll without regex
    int flags = 0;
    send(SCI_BEGINUNDOACTION);
    int pos = 0;
    while (true) {
        send(SCI_SETTARGETSTART, pos);
        send(SCI_SETTARGETEND,   getLength());
        send(SCI_SETSEARCHFLAGS, flags);
        int found = (int)send(SCI_SEARCHINTARGET, 1, (sptr_t)"\t");
        if (found < 0) break;
        send(SCI_REPLACETARGET, spaces.size(), (sptr_t)spaces.c_str());
        pos = (int)send(SCI_GETTARGETEND);
        if (pos >= getLength()) break;
    }
    send(SCI_ENDUNDOACTION);
}

void ScintillaView::spacesToTabs() {
    int tabW = (int)send(SCI_GETTABWIDTH);
    if (tabW <= 0) tabW = 4;
    std::string spaces(tabW, ' ');
    int flags = 0;
    send(SCI_BEGINUNDOACTION);
    int pos = 0;
    while (true) {
        send(SCI_SETTARGETSTART, pos);
        send(SCI_SETTARGETEND,   getLength());
        send(SCI_SETSEARCHFLAGS, flags);
        int found = (int)send(SCI_SEARCHINTARGET,
                              spaces.size(), (sptr_t)spaces.c_str());
        if (found < 0) break;
        send(SCI_REPLACETARGET, 1, (sptr_t)"\t");
        pos = (int)send(SCI_GETTARGETEND);
        if (pos >= getLength()) break;
    }
    send(SCI_ENDUNDOACTION);
}

// ---------------------------------------------------------------------------
// Comment / uncomment
// ---------------------------------------------------------------------------

void ScintillaView::toggleLineComment() {
    // Determine comment prefix
    std::string prefix;
    switch (_currentLexer) {
        case LexerType::Cpp: case LexerType::CSharp:
        case LexerType::Java: case LexerType::JavaScript:
        case LexerType::Go:   case LexerType::Swift:
        case LexerType::Rust:
            prefix = "//"; break;
        case LexerType::Python: case LexerType::Bash:
        case LexerType::Ruby:   case LexerType::CMake:
        case LexerType::Makefile: case LexerType::YAML:
        case LexerType::Ini: case LexerType::Perl:
            prefix = "#"; break;
        case LexerType::SQL: case LexerType::Lua:
            prefix = "--"; break;
        case LexerType::VB:
            prefix = "'"; break;
        case LexerType::Asm:
            prefix = ";"; break;
        case LexerType::HTML: case LexerType::XML: case LexerType::PHP:
            return;  // no simple single-line comment form
        default:
            prefix = "//"; break;
    }

    int selStart = (int)send(SCI_GETSELECTIONSTART);
    int selEnd   = (int)send(SCI_GETSELECTIONEND);
    int startLine = (int)send(SCI_LINEFROMPOSITION, selStart);
    int endLine   = (int)send(SCI_LINEFROMPOSITION,
                              selEnd > selStart ? selEnd - 1 : selEnd);

    // Check if all selected lines already start with prefix (after indent)
    bool allCommented = true;
    for (int line = startLine; line <= endLine && allCommented; ++line) {
        int indentPos = (int)send(SCI_GETLINEINDENTPOSITION, line);
        int lineEnd   = (int)send(SCI_GETLINEENDPOSITION,    line);
        if (indentPos >= lineEnd) continue;  // blank line: skip check

        int readLen = std::min((int)prefix.size(), lineEnd - indentPos);
        std::string buf(readLen + 1, '\0');  // +1 for NUL
        Sci_TextRangeFull tr;
        tr.chrg.cpMin = indentPos;
        tr.chrg.cpMax = indentPos + readLen;
        tr.lpstrText  = &buf[0];
        send(SCI_GETTEXTRANGEFULL, 0, (sptr_t)&tr);
        buf.resize(readLen);
        if (buf != prefix) allCommented = false;
    }

    send(SCI_BEGINUNDOACTION);
    for (int line = startLine; line <= endLine; ++line) {
        int indentPos = (int)send(SCI_GETLINEINDENTPOSITION, line);
        int lineEnd   = (int)send(SCI_GETLINEENDPOSITION,    line);
        if (indentPos >= lineEnd && !allCommented) {
            // blank line: insert prefix
            send(SCI_INSERTTEXT, indentPos, (sptr_t)prefix.c_str());
            continue;
        }
        if (allCommented) {
            // Remove prefix
            send(SCI_SETTARGETSTART, indentPos);
            send(SCI_SETTARGETEND,   indentPos + (int)prefix.size());
            send(SCI_REPLACETARGET,  0, (sptr_t)"");
        } else {
            // Insert prefix
            send(SCI_INSERTTEXT, indentPos, (sptr_t)prefix.c_str());
        }
    }
    send(SCI_ENDUNDOACTION);
}

// ---------------------------------------------------------------------------
// Auto-complete
// ---------------------------------------------------------------------------

void ScintillaView::showAutoComplete() {
    int curPos    = (int)send(SCI_GETCURRENTPOS);
    int wordStart = (int)send(SCI_WORDSTARTPOSITION, curPos, 1);
    int wordLen   = curPos - wordStart;
    if (wordLen < 2) { send(SCI_AUTOCCANCEL); return; }

    // Performance guard: skip for large documents
    int docLen = getLength();
    if (docLen > 512 * 1024) return;

    // Extract current word prefix
    std::string prefix(wordLen + 1, '\0');  // +1 for NUL
    Sci_TextRangeFull trp;
    trp.chrg.cpMin = wordStart;
    trp.chrg.cpMax = curPos;
    trp.lpstrText  = &prefix[0];
    send(SCI_GETTEXTRANGEFULL, 0, (sptr_t)&trp);
    prefix.resize(wordLen);
    std::string prefixLower = toLower(prefix);

    // Collect all words from document
    std::string docText = getText();
    std::set<std::string> wordSet;
    std::string cur;
    for (unsigned char c : docText) {
        if (std::isalnum(c) || c == '_') {
            cur += (char)c;
        } else {
            if (cur.size() >= 2) wordSet.insert(cur);
            cur.clear();
        }
    }
    if (cur.size() >= 2) wordSet.insert(cur);

    // Build filtered word list (starts with prefix, exclude exact match)
    std::string wordList;
    for (const auto& w : wordSet) {
        if (w == prefix) continue;
        if (toLower(w).substr(0, prefixLower.size()) == prefixLower) {
            if (!wordList.empty()) wordList += ' ';
            wordList += w;
        }
    }

    if (!wordList.empty())
        send(SCI_AUTOCSHOW, wordLen, (sptr_t)wordList.c_str());
    else
        send(SCI_AUTOCCANCEL);
}

// ---------------------------------------------------------------------------
// Bookmarks
// ---------------------------------------------------------------------------

void ScintillaView::toggleBookmark() {
    int line = getCurrentLine();
    int mask = (int)send(SCI_MARKERGET, line);
    if (mask & 1) send(SCI_MARKERDELETE, line, 0);
    else          send(SCI_MARKERADD,    line, 0);
}

void ScintillaView::gotoNextBookmark() {
    int line = getCurrentLine();
    int next = (int)send(SCI_MARKERNEXT, line + 1, 1);
    if (next < 0) next = (int)send(SCI_MARKERNEXT, 0, 1);  // wrap
    if (next >= 0) gotoLine(next);
}

void ScintillaView::gotoPrevBookmark() {
    int line = getCurrentLine();
    int prev = (int)send(SCI_MARKERPREVIOUS, line - 1, 1);
    if (prev < 0) prev = (int)send(SCI_MARKERPREVIOUS, getLineCount() - 1, 1);
    if (prev >= 0) gotoLine(prev);
}

void ScintillaView::clearAllBookmarks() { send(SCI_MARKERDELETEALL, 0); }

// ---------------------------------------------------------------------------
// Info helpers
// ---------------------------------------------------------------------------

std::string ScintillaView::getLexerName() const {
    switch (_currentLexer) {
        case LexerType::Cpp:        return "C/C++";
        case LexerType::CSharp:     return "C#";
        case LexerType::Java:       return "Java";
        case LexerType::JavaScript: return "JavaScript";
        case LexerType::Python:     return "Python";
        case LexerType::HTML:       return "HTML";
        case LexerType::XML:        return "XML";
        case LexerType::CSS:        return "CSS";
        case LexerType::PHP:        return "PHP";
        case LexerType::Bash:       return "Bash";
        case LexerType::Makefile:   return "Makefile";
        case LexerType::SQL:        return "SQL";
        case LexerType::Lua:        return "Lua";
        case LexerType::Perl:       return "Perl";
        case LexerType::Ruby:       return "Ruby";
        case LexerType::Rust:       return "Rust";
        case LexerType::Go:         return "Go";
        case LexerType::CMake:      return "CMake";
        case LexerType::Ini:        return "INI";
        case LexerType::Diff:       return "Diff";
        case LexerType::Markdown:   return "Markdown";
        case LexerType::JSON:       return "JSON";
        case LexerType::YAML:       return "YAML";
        case LexerType::TCL:        return "Tcl";
        case LexerType::Pascal:     return "Pascal";
        case LexerType::Ada:        return "Ada";
        case LexerType::Fortran:    return "Fortran";
        case LexerType::Asm:        return "Assembly";
        case LexerType::VB:         return "VB";
        case LexerType::Swift:      return "Swift";
        default:                    return "Normal Text";
    }
}

int ScintillaView::getSelectionStart() const {
    return (int)send(SCI_GETSELECTIONSTART);
}

int ScintillaView::getSelectionEnd() const {
    return (int)send(SCI_GETSELECTIONEND);
}

// ---------------------------------------------------------------------------
// Phase 2: Find enhancements
// ---------------------------------------------------------------------------

// File-scope helper: iterate all matches in doc, call fn(startPos, endPos)
static int scv_searchAll(ScintillaObject* sci, const std::string& text,
                          bool matchCase, bool wholeWord, bool regex,
                          const std::function<void(int,int)>& fn) {
    if (text.empty()) return 0;
    int flags = 0;
    if (matchCase) flags |= SCFIND_MATCHCASE;
    if (wholeWord) flags |= SCFIND_WHOLEWORD;
    if (regex)     flags |= SCFIND_REGEXP | SCFIND_POSIX;

    int docLen = (int)scintilla_send_message(sci, SCI_GETLENGTH, 0, 0);
    scintilla_send_message(sci, SCI_SETSEARCHFLAGS, flags, 0);

    int pos = 0, count = 0;
    while (pos <= docLen) {
        scintilla_send_message(sci, SCI_SETTARGETSTART, pos, 0);
        scintilla_send_message(sci, SCI_SETTARGETEND,   docLen, 0);
        int found = (int)scintilla_send_message(sci, SCI_SEARCHINTARGET,
                                                 text.size(), (sptr_t)text.c_str());
        if (found < 0) break;
        int end = (int)scintilla_send_message(sci, SCI_GETTARGETEND, 0, 0);
        fn(found, end);
        ++count;
        pos = (end > found) ? end : found + 1;
    }
    return count;
}

int ScintillaView::highlightAllMatches(const std::string& text,
                                        bool matchCase, bool wholeWord, bool regex) {
    if (text.empty()) { clearAllHighlights(); return 0; }
    ScintillaObject* sci = SCINTILLA(_sci);
    // Configure indicator 0: yellow rounded box
    send(SCI_INDICSETSTYLE,      0, INDIC_ROUNDBOX);
    send(SCI_INDICSETFORE,       0, 0xFFFF00);   // yellow (RGB)
    send(SCI_INDICSETALPHA,      0, 80);
    send(SCI_INDICSETOUTLINEALPHA, 0, 150);
    send(SCI_SETINDICATORCURRENT, 0);
    send(SCI_INDICATORCLEARRANGE, 0, getLength());
    return scv_searchAll(sci, text, matchCase, wholeWord, regex,
                         [this](int s, int e) {
                             send(SCI_INDICATORFILLRANGE, s, e - s);
                         });
}

void ScintillaView::clearAllHighlights() {
    send(SCI_SETINDICATORCURRENT, 0);
    send(SCI_INDICATORCLEARRANGE, 0, getLength());
}

int ScintillaView::selectAllMatches(const std::string& text,
                                     bool matchCase, bool wholeWord, bool regex) {
    if (text.empty()) return 0;
    ScintillaObject* sci = SCINTILLA(_sci);
    bool first = true;
    return scv_searchAll(sci, text, matchCase, wholeWord, regex,
                         [&](int s, int e) {
                             if (first) {
                                 send(SCI_SETSEL, s, e);
                                 first = false;
                             } else {
                                 send(SCI_ADDSELECTION, e, s);
                             }
                         });
}

int ScintillaView::countMatches(const std::string& text,
                                 bool matchCase, bool wholeWord, bool regex) {
    if (text.empty()) return 0;
    ScintillaObject* sci = SCINTILLA(_sci);
    return scv_searchAll(sci, text, matchCase, wholeWord, regex,
                         [](int, int) {});
}

int ScintillaView::bookmarkAllMatches(const std::string& text,
                                       bool matchCase, bool wholeWord, bool regex) {
    if (text.empty()) return 0;
    ScintillaObject* sci = SCINTILLA(_sci);
    return scv_searchAll(sci, text, matchCase, wholeWord, regex,
                         [sci](int s, int /*e*/) {
                             int line = (int)scintilla_send_message(
                                 sci, SCI_LINEFROMPOSITION, s, 0);
                             scintilla_send_message(sci, SCI_MARKERADD, line, 0);
                         });
}

// ---------------------------------------------------------------------------
// Phase 3: DocumentMap helpers
// ---------------------------------------------------------------------------

int ScintillaView::getFirstVisibleLine() const {
    return (int)send(SCI_GETFIRSTVISIBLELINE);
}

int ScintillaView::getLinesOnScreen() const {
    return (int)send(SCI_LINESONSCREEN);
}

int ScintillaView::getLineLength(int line) const {
    if (line < 0 || line >= getLineCount()) return 0;
    int start = (int)send(SCI_POSITIONFROMLINE,  line);
    int end   = (int)send(SCI_GETLINEENDPOSITION, line);
    return std::max(0, end - start);
}

// Phase 5: macro playback — send a SCI_* message exactly as recorded
void ScintillaView::replayMessage(unsigned int msg, uptr_t wp, sptr_t lp) {
    send(msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Phase 6: Edge column
// ---------------------------------------------------------------------------

void ScintillaView::setEdgeMode(bool show) {
    send(SCI_SETEDGEMODE, show ? EDGE_BACKGROUND : EDGE_NONE);
}

void ScintillaView::setEdgeColumn(int col) {
    send(SCI_SETEDGECOLUMN, col);
}
