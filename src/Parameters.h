#pragma once
// Notepad++ Linux port - application settings
// Persisted to ~/.config/notepad++/settings.conf

#include <string>
#include <vector>

struct RecentFile {
    std::string path;
    int lineNumber = 0;
};

struct SessionFile {
    std::string path;
    int cursorLine = 0;  // 0-based
};

struct AppSettings {
    bool wordWrap        = false;
    bool showWhitespace  = false;
    bool showLineNumbers = true;
    bool showStatusBar   = true;
    bool showToolbar     = true;
    bool autoIndent      = true;
    bool tabUseSpaces    = false;
    int  tabWidth        = 4;
    int  zoomLevel       = 0;

    std::string fontName = "Monospace";
    int         fontSize = 11;
    std::string theme    = "default";   // "default" | "dark" | "zenburn"

    int                     maxRecentFiles = 15;
    std::vector<RecentFile> recentFiles;

    int  windowX         = 100;
    int  windowY         = 100;
    int  windowWidth     = 1024;
    int  windowHeight    = 768;
    bool windowMaximized = false;

    bool searchMatchCase   = false;
    bool searchWholeWord   = false;
    bool searchRegex       = false;
    bool searchWrapAround  = true;

    // Phase 6
    bool restoreSession  = false;
    bool edgeEnabled     = true;
    int  edgeColumn      = 120;
};

class Parameters {
public:
    static Parameters& getInstance();

    AppSettings&       getSettings()       { return _settings; }
    const AppSettings& getSettings() const { return _settings; }

    void load();
    void save();

    std::string getConfigDir()  const;
    std::string getConfigFile() const;

    void addRecentFile(const std::string& path, int lineNumber = 0);
    void removeRecentFile(const std::string& path);

    // Phase 6: session persistence
    struct SessionData {
        std::vector<SessionFile> files;
        int activeIdx = 0;
    };
    void        saveSession(const std::vector<SessionFile>& files, int activeIdx);
    SessionData loadSession() const;
    std::string getSessionFile() const;

private:
    Parameters() = default;
    AppSettings _settings;
};
