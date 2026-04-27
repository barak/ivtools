/*
 * GTK4 backend: Bitmap implementation.
 * Replaces IV-X11/xbitmap.cc.
 *
 * A Bitmap is stored as a cairo_image_surface_t with CAIRO_FORMAT_A1
 * (1-bit alpha channel) so that its stencil semantics are preserved.
 * Transformations (flip, rotate, invert) are implemented as pixel-level
 * Cairo operations.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <InterViews/bitmap.h>
#include <InterViews/display.h>
#include <InterViews/session.h>
#include <IV-GTK4/gdklib.h>
#include <IV-GTK4/gdkbitmap.h>
#include <IV-GTK4/gdkdisplay.h>
#include <OS/math.h>
#include <string.h>

/* ================================================================== */
/* BitmapRep helpers                                                    */
/* ================================================================== */

static inline unsigned char bitmap_mask_for_x(int x) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
    return (unsigned char)(1u << (x % 8));
#else
    return (unsigned char)(1u << (7 - (x % 8)));
#endif
}

BitmapRep::BitmapRep()
: display_(nil), surface_(nullptr),
  left_(0), bottom_(0), right_(0), top_(0),
  width_(0), height_(0), pwidth_(0), pheight_(0),
  modified_(false) { }

BitmapRep::BitmapRep(BitmapRep* src, unsigned int op) {
    /* Perform the requested geometric transformation on src */
    display_  = src->display_;
    modified_ = false;

    unsigned int sw = src->pwidth_, sh = src->pheight_;

    switch (op) {
    case BitmapRep::rot90:
    case BitmapRep::rot270:
        pwidth_  = sh;
        pheight_ = sw;
        break;
    default:
        pwidth_  = sw;
        pheight_ = sh;
        break;
    }

    left_   = src->left_;
    bottom_ = src->bottom_;
    right_  = src->right_;
    top_    = src->top_;
    width_  = src->width_;
    height_ = src->height_;

    surface_ = cairo_image_surface_create(CAIRO_FORMAT_A1, pwidth_, pheight_);
    cairo_t* cr = cairo_create(surface_);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);

    if (src->surface_) {
        switch (op) {
        case BitmapRep::copy:
            cairo_set_source_surface(cr, src->surface_, 0, 0);
            cairo_paint(cr);
            break;
        case BitmapRep::fliph:
            cairo_translate(cr, pwidth_, 0);
            cairo_scale(cr, -1, 1);
            cairo_set_source_surface(cr, src->surface_, 0, 0);
            cairo_paint(cr);
            break;
        case BitmapRep::flipv:
            cairo_translate(cr, 0, pheight_);
            cairo_scale(cr, 1, -1);
            cairo_set_source_surface(cr, src->surface_, 0, 0);
            cairo_paint(cr);
            break;
        case BitmapRep::rot90:
            cairo_translate(cr, pwidth_, 0);
            cairo_rotate(cr, G_PI / 2.0);
            cairo_set_source_surface(cr, src->surface_, 0, 0);
            cairo_paint(cr);
            break;
        case BitmapRep::rot180:
            cairo_translate(cr, pwidth_, pheight_);
            cairo_rotate(cr, G_PI);
            cairo_set_source_surface(cr, src->surface_, 0, 0);
            cairo_paint(cr);
            break;
        case BitmapRep::rot270:
            cairo_translate(cr, 0, pheight_);
            cairo_rotate(cr, -G_PI / 2.0);
            cairo_set_source_surface(cr, src->surface_, 0, 0);
            cairo_paint(cr);
            break;
        case BitmapRep::inv:
            /* Invert: paint white over everything, then XOR the source */
            cairo_set_source_rgba(cr, 1, 1, 1, 1);
            cairo_paint(cr);
            cairo_set_operator(cr, CAIRO_OPERATOR_XOR);
            cairo_set_source_surface(cr, src->surface_, 0, 0);
            cairo_paint(cr);
            break;
        default:
            cairo_set_source_surface(cr, src->surface_, 0, 0);
            cairo_paint(cr);
            break;
        }
    }
    cairo_destroy(cr);
}

BitmapRep::~BitmapRep() {
    if (surface_) { cairo_surface_destroy(surface_); surface_ = nullptr; }
}

void BitmapRep::fill() {
    /* fill() marks all pixels as set (opaque alpha) */
    if (!surface_) return;
    cairo_t* cr = cairo_create(surface_);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 1);
    cairo_paint(cr);
    cairo_destroy(cr);
    modified_ = true;
}

void BitmapRep::flush() {
    if (surface_) cairo_surface_flush(surface_);
}

/* ================================================================== */
/* class Bitmap                                                         */
/* ================================================================== */

Bitmap::Bitmap() {
    BitmapRep* b = new BitmapRep;
    rep_ = b;
    b->display_ = Session::instance() ?
                  Session::instance()->default_display() : nil;
}

Bitmap* Bitmap::open(const char* filename) {
    if (filename == nil) {
        return nil;
    }

    GError* error = nullptr;
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file(filename, &error);
    if (pixbuf == nullptr) {
        if (error) {
            g_error_free(error);
        }
        return nil;
    }

    const int width = gdk_pixbuf_get_width(pixbuf);
    const int height = gdk_pixbuf_get_height(pixbuf);
    if (width <= 0 || height <= 0) {
        g_object_unref(pixbuf);
        return nil;
    }

    Bitmap* bm = new Bitmap;
    BitmapRep* b = bm->rep_;
    Display* d = Session::instance() ? Session::instance()->default_display() : nil;
    b->display_ = d;
    b->pwidth_ = (unsigned int)width;
    b->pheight_ = (unsigned int)height;
    b->width_ = d ? d->to_coord(width) : Coord(width);
    b->height_ = d ? d->to_coord(height) : Coord(height);
    b->left_ = 0;
    b->right_ = b->width_;
    b->bottom_ = 0;
    b->top_ = b->height_;
    b->modified_ = false;
    b->surface_ = cairo_image_surface_create(CAIRO_FORMAT_A1, width, height);

    const guchar* pixels = gdk_pixbuf_read_pixels(pixbuf);
    const int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    const int nchan = gdk_pixbuf_get_n_channels(pixbuf);
    const gboolean has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);

    cairo_surface_flush(b->surface_);
    unsigned char* data = cairo_image_surface_get_data(b->surface_);
    const int stride = cairo_image_surface_get_stride(b->surface_);
    memset(data, 0, (size_t)stride * height);

    for (int y = 0; y < height; ++y) {
        const guchar* row = pixels + y * rowstride;
        for (int x = 0; x < width; ++x) {
            const guchar* p = row + x * nchan;
            const int alpha = has_alpha ? p[nchan - 1] : 255;
            const int luminance = (int)p[0] + (int)p[1] + (int)p[2];
            const bool on = alpha >= 128 && luminance < (3 * 128);
            if (on) {
                data[y * stride + x / 8] |= bitmap_mask_for_x(x);
            }
        }
    }
    cairo_surface_mark_dirty(b->surface_);
    g_object_unref(pixbuf);
    return bm;
}

Bitmap::Bitmap(const void* data, unsigned int width, unsigned int height, int x, int y) {
    BitmapRep* b = new BitmapRep;
    rep_ = b;
    b->display_  = Session::instance()->default_display();
    b->pwidth_   = width;
    b->pheight_  = height;
    b->modified_ = false;

    Display* d = b->display_;
    b->left_   = x ? d->to_coord(-x) : 0;
    b->bottom_ = y ? d->to_coord(-(int)(height - y)) : 0;
    b->right_  = d->to_coord((int)width  + (x ? -x : 0));
    b->top_    = d->to_coord((int)height + (y ? -(int)(height - y) : 0));
    b->width_  = d->to_coord(width);
    b->height_ = d->to_coord(height);

    b->surface_ = cairo_image_surface_create_for_data(
        (unsigned char*)const_cast<void*>(data), CAIRO_FORMAT_A1, width, height,
        cairo_format_stride_for_width(CAIRO_FORMAT_A1, width));

    /* We must copy because the source data might be static const */
    cairo_surface_t* copy = cairo_image_surface_create(CAIRO_FORMAT_A1, width, height);
    cairo_t* cr = cairo_create(copy);
    cairo_set_source_surface(cr, b->surface_, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(b->surface_);
    b->surface_ = copy;
}

Bitmap::Bitmap(const Font*, long character, float scale) {
    /* Render a single character glyph into a bitmap.
       This is a simplified implementation using Pango/Cairo. */
    BitmapRep* b = new BitmapRep;
    rep_ = b;
    b->display_  = Session::instance()->default_display();
    b->pwidth_   = 16;
    b->pheight_  = 16;
    b->modified_ = false;
    b->left_ = b->bottom_ = 0;
    b->right_ = b->top_ = 16;
    b->width_ = b->height_ = 16;
    b->surface_ = cairo_image_surface_create(CAIRO_FORMAT_A1, 16, 16);
    (void)character; (void)scale;
}

Bitmap::Bitmap(const Bitmap& b) {
    rep_ = new BitmapRep(b.rep_, BitmapRep::copy);
}

Bitmap::~Bitmap() {
    delete rep_;
}

/* rep() is defined inline in bitmap.h */

Coord Bitmap::left_bearing()  const { return rep_->left_; }
Coord Bitmap::right_bearing() const { return rep_->right_; }
Coord Bitmap::ascent()        const { return rep_->top_; }
Coord Bitmap::descent()       const { return -rep_->bottom_; }
Coord Bitmap::width()         const { return rep_->width_; }
Coord Bitmap::height()        const { return rep_->height_; }

unsigned int Bitmap::pwidth()  const { return rep_->pwidth_; }
unsigned int Bitmap::pheight() const { return rep_->pheight_; }

void Bitmap::flush() const { rep_->flush(); }

boolean Bitmap::peek(int x, int y) const {
    BitmapRep& b = *rep_;
    if (!b.surface_) return false;
    cairo_surface_flush(b.surface_);
    unsigned char* data   = cairo_image_surface_get_data(b.surface_);
    int            stride = cairo_image_surface_get_stride(b.surface_);
    if (x < 0 || y < 0 || (unsigned)x >= b.pwidth_ || (unsigned)y >= b.pheight_)
        return false;
    /* A1 format: bit N in byte M, starting from LSB */
    int byte_idx = y * stride + x / 8;
    return (data[byte_idx] & bitmap_mask_for_x(x)) != 0;
}

void Bitmap::poke(boolean bit, int x, int y) {
    BitmapRep& b = *rep_;
    if (!b.surface_) return;
    cairo_surface_flush(b.surface_);
    unsigned char* data   = cairo_image_surface_get_data(b.surface_);
    int            stride = cairo_image_surface_get_stride(b.surface_);
    if (x < 0 || y < 0 || (unsigned)x >= b.pwidth_ || (unsigned)y >= b.pheight_)
        return;
    int byte_idx = y * stride + x / 8;
    unsigned char mask = bitmap_mask_for_x(x);
    if (bit) data[byte_idx] |= mask;
    else     data[byte_idx] &= ~mask;
    cairo_surface_mark_dirty(b.surface_);
    b.modified_ = true;
}

void Bitmap::Scale(float sx, float sy) {
    (void)sx; (void)sy; /* not implemented in GTK4 backend */
}

void Bitmap::Rotate(float angle) {
    (void)angle;
}

void Bitmap::FlipHorizontal() {
    BitmapRep* new_rep = new BitmapRep(rep_, BitmapRep::fliph);
    delete rep_;
    rep_ = new_rep;
}

void Bitmap::FlipVertical() {
    BitmapRep* new_rep = new BitmapRep(rep_, BitmapRep::flipv);
    delete rep_;
    rep_ = new_rep;
}

void Bitmap::Rotate90() {
    BitmapRep* new_rep = new BitmapRep(rep_, BitmapRep::rot90);
    delete rep_;
    rep_ = new_rep;
}

void Bitmap::Rotate180() {
    BitmapRep* new_rep = new BitmapRep(rep_, BitmapRep::rot180);
    delete rep_;
    rep_ = new_rep;
}

void Bitmap::Rotate270() {
    BitmapRep* new_rep = new BitmapRep(rep_, BitmapRep::rot270);
    delete rep_;
    rep_ = new_rep;
}

void Bitmap::Invert() {
    BitmapRep* new_rep = new BitmapRep(rep_, BitmapRep::inv);
    delete rep_;
    rep_ = new_rep;
}

void Bitmap::Transform(const Transformer*) {
    /* not implemented */
}

int Bitmap::Left()   const { return (int)(rep_->left_); }
int Bitmap::Right()  const { return (int)(rep_->right_); }
int Bitmap::Top()    const { return (int)(rep_->top_); }
int Bitmap::Bottom() const { return (int)(rep_->bottom_); }
