/*
 * Copyright (c) 1987, 1988, 1989, 1990, 1991 Stanford University
 * Copyright (c) 1991 Silicon Graphics, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that (i) the above copyright notices and this permission notice appear in
 * all copies of the software and related documentation, and (ii) the names of
 * Stanford and Silicon Graphics may not be used in any advertising or
 * publicity relating to the software without the specific, prior written
 * permission of Stanford and Silicon Graphics.
 *
 * THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * IN NO EVENT SHALL STANFORD OR SILICON GRAPHICS BE LIABLE FOR
 * ANY SPECIAL, INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND,
 * OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY OF
 * LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

/*
 * GTK4 backend: replaces IV-X11/xcanvas.h
 * Defines CanvasRep using Cairo surfaces rather than X11 Drawables/GCs.
 */

#ifndef iv_gtkcanvas_h
#define iv_gtkcanvas_h

#include <InterViews/canvas.h>
#include <IV-GTK4/gdklib.h>
#include <IV-GTK4/gdkdefs.h>
#include <IV-GTK4/gdkutil.h>

#include <InterViews/_enter.h>

class ClippingStack;
class Display;
class TransformerStack;

class CanvasDamage {
public:
    Coord left;
    Coord bottom;
    Coord right;
    Coord top;
};

/*
 * TextRenderInfo
 *
 * Buffered text drawn via Pango/Cairo.  Instead of XTextItem arrays we
 * collect UTF-8 bytes and call pango_cairo_show_layout() on flush().
 */
class TextRenderInfo {
public:
    CanvasRep*   canvas_;
    cairo_t*     cr_;          /* replaces GC drawgc_ */
    int          x0_;
    int          y0_;
    Coord        width_;
    Coord        curx_;
    Coord        cury_;
    char*        text_;
    char*        cur_text_;
    int          spaces_;
    /* XTextItem* items_ removed – Pango handles spacing internally */
};

/*
 * PathRenderInfo
 *
 * In X11 this held an XPoint[] array accumulated before stroke/fill.
 * In GTK4 we feed coordinates directly into a Cairo path; the struct is
 * kept for the curx_/cury_ tracking used by Canvas::character().
 */
class PathRenderInfo {
public:
    Coord curx_;
    Coord cury_;
    /* Points are accumulated in a simple resizable array */
    double* px_;          /* x coordinates */
    double* py_;          /* y coordinates */
    int     npoints_;
    int     max_points_;
    bool    path_open_;   /* true between move_to and close/stroke/fill */
};

/*
 * CanvasRep
 *
 * Replaces the X11 CanvasRep.  Instead of an XDrawable + GC pair we hold:
 *   surface_   – offscreen Cairo image surface (the "draw buffer")
 *   cr_        – Cairo context for surface_ (the "draw GC")
 *   copy_surface_ – second surface for double-buffering ("copy buffer")
 *   copy_cr_   – Cairo context for copy_surface_
 *
 * When the GTK4 "draw" signal fires the widget's cairo context is
 * temporarily saved in widget_cr_ so that finish_repair() can blit
 * surface_ to the screen.
 */
class CanvasRep {
public:
    Display*  display_;
    Window*   window_;
    Coord     width_;
    Coord     height_;
    PixelCoord pwidth_;
    PixelCoord pheight_;

    boolean   damaged_        : 1;
    boolean   on_damage_list_ : 1;
    boolean   repairing_      : 1;
    CanvasDamage damage_;

    /* Cairo drawing surfaces replacing XDrawable / Pixmap */
    cairo_surface_t* surface_;      /* offscreen draw surface */
    cairo_t*         cr_;           /* cairo context for surface_ */
    cairo_surface_t* copy_surface_; /* double-buffer copy surface */
    cairo_t*         copy_cr_;      /* cairo context for copy_surface_ */
    cairo_t*         widget_cr_;    /* GTK4 draw-callback context (transient) */

    /* Clip rectangle for current repair pass */
    cairo_rectangle_int_t clip_rect_;

    /* Clipping region (replaces Region clipping_) */
    cairo_region_t* clipping_;
    cairo_region_t* empty_;

    const Brush* brush_;
    const Color* color_;
    const Font*  font_;

    /* Current drawing state */
    cairo_operator_t op_;       /* replaces int op_ / GXcopy etc. */
    cairo_pattern_t* stipple_;  /* replaces Pixmap stipple_ */
    GdkRGBA          rgba_;     /* current foreground colour */
    int   brush_width_;
    double* dash_list_;         /* replaces char* dash_list_ */
    int   dash_count_;

    /* Pango layout used for text rendering */
    PangoLayout*    pango_layout_;
    boolean         text_twobyte_;   /* kept for source compat */
    boolean         text_reencode_;
    boolean         font_is_scaled_;
    boolean         transformed_;

    TransformerStack* transformers_;
    ClippingStack*    clippers_;

    static TextRenderInfo text_;
    static PathRenderInfo path_;

    enum { unbound = 0 };

    GdkDisplay* dpy() const;
    Transformer& matrix() const;

    void flush();
    void swapbuffers();
    void brush(const Brush*);
    void color(const Color*);
    void font(const Font*);

    void new_damage();
    void clear_damage();
    boolean start_repair();
    void finish_repair();

    void bind(boolean double_buffered);
    void unbind();

    /* For backward compatibility */
    CanvasLocation status_;

    void wait_for_copy();
};

#include <InterViews/_leave.h>

#endif /* iv_gtkcanvas_h */
