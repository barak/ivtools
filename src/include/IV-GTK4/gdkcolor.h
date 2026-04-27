/*
 * GTK4 backend: replaces IV-X11/xcolor.h
 * Defines ColorRep using GdkRGBA (float RGBA) instead of X11 XColor/pixel.
 */

#ifndef iv_gdkcolor_h
#define iv_gdkcolor_h

#include <InterViews/boolean.h>
#include <InterViews/color.h>
#include <IV-GTK4/gdklib.h>
#include <IV-GTK4/gtkwindow.h>

#include <InterViews/_enter.h>

class WindowVisual;

class ColorRep {
public:
    WindowVisual*    visual_;
    ColorOp          op_;
    boolean          masking_;

    /*
     * xcolor_ is kept for source-level compatibility with existing code
     * that accesses xcolor_.pixel (e.g. stencil colour lookup).
     * In GTK4 .pixel is always 0; real colour values are in rgba_.
     */
    XColor           xcolor_;

    /*
     * rgba_ holds the actual colour components (0.0–1.0).
     * This is what gets passed to cairo_set_source_rgba().
     */
    GdkRGBA          rgba_;

    /*
     * stipple_ replaces the X11 Pixmap stipple used for alpha transparency.
     * In GTK4 we emulate stippled fill via a Cairo pattern.
     */
    cairo_pattern_t* stipple_;

    /* Cairo drawing operator corresponding to ColorOp */
    cairo_operator_t cairo_op_;
};

#include <InterViews/_leave.h>

#endif /* iv_gdkcolor_h */
