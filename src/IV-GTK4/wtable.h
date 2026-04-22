/*
 * GTK4 backend: WindowTable header
 * Maps GtkWidget* → Window* (replacing XWindow → Window* from IV-X11/wtable.h)
 */

#ifndef iv_gtk4_wtable_h
#define iv_gtk4_wtable_h

#include <OS/table.h>
#include <InterViews/enter-scope.h>
#include <IV-GTK4/gdklib.h>

class Window;

/*
 * We key the table on the GtkWidget* pointer value cast to unsigned long.
 * Since GtkWidget* is a pointer, its integer representation is unique
 * enough to serve as a hash key.
 */
typedef unsigned long GtkWidgetKey;

declareTable(WindowTable, GtkWidgetKey, Window*)

/*
 * Convenience wrappers that accept GtkWidget* and convert to key.
 */
static inline GtkWidgetKey widget_to_key(GtkWidget* w)
{
    return reinterpret_cast<GtkWidgetKey>(w);
}

#endif /* iv_gtk4_wtable_h */
