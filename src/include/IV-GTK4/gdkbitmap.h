/*
 * GTK4 backend: replaces IV-X11/xbitmap.h
 * Defines BitmapRep using a cairo_image_surface_t* (A1 or ARGB32) instead
 * of XImage* + Pixmap.
 */

#ifndef iv_gdkbitmap_h
#define iv_gdkbitmap_h

#include <InterViews/coord.h>
#include <IV-GTK4/gdklib.h>

#include <InterViews/_enter.h>

class BitmapRep {
public:
    enum { copy, fliph, flipv, rot90, rot180, rot270, inv };

    BitmapRep();
    BitmapRep(BitmapRep*, unsigned int op);
    ~BitmapRep();

    void fill();
    void flush();

    Display*         display_;
    /*
     * surface_ replaces XImage* image_ + Pixmap pixmap_.
     * We use CAIRO_FORMAT_A1 (1-bit alpha) for compatibility with the
     * X11 "1-bit deep pixmap" semantics.  Some operations (e.g. rotate)
     * may temporarily use ARGB32.
     */
    cairo_surface_t* surface_;

    Coord            left_;
    Coord            bottom_;
    Coord            right_;
    Coord            top_;
    Coord            width_;
    Coord            height_;
    unsigned int     pwidth_;
    unsigned int     pheight_;
    boolean          modified_;
};

#include <InterViews/_leave.h>

#endif /* iv_gdkbitmap_h */
