#pragma once
// Notepad++ Linux port - Find in Files dialog

#include <gtk/gtk.h>
#include <string>
#include <functional>
#include <vector>

struct FindInFilesResult {
    std::string path;
    int         line;   // 0-based
    std::string text;   // trimmed line content
};

class FindInFilesDlg {
public:
    using ResultCb = std::function<void(const FindInFilesResult&)>;
    using DoneCb   = std::function<void(int totalCount, bool cancelled)>;

    explicit FindInFilesDlg(GtkWindow* parent, ResultCb resultCb, DoneCb doneCb);
    ~FindInFilesDlg();

    void show();
    bool isVisible() const;
    void hide();

private:
    GtkWindow* _parent    = nullptr;
    GtkWidget* _dialog    = nullptr;
    ResultCb   _resultCb;
    DoneCb     _doneCb;

    GtkWidget* _searchEntry    = nullptr;
    GtkWidget* _dirChooser     = nullptr;
    GtkWidget* _filterEntry    = nullptr;
    GtkWidget* _recursiveCheck = nullptr;
    GtkWidget* _matchCaseCheck = nullptr;
    GtkWidget* _regexCheck     = nullptr;
    GtkWidget* _statusLabel    = nullptr;
    GtkWidget* _findBtn        = nullptr;

    // Search state (main-thread idle-based, no threads)
    struct SearchCtx {
        FindInFilesDlg*           dlg;
        std::string               searchText;
        bool                      matchCase;
        bool                      useRegex;
        std::vector<std::string>  files;
        std::size_t               fileIdx    = 0;
        int                       totalMatches = 0;
        bool                      cancelled  = false;
    };
    SearchCtx* _searchCtx  = nullptr;   // non-null while searching
    guint      _idleSource = 0;

    void buildDialog();
    void startSearch();
    void cancelSearch();
    void setStatus(const std::string& msg, bool error = false);

    // Called from idle step when a result is found
    void onSearchResult(const std::string& path, int line, const std::string& text);
    // Called from idle step when search is complete
    void onSearchDone(int total, bool cancelled);

    static gboolean idleSearchStep(gpointer data);
    static bool     matchesFilter (const std::string& filename, const std::string& filter);

    static void     onFindClicked (GtkButton*, gpointer);
    static void     onCloseClicked(GtkButton*, gpointer);
    static gboolean onDelete      (GtkWidget*, GdkEvent*, gpointer);
    static gboolean onKey         (GtkWidget*, GdkEventKey*, gpointer);
};
