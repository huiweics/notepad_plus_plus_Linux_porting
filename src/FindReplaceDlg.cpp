#include "FindReplaceDlg.h"
#include "ScintillaView.h"

FindReplaceDlg::FindReplaceDlg(GtkWindow* parent, GetViewCb getView)
    : _parent(parent), _getView(std::move(getView))
{
    buildDialog();
}

FindReplaceDlg::~FindReplaceDlg() {
    if (_dialog) gtk_widget_destroy(_dialog);
}

void FindReplaceDlg::buildDialog() {
    _dialog = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(_dialog), "Find");
    gtk_window_set_transient_for(GTK_WINDOW(_dialog), _parent);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(_dialog), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(_dialog), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(_dialog), 10);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_add(GTK_CONTAINER(_dialog), vbox);

    // Find row
    GtkWidget* findRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(vbox), findRow, FALSE, FALSE, 2);

    GtkWidget* findLbl = gtk_label_new("Find:");
    gtk_widget_set_size_request(findLbl, 75, -1);
    gtk_label_set_xalign(GTK_LABEL(findLbl), 1.0f);
    gtk_box_pack_start(GTK_BOX(findRow), findLbl, FALSE, FALSE, 0);
    _findEntry = gtk_entry_new();
    gtk_widget_set_size_request(_findEntry, 300, -1);
    gtk_box_pack_start(GTK_BOX(findRow), _findEntry, TRUE, TRUE, 0);

    // Replace row
    GtkWidget* replRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(vbox), replRow, FALSE, FALSE, 2);

    _replaceLabel = gtk_label_new("Replace:");
    gtk_widget_set_size_request(_replaceLabel, 75, -1);
    gtk_label_set_xalign(GTK_LABEL(_replaceLabel), 1.0f);
    gtk_box_pack_start(GTK_BOX(replRow), _replaceLabel, FALSE, FALSE, 0);
    _replaceEntry = gtk_entry_new();
    gtk_widget_set_size_request(_replaceEntry, 300, -1);
    gtk_box_pack_start(GTK_BOX(replRow), _replaceEntry, TRUE, TRUE, 0);

    // Options frame
    GtkWidget* optFrame = gtk_frame_new("Options");
    gtk_box_pack_start(GTK_BOX(vbox), optFrame, FALSE, FALSE, 4);
    GtkWidget* optGrid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(optGrid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(optGrid), 4);
    gtk_container_set_border_width(GTK_CONTAINER(optGrid), 6);
    gtk_container_add(GTK_CONTAINER(optFrame), optGrid);

    _matchCaseCheck  = gtk_check_button_new_with_label("Match case");
    _wholeWordCheck  = gtk_check_button_new_with_label("Whole word");
    _regexCheck      = gtk_check_button_new_with_label("Regular expression");
    _wrapAroundCheck = gtk_check_button_new_with_label("Wrap around");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_wrapAroundCheck), TRUE);
    gtk_grid_attach(GTK_GRID(optGrid), _matchCaseCheck,  0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(optGrid), _wholeWordCheck,  0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(optGrid), _regexCheck,      1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(optGrid), _wrapAroundCheck, 1, 1, 1, 1);

    // Direction frame
    GtkWidget* dirFrame = gtk_frame_new("Direction");
    gtk_box_pack_start(GTK_BOX(vbox), dirFrame, FALSE, FALSE, 4);
    GtkWidget* dirRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(dirRow), 6);
    gtk_container_add(GTK_CONTAINER(dirFrame), dirRow);
    _dirForward  = gtk_radio_button_new_with_label(nullptr, "Forward");
    _dirBackward = gtk_radio_button_new_with_label_from_widget(
                       GTK_RADIO_BUTTON(_dirForward), "Backward");
    gtk_box_pack_start(GTK_BOX(dirRow), _dirForward,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(dirRow), _dirBackward, FALSE, FALSE, 0);

    // Status
    _statusLabel = gtk_label_new("");
    gtk_widget_set_halign(_statusLabel, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), _statusLabel, FALSE, FALSE, 2);

    // Main button row
    GtkWidget* btnRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(vbox), btnRow, FALSE, FALSE, 4);

    GtkWidget* fnBtn  = gtk_button_new_with_label("Find Next");
    GtkWidget* fpBtn  = gtk_button_new_with_label("Find Prev");
    _replaceBtn       = gtk_button_new_with_label("Replace");
    _replaceAllBtn    = gtk_button_new_with_label("Replace All");
    _replaceInAllBtn  = gtk_button_new_with_label("In All Docs");
    GtkWidget* clsBtn = gtk_button_new_with_label("Close");

    gtk_box_pack_start(GTK_BOX(btnRow), fnBtn,            FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btnRow), fpBtn,            FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btnRow), _replaceBtn,      FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btnRow), _replaceAllBtn,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btnRow), _replaceInAllBtn, FALSE, FALSE, 0);
    gtk_box_pack_end  (GTK_BOX(btnRow), clsBtn,           FALSE, FALSE, 0);

    g_signal_connect(fnBtn,           "clicked", G_CALLBACK(onFindNext),    this);
    g_signal_connect(fpBtn,           "clicked", G_CALLBACK(onFindPrev),    this);
    g_signal_connect(_replaceBtn,     "clicked", G_CALLBACK(onReplace),     this);
    g_signal_connect(_replaceAllBtn,  "clicked", G_CALLBACK(onReplaceAll),  this);
    g_signal_connect(_replaceInAllBtn,"clicked", G_CALLBACK(onReplaceInAll),this);
    g_signal_connect(clsBtn,          "clicked", G_CALLBACK(onClose),       this);
    g_signal_connect(_findEntry, "activate",     G_CALLBACK(onActivate),    this);
    g_signal_connect(_dialog, "delete-event",    G_CALLBACK(onDelete),      this);
    g_signal_connect(_dialog, "key-press-event", G_CALLBACK(onKey),         this);

    // Actions frame (find mode: highlight / select / count / bookmark)
    _actionsFrame = gtk_frame_new("Actions");
    gtk_box_pack_start(GTK_BOX(vbox), _actionsFrame, FALSE, FALSE, 4);
    GtkWidget* actRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(actRow), 6);
    gtk_container_add(GTK_CONTAINER(_actionsFrame), actRow);

    _highlightAllBtn = gtk_button_new_with_label("Highlight All");
    _clearHlBtn      = gtk_button_new_with_label("Clear");
    _countBtn        = gtk_button_new_with_label("Count");
    _selectAllBtn    = gtk_button_new_with_label("Select All");
    _bookmarkAllBtn  = gtk_button_new_with_label("Bookmark All");

    gtk_box_pack_start(GTK_BOX(actRow), _highlightAllBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actRow), _clearHlBtn,      FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actRow), _countBtn,        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actRow), _selectAllBtn,    FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actRow), _bookmarkAllBtn,  FALSE, FALSE, 0);

    g_signal_connect(_highlightAllBtn, "clicked", G_CALLBACK(onHighlightAll), this);
    g_signal_connect(_clearHlBtn,      "clicked", G_CALLBACK(onClearHL),      this);
    g_signal_connect(_countBtn,        "clicked", G_CALLBACK(onCountAll),     this);
    g_signal_connect(_selectAllBtn,    "clicked", G_CALLBACK(onSelectAll),    this);
    g_signal_connect(_bookmarkAllBtn,  "clicked", G_CALLBACK(onBookmarkAll),  this);

    gtk_widget_show_all(_dialog);
    gtk_widget_hide(_dialog);
    setReplaceMode(false);
}

void FindReplaceDlg::showFind(const std::string& sel) {
    setReplaceMode(false);
    gtk_window_set_title(GTK_WINDOW(_dialog), "Find");
    if (!sel.empty()) gtk_entry_set_text(GTK_ENTRY(_findEntry), sel.c_str());
    gtk_widget_show(_dialog);
    gtk_window_present(GTK_WINDOW(_dialog));
    gtk_widget_grab_focus(_findEntry);
}

void FindReplaceDlg::showFindReplace(const std::string& sel) {
    setReplaceMode(true);
    gtk_window_set_title(GTK_WINDOW(_dialog), "Find / Replace");
    if (!sel.empty()) gtk_entry_set_text(GTK_ENTRY(_findEntry), sel.c_str());
    gtk_widget_show(_dialog);
    gtk_window_present(GTK_WINDOW(_dialog));
    gtk_widget_grab_focus(_findEntry);
}

bool FindReplaceDlg::isVisible() const {
    return _dialog && gtk_widget_get_visible(_dialog);
}

void FindReplaceDlg::hide() {
    if (_dialog) gtk_widget_hide(_dialog);
}

void FindReplaceDlg::focusFindEntry() {
    gtk_widget_grab_focus(_findEntry);
}

void FindReplaceDlg::setReplaceMode(bool on) {
    _replaceMode = on;
    if (on) {
        gtk_widget_show(_replaceLabel);
        gtk_widget_show(_replaceEntry);
        gtk_widget_set_sensitive(_replaceBtn,      TRUE);
        gtk_widget_set_sensitive(_replaceAllBtn,   TRUE);
        gtk_widget_set_sensitive(_replaceInAllBtn, TRUE);
        gtk_widget_hide(_actionsFrame);
    } else {
        gtk_widget_hide(_replaceLabel);
        gtk_widget_hide(_replaceEntry);
        gtk_widget_set_sensitive(_replaceBtn,      FALSE);
        gtk_widget_set_sensitive(_replaceAllBtn,   FALSE);
        gtk_widget_set_sensitive(_replaceInAllBtn, FALSE);
        gtk_widget_show(_actionsFrame);
    }
}

void FindReplaceDlg::syncFromWidgets() {
    _opts.findText    = gtk_entry_get_text(GTK_ENTRY(_findEntry));
    _opts.replaceText = gtk_entry_get_text(GTK_ENTRY(_replaceEntry));
    _opts.matchCase   = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(_matchCaseCheck));
    _opts.wholeWord   = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(_wholeWordCheck));
    _opts.regex       = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(_regexCheck));
    _opts.wrapAround  = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(_wrapAroundCheck));
    _opts.direction   = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(_dirBackward))
                        ? FindDirection::Backward : FindDirection::Forward;
}

void FindReplaceDlg::setStatus(const std::string& msg, bool error) {
    if (error) {
        std::string m = "<span color='red'>" + msg + "</span>";
        gtk_label_set_markup(GTK_LABEL(_statusLabel), m.c_str());
    } else {
        gtk_label_set_text(GTK_LABEL(_statusLabel), msg.c_str());
    }
}

void FindReplaceDlg::doFindNext(bool backward) {
    syncFromWidgets();
    if (_opts.findText.empty()) { setStatus(""); return; }
    ScintillaView* view = _getView();
    if (!view) return;

    bool bwd = backward || (_opts.direction == FindDirection::Backward);
    int  pos = -1;

    if (bwd) {
        int docLen   = view->getLength();
        int selStart = view->getSelectionStart();
        int tStart   = selStart > 0 ? selStart - 1 : 0;
        int flags    = 0;
        if (_opts.matchCase) flags |= SCFIND_MATCHCASE;
        if (_opts.wholeWord) flags |= SCFIND_WHOLEWORD;
        if (_opts.regex)     flags |= SCFIND_REGEXP | SCFIND_POSIX;

        scintilla_send_message(SCINTILLA(view->getWidget()),
                                SCI_SETTARGETSTART, tStart, 0);
        scintilla_send_message(SCINTILLA(view->getWidget()),
                                SCI_SETTARGETEND, 0, 0);
        scintilla_send_message(SCINTILLA(view->getWidget()),
                                SCI_SETSEARCHFLAGS, flags, 0);
        pos = (int)scintilla_send_message(SCINTILLA(view->getWidget()),
                                           SCI_SEARCHINTARGET,
                                           _opts.findText.size(),
                                           (sptr_t)_opts.findText.c_str());
        if (pos == -1 && _opts.wrapAround) {
            scintilla_send_message(SCINTILLA(view->getWidget()),
                                    SCI_SETTARGETSTART, docLen, 0);
            scintilla_send_message(SCINTILLA(view->getWidget()),
                                    SCI_SETTARGETEND, 0, 0);
            pos = (int)scintilla_send_message(SCINTILLA(view->getWidget()),
                                               SCI_SEARCHINTARGET,
                                               _opts.findText.size(),
                                               (sptr_t)_opts.findText.c_str());
        }
        if (pos >= 0) {
            int tEnd = (int)scintilla_send_message(SCINTILLA(view->getWidget()),
                                                    SCI_GETTARGETEND, 0, 0);
            scintilla_send_message(SCINTILLA(view->getWidget()),
                                    SCI_SETSEL, pos, tEnd);
            scintilla_send_message(SCINTILLA(view->getWidget()),
                                    SCI_SCROLLCARET, 0, 0);
        }
    } else {
        pos = view->findNext(_opts.findText, _opts.matchCase,
                             _opts.wholeWord, _opts.regex, _opts.wrapAround);
    }

    setStatus(pos >= 0 ? "" : "\"" + _opts.findText + "\" not found", pos < 0);
}

void FindReplaceDlg::doReplace() {
    syncFromWidgets();
    if (_opts.findText.empty()) return;
    ScintillaView* view = _getView();
    if (!view) return;
    bool found = view->replaceAndFindNext(_opts.findText, _opts.replaceText,
                                          _opts.matchCase, _opts.wholeWord,
                                          _opts.regex, _opts.wrapAround);
    setStatus(found ? "" : "\"" + _opts.findText + "\" not found", !found);
}

void FindReplaceDlg::doReplaceAll() {
    syncFromWidgets();
    if (_opts.findText.empty()) return;
    ScintillaView* view = _getView();
    if (!view) return;
    int n = view->replaceAll(_opts.findText, _opts.replaceText,
                              _opts.matchCase, _opts.wholeWord, _opts.regex);
    if (n > 0) setStatus(std::to_string(n) + " replacement(s) made.");
    else        setStatus("\"" + _opts.findText + "\" not found", true);
}

void FindReplaceDlg::doHighlightAll() {
    syncFromWidgets();
    if (_opts.findText.empty()) return;
    ScintillaView* view = _getView();
    if (!view) return;
    int n = view->highlightAllMatches(_opts.findText,
                                       _opts.matchCase, _opts.wholeWord, _opts.regex);
    setStatus(n > 0 ? std::to_string(n) + " match(es) highlighted."
                    : "\"" + _opts.findText + "\" not found", n == 0);
}

void FindReplaceDlg::doClearHighlights() {
    ScintillaView* view = _getView();
    if (view) view->clearAllHighlights();
    setStatus("");
}

void FindReplaceDlg::doCountAll() {
    syncFromWidgets();
    if (_opts.findText.empty()) return;
    ScintillaView* view = _getView();
    if (!view) return;
    int n = view->countMatches(_opts.findText,
                                _opts.matchCase, _opts.wholeWord, _opts.regex);
    setStatus(std::to_string(n) + " match(es) found.", n == 0);
}

void FindReplaceDlg::doSelectAll() {
    syncFromWidgets();
    if (_opts.findText.empty()) return;
    ScintillaView* view = _getView();
    if (!view) return;
    int n = view->selectAllMatches(_opts.findText,
                                    _opts.matchCase, _opts.wholeWord, _opts.regex);
    setStatus(n > 0 ? std::to_string(n) + " match(es) selected."
                    : "\"" + _opts.findText + "\" not found", n == 0);
}

void FindReplaceDlg::doBookmarkAll() {
    syncFromWidgets();
    if (_opts.findText.empty()) return;
    ScintillaView* view = _getView();
    if (!view) return;
    int n = view->bookmarkAllMatches(_opts.findText,
                                      _opts.matchCase, _opts.wholeWord, _opts.regex);
    setStatus(n > 0 ? std::to_string(n) + " line(s) bookmarked."
                    : "\"" + _opts.findText + "\" not found", n == 0);
}

void FindReplaceDlg::doReplaceInAllDocs() {
    syncFromWidgets();
    if (_opts.findText.empty() || !_replaceAllDocsCb) return;
    int n = _replaceAllDocsCb(_opts.findText, _opts.replaceText,
                               _opts.matchCase, _opts.wholeWord, _opts.regex);
    if (n > 0) setStatus(std::to_string(n) + " replacement(s) in all documents.");
    else        setStatus("\"" + _opts.findText + "\" not found in any document.", true);
}

void FindReplaceDlg::onFindNext    (GtkButton*, gpointer d) { ((FindReplaceDlg*)d)->doFindNext(false); }
void FindReplaceDlg::onFindPrev    (GtkButton*, gpointer d) { ((FindReplaceDlg*)d)->doFindNext(true); }
void FindReplaceDlg::onReplace     (GtkButton*, gpointer d) { ((FindReplaceDlg*)d)->doReplace(); }
void FindReplaceDlg::onReplaceAll  (GtkButton*, gpointer d) { ((FindReplaceDlg*)d)->doReplaceAll(); }
void FindReplaceDlg::onReplaceInAll(GtkButton*, gpointer d) { ((FindReplaceDlg*)d)->doReplaceInAllDocs(); }
void FindReplaceDlg::onClose       (GtkButton*, gpointer d) { ((FindReplaceDlg*)d)->hide(); }
void FindReplaceDlg::onActivate    (GtkEntry*,  gpointer d) { ((FindReplaceDlg*)d)->doFindNext(false); }
void FindReplaceDlg::onHighlightAll(GtkButton*, gpointer d) { ((FindReplaceDlg*)d)->doHighlightAll(); }
void FindReplaceDlg::onClearHL     (GtkButton*, gpointer d) { ((FindReplaceDlg*)d)->doClearHighlights(); }
void FindReplaceDlg::onCountAll    (GtkButton*, gpointer d) { ((FindReplaceDlg*)d)->doCountAll(); }
void FindReplaceDlg::onSelectAll   (GtkButton*, gpointer d) { ((FindReplaceDlg*)d)->doSelectAll(); }
void FindReplaceDlg::onBookmarkAll (GtkButton*, gpointer d) { ((FindReplaceDlg*)d)->doBookmarkAll(); }

gboolean FindReplaceDlg::onKey(GtkWidget*, GdkEventKey* ev, gpointer d) {
    auto* self = (FindReplaceDlg*)d;
    if (ev->keyval == GDK_KEY_Escape) { self->hide(); return TRUE; }
    if (ev->keyval == GDK_KEY_F3) {
        self->doFindNext((ev->state & GDK_SHIFT_MASK) != 0);
        return TRUE;
    }
    return FALSE;
}

gboolean FindReplaceDlg::onDelete(GtkWidget*, GdkEvent*, gpointer d) {
    ((FindReplaceDlg*)d)->hide();
    return TRUE;
}
