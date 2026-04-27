/*
 * GTK4 backend: replaces IV-X11/xpattern.h
 * Defines PatternRep using a cairo_pattern_t* instead of an X11 Pixmap.
 */

#ifndef iv_gtkpattern_h
#define iv_gtkpattern_h

#include <IV-GTK4/gdklib.h>

#include <InterViews/_enter.h>

class Display;

class PatternRep {
public:
    Display*         display_;
    /*
     * pixmap_ is now a cairo_surface_t* (replaces Pixmap).
     * The typedef for Pixmap in gdklib.h maps to cairo_surface_t*, so
     * existing code like `p->pixmap_` still compiles.
     */
    cairo_surface_t* pixmap_;
};

#include <InterViews/_leave.h>

#endif /* iv_gtkpattern_h */
