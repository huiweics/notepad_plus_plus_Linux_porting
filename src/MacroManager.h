#pragma once
// Notepad++ Linux port - Macro recording and playback (Phase 5)
//
// Recording works via the Scintilla SCN_MACRORECORD notification:
//   SCI_STARTRECORD causes Scintilla to fire SCN_MACRORECORD for every
//   recordable editing action (cursor movement, text insert/delete, etc.).
//   The notification carries the SCI message + wParam + lParam, which we
//   store verbatim and replay on demand.

#include <Scintilla.h>
#include <string>
#include <vector>
#include <map>

class ScintillaView;   // forward

class MacroManager {
public:
    // One recorded step
    struct Action {
        unsigned int msg;
        uptr_t       wParam;
        sptr_t       lParam;   // 0 when text is stored in `text`
        std::string  text;     // non-empty for SCI_REPLACESEL / ADDTEXT / INSERTTEXT
    };

    MacroManager();
    ~MacroManager();

    bool isRecording()  const { return _recording; }
    bool hasRecording() const { return !_current.empty(); }

    // Recording control (caller must also send SCI_STARTRECORD / SCI_STOPRECORD
    // to each active Scintilla view — see NotepadPlusGtk callbacks)
    void startRecord();
    void stopRecord();

    // Called from the MacroRecordCb installed on each ScintillaView
    void appendAction(unsigned int msg, uptr_t wp, sptr_t lp);

    // Playback
    void play(ScintillaView* view, int times = 1);

    // Named macros (persisted to ~/.config/notepad++/macros.conf)
    bool saveNamed  (const std::string& name);      // copies _current
    bool deleteNamed(const std::string& name);
    std::vector<std::string> getNamedMacros() const;
    void playNamed  (const std::string& name, ScintillaView* view, int times = 1);

    void loadFromFile();
    void saveToFile() const;

private:
    bool _recording = false;
    std::vector<Action> _current;
    std::map<std::string, std::vector<Action>> _saved;

    static std::string configFilePath();
    static std::string hexEncode(const std::string& s);
    static std::string hexDecode(const std::string& s);
};
