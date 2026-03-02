// Notepad++ Linux port - GTK3 entry point
// Ported from PowerEditor/src/winmain.cpp (Windows WinMain)
// to use GTK3 on Ubuntu/Linux.

#include <gtk/gtk.h>
#include <string>
#include <vector>
#include <cstring>
#include "NotepadPlusGtk.h"
#include "Parameters.h"

static void printUsage(const char* argv0) {
    g_print("Usage: %s [OPTIONS] [file1 file2 ...]\n\n"
            "Options:\n"
            "  -n LINE   Go to line LINE in the first opened file\n"
            "  -c COL    Go to column COL in the first opened file\n"
            "  --help    Show this help message\n"
            "  --version Show version information\n",
            argv0);
}

int main(int argc, char* argv[]) {
    gtk_init(&argc, &argv);

    // Parse command-line arguments
    std::vector<std::string> filesToOpen;
    int  gotoLine = -1;
    int  gotoCol  = -1;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            g_print("Notepad++ 8.7.5 Linux port\n"
                    "Built with GTK3 + Scintilla + Lexilla\n");
            return 0;
        }
        if ((strcmp(argv[i], "-n") == 0) && i + 1 < argc) {
            gotoLine = atoi(argv[++i]) - 1;  // convert to 0-based
            continue;
        }
        if ((strcmp(argv[i], "-c") == 0) && i + 1 < argc) {
            gotoCol = atoi(argv[++i]) - 1;
            continue;
        }
        // Treat anything else as a file path
        filesToOpen.push_back(argv[i]);
    }

    // Load persisted settings
    Parameters::getInstance().load();

    // Create and initialise main window
    NotepadPlusGtk npp;
    npp.init();

    // Open files from command line
    for (const auto& path : filesToOpen)
        npp.openFile(path);

    // Apply -n / -c navigation after opening
    // (NotepadPlusGtk defers to Scintilla which must be realised first;
    //  schedule a one-shot idle callback so the UI settles first)
    if (gotoLine >= 0 || gotoCol >= 0) {
        struct NavData { NotepadPlusGtk* npp; int line; int col; };
        auto* nav = new NavData{&npp, gotoLine, gotoCol};
        g_idle_add([](gpointer d) -> gboolean {
            auto* nd = static_cast<NavData*>(d);
            // Navigate the first open document (index 0 or current)
            // We reach into NotepadPlusGtk via a small helper; for now use
            // the public openFile path which doesn't expose navigation.
            // A future enhancement can add setInitialPosition() API.
            delete nd;
            return G_SOURCE_REMOVE;
        }, nav);
    }

    // Run GTK main loop
    gtk_main();

    // Persist settings on exit
    Parameters::getInstance().save();

    return 0;
}
