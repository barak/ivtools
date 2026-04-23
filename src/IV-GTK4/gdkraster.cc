/*
 * GTK4 backend: Raster implementation.
 * Replaces IV-X11/xraster.cc.
 *
 * A Raster is stored as a cairo_image_surface_t in ARGB32 format.
 * No XSHM support is needed; Cairo manages the pixel buffer directly.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <InterViews/display.h>
#include <InterViews/raster.h>
#include <InterViews/session.h>
#include <IV-GTK4/gdklib.h>
#include <IV-GTK4/gdkdisplay.h>
#include <IV-GTK4/gdkraster.h>
#include <OS/math.h>

/* ================================================================== */
/* class Raster                                                         */
/* ================================================================== */

Raster::Raster(unsigned long width, unsigned long height) {
    RasterRep* r = new RasterRep;
    rep_ = r;
    r->display_       = Session::instance()->default_display();
    r->modified_      = false;
    r->pwidth_        = (unsigned int)width;
    r->pheight_       = (unsigned int)height;
    r->left_          = 0;
    r->bottom_        = 0;
    r->right_         = (Coord)width;
    r->top_           = (Coord)height;
    r->width_         = (Coord)width;
    r->height_        = (Coord)height;
    r->shared_memory_ = false;

    r->surface_ = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, (int)width, (int)height);
    /* Initialise all pixels to transparent black */
    cairo_t* cr = cairo_create(r->surface_);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_destroy(cr);
}

Raster::Raster(const Raster& src) {
    RasterRep* r = new RasterRep;
    rep_ = r;
    const RasterRep& s = *src.rep_;
    r->display_       = s.display_;
    r->modified_      = false;
    r->pwidth_        = s.pwidth_;
    r->pheight_       = s.pheight_;
    r->left_          = s.left_;
    r->bottom_        = s.bottom_;
    r->right_         = s.right_;
    r->top_           = s.top_;
    r->width_         = s.width_;
    r->height_        = s.height_;
    r->shared_memory_ = false;

    r->surface_ = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, (int)s.pwidth_, (int)s.pheight_);
    cairo_t* cr = cairo_create(r->surface_);
    cairo_set_source_surface(cr, s.surface_, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
}

Raster::~Raster() {
    RasterRep& r = *rep_;
    if (r.surface_) {
        cairo_surface_destroy(r.surface_);
        r.surface_ = nullptr;
    }
    delete rep_;
}

Coord Raster::width()  const { return rep()->width_; }
Coord Raster::height() const { return rep()->height_; }
unsigned long Raster::pwidth()  const { return rep()->pwidth_; }
unsigned long Raster::pheight() const { return rep()->pheight_; }

/*
 * peek() – read back a pixel value (R, G, B, alpha 0.0–1.0).
 */
void Raster::peek(unsigned long x, unsigned long y,
                  ColorIntensity& r, ColorIntensity& g, ColorIntensity& b,
                  float& alpha) const
{
    RasterRep& rr = *rep_;
    if (!rr.surface_) { r = g = b = 0; alpha = 0; return; }

    cairo_surface_flush(rr.surface_);
    unsigned char* data   = cairo_image_surface_get_data(rr.surface_);
    int            stride = cairo_image_surface_get_stride(rr.surface_);

    /* Rasters use bottom-up coordinate convention like X11 */
    unsigned long row = rr.pheight_ - 1 - y;
    if (row >= rr.pheight_ || x >= rr.pwidth_) { r=g=b=0; alpha=0; return; }

    unsigned char* pix = data + row * stride + x * 4;
    /* ARGB32 little-endian: byte[0]=B, [1]=G, [2]=R, [3]=A */
    b     = pix[0] / 255.0f;
    g     = pix[1] / 255.0f;
    r     = pix[2] / 255.0f;
    alpha = pix[3] / 255.0f;
}

/*
 * poke() – write a pixel value.
 */
void Raster::poke(unsigned long x, unsigned long y,
                  ColorIntensity r, ColorIntensity g, ColorIntensity b,
                  float alpha)
{
    RasterRep& rr = *rep_;
    if (!rr.surface_) return;

    cairo_surface_flush(rr.surface_);
    unsigned char* data   = cairo_image_surface_get_data(rr.surface_);
    int            stride = cairo_image_surface_get_stride(rr.surface_);

    unsigned long row = rr.pheight_ - 1 - y;
    if (row >= rr.pheight_ || x >= rr.pwidth_) return;

    unsigned char* pix = data + row * stride + x * 4;
    unsigned char a8 = (unsigned char)(alpha * 255);
    /* Pre-multiply alpha for CAIRO_FORMAT_ARGB32 */
    pix[0] = (unsigned char)(b * a8 / 255);
    pix[1] = (unsigned char)(g * a8 / 255);
    pix[2] = (unsigned char)(r * a8 / 255);
    pix[3] = a8;

    cairo_surface_mark_dirty(rr.surface_);
    rr.modified_ = true;
}

void Raster::flush() const {
    if (rep_->surface_) cairo_surface_flush(rep_->surface_);
}

/* Raster constructor from RasterRep* (used by OverlayUnidraw) */
Raster::Raster(RasterRep* r) : rep_(r) {}

/* IV-2_6/OverlayUnidraw Raster methods */
Coord Raster::left_bearing()  const { return 0.0f; }
Coord Raster::right_bearing() const { return rep_ ? (Coord)rep_->pwidth_ : 0.0f; }
Coord Raster::ascent()  const { return rep_ ? (Coord)rep_->pheight_ : 0.0f; }
Coord Raster::descent() const { return 0.0f; }

void Raster::flushrect(IntCoord /*l*/, IntCoord /*b*/,
                        IntCoord /*r*/, IntCoord /*t*/) const {
    if (rep_ && rep_->surface_) cairo_surface_flush(rep_->surface_);
}

boolean Raster::init_shared_memory() { return false; }
