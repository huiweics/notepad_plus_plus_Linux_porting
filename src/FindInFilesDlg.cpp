#include "FindInFilesDlg.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <regex>

namespace fs = std::filesystem;

FindInFilesDlg::FindInFilesDlg(GtkWindow* parent, ResultCb resultCb, DoneCb doneCb)
    : _parent(parent)
    , _resultCb(std::move(resultCb))
    , _doneCb(std::move(doneCb))
{
    buildDialog();
}

FindInFilesDlg::~FindInFilesDlg() {
    cancelSearch();
    if (_dialog) gtk_widget_destroy(_dialog);
}

void FindInFilesDlg::buildDialog() {
    _dialog = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(_dialog), "Find in Files");
    gtk_window_set_transient_for(GTK_WINDOW(_dialog), _parent);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(_dialog), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(_dialog), 500, -1);
    gtk_container_set_border_width(GTK_CONTAINER(_dialog), 10);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_add(GTK_CONTAINER(_dialog), vbox);

    // Search text row
    {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(vbox), row, FALSE, FALSE, 2);
        GtkWidget* lbl = gtk_label_new("Search:");
        gtk_widget_set_size_request(lbl, 80, -1);
        gtk_label_set_xalign(GTK_LABEL(lbl), 1.0f);
        gtk_box_pack_start(GTK_BOX(row), lbl, FALSE, FALSE, 0);
        _searchEntry = gtk_entry_new();
        gtk_box_pack_start(GTK_BOX(row), _searchEntry, TRUE, TRUE, 0);
    }

    // Directory row
    {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(vbox), row, FALSE, FALSE, 2);
        GtkWidget* lbl = gtk_label_new("Directory:");
        gtk_widget_set_size_request(lbl, 80, -1);
        gtk_label_set_xalign(GTK_LABEL(lbl), 1.0f);
        gtk_box_pack_start(GTK_BOX(row), lbl, FALSE, FALSE, 0);
        _dirChooser = gtk_file_chooser_button_new("Select Directory",
                                                   GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(_dirChooser),
                                             g_get_home_dir());
        gtk_box_pack_start(GTK_BOX(row), _dirChooser, TRUE, TRUE, 0);
    }

    // Filter row
    {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(vbox), row, FALSE, FALSE, 2);
        GtkWidget* lbl = gtk_label_new("Filter:");
        gtk_widget_set_size_request(lbl, 80, -1);
        gtk_label_set_xalign(GTK_LABEL(lbl), 1.0f);
        gtk_box_pack_start(GTK_BOX(row), lbl, FALSE, FALSE, 0);
        _filterEntry = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(_filterEntry), "*");
        gtk_entry_set_placeholder_text(GTK_ENTRY(_filterEntry), "*.cpp;*.h;*.py");
        gtk_box_pack_start(GTK_BOX(row), _filterEntry, TRUE, TRUE, 0);
    }

    // Options frame
    {
        GtkWidget* optFrame = gtk_frame_new("Options");
        gtk_box_pack_start(GTK_BOX(vbox), optFrame, FALSE, FALSE, 4);
        GtkWidget* optRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_container_set_border_width(GTK_CONTAINER(optRow), 6);
        gtk_container_add(GTK_CONTAINER(optFrame), optRow);
        _matchCaseCheck = gtk_check_button_new_with_label("Match case");
        _regexCheck     = gtk_check_button_new_with_label("Regular expression");
        _recursiveCheck = gtk_check_button_new_with_label("Recursive");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_recursiveCheck), TRUE);
        gtk_box_pack_start(GTK_BOX(optRow), _matchCaseCheck, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(optRow), _regexCheck,     FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(optRow), _recursiveCheck, FALSE, FALSE, 0);
    }

    // Status label
    _statusLabel = gtk_label_new("");
    gtk_widget_set_halign(_statusLabel, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), _statusLabel, FALSE, FALSE, 2);

    // Button row
    {
        GtkWidget* btnRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(vbox), btnRow, FALSE, FALSE, 4);
        _findBtn              = gtk_button_new_with_label("Find All");
        GtkWidget* closeBtn   = gtk_button_new_with_label("Close");
        gtk_box_pack_start(GTK_BOX(btnRow), _findBtn,  FALSE, FALSE, 0);
        gtk_box_pack_end  (GTK_BOX(btnRow), closeBtn,  FALSE, FALSE, 0);
        g_signal_connect(_findBtn, "clicked", G_CALLBACK(onFindClicked),  this);
        g_signal_connect(closeBtn, "clicked", G_CALLBACK(onCloseClicked), this);
    }

    g_signal_connect(_dialog, "delete-event",    G_CALLBACK(onDelete), this);
    g_signal_connect(_dialog, "key-press-event", G_CALLBACK(onKey),    this);

    gtk_widget_show_all(_dialog);
    gtk_widget_hide(_dialog);
}

void FindInFilesDlg::show() {
    gtk_widget_show(_dialog);
    gtk_window_present(GTK_WINDOW(_dialog));
    gtk_widget_grab_focus(_searchEntry);
}

bool FindInFilesDlg::isVisible() const {
    return _dialog && gtk_widget_get_visible(_dialog);
}

void FindInFilesDlg::hide() {
    if (_dialog) gtk_widget_hide(_dialog);
}

void FindInFilesDlg::setStatus(const std::string& msg, bool error) {
    if (error) {
        std::string m = "<span color='red'>" + msg + "</span>";
        gtk_label_set_markup(GTK_LABEL(_statusLabel), m.c_str());
    } else {
        gtk_label_set_text(GTK_LABEL(_statusLabel), msg.c_str());
    }
}

void FindInFilesDlg::cancelSearch() {
    if (_idleSource) {
        g_source_remove(_idleSource);
        _idleSource = 0;
    }
    delete _searchCtx;
    _searchCtx = nullptr;
    gtk_button_set_label(GTK_BUTTON(_findBtn), "Find All");
    gtk_widget_set_sensitive(_findBtn, TRUE);
}

void FindInFilesDlg::startSearch() {
    const char* text = gtk_entry_get_text(GTK_ENTRY(_searchEntry));
    if (!text || text[0] == '\0') {
        setStatus("Please enter a search term.", true);
        return;
    }
    gchar* dir = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(_dirChooser));
    if (!dir) {
        setStatus("Please select a directory.", true);
        return;
    }

    cancelSearch();  // cancel any existing search

    std::string dirStr = dir;
    g_free(dir);

    std::string filter    = gtk_entry_get_text(GTK_ENTRY(_filterEntry));
    bool recursive  = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(_recursiveCheck));
    bool matchCase  = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(_matchCaseCheck));
    bool useRegex   = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(_regexCheck));

    // Collect all candidate files
    auto* ctx = new SearchCtx;
    ctx->dlg        = this;
    ctx->searchText = text;
    ctx->matchCase  = matchCase;
    ctx->useRegex   = useRegex;

    try {
        auto addFile = [&](const fs::path& p) {
            if (fs::is_regular_file(p) && matchesFilter(p.filename().string(), filter))
                ctx->files.push_back(p.string());
        };
        if (recursive) {
            for (auto& entry : fs::recursive_directory_iterator(
                    dirStr, fs::directory_options::skip_permission_denied))
                addFile(entry.path());
        } else {
            for (auto& entry : fs::directory_iterator(dirStr))
                addFile(entry.path());
        }
    } catch (...) {
        delete ctx;
        setStatus("Cannot access directory: " + dirStr, true);
        return;
    }

    if (ctx->files.empty()) {
        delete ctx;
        setStatus("No files match the filter.", true);
        return;
    }

    _searchCtx = ctx;
    setStatus("Searching... 0/" + std::to_string(ctx->files.size()) + " files");
    gtk_button_set_label(GTK_BUTTON(_findBtn), "Stop");

    // Start idle-based search
    _idleSource = g_idle_add(idleSearchStep, this);
}

// Called from idle: returns TRUE to continue, FALSE when done
gboolean FindInFilesDlg::idleSearchStep(gpointer data) {
    auto* self = static_cast<FindInFilesDlg*>(data);
    auto* ctx  = self->_searchCtx;
    if (!ctx) return G_SOURCE_REMOVE;

    if (ctx->cancelled || ctx->fileIdx >= ctx->files.size()) {
        self->_idleSource = 0;
        self->onSearchDone(ctx->totalMatches, ctx->cancelled);
        delete ctx;
        self->_searchCtx = nullptr;
        return G_SOURCE_REMOVE;
    }

    // Process up to 5 files per idle step for responsiveness
    int processed = 0;
    while (ctx->fileIdx < ctx->files.size() && processed < 5) {
        const std::string& path = ctx->files[ctx->fileIdx++];
        ++processed;

        std::ifstream f(path, std::ios::binary);
        if (!f) continue;

        // Skip binary files (check first 512 bytes for null bytes)
        {
            char buf[512];
            std::streamsize n = f.read(buf, sizeof(buf)).gcount();
            bool binary = false;
            for (std::streamsize i = 0; i < n; ++i)
                if (buf[i] == '\0') { binary = true; break; }
            if (binary) continue;
            f.seekg(0);
        }

        int lineNum = 0;
        std::string line;

        if (ctx->useRegex) {
            std::regex rx;
            try {
                auto flags = std::regex::ECMAScript;
                if (!ctx->matchCase) flags |= std::regex::icase;
                rx = std::regex(ctx->searchText, flags);
            } catch (...) {
                ctx->cancelled = true;
                self->setStatus("Invalid regular expression.", true);
                break;
            }
            while (std::getline(f, line)) {
                ++lineNum;
                if (std::regex_search(line, rx)) {
                    ++ctx->totalMatches;
                    self->onSearchResult(path, lineNum - 1, line);
                }
            }
        } else if (ctx->matchCase) {
            while (std::getline(f, line)) {
                ++lineNum;
                if (line.find(ctx->searchText) != std::string::npos) {
                    ++ctx->totalMatches;
                    self->onSearchResult(path, lineNum - 1, line);
                }
            }
        } else {
            // Case-insensitive search
            std::string lsearch = ctx->searchText;
            std::transform(lsearch.begin(), lsearch.end(), lsearch.begin(), ::tolower);
            while (std::getline(f, line)) {
                ++lineNum;
                std::string lline = line;
                std::transform(lline.begin(), lline.end(), lline.begin(), ::tolower);
                if (lline.find(lsearch) != std::string::npos) {
                    ++ctx->totalMatches;
                    self->onSearchResult(path, lineNum - 1, line);
                }
            }
        }
    }

    // Update progress
    if (ctx && !ctx->cancelled) {
        self->setStatus("Searching... " + std::to_string(ctx->fileIdx) +
                        "/" + std::to_string(ctx->files.size()) + " files, " +
                        std::to_string(ctx->totalMatches) + " match(es)");
    }

    return G_SOURCE_CONTINUE;
}

void FindInFilesDlg::onSearchResult(const std::string& path, int line,
                                     const std::string& text) {
    // Trim leading whitespace from display text
    std::string trimmed = text;
    auto it = std::find_if(trimmed.begin(), trimmed.end(),
                            [](unsigned char c){ return !std::isspace(c); });
    trimmed.erase(trimmed.begin(), it);

    _resultCb(FindInFilesResult{path, line, trimmed});
}

void FindInFilesDlg::onSearchDone(int total, bool wasCancelled) {
    gtk_button_set_label(GTK_BUTTON(_findBtn), "Find All");
    gtk_widget_set_sensitive(_findBtn, TRUE);

    if (wasCancelled)
        setStatus("Search cancelled. " + std::to_string(total) + " match(es) found.");
    else if (total == 0)
        setStatus("No matches found.", true);
    else
        setStatus("Done. " + std::to_string(total) + " match(es) found.");

    _doneCb(total, wasCancelled);
}

bool FindInFilesDlg::matchesFilter(const std::string& filename, const std::string& filter) {
    if (filter.empty() || filter == "*") return true;
    std::istringstream ss(filter);
    std::string pat;
    while (std::getline(ss, pat, ';')) {
        // Trim whitespace
        while (!pat.empty() && std::isspace((unsigned char)pat.front())) pat.erase(pat.begin());
        while (!pat.empty() && std::isspace((unsigned char)pat.back()))  pat.pop_back();
        if (pat.empty() || pat == "*") return true;
        if (pat.size() >= 2 && pat[0] == '*' && pat[1] == '.') {
            const std::string ext = pat.substr(1);
            if (filename.size() >= ext.size() &&
                filename.compare(filename.size() - ext.size(), ext.size(), ext) == 0)
                return true;
        } else if (filename == pat) {
            return true;
        }
    }
    return false;
}

void FindInFilesDlg::onFindClicked(GtkButton*, gpointer d) {
    auto* self = static_cast<FindInFilesDlg*>(d);
    if (self->_searchCtx) {
        // Currently searching: Stop
        self->_searchCtx->cancelled = true;
    } else {
        self->startSearch();
    }
}

void FindInFilesDlg::onCloseClicked(GtkButton*, gpointer d) {
    static_cast<FindInFilesDlg*>(d)->hide();
}

gboolean FindInFilesDlg::onDelete(GtkWidget*, GdkEvent*, gpointer d) {
    static_cast<FindInFilesDlg*>(d)->hide();
    return TRUE;
}

gboolean FindInFilesDlg::onKey(GtkWidget*, GdkEventKey* ev, gpointer d) {
    if (ev->keyval == GDK_KEY_Escape) {
        static_cast<FindInFilesDlg*>(d)->hide();
        return TRUE;
    }
    return FALSE;
}
