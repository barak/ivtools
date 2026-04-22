/*
 * GTK4 backend: replaces IV-X11/xraster.h
 * Defines RasterRep using a cairo_image_surface_t* (ARGB32) instead of
 * XImage* + Pixmap + optional XSHM.
 */

#ifndef iv_gdkraster_h
#define iv_gdkraster_h

#include <InterViews/coord.h>
#include <IV-GTK4/gdklib.h>

#include <InterViews/_enter.h>

class Display;

class RasterRep {
public:
    Display*         display_;
    boolean          modified_;
    Coord            left_;
    Coord            bottom_;
    Coord            right_;
    Coord            top_;
    Coord            width_;
    Coord            height_;
    unsigned int     pwidth_;
    unsigned int     pheight_;

    /*
     * surface_ replaces XImage* image_ + Pixmap pixmap_.
     * It is a cairo_image_surface_t with CAIRO_FORMAT_ARGB32 format so that
     * each pixel carries a full RGBA value.  cairo_surface_paint() copies it
     * to the draw surface in Canvas::image().
     */
    cairo_surface_t* surface_;

    /* shared_memory_ is always false in the GTK4 backend (no XSHM) */
    boolean          shared_memory_;
};

#include <InterViews/_leave.h>

#endif /* iv_gdkraster_h */
