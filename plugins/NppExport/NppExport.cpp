// NppExport.cpp  —  Notepad++ Linux bundled plugin (Phase 4)
// Exports the current document to a styled HTML file and opens it in the
// default browser.  For large files (> 512 KB) it falls back to plain text.

#include "npp_plugin_api.h"
#include <string>
#include <sstream>
#include <iomanip>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <glib.h>  // for g_spawn_command_line_async

static NppData g_nppData;

// ---- Helpers ----------------------------------------------------------------

static std::string colorToHex(int sciColor) {
    // Scintilla stores colors as BGR (0x00BBGGRR)
    int r = (sciColor      ) & 0xFF;
    int g = (sciColor >>  8) & 0xFF;
    int b = (sciColor >> 16) & 0xFF;
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    return buf;
}

static void htmlAppendChar(std::string& out, char c) {
    switch (c) {
        case '<': out += "&lt;";  break;
        case '>': out += "&gt;";  break;
        case '&': out += "&amp;"; break;
        case '"': out += "&quot;";break;
        default:  out += c;       break;
    }
}

// ---- HTML export ------------------------------------------------------------

static void doExportHtml() {
    GtkWidget* sci = npp_get_current_scintilla();
    if (!sci) return;
    ScintillaObject* scio = SCINTILLA(sci);

    int docLen = (int)scintilla_send_message(scio, SCI_GETLENGTH, 0, 0);
    if (docLen <= 0) return;

    // Read the document text
    std::string text(docLen + 1, '\0');
    scintilla_send_message(scio, SCI_GETTEXT, docLen + 1, (sptr_t)text.data());
    text.resize(docLen);

    // Default colors from STYLE_DEFAULT
    int defaultFg = (int)scintilla_send_message(scio, SCI_STYLEGETFORE, STYLE_DEFAULT, 0);
    int defaultBg = (int)scintilla_send_message(scio, SCI_STYLEGETBACK, STYLE_DEFAULT, 0);

    std::string html;
    html.reserve(docLen * 6);
    html += "<!DOCTYPE html>\n<html>\n<head>\n"
            "<meta charset=\"UTF-8\">\n"
            "<title>Exported from Notepad++</title>\n"
            "<style>\nbody { margin: 0; }\n"
            "pre { font-family: 'Courier New', Courier, monospace; "
            "font-size: 13px; padding: 10px; "
            "background: " + colorToHex(defaultBg) + "; "
            "color: " + colorToHex(defaultFg) + "; }\n"
            "</style>\n</head>\n<body>\n<pre>";

    bool styled = (docLen <= 512 * 1024);  // full styling only for files ≤ 512 KB
    int  prevStyle = -1;
    bool spanOpen  = false;

    for (int i = 0; i < docLen; ++i) {
        char ch = text[i];
        if (styled) {
            int style = (int)scintilla_send_message(scio, SCI_GETSTYLEAT, i, 0);
            if (style != prevStyle) {
                if (spanOpen) { html += "</span>"; spanOpen = false; }
                int fg   = (int)scintilla_send_message(scio, SCI_STYLEGETFORE,   style, 0);
                int bg   = (int)scintilla_send_message(scio, SCI_STYLEGETBACK,   style, 0);
                bool bld = (bool)scintilla_send_message(scio, SCI_STYLEGETBOLD,  style, 0);
                bool itl = (bool)scintilla_send_message(scio, SCI_STYLEGETITALIC,style, 0);
                // Only open a <span> when the style differs from the default
                if (fg != defaultFg || bg != defaultBg || bld || itl) {
                    html += "<span style=\"color:";
                    html += colorToHex(fg);
                    html += ";background:";
                    html += colorToHex(bg);
                    if (bld) html += ";font-weight:bold";
                    if (itl) html += ";font-style:italic";
                    html += "\">";
                    spanOpen = true;
                }
                prevStyle = style;
            }
        }
        htmlAppendChar(html, ch);
    }
    if (spanOpen) html += "</span>";
    html += "</pre>\n</body>\n</html>\n";

    // Write to a temporary file and open it in the default browser
    char tmpl[] = "/tmp/npp_export_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        GtkWidget* dlg = gtk_message_dialog_new(
            GTK_WINDOW(g_nppData.nppHandle), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "Cannot create temporary file for HTML export.");
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        return;
    }
    close(fd);

    std::string htmlPath = std::string(tmpl) + ".html";
    rename(tmpl, htmlPath.c_str());

    FILE* f = fopen(htmlPath.c_str(), "w");
    if (f) {
        fwrite(html.data(), 1, html.size(), f);
        fclose(f);
        std::string cmd = "xdg-open \"" + htmlPath + "\"";
        g_spawn_command_line_async(cmd.c_str(), nullptr);
    } else {
        GtkWidget* dlg = gtk_message_dialog_new(
            GTK_WINDOW(g_nppData.nppHandle), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
            "Cannot write HTML export to:\n%s", htmlPath.c_str());
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
    }
}

// ---- Plain text export (RTF stub — saves as plain .txt) ---------------------

static void doExportText() {
    GtkWidget* sci = npp_get_current_scintilla();
    if (!sci) return;
    ScintillaObject* scio = SCINTILLA(sci);

    int docLen = (int)scintilla_send_message(scio, SCI_GETLENGTH, 0, 0);
    if (docLen <= 0) return;

    std::string text(docLen + 1, '\0');
    scintilla_send_message(scio, SCI_GETTEXT, docLen + 1, (sptr_t)text.data());
    text.resize(docLen);

    GtkWidget* dlg = gtk_file_chooser_dialog_new(
        "Export as Plain Text", GTK_WINDOW(g_nppData.nppHandle),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save",   GTK_RESPONSE_ACCEPT,
        nullptr);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), "export.txt");

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (path) {
            FILE* f = fopen(path, "w");
            if (f) { fwrite(text.data(), 1, text.size(), f); fclose(f); }
            g_free(path);
        }
    }
    gtk_widget_destroy(dlg);
}

// ---- Plugin API exports -----------------------------------------------------

static FuncItem g_funcs[2];

extern "C" {

void setInfo(NppData d) {
    g_nppData = d;
}

const char* getName() {
    return "NppExport";
}

FuncItem* getFuncsArray(int* nbF) {
    strncpy(g_funcs[0].itemName, "Export to HTML",  FUNC_ITEM_NAME_LEN - 1);
    g_funcs[0].pFunc    = doExportHtml;
    g_funcs[0].cmdID    = 0;
    g_funcs[0].checked  = false;
    g_funcs[0].shortcut = nullptr;

    strncpy(g_funcs[1].itemName, "Export to Text File", FUNC_ITEM_NAME_LEN - 1);
    g_funcs[1].pFunc    = doExportText;
    g_funcs[1].cmdID    = 0;
    g_funcs[1].checked  = false;
    g_funcs[1].shortcut = nullptr;

    *nbF = 2;
    return g_funcs;
}

void beNotified(SCNotification* /*notif*/) {}

long messageProc(unsigned int /*msg*/, unsigned long /*wp*/, long /*lp*/) {
    return 0;
}

} // extern "C"
