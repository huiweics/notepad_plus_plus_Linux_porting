#include "MacroManager.h"
#include "ScintillaView.h"
#include <Scintilla.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Messages whose lParam is a C-string pointer during SCN_MACRORECORD
// ---------------------------------------------------------------------------

static bool msgHasTextParam(unsigned int msg) {
    return msg == SCI_REPLACESEL ||
           msg == SCI_ADDTEXT    ||
           msg == SCI_INSERTTEXT;
}

// ---------------------------------------------------------------------------
// Helpers: hex encode / decode for persistence
// ---------------------------------------------------------------------------

std::string MacroManager::hexEncode(const std::string& s) {
    std::ostringstream out;
    for (unsigned char c : s)
        out << std::hex << std::setw(2) << std::setfill('0') << (int)c;
    return out.str();
}

std::string MacroManager::hexDecode(const std::string& h) {
    std::string out;
    for (size_t i = 0; i + 1 < h.size(); i += 2) {
        char buf[3] = { h[i], h[i+1], '\0' };
        out += (char)std::strtol(buf, nullptr, 16);
    }
    return out;
}

std::string MacroManager::configFilePath() {
    const char* home = getenv("HOME");
    if (home) return std::string(home) + "/.config/notepad++/macros.conf";
    return "/tmp/notepad++_macros.conf";
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

MacroManager::MacroManager() {
    loadFromFile();
}

MacroManager::~MacroManager() {
    saveToFile();
}

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------

void MacroManager::startRecord() {
    _current.clear();
    _recording = true;
}

void MacroManager::stopRecord() {
    _recording = false;
}

void MacroManager::appendAction(unsigned int msg, uptr_t wp, sptr_t lp) {
    Action a;
    a.msg    = msg;
    a.wParam = wp;
    a.lParam = lp;
    // For text-bearing messages capture the C-string while the pointer is valid
    if (msgHasTextParam(msg) && lp) {
        a.text   = reinterpret_cast<const char*>(lp);
        a.lParam = 0;
    }
    _current.push_back(std::move(a));
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

void MacroManager::play(ScintillaView* view, int times) {
    if (!view || _current.empty()) return;
    view->replayMessage(SCI_BEGINUNDOACTION);
    for (int t = 0; t < times; ++t) {
        for (const auto& a : _current) {
            sptr_t lp = a.text.empty() ? a.lParam
                                       : (sptr_t)a.text.c_str();
            view->replayMessage(a.msg, a.wParam, lp);
        }
    }
    view->replayMessage(SCI_ENDUNDOACTION);
}

// ---------------------------------------------------------------------------
// Named macros
// ---------------------------------------------------------------------------

bool MacroManager::saveNamed(const std::string& name) {
    if (name.empty() || _current.empty()) return false;
    _saved[name] = _current;
    saveToFile();
    return true;
}

bool MacroManager::deleteNamed(const std::string& name) {
    auto it = _saved.find(name);
    if (it == _saved.end()) return false;
    _saved.erase(it);
    saveToFile();
    return true;
}

std::vector<std::string> MacroManager::getNamedMacros() const {
    std::vector<std::string> v;
    v.reserve(_saved.size());
    for (const auto& kv : _saved) v.push_back(kv.first);
    return v;
}

void MacroManager::playNamed(const std::string& name,
                              ScintillaView* view, int times) {
    auto it = _saved.find(name);
    if (it == _saved.end() || !view) return;
    view->replayMessage(SCI_BEGINUNDOACTION);
    for (int t = 0; t < times; ++t) {
        for (const auto& a : it->second) {
            sptr_t lp = a.text.empty() ? a.lParam
                                       : (sptr_t)a.text.c_str();
            view->replayMessage(a.msg, a.wParam, lp);
        }
    }
    view->replayMessage(SCI_ENDUNDOACTION);
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
//
// File format:
//   [MacroName]
//   msg wParam lParam
//   msg wParam lParam T:hexencodedtext
//

void MacroManager::loadFromFile() {
    std::ifstream f(configFilePath());
    if (!f) return;

    std::string line, curName;
    std::vector<Action> curActs;

    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;

        if (line[0] == '[') {
            // Flush previous macro
            if (!curName.empty() && !curActs.empty())
                _saved[curName] = std::move(curActs);
            auto end = line.find(']');
            curName  = (end != std::string::npos) ? line.substr(1, end - 1)
                                                   : line.substr(1);
            curActs.clear();
        } else {
            std::istringstream ss(line);
            unsigned int msg;
            uptr_t wp;
            sptr_t lp;
            if (!(ss >> msg >> wp >> lp)) continue;
            Action a{msg, wp, lp, ""};
            std::string token;
            if (ss >> token && token.size() > 2 && token[0]=='T' && token[1]==':')
                a.text = hexDecode(token.substr(2));
            curActs.push_back(std::move(a));
        }
    }
    if (!curName.empty() && !curActs.empty())
        _saved[curName] = std::move(curActs);
}

void MacroManager::saveToFile() const {
    if (_saved.empty()) return;
    std::string path = configFilePath();
    auto slash = path.rfind('/');
    if (slash != std::string::npos)
        fs::create_directories(path.substr(0, slash));
    std::ofstream f(path);
    if (!f) return;
    f << "# Notepad++ Linux macro definitions\n";
    for (const auto& kv : _saved) {
        f << '[' << kv.first << "]\n";
        for (const auto& a : kv.second) {
            f << a.msg << ' ' << a.wParam << ' ' << a.lParam;
            if (!a.text.empty()) f << " T:" << hexEncode(a.text);
            f << '\n';
        }
    }
}
