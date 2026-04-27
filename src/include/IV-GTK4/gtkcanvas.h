/*
 * GTK4 backend: replaces IV-X11/xcanvas.h
 * Defines CanvasRep, TextRenderInfo, PathRenderInfo using Cairo/Pango.
 */

#ifndef iv_gtkcanvas_h
#define iv_gtkcanvas_h

#include <InterViews/canvas.h>
#include <IV-GTK4/gdklib.h>
#include <IV-GTK4/gdkdefs.h>

#include <InterViews/_enter.h>

class ClipStack;
class Display;
class TransformerStack;

/* ------------------------------------------------------------------ */
/* Simple 2-D point type (replaces XPoint)                            */
/* ------------------------------------------------------------------ */
struct GtkPoint {
    short x;
    short y;
};

/* ------------------------------------------------------------------ */
/* CanvasDamage (same layout as X11 version)                          */
/* ------------------------------------------------------------------ */
class CanvasDamage {
public:
    Coord left;
    Coord bottom;
    Coord right;
    Coord top;
};

/* ------------------------------------------------------------------ */
/* TextRenderInfo                                                       */
/* ------------------------------------------------------------------ */
class TextRenderInfo {
public:
    CanvasRep* canvas_;
    cairo_t*   drawcr_;
    int        x0_;
    int        y0_;
    Coord      width_;
    Coord      curx_;
    Coord      cury_;
    char*      text_;
    char*      cur_text_;
    int        spaces_;
    /* No XTextItem; text is rendered directly via Pango */
};

/* ------------------------------------------------------------------ */
/* PathRenderInfo                                                       */
/* ------------------------------------------------------------------ */
class PathRenderInfo {
public:
    Coord      curx_;
    Coord      cury_;
    GtkPoint*  point_;
    GtkPoint*  cur_point_;
    GtkPoint*  end_point_;
};

/* ------------------------------------------------------------------ */
/* CanvasRep                                                            */
/* ------------------------------------------------------------------ */
class CanvasRep {
public:
    Display*          display_;
    Window*           window_;

    /* Back-buffer for double-buffering (replaces XDrawable xdrawable_) */
    cairo_surface_t*  surface_;
    XDrawable         xdrawable_;
    XDrawable         drawbuffer_;
    /* Drawing context onto the back-buffer */
    cairo_t*          cr_;
    GC                copygc_;
    /* GTK4 draw-callback context (front buffer / screen).
       Non-null ONLY while on_draw() is executing for this canvas.
       DisplayRep::needs_repair() checks this to avoid scheduling a
       redundant gtk_widget_queue_draw() during on_draw(), which would
       otherwise create a continuous per-frame redraw loop. */
    cairo_t*          widget_cr_;
    /* Auxiliary surface for copybuffer (replaces XDrawable copybuffer_) */
    cairo_surface_t*  copysurface_;
    XDrawable         copybuffer_;

    Coord             width_;
    Coord             height_;
    PixelCoord        pwidth_;
    PixelCoord        pheight_;

    boolean           damaged_        : 1;
    boolean           on_damage_list_ : 1;
    boolean           repairing_      : 1;
    CanvasDamage      damage_;

    /* Current clip rectangle (replaces XRectangle clip_) */
    XRectangle         clip_;
    cairo_rectangle_int_t clip_rect_;

    const Brush*      brush_;
    const Color*      color_;
    const Font*       font_;

    /* Clipping region stack (replaces Region clipping_ / Region empty_) */
    cairo_region_t*   clipping_;
    cairo_region_t*   empty_;

    /* Drawing mode (GDK/Cairo operator, replaces XGCValues.function) */
    int               op_;

    /* Stipple pattern (replaces Pixmap stipple_) */
    cairo_pattern_t*  stipple_;

    unsigned long     pixel_;       /* not used in GTK4; ABI compat */
    int               brush_width_;
    char*             dash_list_;
    int               dash_count_;

    /* Pango layout for the current font (replaces XFontStruct* xfont_) */
    PangoLayout*      playout_;

    boolean           text_twobyte_;
    boolean           text_reencode_;
    boolean           font_is_scaled_;
    boolean           transformed_;
    boolean           double_buffered_;

    TransformerStack* transformers_;
    ClipStack*        clippers_;

    static TextRenderInfo text_;
    static PathRenderInfo path_;

    enum { unbound = 0 };

    /* Accessor for the top-of-stack Transformer */
    Transformer& matrix();

    void flush();
    void swapbuffers();

    /* Apply the current clip region to cr_ */
    void apply_clip();

    /* Damage tracking helpers */
    void new_damage();
    void clear_damage();
    boolean start_repair();
    void finish_repair();

    /* Surface lifecycle */
    void bind(boolean double_buffered);
    void unbind();

    /* needs_repair: notify the display about a damaged window */
    void needs_repair(Window*);

    /* ABI compat */
    CanvasLocation status_;

    XDisplay* dpy() const;
};

#include <InterViews/_leave.h>

#endif /* iv_gtkcanvas_h */
