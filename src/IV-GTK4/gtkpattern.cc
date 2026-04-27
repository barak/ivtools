/*
 * GTK4 backend: Pattern implementation.
 * Replaces IV-X11/xpattern.cc.
 *
 * A Pattern is stored as a PatternRep holding a cairo_surface_t*.
 * The 1-bit pattern data is rendered into an A1 cairo surface
 * for use as a stipple mask in cairo_mask().
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <InterViews/pattern.h>
#include <InterViews/display.h>
#include <InterViews/session.h>
#include <IV-GTK4/gdklib.h>
#include <IV-GTK4/gdkdisplay.h>
#include <IV-GTK4/gtkpattern.h>

/* Build a cairo A1 surface from char bitmap data (width x height bits). */
static cairo_surface_t* build_surface(
    const char* data, unsigned int width, unsigned int height)
{
    if (!data || width == 0 || height == 0)
        return nullptr;

    cairo_surface_t* surf = cairo_image_surface_create(
        CAIRO_FORMAT_A1, (int)width, (int)height);
    cairo_surface_flush(surf);
    unsigned char* dst   = cairo_image_surface_get_data(surf);
    int            stride = cairo_image_surface_get_stride(surf);
    unsigned int   nbytes = (width + 7) / 8;

    for (unsigned int row = 0; row < height; row++) {
        const unsigned char* src = (const unsigned char*)data + row * nbytes;
        unsigned char*       d   = dst + row * stride;
        for (unsigned int b = 0; b < nbytes; b++)
            d[b] = src[b];
    }
    cairo_surface_mark_dirty(surf);
    return surf;
}

/* ------------------------------------------------------------------ */
/* Pattern constructors                                                 */
/* ------------------------------------------------------------------ */

void Pattern::init(
    const char* data, unsigned int width, unsigned int height)
{
    rep_ = new PatternRep;
    rep_->display_ = Session::instance() ? Session::instance()->default_display() : nullptr;
    rep_->pixmap_  = build_surface(data, width, height);
}

Pattern::Pattern() {
    init(nullptr, 0, 0);
}

Pattern::Pattern(const char* data, unsigned int width, unsigned int height) {
    init(data, width, height);
}

Pattern::Pattern(int p) {
    /* 4×4 pattern encoded in the low 16 bits of p */
    char buf[2];
    buf[0] = (char)((p >> 8) & 0xff);
    buf[1] = (char)(p & 0xff);
    /* Each nibble encodes one row of 4 pixels */
    char pat[2];
    pat[0] = (char)(((p & 0xf000) >> 8) | ((p & 0x0f00) >> 8));
    pat[1] = (char)(((p & 0x00f0)     ) | ((p & 0x000f)     ));
    init(pat, 4, 4);
}

Pattern::Pattern(const int* data) {
    /* 16×16 pattern: each int encodes one 16-pixel row (high byte first) */
    char buf[32];
    for (int i = 0; i < 16; i++) {
        buf[i*2    ] = (char)((data[i] >> 8) & 0xff);
        buf[i*2 + 1] = (char)( data[i]       & 0xff);
    }
    init(buf, 16, 16);
}

Pattern::~Pattern() {
    PatternRep* p = rep_;
    if (p && p->pixmap_) {
        cairo_surface_destroy(p->pixmap_);
        p->pixmap_ = nullptr;
    }
    delete p;
}
