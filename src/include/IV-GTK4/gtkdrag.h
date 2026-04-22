/*
 * GTK4 backend: replaces IV-X11/xdrag.h
 * Defines the GtkDrag helper class used to detect and handle drag events.
 */

#ifndef iv_gtkdrag_h
#define iv_gtkdrag_h

#include <InterViews/_enter.h>
#include <IV-GTK4/gdkdefs.h>

class GtkDrag {
public:
    /*
     * isDrag() – returns true when the synthetic XEvent encodes a drag
     * operation.  In GTK4 drags arrive via GtkDropTarget signals; this
     * helper is called from the event dispatch path after the event has
     * been synthesised by gdkevent.cc.
     */
    static boolean isDrag(const XEvent&);

    /*
     * locate() – fills in the pointer x/y coordinates for a drag event.
     */
    static void locate(const XEvent&, int& x, int& y);

    /*
     * register_target() – set up a window to accept drops.
     */
    static void register_target(Window*, const char** types, int n_types);

    /*
     * start_drag() – initiate a drag-and-drop gesture from a window.
     */
    static void start_drag(Window*, const char* data, int data_len,
                           unsigned int action);
};

/* Source-level alias so code that says "XDrag::isDrag()" still compiles */
typedef GtkDrag XDrag;

#include <InterViews/_leave.h>

#endif /* iv_gtkdrag_h */
