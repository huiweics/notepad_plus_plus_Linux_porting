#pragma once
// Notepad++ Linux port - Document map (minimap) panel (Phase 3)

#include <gtk/gtk.h>
#include "ScintillaView.h"

class DocumentMap {
public:
    DocumentMap();
    ~DocumentMap() = default;

    GtkWidget* getWidget() const { return _box; }

    // Full redraw — call on tab switch or language change
    void update(ScintillaView* view);

    // Lightweight redraw — call on scroll / cursor move
    void scrollUpdate(ScintillaView* view);

private:
    GtkWidget*     _box        = nullptr;
    GtkWidget*     _drawArea   = nullptr;
    ScintillaView* _view       = nullptr;

    static gboolean onDraw(GtkWidget*, cairo_t*, gpointer);
    static gboolean onButtonPress(GtkWidget*, GdkEventButton*, gpointer);
};
