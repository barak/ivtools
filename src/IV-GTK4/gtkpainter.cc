/*
 * GTK4 backend: Painter implementation.
 * Replaces IV-2_6/xpainter.cc.
 *
 * The Painter class wraps the Cairo drawing API.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <InterViews/bitmap.h>
#include <InterViews/brush.h>
#include <InterViews/canvas.h>
#include <InterViews/color.h>
#include <InterViews/font.h>
#include <IV-2_6/InterViews/painter.h>
#include <InterViews/pattern.h>
#include <InterViews/raster.h>
#include <InterViews/transformer.h>
#include <IV-GTK4/gdklib.h>
#include <IV-GTK4/gtkbrush.h>
#include <IV-GTK4/gtkcanvas.h>
#include <IV-GTK4/gdkdisplay.h>
#include <IV-GTK4/gdkfont.h>
#include <IV-GTK4/gtkpainter.h>
#include <InterViews/session.h>
#include <OS/math.h>
#include <string.h>

/* ================================================================== */
/* PainterRep                                                          */
/* ================================================================== */

PainterRep::PainterRep() {
    display       = Session::instance() ? Session::instance()->default_display() : nullptr;
    fillgc        = nullptr;
    dashgc        = nullptr;
    fillbg        = true;
    overwrite     = false;
    x_or          = false;
    clipped       = false;
    clipped_canvas_ = nullptr;
    fill_pattern_ = nullptr;
    dashes_       = nullptr;
    n_dashes_     = 0;
    dash_offset_  = 0.0;
    memset(xclip, 0, sizeof(xclip));
}

PainterRep::~PainterRep() {
    if (fill_pattern_) { cairo_pattern_destroy(fill_pattern_); fill_pattern_ = nullptr; }
    delete[] dashes_;
}

void PainterRep::PrepareFill(const Pattern* /*p*/) {
    /* Pattern rendering is a no-op in this minimal port */
}

void PainterRep::PrepareDash(const Brush* /*b*/) {
    /* Dash configuration is handled per-draw call in gtkbrush */
}

/* ================================================================== */
/* Short-hand for point allocation                                     */
/* ================================================================== */

static const int XPointListSize = 200;
static GtkPoint gtkpoints[XPointListSize];

static GtkPoint* AllocPts(int n) {
    return (n <= XPointListSize) ? gtkpoints : new GtkPoint[n];
}

static void FreePts(GtkPoint* v) {
    if (v != gtkpoints) delete[] v;
}

/* ================================================================== */
/* class Painter                                                        */
/* ================================================================== */

Painter::Painter() {
    rep = new PainterRep;
    Init();
}

Painter::Painter(Painter* copy) {
    rep = new PainterRep;
    foreground = nil;
    background = nil;
    pattern    = nil;
    br         = nil;
    font       = nil;
    matrix     = nil;
    style      = 0;
    if (copy) {
        SetColors(copy->foreground, copy->background);
        SetPattern(copy->pattern);
        SetBrush(copy->br);
        SetFont(copy->font);
        SetTransformer(copy->matrix);
        style = copy->style;
        rep->fillbg = copy->rep->fillbg;
    }
}

Painter::~Painter() {
    Unref(foreground);
    Unref(background);
    Unref(pattern);
    Unref(br);
    Unref(font);
    Unref(matrix);
    delete rep;
}

void Painter::FillBg(boolean b) {
    rep->fillbg = b;
}

boolean Painter::BgFilled() const { return rep->fillbg; }

void Painter::SetColors(const Color* fg, const Color* bg) {
    Unref(foreground); Resource::ref(fg); foreground = fg;
    Unref(background); Resource::ref(bg); background = bg;
}

void Painter::SetPattern(const Pattern* p) {
    Unref(pattern); Resource::ref(p); pattern = p;
    if (p && rep) rep->PrepareFill(p);
}

void Painter::SetBrush(const Brush* b) {
    Unref(br); Resource::ref(b); br = b;
    if (b && rep) rep->PrepareDash(b);
}

void Painter::SetFont(const Font* f) {
    Unref(font); Resource::ref(f); font = f;
}

void Painter::Clip(Canvas* c,
                   IntCoord x0, IntCoord y0,
                   IntCoord x1, IntCoord y1) {
    if (!c) return;
    cairo_t* cr = c->rep()->cr_;
    if (!cr) return;

    /* If a prior clip is still active, remove it before setting a new one. */
    if (rep->clipped && rep->clipped_canvas_) {
        cairo_t* old_cr = rep->clipped_canvas_->rep()->cr_;
        if (old_cr) cairo_restore(old_cr);
    }

    /* Map IV-2.6 pixel corners to Cairo pixel coords. */
    IntCoord mx0, my0, mx1, my1;
    Map(c, x0, y0, mx0, my0);
    Map(c, x1, y1, mx1, my1);

    double cx = (double)Math::min(mx0, mx1);
    double cy = (double)Math::min(my0, my1);   /* smaller Cairo y = top */
    double cw = (double)(Math::abs(mx1 - mx0) + 1);
    double ch = (double)(Math::abs(my1 - my0) + 1);

    /* Push a save level so that NoClip() can restore with cairo_restore(). */
    cairo_save(cr);
    cairo_rectangle(cr, cx, cy, cw, ch);
    cairo_clip(cr);

    rep->clipped         = true;
    rep->clipped_canvas_ = c;
}

void Painter::NoClip() {
    if (rep->clipped && rep->clipped_canvas_) {
        cairo_t* cr = rep->clipped_canvas_->rep()->cr_;
        if (cr) cairo_restore(cr);
        rep->clipped_canvas_ = nullptr;
    }
    rep->clipped = false;
}

void Painter::SetOverwrite(boolean b) {
    rep->overwrite = b;
}

void Painter::SetPlaneMask(int /*mask*/) {
    /* No plane masks in Cairo */
}

/* Map 2.6 integer coordinates to canvas coordinates */
void Painter::Map(Canvas* c, IntCoord x, IntCoord y, IntCoord& mx, IntCoord& my)
{
    if (matrix == nil) {
        mx = x; my = y;
    } else {
        matrix->Transform(x, y, mx, my);
    }
    mx += xoff;
    my = c->pheight() - 1 - (my + yoff);
}

void Painter::MapList(Canvas* c, IntCoord x[], IntCoord y[], int n,
                      IntCoord mx[], IntCoord my[])
{
    IntCoord *xp = x, *yp = y, *mxp = mx, *myp = my;
    IntCoord* lim = &x[n];
    if (matrix == nil) {
        for (; xp < lim; xp++, yp++, mxp++, myp++) {
            *mxp = *xp + xoff;
            *myp = c->pheight() - 1 - (*yp + yoff);
        }
    } else {
        for (; xp < lim; xp++, yp++, mxp++, myp++) {
            matrix->Transform(*xp, *yp, *mxp, *myp);
            *mxp += xoff;
            *myp = c->pheight() - 1 - (*myp + yoff);
        }
    }
}

void Painter::MapList(Canvas* c, float x[], float y[], int n,
                      IntCoord mx[], IntCoord my[])
{
    float *xp = x, *yp = y, *lim = &x[n];
    IntCoord *mxp = mx, *myp = my;
    float tmpx, tmpy;
    if (matrix == nil) {
        for (; xp < lim; xp++, yp++, mxp++, myp++) {
            *mxp = Math::round(*xp + xoff);
            *myp = Math::round(c->pheight() - 1 - (*yp + yoff));
        }
    } else {
        for (; xp < lim; xp++, yp++, mxp++, myp++) {
            matrix->Transform(*xp, *yp, tmpx, tmpy);
            *mxp = Math::round(tmpx + xoff);
            *myp = Math::round(c->pheight() - 1 - (tmpy + yoff));
        }
    }
}

void Painter::Begin_xor() {
    rep->x_or = true;
}

void Painter::End_xor() {
    rep->x_or = false;
}

/* ================================================================== */
/* Drawing helpers                                                      */
/* ================================================================== */

/* Get a Cairo context from the Canvas's CanvasRep, set up with the
   current color and brush.  Returns nullptr if canvas is not drawable. */
static cairo_t* get_cr(Canvas* c, const Color* clr, const Brush* b,
                        bool fill_op, bool xor_mode) {
    if (!c) return nullptr;
    CanvasRep* cr = c->rep();
    if (!cr || !cr->cr_) return nullptr;
    cairo_t* cctx = cr->cr_;

    /* Apply color using Color::intensities() + Color::alpha() */
    if (clr) {
        ColorIntensity r = 0.0f, g = 0.0f, bl = 0.0f;
        float a = clr->alpha();
        clr->intensities(r, g, bl);
        cairo_set_source_rgba(cctx, (double)r, (double)g, (double)bl, (double)a);
    }
    /* Apply line width / dash if stroking */
    if (!fill_op && b) {
        BrushRep* brep = b->rep(cr->display_);
        if (brep) {
            cairo_set_line_width(cctx, (double)brep->width_);
            if (brep->dash_list_ && brep->dash_count_ > 0) {
                double* dv = new double[brep->dash_count_];
                for (int i = 0; i < brep->dash_count_; i++)
                    dv[i] = (double)(unsigned char)brep->dash_list_[i];
                cairo_set_dash(cctx, dv, brep->dash_count_, 0.0);
                delete[] dv;
            } else {
                cairo_set_dash(cctx, nullptr, 0, 0.0);
            }
        }
    }
    if (xor_mode) {
        cairo_set_operator(cctx, CAIRO_OPERATOR_XOR);
    } else {
        cairo_set_operator(cctx, CAIRO_OPERATOR_OVER);
    }
    return cctx;
}

/* Convert mapped IV-2_6 pixel coordinates to cairo coordinates.
   Map() already flips Y: my = pheight-1-y.  Cairo also has y=0 at top.
   So we just pass through the already-flipped coords. */
static void iv2cairo(CanvasRep* /*cr*/, IntCoord x, IntCoord y,
                     double& cx, double& cy) {
    cx = (double)x;
    cy = (double)y;
}

/* ================================================================== */
/* Drawing primitives                                                   */
/* ================================================================== */

void Painter::Point(Canvas* c, IntCoord x, IntCoord y) {
    IntCoord mx, my;
    Map(c, x, y, mx, my);
    cairo_t* cr = get_cr(c, foreground, br, false, rep->x_or);
    if (!cr) return;
    CanvasRep* crep = c->rep();
    double cx, cy;
    iv2cairo(crep, mx, my, cx, cy);
    cairo_rectangle(cr, cx, cy, 1.0, 1.0);
    cairo_fill(cr);
}

void Painter::MultiPoint(Canvas* c, IntCoord x[], IntCoord y[], int n) {
    for (int i = 0; i < n; i++) Point(c, x[i], y[i]);
}

void Painter::Line(Canvas* c, IntCoord x1, IntCoord y1, IntCoord x2, IntCoord y2) {
    IntCoord mx1, my1, mx2, my2;
    Map(c, x1, y1, mx1, my1);
    Map(c, x2, y2, mx2, my2);
    cairo_t* cr = get_cr(c, foreground, br, false, rep->x_or);
    if (!cr) return;
    CanvasRep* crep = c->rep();
    double cx1, cy1, cx2, cy2;
    iv2cairo(crep, mx1, my1, cx1, cy1);
    iv2cairo(crep, mx2, my2, cx2, cy2);
    cairo_move_to(cr, cx1, cy1);
    cairo_line_to(cr, cx2, cy2);
    cairo_stroke(cr);
}

void Painter::Rect(Canvas* c, IntCoord x1, IntCoord y1, IntCoord x2, IntCoord y2) {
    IntCoord mx1, my1, mx2, my2;
    Map(c, x1, y1, mx1, my1);
    Map(c, x2, y2, mx2, my2);
    cairo_t* cr = get_cr(c, foreground, br, false, rep->x_or);
    if (!cr) return;
    CanvasRep* crep = c->rep();
    double cx1, cy1, cx2, cy2;
    iv2cairo(crep, mx1, my1, cx1, cy1);
    iv2cairo(crep, mx2, my2, cx2, cy2);
    if (cx1 > cx2) { double t = cx1; cx1 = cx2; cx2 = t; }
    if (cy1 > cy2) { double t = cy1; cy1 = cy2; cy2 = t; }
    cairo_rectangle(cr, cx1, cy1, cx2 - cx1, cy2 - cy1);
    cairo_stroke(cr);
}

void Painter::FillRect(Canvas* c, IntCoord x1, IntCoord y1, IntCoord x2, IntCoord y2) {
    IntCoord mx1, my1, mx2, my2;
    Map(c, x1, y1, mx1, my1);
    Map(c, x2, y2, mx2, my2);
    cairo_t* cr = get_cr(c, foreground, br, true, rep->x_or);
    if (!cr) return;
    CanvasRep* crep = c->rep();
    double cx1, cy1, cx2, cy2;
    iv2cairo(crep, mx1, my1, cx1, cy1);
    iv2cairo(crep, mx2, my2, cx2, cy2);
    if (cx1 > cx2) { double t = cx1; cx1 = cx2; cx2 = t; }
    if (cy1 > cy2) { double t = cy1; cy1 = cy2; cy2 = t; }
    cairo_rectangle(cr, cx1, cy1, cx2 - cx1, cy2 - cy1);
    cairo_fill(cr);
}

void Painter::ClearRect(Canvas* c, IntCoord x1, IntCoord y1, IntCoord x2, IntCoord y2) {
    IntCoord mx1, my1, mx2, my2;
    Map(c, x1, y1, mx1, my1);
    Map(c, x2, y2, mx2, my2);
    cairo_t* cr = get_cr(c, background, nullptr, true, false);
    if (!cr) return;
    CanvasRep* crep = c->rep();
    double cx1, cy1, cx2, cy2;
    iv2cairo(crep, mx1, my1, cx1, cy1);
    iv2cairo(crep, mx2, my2, cx2, cy2);
    if (cx1 > cx2) { double t = cx1; cx1 = cx2; cx2 = t; }
    if (cy1 > cy2) { double t = cy1; cy1 = cy2; cy2 = t; }
    cairo_rectangle(cr, cx1, cy1, cx2 - cx1, cy2 - cy1);
    cairo_fill(cr);
}

void Painter::Circle(Canvas* c, IntCoord x, IntCoord y, int r) {
    IntCoord mx, my;
    Map(c, x, y, mx, my);
    cairo_t* cr = get_cr(c, foreground, br, false, rep->x_or);
    if (!cr) return;
    CanvasRep* crep = c->rep();
    double cx, cy;
    iv2cairo(crep, mx, my, cx, cy);
    cairo_arc(cr, cx, cy, (double)r, 0.0, 2.0 * G_PI);
    cairo_stroke(cr);
}

void Painter::FillCircle(Canvas* c, IntCoord x, IntCoord y, int r) {
    IntCoord mx, my;
    Map(c, x, y, mx, my);
    cairo_t* cr = get_cr(c, foreground, br, true, rep->x_or);
    if (!cr) return;
    CanvasRep* crep = c->rep();
    double cx, cy;
    iv2cairo(crep, mx, my, cx, cy);
    cairo_arc(cr, cx, cy, (double)r, 0.0, 2.0 * G_PI);
    cairo_fill(cr);
}

void Painter::MultiLine(Canvas* c, IntCoord x[], IntCoord y[], int n) {
    if (n < 2) return;
    cairo_t* cr = get_cr(c, foreground, br, false, rep->x_or);
    if (!cr) return;
    CanvasRep* crep = c->rep();
    IntCoord mx, my;
    Map(c, x[0], y[0], mx, my);
    double cx, cy;
    iv2cairo(crep, mx, my, cx, cy);
    cairo_move_to(cr, cx, cy);
    for (int i = 1; i < n; i++) {
        Map(c, x[i], y[i], mx, my);
        iv2cairo(crep, mx, my, cx, cy);
        cairo_line_to(cr, cx, cy);
    }
    cairo_stroke(cr);
}

void Painter::MultiLineNoMap(Canvas* c, IntCoord x[], IntCoord y[], int n) {
    MultiLine(c, x, y, n);
}

void Painter::Polygon(Canvas* c, IntCoord x[], IntCoord y[], int n) {
    if (n < 2) return;
    cairo_t* cr = get_cr(c, foreground, br, false, rep->x_or);
    if (!cr) return;
    CanvasRep* crep = c->rep();
    IntCoord mx, my;
    Map(c, x[0], y[0], mx, my);
    double cx, cy;
    iv2cairo(crep, mx, my, cx, cy);
    cairo_move_to(cr, cx, cy);
    for (int i = 1; i < n; i++) {
        Map(c, x[i], y[i], mx, my);
        iv2cairo(crep, mx, my, cx, cy);
        cairo_line_to(cr, cx, cy);
    }
    cairo_close_path(cr);
    cairo_stroke(cr);
}

void Painter::FillPolygon(Canvas* c, IntCoord x[], IntCoord y[], int n) {
    if (n < 2) return;
    cairo_t* cr = get_cr(c, foreground, br, true, rep->x_or);
    if (!cr) return;
    CanvasRep* crep = c->rep();
    IntCoord mx, my;
    Map(c, x[0], y[0], mx, my);
    double cx, cy;
    iv2cairo(crep, mx, my, cx, cy);
    cairo_move_to(cr, cx, cy);
    for (int i = 1; i < n; i++) {
        Map(c, x[i], y[i], mx, my);
        iv2cairo(crep, mx, my, cx, cy);
        cairo_line_to(cr, cx, cy);
    }
    cairo_close_path(cr);
    cairo_fill(cr);
}

void Painter::FillPolygonNoMap(Canvas* c, IntCoord x[], IntCoord y[], int n) {
    FillPolygon(c, x, y, n);
}

void Painter::Copy(Canvas* src, IntCoord x1, IntCoord y1,
                   IntCoord x2, IntCoord y2,
                   Canvas* dst, IntCoord x0, IntCoord y0)
{
    if (!src || !dst) return;
    CanvasRep* src_rep = src->rep();
    CanvasRep* dst_rep = dst->rep();
    cairo_t* src_cr = src_rep ? src_rep->cr_ : nullptr;
    cairo_t* dst_cr = dst_rep ? dst_rep->cr_ : nullptr;
    if (!src_cr || !dst_cr) return;

    /* Obtain the backing surface via the Cairo context: this works for both
       top-level canvases (which have their own surface_) and sub-window
       canvases (whose cr_ is a translated context on the parent surface). */
    cairo_surface_t* src_surf = cairo_get_target(src_cr);
    cairo_surface_t* dst_surf = cairo_get_target(dst_cr);
    if (!src_surf || !dst_surf) return;

    /* Map source corners to Cairo pixel coords (y from top, local). */
    IntCoord smx1, smy1, smx2, smy2;
    Map(src, x1, y1, smx1, smy1);
    Map(src, x2, y2, smx2, smy2);

    /* Source rect in the context's user-space (smaller y = top). */
    int src_x = Math::min(smx1, smx2);
    int src_y = Math::min(smy1, smy2);
    int src_w = Math::abs(smx2 - smx1) + 1;
    int src_h = Math::abs(smy2 - smy1) + 1;

    /* Source rect in surface (device) space: account for any translation
       on the source context (sub-window offset within the parent surface). */
    cairo_matrix_t src_mat;
    cairo_get_matrix(src_cr, &src_mat);
    int surf_src_x = (int)(src_x + src_mat.x0);
    int surf_src_y = (int)(src_y + src_mat.y0);

    /* Map destination origin.  Map() gives the Cairo y of the IV-2.6
       bottom edge; the destination top sits src_h pixels above it. */
    IntCoord dmx0, dmy0;
    Map(dst, x0, y0, dmx0, dmy0);
    int dst_x = dmx0;
    int dst_y = dmy0 - src_h + 1;   /* Cairo user-space y of destination top */

    /* When copying within the same backing surface we must stage through a
       temporary to avoid aliasing when source and destination overlap. */
    cairo_surface_t* pattern_surf;
    cairo_surface_t* tmp = nullptr;
    double pat_ox, pat_oy;  /* user-space coords where surface (0,0) appears */

    if (src_surf == dst_surf) {
        /* Capture the source region into a temporary image. */
        tmp = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, src_w, src_h);
        cairo_t* tc = cairo_create(tmp);
        cairo_set_source_surface(tc, src_surf,
                                 -(double)surf_src_x, -(double)surf_src_y);
        cairo_paint(tc);
        cairo_destroy(tc);
        pattern_surf = tmp;
        /* tmp(0,0) == src_surf(surf_src_x, surf_src_y).  In the destination
           context's user space the pattern origin (0,0) should appear at
           (dst_x, dst_y), i.e. pat_ox = dst_x, pat_oy = dst_y. */
        pat_ox = (double)dst_x;
        pat_oy = (double)dst_y;
    } else {
        pattern_surf = src_surf;
        /* src_surf(surf_src_x, surf_src_y) should appear at user (dst_x, dst_y):
           pat_ox = dst_x - surf_src_x, pat_oy = dst_y - surf_src_y. */
        pat_ox = (double)(dst_x - surf_src_x);
        pat_oy = (double)(dst_y - surf_src_y);
    }

    cairo_save(dst_cr);
    cairo_set_operator(dst_cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(dst_cr, pattern_surf, pat_ox, pat_oy);
    cairo_rectangle(dst_cr, (double)dst_x, (double)dst_y,
                    (double)src_w, (double)src_h);
    cairo_fill(dst_cr);
    cairo_restore(dst_cr);

    if (tmp) cairo_surface_destroy(tmp);
}

void Painter::Text(Canvas* c, const char* s, int len, IntCoord x, IntCoord y) {
    if (!c || !s || !font) return;
    IntCoord mx, my;
    Map(c, x, y, mx, my);
    CanvasRep* crep = c->rep();
    /* Map() converts IV-2.6 pixel y (from bottom) to Cairo pixel y (from top).
       Canvas::character() expects InterViews Coord y (from bottom), so undo
       the flip before converting pixels to points. */
    IntCoord iv_my = (IntCoord)crep->pheight_ - 1 - my;
    ivCoord fx = crep->display_ ? crep->display_->to_coord(mx)    : (ivCoord)mx;
    ivCoord fy = crep->display_ ? crep->display_->to_coord(iv_my) : (ivCoord)iv_my;
    for (int i = 0; i < len; i++) {
        ivCoord w = font->width(s[i]);
        c->character(font, s[i], w, foreground, fx, fy);
        fx += w;
    }
}

void Painter::Stencil(Canvas* c, IntCoord x, IntCoord y,
                      Bitmap* image, Bitmap* /*mask*/) {
    if (!c || !image) return;
    IntCoord mx, my;
    Map(c, x, y, mx, my);
    CanvasRep* crep = c->rep();
    /* Same Y-flip correction as in Painter::Text */
    IntCoord iv_my = (IntCoord)crep->pheight_ - 1 - my;
    ivCoord fx = crep->display_ ? crep->display_->to_coord(mx)    : (ivCoord)mx;
    ivCoord fy = crep->display_ ? crep->display_->to_coord(iv_my) : (ivCoord)iv_my;
    c->stencil(image, foreground, fx, fy);
}

void Painter::RasterRect(Canvas* c, IntCoord x, IntCoord y, Raster* r) {
    if (!c || !r) return;
    IntCoord mx, my;
    Map(c, x, y, mx, my);
    CanvasRep* crep = c->rep();
    /* Same Y-flip correction as in Painter::Text */
    IntCoord iv_my = (IntCoord)crep->pheight_ - 1 - my;
    ivCoord fx = crep->display_ ? crep->display_->to_coord(mx)    : (ivCoord)mx;
    ivCoord fy = crep->display_ ? crep->display_->to_coord(iv_my) : (ivCoord)iv_my;
    c->image(r, fx, fy);
}

/* DrawTransformedImage is used for rotated Bitmap/Raster operations */
void DrawTransformedImage(
    cairo_surface_t* /*src*/, int /*sx0*/, int /*sy0*/,
    cairo_surface_t* /*mask*/, int /*mx0*/, int /*my0*/,
    cairo_surface_t* /*dst*/, unsigned int /*height*/,
    int /*dx0*/, int /*dy0*/,
    boolean /*stencil*/, GdkRGBA /*fg*/, GdkRGBA /*bg*/,
    cairo_t* /*cr*/, const Transformer& /*matrix*/)
{
    /* Transformed image drawing not implemented in this port */
}
