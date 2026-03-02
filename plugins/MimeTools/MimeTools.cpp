// MimeTools.cpp  —  Notepad++ Linux bundled plugin (Phase 4)
// Provides Base64 encode/decode and URL encode/decode of selected text.

#include "npp_plugin_api.h"
#include <string>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <cstring>
#include <cstdlib>

// ---- Helpers ----------------------------------------------------------------

static NppData g_nppData;

// Get selected text from the current editor
static std::string getSelection() {
    GtkWidget* sci = npp_get_current_scintilla();
    if (!sci) return "";
    ScintillaObject* scio = SCINTILLA(sci);
    // SCI_GETSELTEXT returns byte count including null terminator
    int selLen = (int)scintilla_send_message(scio, SCI_GETSELTEXT, 0, 0);
    if (selLen <= 1) return "";  // empty or just null
    std::string buf(selLen, '\0');
    scintilla_send_message(scio, SCI_GETSELTEXT, 0, (sptr_t)buf.data());
    buf.resize(selLen - 1);  // strip trailing null
    return buf;
}

// Replace the current selection with text
static void replaceSelection(const std::string& s) {
    GtkWidget* sci = npp_get_current_scintilla();
    if (!sci) return;
    scintilla_send_message(SCINTILLA(sci), SCI_REPLACESEL, 0, (sptr_t)s.c_str());
}

// ---- Base64 -----------------------------------------------------------------

static const char kB64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64Encode(const std::string& in) {
    std::string out;
    int val = 0, bits = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        bits += 8;
        while (bits >= 0) {
            out += kB64Chars[(val >> bits) & 0x3F];
            bits -= 6;
        }
    }
    if (bits > -6)
        out += kB64Chars[((val << 8) >> (bits + 8)) & 0x3F];
    while (out.size() % 4) out += '=';
    return out;
}

static std::string base64Decode(const std::string& in) {
    std::string out;
    int T[256];
    for (int i = 0; i < 256; ++i) T[i] = -1;
    for (int i = 0; i < 64; ++i) T[(unsigned char)kB64Chars[i]] = i;
    int val = 0, bits = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        bits += 6;
        if (bits >= 0) {
            out += (char)((val >> bits) & 0xFF);
            bits -= 8;
        }
    }
    return out;
}

static void doBase64Encode() {
    std::string sel = getSelection();
    if (!sel.empty()) replaceSelection(base64Encode(sel));
}

static void doBase64Decode() {
    std::string sel = getSelection();
    if (!sel.empty()) replaceSelection(base64Decode(sel));
}

// ---- URL encoding -----------------------------------------------------------

static std::string urlEncode(const std::string& s) {
    std::ostringstream out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out << c;
        else
            out << '%' << std::uppercase << std::hex
                << std::setw(2) << std::setfill('0') << (int)c;
    }
    return out.str();
}

static std::string urlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            char hex[3] = { s[i+1], s[i+2], '\0' };
            out += (char)strtol(hex, nullptr, 16);
            i += 2;
        } else if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return out;
}

static void doUrlEncode() {
    std::string sel = getSelection();
    if (!sel.empty()) replaceSelection(urlEncode(sel));
}

static void doUrlDecode() {
    std::string sel = getSelection();
    if (!sel.empty()) replaceSelection(urlDecode(sel));
}

// ---- ROT-13 -----------------------------------------------------------------

static std::string rot13(const std::string& s) {
    std::string out(s);
    for (char& c : out) {
        if      (c >= 'a' && c <= 'z') c = 'a' + (c - 'a' + 13) % 26;
        else if (c >= 'A' && c <= 'Z') c = 'A' + (c - 'A' + 13) % 26;
    }
    return out;
}

static void doRot13() {
    std::string sel = getSelection();
    if (!sel.empty()) replaceSelection(rot13(sel));
}

// ---- ASCII <-> Hex ----------------------------------------------------------

static std::string asciiToHex(const std::string& s) {
    std::ostringstream out;
    for (unsigned char c : s)
        out << std::hex << std::setw(2) << std::setfill('0') << (int)c;
    return out.str();
}

static std::string hexToAscii(const std::string& s) {
    std::string out;
    for (size_t i = 0; i + 1 < s.size(); i += 2) {
        char hex[3] = { s[i], s[i+1], '\0' };
        out += (char)strtol(hex, nullptr, 16);
    }
    return out;
}

static void doAsciiToHex() {
    std::string sel = getSelection();
    if (!sel.empty()) replaceSelection(asciiToHex(sel));
}

static void doHexToAscii() {
    std::string sel = getSelection();
    if (!sel.empty()) replaceSelection(hexToAscii(sel));
}

// ---- Plugin API exports -----------------------------------------------------

static FuncItem g_funcs[7];

extern "C" {

void setInfo(NppData d) {
    g_nppData = d;
}

const char* getName() {
    return "MIME Tools";
}

FuncItem* getFuncsArray(int* nbF) {
    int i = 0;
    auto add = [&](const char* name, void(*fn)(void)) {
        strncpy(g_funcs[i].itemName, name, FUNC_ITEM_NAME_LEN - 1);
        g_funcs[i].itemName[FUNC_ITEM_NAME_LEN - 1] = '\0';
        g_funcs[i].pFunc    = fn;
        g_funcs[i].cmdID    = 0;
        g_funcs[i].checked  = false;
        g_funcs[i].shortcut = nullptr;
        ++i;
    };
    add("Base64 Encode",  doBase64Encode);
    add("Base64 Decode",  doBase64Decode);
    add("URL Encode",     doUrlEncode);
    add("URL Decode",     doUrlDecode);
    add("ROT-13",         doRot13);
    add("ASCII to Hex",   doAsciiToHex);
    add("Hex to ASCII",   doHexToAscii);
    *nbF = i;
    return g_funcs;
}

void beNotified(SCNotification* /*notif*/) {}

long messageProc(unsigned int /*msg*/, unsigned long /*wp*/, long /*lp*/) {
    return 0;
}

} // extern "C"
