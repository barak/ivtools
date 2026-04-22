/*
 * Copyright (c) 1987, 1988, 1989, 1990, 1991 Stanford University
 * Copyright (c) 1991 Silicon Graphics, Inc.
 *
 * GTK4 backend: Cursor implementation.
 * Replaces IV-X11/xcursor.cc.
 *
 * Custom cursors are built from a GdkTexture (ARGB32 image) via
 * gdk_cursor_new_from_texture().  Named cursors use gdk_cursor_new_from_name().
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <InterViews/bitmap.h>
#include <InterViews/color.h>
#include <InterViews/cursor.h>
#include <InterViews/display.h>
#include <InterViews/font.h>
#include <InterViews/session.h>
#include <InterViews/style.h>
#include <IV-GTK4/gdklib.h>
#include <OS/string.h>
#include <IV-GTK4/gdkbitmap.h>
#include <IV-GTK4/gdkcolor.h>
#include <IV-GTK4/gdkcursor.h>
#include <IV-GTK4/gdkdisplay.h>
#include <IV-GTK4/gtkwindow.h>
#include <OS/list.h>

/* ================================================================== */
/* CursorRep                                                           */
/* ================================================================== */

CursorRep::CursorRep(const Color* fg, const Color* bg)
: fg_(fg), bg_(bg), display_(nil), gdkcursor_(nullptr)
{
    Resource::ref(fg_);
    Resource::ref(bg_);
}

CursorRep::~CursorRep() {
    if (gdkcursor_) { g_object_unref(gdkcursor_); gdkcursor_ = nullptr; }
    Resource::unref(fg_);
    Resource::unref(bg_);
}

GdkCursor* CursorRep::gdk_cursor(Display* d, WindowVisual* wv) {
    if (!gdkcursor_) {
        display_ = d;
        make_cursor(d, wv);
    }
    return gdkcursor_;
}

const Color* CursorRep::make_color(Display* d, Style* s,
    const char* str1, const char* str2, const char* str3,
    const char* default_value)
{
    String v;
    if ((str1 && s->find_attribute(str1, v)) ||
        (str2 && s->find_attribute(str2, v)) ||
        (str3 && s->find_attribute(str3, v)))
    {
        const Color* c = Color::lookup(d, v);
        if (c) return c;
    }
    return Color::lookup(d, String(default_value));
}

/* ================================================================== */
/* CursorRepData – from raw 16×16 scanline arrays                     */
/* ================================================================== */

CursorRepData::CursorRepData(short x_hot, short y_hot,
    const int* pat, const int* mask,
    const Color* fg, const Color* bg)
: CursorRep(fg, bg), x_(x_hot), y_(y_hot), pat_(pat), mask_(mask) { }

CursorRepData::~CursorRepData() { }

cairo_surface_t* CursorRepData::make_cursor_surface(const int* scanline,
    int width, int height)
{
    cairo_surface_t* surf = cairo_image_surface_create(
        CAIRO_FORMAT_A1, width, height);
    unsigned char* data = cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf);

    for (int row = 0; row < height; ++row) {
        /* Each int in scanline[] encodes one row of 16 pixels (MSB first) */
        unsigned int bits = (unsigned int)scanline[row];
        for (int col = 0; col < width; ++col) {
            int byte_idx = col / 8;
            int bit_idx  = 7 - (col % 8);
            int pix = (bits >> (15 - col)) & 1;
            if (pix) data[row * stride + byte_idx] |= (1 << bit_idx);
            else     data[row * stride + byte_idx] &= ~(1 << bit_idx);
        }
    }
    cairo_surface_mark_dirty(surf);
    return surf;
}

void CursorRepData::make_cursor(Display* d, WindowVisual* wv) {
    /* Build a 16×16 ARGB32 image from the pattern and mask bitmasks */
    const int W = 16, H = 16;

    /* Determine foreground and background colours */
    GdkRGBA fg_rgba = { 0.0, 0.0, 0.0, 1.0 }; /* default black */
    GdkRGBA bg_rgba = { 1.0, 1.0, 1.0, 1.0 }; /* default white */
    if (fg_ && d) {
        ColorRep* cr = fg_->rep(wv ? wv : d->rep()->default_visual_);
        fg_rgba = cr->rgba_;
    }
    if (bg_ && d) {
        ColorRep* cr = bg_->rep(wv ? wv : d->rep()->default_visual_);
        bg_rgba = cr->rgba_;
    }

    /* Create an ARGB32 surface */
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    unsigned char* data = cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf);

    for (int row = 0; row < H; ++row) {
        unsigned int pat_bits  = (unsigned int)pat_[row];
        unsigned int mask_bits = (unsigned int)mask_[row];
        for (int col = 0; col < W; ++col) {
            int bit = (1 << (15 - col));
            int is_mask = (mask_bits & bit) ? 1 : 0;
            int is_pat  = (pat_bits  & bit) ? 1 : 0;

            unsigned char r, g, b, a;
            if (!is_mask) { r = g = b = 0; a = 0; } /* transparent */
            else if (is_pat) {
                r = (unsigned char)(fg_rgba.red   * 255);
                g = (unsigned char)(fg_rgba.green * 255);
                b = (unsigned char)(fg_rgba.blue  * 255);
                a = (unsigned char)(fg_rgba.alpha * 255);
            } else {
                r = (unsigned char)(bg_rgba.red   * 255);
                g = (unsigned char)(bg_rgba.green * 255);
                b = (unsigned char)(bg_rgba.blue  * 255);
                a = (unsigned char)(bg_rgba.alpha * 255);
            }
            /* ARGB32 layout (little-endian): BGRA */
            int idx = row * stride + col * 4;
            data[idx+0] = b;
            data[idx+1] = g;
            data[idx+2] = r;
            data[idx+3] = a;
        }
    }
    cairo_surface_flush(surf);
    unsigned char* pixdata = cairo_image_surface_get_data(surf);
    int            pixstride = cairo_image_surface_get_stride(surf);
    GBytes*        bytes   = g_bytes_new_static(pixdata, (gsize)(pixstride * H));
    GdkTexture* texture = gdk_memory_texture_new(W, H,
        GDK_MEMORY_B8G8R8A8_PREMULTIPLIED, bytes, (gsize)pixstride);
    g_bytes_unref(bytes);
    cairo_surface_destroy(surf);

    gdkcursor_ = gdk_cursor_new_from_texture(texture, x_, y_, nullptr);
    g_object_unref(texture);
}

/* ================================================================== */
/* CursorRepBitmap – from Bitmap pat/mask objects                     */
/* ================================================================== */

CursorRepBitmap::CursorRepBitmap(const Bitmap* pat, const Bitmap* mask,
    const Color* fg, const Color* bg)
: CursorRep(fg, bg), pat_(pat), mask_(mask) { }

CursorRepBitmap::~CursorRepBitmap() { }

void CursorRepBitmap::make_cursor(Display* d, WindowVisual* wv) {
    if (!pat_) { gdkcursor_ = gdk_cursor_new_from_name("default", nullptr); return; }

    BitmapRep* br = pat_->rep();
    if (!br || !br->surface_) {
        gdkcursor_ = gdk_cursor_new_from_name("default", nullptr);
        return;
    }

    /* Determine hot-spot (centre of bitmap) */
    int hot_x = (int)(br->pwidth_  / 2);
    int hot_y = (int)(br->pheight_ / 2);

    /* Determine foreground and background colours */
    GdkRGBA fg_rgba = { 0.0, 0.0, 0.0, 1.0 };
    GdkRGBA bg_rgba = { 1.0, 1.0, 1.0, 1.0 };
    WindowVisual* vis = wv ? wv : (d ? d->rep()->default_visual_ : nullptr);
    if (fg_ && vis) { ColorRep* cr = fg_->rep(vis); fg_rgba = cr->rgba_; }
    if (bg_ && vis) { ColorRep* cr = bg_->rep(vis); bg_rgba = cr->rgba_; }

    int W = (int)br->pwidth_, H = (int)br->pheight_;

    /* Build ARGB32 surface from A1 bitmap */
    cairo_surface_t* argb_surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    cairo_t* cr_tmp = cairo_create(argb_surf);

    /* First fill with background */
    cairo_set_source_rgba(cr_tmp, bg_rgba.red, bg_rgba.green,
                          bg_rgba.blue, bg_rgba.alpha);
    cairo_paint(cr_tmp);

    /* Mask with pattern bitmap to apply foreground */
    if (br->surface_) {
        cairo_set_source_rgba(cr_tmp, fg_rgba.red, fg_rgba.green,
                              fg_rgba.blue, fg_rgba.alpha);
        cairo_mask_surface(cr_tmp, br->surface_, 0, 0);
    }

    /* Apply alpha mask if provided */
    if (mask_ && mask_->rep() && mask_->rep()->surface_) {
        cairo_set_operator(cr_tmp, CAIRO_OPERATOR_DEST_IN);
        cairo_mask_surface(cr_tmp, mask_->rep()->surface_, 0, 0);
    }

    cairo_destroy(cr_tmp);

    cairo_surface_flush(argb_surf);
    unsigned char* raw2   = cairo_image_surface_get_data(argb_surf);
    int            stride2 = cairo_image_surface_get_stride(argb_surf);
    GBytes* bytes = g_bytes_new_static(raw2, (gsize)(stride2 * H));
    GdkTexture* texture = gdk_memory_texture_new(W, H,
        GDK_MEMORY_B8G8R8A8_PREMULTIPLIED, bytes, (gsize)stride2);
    g_bytes_unref(bytes);
    cairo_surface_destroy(argb_surf);

    gdkcursor_ = gdk_cursor_new_from_texture(texture, hot_x, hot_y, nullptr);
    g_object_unref(texture);
}

/* ================================================================== */
/* CursorRepFont – from a font character code                         */
/* ================================================================== */

CursorRepFont::CursorRepFont(const Font* font, int pat, int mask,
    const Color* fg, const Color* bg)
: CursorRep(fg, bg), font_(font), pat_(pat), mask_(mask)
{
    Resource::ref(font_);
}

CursorRepFont::~CursorRepFont() { Resource::unref(font_); }

void CursorRepFont::make_cursor(Display* d, WindowVisual*) {
    /* For font-based cursors we use the GDK named cursor fallback.
       A proper implementation would render the glyph into an ARGB surface. */
    gdkcursor_ = gdk_cursor_new_from_name("default", nullptr);
    (void)d;
}

/* ================================================================== */
/* CursorRepXFont – from a cursor-font glyph index                   */
/* ================================================================== */

/*
 * Map a subset of the X11 cursor-font indices to GTK/CSS cursor names.
 * Only the most common ones are covered; others fall back to "default".
 */
static const char* x11_cursor_to_css(int code) {
    switch (code) {
    case 2:   return "top_left_arrow";
    case 30:  return "watch";
    case 34:  return "crosshair";
    case 52:  return "hand1";
    case 58:  return "hand2";
    case 60:  return "help";
    case 68:  return "left_side";
    case 70:  return "right_side";
    case 74:  return "move";
    case 76:  return "ne-resize";
    case 78:  return "nw-resize";
    case 86:  return "s-resize";
    case 116: return "text";
    case 130: return "top_left_corner";
    case 132: return "top_right_corner";
    case 136: return "ns-resize";
    case 138: return "ew-resize";
    default:  return "default";
    }
}

CursorRepXFont::CursorRepXFont(int code, const Color* fg, const Color* bg)
: CursorRep(fg, bg), code_(code) { }

CursorRepXFont::~CursorRepXFont() { }

void CursorRepXFont::make_cursor(Display*, WindowVisual*) {
    const char* name = x11_cursor_to_css(code_);
    gdkcursor_ = gdk_cursor_new_from_name(name, nullptr);
    if (!gdkcursor_) gdkcursor_ = gdk_cursor_new_from_name("default", nullptr);
}

/* ================================================================== */
/* class Cursor                                                         */
/* ================================================================== */

Cursor* defaultCursor;
Cursor* arrow;
Cursor* crosshairs;
Cursor* ltarrow;
Cursor* lfast;
Cursor* rfast;
Cursor* ufast;
Cursor* dfast;
Cursor* hourglass;
Cursor* upperleft;
Cursor* upperright;
Cursor* lowerleft;
Cursor* lowerright;
Cursor* noCursor;

/* Cursor::Cursor(Bitmap*, Bitmap*, Color*, Color*) etc. are implemented in
   InterViews/cursor.cc which calls into CursorRepBitmap / CursorRepData etc. */

static const int arrow_pat[16] = {
    0x8000, 0xc000, 0xe000, 0xf000,
    0xf800, 0xfc00, 0xfe00, 0xff00,
    0xff80, 0xffc0, 0xfc00, 0xde00,
    0xcf00, 0x0780, 0x0780, 0x03c0
};
static const int arrow_mask[16] = {
    0xc000, 0xe000, 0xf000, 0xf800,
    0xfc00, 0xfe00, 0xff00, 0xff80,
    0xffc0, 0xffe0, 0xffe0, 0xff00,
    0xef80, 0xc7c0, 0x07c0, 0x03e0
};

void Cursor::init() {
    arrow      = new Cursor(1,  0, arrow_pat, arrow_mask, nil, nil);
    crosshairs = new Cursor(34);
    ltarrow    = new Cursor(68);
    lfast      = new Cursor(68);
    rfast      = new Cursor(70);
    ufast      = new Cursor(86);
    dfast      = new Cursor(86);
    hourglass  = new Cursor(30);
    upperleft  = new Cursor(130);
    upperright = new Cursor(132);
    lowerleft  = new Cursor(12);
    lowerright = new Cursor(14);
    noCursor   = new Cursor(0,  0, arrow_pat, arrow_mask, nil, nil);
    defaultCursor = arrow;
}
