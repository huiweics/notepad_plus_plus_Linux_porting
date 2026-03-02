#include "Parameters.h"
#include <fstream>
#include <sys/stat.h>
#include <filesystem>

namespace fs = std::filesystem;

Parameters& Parameters::getInstance() {
    static Parameters instance;
    return instance;
}

std::string Parameters::getConfigDir() const {
    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) return std::string(xdg) + "/notepad++";
    const char* home = getenv("HOME");
    return std::string(home ? home : "/tmp") + "/.config/notepad++";
}

std::string Parameters::getConfigFile() const {
    return getConfigDir() + "/settings.conf";
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

void Parameters::load() {
    std::ifstream f(getConfigFile());
    if (!f.is_open()) return;

    auto& s = _settings;
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        try {
            if      (key == "wordWrap")         s.wordWrap         = (val == "1");
            else if (key == "showWhitespace")   s.showWhitespace   = (val == "1");
            else if (key == "showLineNumbers")  s.showLineNumbers  = (val == "1");
            else if (key == "showStatusBar")    s.showStatusBar    = (val == "1");
            else if (key == "showToolbar")      s.showToolbar      = (val == "1");
            else if (key == "autoIndent")       s.autoIndent       = (val == "1");
            else if (key == "tabUseSpaces")     s.tabUseSpaces     = (val == "1");
            else if (key == "tabWidth")         s.tabWidth         = std::stoi(val);
            else if (key == "zoomLevel")        s.zoomLevel        = std::stoi(val);
            else if (key == "fontName")         s.fontName         = val;
            else if (key == "fontSize")         s.fontSize         = std::stoi(val);
            else if (key == "theme")            s.theme            = val;
            else if (key == "maxRecentFiles")   s.maxRecentFiles   = std::stoi(val);
            else if (key == "windowX")          s.windowX          = std::stoi(val);
            else if (key == "windowY")          s.windowY          = std::stoi(val);
            else if (key == "windowWidth")      s.windowWidth      = std::stoi(val);
            else if (key == "windowHeight")     s.windowHeight     = std::stoi(val);
            else if (key == "windowMaximized")  s.windowMaximized  = (val == "1");
            else if (key == "searchMatchCase")  s.searchMatchCase  = (val == "1");
            else if (key == "searchWholeWord")  s.searchWholeWord  = (val == "1");
            else if (key == "searchRegex")      s.searchRegex      = (val == "1");
            else if (key == "searchWrapAround") s.searchWrapAround = (val == "1");
            else if (key == "restoreSession")   s.restoreSession   = (val == "1");
            else if (key == "edgeEnabled")      s.edgeEnabled      = (val == "1");
            else if (key == "edgeColumn")       s.edgeColumn       = std::stoi(val);
            else if (key == "recent") {
                auto sep = val.rfind(':');
                RecentFile rf;
                if (sep != std::string::npos) {
                    rf.path       = val.substr(0, sep);
                    rf.lineNumber = std::stoi(val.substr(sep + 1));
                } else {
                    rf.path = val;
                }
                if (!rf.path.empty()) s.recentFiles.push_back(rf);
            }
        } catch (...) {}
    }
}

void Parameters::save() {
    std::string dir = getConfigDir();
    mkdir(dir.c_str(), 0755);

    std::ofstream f(getConfigFile());
    if (!f.is_open()) return;

    auto& s = _settings;
    auto b = [](bool v) { return v ? "1" : "0"; };

    f << "# Notepad++ Linux settings\n"
      << "wordWrap="         << b(s.wordWrap)         << "\n"
      << "showWhitespace="   << b(s.showWhitespace)   << "\n"
      << "showLineNumbers="  << b(s.showLineNumbers)  << "\n"
      << "showStatusBar="    << b(s.showStatusBar)    << "\n"
      << "showToolbar="      << b(s.showToolbar)      << "\n"
      << "autoIndent="       << b(s.autoIndent)       << "\n"
      << "tabUseSpaces="     << b(s.tabUseSpaces)     << "\n"
      << "tabWidth="         << s.tabWidth            << "\n"
      << "zoomLevel="        << s.zoomLevel           << "\n"
      << "fontName="         << s.fontName            << "\n"
      << "fontSize="         << s.fontSize            << "\n"
      << "theme="            << s.theme               << "\n"
      << "maxRecentFiles="   << s.maxRecentFiles      << "\n"
      << "windowX="          << s.windowX             << "\n"
      << "windowY="          << s.windowY             << "\n"
      << "windowWidth="      << s.windowWidth         << "\n"
      << "windowHeight="     << s.windowHeight        << "\n"
      << "windowMaximized="  << b(s.windowMaximized)  << "\n"
      << "searchMatchCase="  << b(s.searchMatchCase)  << "\n"
      << "searchWholeWord="  << b(s.searchWholeWord)  << "\n"
      << "searchRegex="      << b(s.searchRegex)      << "\n"
      << "searchWrapAround=" << b(s.searchWrapAround) << "\n"
      << "restoreSession="   << b(s.restoreSession)   << "\n"
      << "edgeEnabled="      << b(s.edgeEnabled)      << "\n"
      << "edgeColumn="       << s.edgeColumn          << "\n";

    for (auto& rf : s.recentFiles)
        f << "recent=" << rf.path << ":" << rf.lineNumber << "\n";
}

void Parameters::addRecentFile(const std::string& path, int lineNumber) {
    auto& files = _settings.recentFiles;
    for (auto it = files.begin(); it != files.end(); ++it) {
        if (it->path == path) { files.erase(it); break; }
    }
    files.insert(files.begin(), {path, lineNumber});
    if ((int)files.size() > _settings.maxRecentFiles)
        files.resize(_settings.maxRecentFiles);
}

void Parameters::removeRecentFile(const std::string& path) {
    auto& files = _settings.recentFiles;
    for (auto it = files.begin(); it != files.end(); ++it) {
        if (it->path == path) { files.erase(it); return; }
    }
}

// ---------------------------------------------------------------------------
// Phase 6: session persistence
// ---------------------------------------------------------------------------

std::string Parameters::getSessionFile() const {
    return getConfigDir() + "/session.conf";
}

void Parameters::saveSession(const std::vector<SessionFile>& files, int activeIdx) {
    std::string dir = getConfigDir();
    fs::create_directories(dir);
    std::ofstream f(getSessionFile());
    if (!f) return;
    f << "# Notepad++ session\n";
    f << "active=" << activeIdx << "\n";
    for (const auto& sf : files)
        if (!sf.path.empty())
            f << "file=" << sf.path << ":" << sf.cursorLine << "\n";
}

Parameters::SessionData Parameters::loadSession() const {
    SessionData data;
    std::ifstream f(getSessionFile());
    if (!f) return data;
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        if (key == "active") {
            try { data.activeIdx = std::stoi(val); } catch (...) {}
        } else if (key == "file") {
            auto sep = val.rfind(':');
            SessionFile sf;
            if (sep != std::string::npos) {
                sf.path       = val.substr(0, sep);
                try { sf.cursorLine = std::stoi(val.substr(sep + 1)); } catch (...) {}
            } else {
                sf.path = val;
            }
            if (!sf.path.empty()) data.files.push_back(sf);
        }
    }
    return data;
}
