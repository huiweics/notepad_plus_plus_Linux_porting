#include "DocumentMap.h"
#include <algorithm>

DocumentMap::DocumentMap() {
    _box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(_box, 110, -1);

    // Header label
    GtkWidget* lbl = gtk_label_new("Document Map");
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_widget_set_margin_start(lbl, 6);
    gtk_widget_set_margin_top(lbl, 4);
    gtk_widget_set_margin_bottom(lbl, 2);
    gtk_box_pack_start(GTK_BOX(_box), lbl, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(_box),
                       gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                       FALSE, FALSE, 0);

    // Drawing area
    _drawArea = gtk_drawing_area_new();
    gtk_box_pack_start(GTK_BOX(_box), _drawArea, TRUE, TRUE, 0);
    gtk_widget_add_events(_drawArea, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(_drawArea, "draw",              G_CALLBACK(onDraw),        this);
    g_signal_connect(_drawArea, "button-press-event",G_CALLBACK(onButtonPress), this);

    gtk_widget_show_all(_box);
}

void DocumentMap::update(ScintillaView* view) {
    _view = view;
    gtk_widget_queue_draw(_drawArea);
}

void DocumentMap::scrollUpdate(ScintillaView* view) {
    if (_view == view)
        gtk_widget_queue_draw(_drawArea);
}

gboolean DocumentMap::onDraw(GtkWidget* widget, cairo_t* cr, gpointer d) {
    auto* self = static_cast<DocumentMap*>(d);

    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);

    // Background
    cairo_set_source_rgb(cr, 0.15, 0.15, 0.15);
    cairo_paint(cr);

    if (!self->_view) return FALSE;

    int totalLines = self->_view->getLineCount();
    if (totalLines <= 0) return FALSE;

    // Height per line (at least 0.5 pixel so something is visible)
    double lineH = (double)h / (double)totalLines;
    if (lineH < 0.5) lineH = 0.5;

    // Draw a horizontal bar for each line proportional to its length
    for (int i = 0; i < totalLines; ++i) {
        double y = (double)i * lineH;
        if (y >= (double)h) break;

        int len = self->_view->getLineLength(i);
        if (len <= 0) continue;

        // Scale: 80 chars => full width
        double barW = std::min((double)len * ((double)(w - 4) / 80.0),
                               (double)(w - 4));
        double barH = std::max(lineH - 0.5, 0.5);

        cairo_set_source_rgba(cr, 0.60, 0.62, 0.65, 0.75);
        cairo_rectangle(cr, 2.0, y, barW, barH);
        cairo_fill(cr);
    }

    // Draw viewport rectangle
    int firstVisible  = self->_view->getFirstVisibleLine();
    int linesOnScreen = self->_view->getLinesOnScreen();

    double vy = (double)firstVisible  * lineH;
    double vh = (double)linesOnScreen * lineH;

    // Clamp to drawing area
    vy = std::max(0.0, std::min(vy, (double)(h - 1)));
    vh = std::max(2.0, std::min(vh, (double)h - vy));

    cairo_set_source_rgba(cr, 0.40, 0.70, 1.00, 0.20);
    cairo_rectangle(cr, 0.0, vy, (double)w, vh);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 0.40, 0.70, 1.00, 0.70);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, 0.5, vy + 0.5, (double)(w - 1), vh - 1.0);
    cairo_stroke(cr);

    return FALSE;
}

gboolean DocumentMap::onButtonPress(GtkWidget* widget, GdkEventButton* ev, gpointer d) {
    auto* self = static_cast<DocumentMap*>(d);
    if (!self->_view) return FALSE;

    int h = gtk_widget_get_allocated_height(widget);
    int totalLines = self->_view->getLineCount();
    if (h <= 0 || totalLines <= 0) return FALSE;

    int targetLine = (int)(ev->y / (double)h * (double)totalLines);
    targetLine = std::max(0, std::min(targetLine, totalLines - 1));

    self->_view->gotoLine(targetLine);
    self->_view->ensureCaretVisible();
    return TRUE;
}
