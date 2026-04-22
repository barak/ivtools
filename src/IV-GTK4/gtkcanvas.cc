/*
 * Copyright (c) 1987, 1988, 1989, 1990, 1991 Stanford University
 * Copyright (c) 1991 Silicon Graphics, Inc.
 *
 * GTK4 backend: Canvas implementation using Cairo.
 * Replaces IV-X11/xcanvas.cc.
 *
 * Architecture:
 *   – An offscreen cairo_image_surface_t is used as the back-buffer.
 *   – All drawing goes to this surface via rep()->cr_.
 *   – finish_repair() blits the damage region to widget_cr_ (the GTK4
 *     draw callback's cairo_t*).
 *   – Clipping is maintained by a stack of cairo_region_t objects.
 *   – Transforms are applied by cairo_transform() each time a new
 *     primitive is drawn.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <InterViews/bitmap.h>
#include <InterViews/brush.h>
#include <InterViews/canvas.h>
#include <InterViews/color.h>
#include <InterViews/display.h>
#include <InterViews/font.h>
#include <InterViews/raster.h>
#include <InterViews/style.h>
#include <InterViews/transformer.h>
#include <InterViews/window.h>
#include <IV-GTK4/gdklib.h>
#include <IV-GTK4/gdkbitmap.h>
#include <IV-GTK4/gtkbrush.h>
#include <IV-GTK4/gtkcanvas.h>
#include <IV-GTK4/gdkcolor.h>
#include <IV-GTK4/gdkdisplay.h>
#include <IV-GTK4/gdkfont.h>
#include <IV-GTK4/gdkraster.h>
#include <OS/math.h>
#include <OS/list.h>
#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Stacks                                                              */
/* ------------------------------------------------------------------ */

declarePtrList(TransformerStack, Transformer)
implementPtrList(TransformerStack, Transformer)

declarePtrList(ClipStack, cairo_region_t)
implementPtrList(ClipStack, cairo_region_t)

/* ------------------------------------------------------------------ */
/* Static render buffers                                               */
/* ------------------------------------------------------------------ */

TextRenderInfo CanvasRep::text_;
PathRenderInfo CanvasRep::path_;

/* ================================================================== */
/* class Canvas                                                         */
/* ================================================================== */

Canvas::Canvas() {
    CanvasRep* c = new CanvasRep;
    rep_ = c;

    /* Initialise static path/text buffers on first use */
    PathRenderInfo* p = &CanvasRep::path_;
    if (!p->point_) {
        p->point_     = new GdkPoint[10];
        p->cur_point_ = p->point_;
        p->end_point_ = p->point_ + 10;
    }
    TextRenderInfo* t = &CanvasRep::text_;
    if (!t->text_) {
        t->text_     = new char[1000];
        t->cur_text_ = t->text_;
    }

    c->surface_     = nullptr;
    c->cr_          = nullptr;
    c->widget_cr_   = nullptr;
    c->clip_region_ = cairo_region_create();
    c->clip_stack_  = new ClipStack;
    c->transformers_= new TransformerStack;
    c->transformed_ = false;

    Transformer* identity = new Transformer;
    c->transformers_->append(identity);

    c->display_  = nil;
    c->window_   = nil;

    c->width_    = 0;
    c->height_   = 0;
    c->pwidth_   = 0;
    c->pheight_  = 0;

    c->brush_       = nil;
    c->brush_width_ = 0;
    c->color_       = nil;
    c->font_        = nil;

    c->damaged_        = false;
    c->on_damage_list_ = false;
    c->repairing_      = false;
    c->status_         = Canvas::unmapped;

    c->double_buffered_ = false;
}

Canvas::~Canvas() {
    CanvasRep* c = rep();
    c->unbind();
    for (ListItr(TransformerStack) i(*c->transformers_); i.more(); i.next())
        delete i.cur();
    delete c->transformers_;
    if (c->clip_region_) cairo_region_destroy(c->clip_region_);
    for (ListItr(ClipStack) j(*c->clip_stack_); j.more(); j.next())
        cairo_region_destroy(j.cur());
    delete c->clip_stack_;
    delete c;
    rep_ = nil;
}

void Canvas::size(Coord width, Coord height) {
    CanvasRep* c = rep();
    c->width_  = width;
    c->height_ = height;
    if (c->display_) {
        c->pwidth_  = c->display_->to_pixels(width);
        c->pheight_ = c->display_->to_pixels(height);
    }
}

void Canvas::psize(PixelCoord pwidth, PixelCoord pheight) {
    CanvasRep& c = *rep();
    c.pwidth_  = pwidth;
    c.pheight_ = pheight;
    if (c.display_) {
        c.width_  = c.display_->to_coord(pwidth);
        c.height_ = c.display_->to_coord(pheight);
    }
}

Coord      Canvas::width()   const { return rep()->width_; }
Coord      Canvas::height()  const { return rep()->height_; }
PixelCoord Canvas::pwidth()  const { return rep()->pwidth_; }
PixelCoord Canvas::pheight() const { return rep()->pheight_; }

PixelCoord Canvas::to_pixels(Coord p)      const { return rep()->display_->to_pixels(p); }
Coord      Canvas::to_coord(PixelCoord p)  const { return rep()->display_->to_coord(p); }
Coord      Canvas::to_pixels_coord(Coord p)const {
    const Display& d = *rep()->display_;
    return d.to_coord(d.to_pixels(p));
}

/* ------------------------------------------------------------------ */
/* Transform management                                                */
/* ------------------------------------------------------------------ */

void Canvas::push_transform() {
    CanvasRep* c = rep();
    c->flush();
    TransformerStack& s = *c->transformers_;
    s.append(new Transformer(*s.item(s.count() - 1)));
}

void Canvas::pop_transform() {
    CanvasRep* c = rep();
    c->flush();
    TransformerStack& s = *c->transformers_;
    long i = s.count() - 1;
    if (i == 0) return; /* underflow */
    delete s.item(i);
    s.remove(i);
    c->transformed_ = !c->matrix().identity();
}

void Canvas::transform(const Transformer& t) {
    CanvasRep* c = rep();
    c->flush();
    c->matrix().premultiply(t);
    c->transformed_ = !c->matrix().identity();
}

void Canvas::transformer(const Transformer& t) {
    CanvasRep* c = rep();
    c->flush();
    c->matrix() = t;
    c->transformed_ = !t.identity();
}

const Transformer& Canvas::transformer() const { return rep()->matrix(); }

/* ------------------------------------------------------------------ */
/* Clipping                                                            */
/* ------------------------------------------------------------------ */

void Canvas::push_clipping() {
    CanvasRep* c = rep();
    c->flush();
    cairo_region_t* old_clip = c->clip_region_;
    cairo_region_t* new_clip = cairo_region_copy(old_clip);
    c->clip_stack_->append(old_clip);
    c->clip_region_ = new_clip;
}

void Canvas::pop_clipping() {
    CanvasRep* c = rep();
    c->flush();
    ClipStack& s = *c->clip_stack_;
    long n = s.count();
    if (n == 0) return; /* underflow */
    cairo_region_destroy(c->clip_region_);
    c->clip_region_ = s.item(n - 1);
    s.remove(n - 1);
    if (c->cr_) c->apply_clip();
}

void Canvas::clip() {
    CanvasRep* c = rep();
    if (!c->cr_) return;

    /* Intersect the current path with the existing clip region */
    cairo_t* cr = c->cr_;
    cairo_clip(cr);
    /* Rebuild the region from Cairo's current clip */
    double x1, y1, x2, y2;
    cairo_clip_extents(cr, &x1, &y1, &x2, &y2);
    cairo_rectangle_int_t r;
    r.x = (int)x1; r.y = (int)y1;
    r.width  = (int)(x2 - x1);
    r.height = (int)(y2 - y1);
    if (c->clip_region_) cairo_region_destroy(c->clip_region_);
    c->clip_region_ = cairo_region_create_rectangle(&r);
}

boolean Canvas::is_clipping() const {
    return !cairo_region_is_empty(rep()->clip_region_);
}

void Canvas::front_buffer() {
    /* Not meaningful in GTK4; drawing always goes to the offscreen surface */
}

void Canvas::back_buffer() { }

/* ------------------------------------------------------------------ */
/* Path primitives                                                     */
/* ------------------------------------------------------------------ */

static const float smoothness = 10.0f;

static boolean straight(const Transformer& tx,
    Coord x0, Coord y0, Coord x1, Coord y1,
    Coord x2, Coord y2, Coord x3, Coord y3)
{
    Coord tx0, tx1, tx2, tx3, ty0, ty1, ty2, ty3;
    tx.transform(x0, y0, tx0, ty0);
    tx.transform(x1, y1, tx1, ty1);
    tx.transform(x2, y2, tx2, ty2);
    tx.transform(x3, y3, tx3, ty3);
    float f = ((tx1+tx2)*(ty0-ty3) + (ty1+ty2)*(tx3-tx0)
               + 2*(tx0*ty3 - ty0*tx3));
    return (f * f) < smoothness;
}

static inline Coord mid(Coord a, Coord b) { return (a + b) / 2; }

/* Append a point to the path buffer, growing the array if necessary */
static GdkPoint* next_point(PathRenderInfo* p) {
    if (p->cur_point_ == p->end_point_) {
        int old_size = (int)(p->cur_point_ - p->point_);
        int new_size = 2 * old_size;
        GdkPoint* np = new GdkPoint[new_size];
        for (int i = 0; i < old_size; i++) np[i] = p->point_[i];
        delete[] p->point_;
        p->point_     = np;
        p->cur_point_ = p->point_ + old_size;
        p->end_point_ = p->point_ + new_size;
    }
    GdkPoint* xp = p->cur_point_;
    p->cur_point_++;
    return xp;
}

void Canvas::new_path() {
    PathRenderInfo* p = &CanvasRep::path_;
    p->curx_ = 0; p->cury_ = 0;
    p->cur_point_ = p->point_;
    if (rep()->cr_) cairo_new_path(rep()->cr_);
}

void Canvas::move_to(Coord x, Coord y) {
    CanvasRep* c = rep();
    PathRenderInfo* p = &CanvasRep::path_;
    p->curx_ = x; p->cury_ = y;

    Coord tx, ty;
    if (c->transformed_) c->matrix().transform(x, y, tx, ty);
    else { tx = x; ty = y; }

    Display* d = c->display_;
    GdkPoint* xp = p->point_;
    xp->x = d->to_pixels(tx);
    xp->y = c->pheight_ - d->to_pixels(ty);
    p->cur_point_ = xp + 1;

    if (c->cr_) cairo_move_to(c->cr_, xp->x, xp->y);
}

void Canvas::line_to(Coord x, Coord y) {
    CanvasRep* c = rep();
    PathRenderInfo* p = &CanvasRep::path_;
    p->curx_ = x; p->cury_ = y;

    Coord tx, ty;
    if (c->transformed_) c->matrix().transform(x, y, tx, ty);
    else { tx = x; ty = y; }

    Display* d = c->display_;
    GdkPoint* xp = next_point(p);
    xp->x = d->to_pixels(tx);
    xp->y = c->pheight_ - d->to_pixels(ty);

    if (c->cr_) cairo_line_to(c->cr_, xp->x, xp->y);
}

void Canvas::curve_to(Coord x, Coord y,
    Coord x1, Coord y1, Coord x2, Coord y2)
{
    CanvasRep* c = rep();
    const Transformer& m = c->matrix();
    if (straight(m, p.curx_, p.cury_, x1, y1, x2, y2, x, y)) {
        line_to(x, y);
        return;
    }
    /* Subdivide */
    Coord midx1 = mid(CanvasRep::path_.curx_, x1);
    Coord midy1 = mid(CanvasRep::path_.cury_, y1);
    Coord midx2 = mid(x1, x2);
    Coord midy2 = mid(y1, y2);
    Coord midx3 = mid(x2, x);
    Coord midy3 = mid(y2, y);
    Coord midx4 = mid(midx1, midx2);
    Coord midy4 = mid(midy1, midy2);
    Coord midx5 = mid(midx2, midx3);
    Coord midy5 = mid(midy2, midy3);
    Coord midx6 = mid(midx4, midx5);
    Coord midy6 = mid(midy4, midy5);
    curve_to(midx6, midy6, midx1, midy1, midx4, midy4);
    curve_to(x, y, midx5, midy5, midx3, midy3);
}

/* ------------------------------------------------------------------ */
/* Damage / repair                                                     */
/* ------------------------------------------------------------------ */

void Canvas::redraw(Coord l, Coord b, Coord r, Coord t) {
    CanvasRep& c = *rep();
    if (!c.damaged_) {
        c.damage_area_.x = c.display_->to_pixels(l);
        c.damage_area_.y = c.pheight_ - c.display_->to_pixels(t);
        c.damage_area_.width  = c.display_->to_pixels(r - l);
        c.damage_area_.height = c.display_->to_pixels(t - b);
    } else {
        cairo_rectangle_int_t nr;
        nr.x = c.display_->to_pixels(l);
        nr.y = c.pheight_ - c.display_->to_pixels(t);
        nr.width  = c.display_->to_pixels(r - l);
        nr.height = c.display_->to_pixels(t - b);
        /* Union the new damage area with the existing one */
        if (nr.x < c.damage_area_.x) {
            c.damage_area_.width += c.damage_area_.x - nr.x;
            c.damage_area_.x = nr.x;
        }
        if (nr.y < c.damage_area_.y) {
            c.damage_area_.height += c.damage_area_.y - nr.y;
            c.damage_area_.y = nr.y;
        }
        int nr_x2 = nr.x + nr.width;
        int da_x2 = c.damage_area_.x + c.damage_area_.width;
        if (nr_x2 > da_x2) c.damage_area_.width = nr_x2 - c.damage_area_.x;
        int nr_y2 = nr.y + nr.height;
        int da_y2 = c.damage_area_.y + c.damage_area_.height;
        if (nr_y2 > da_y2) c.damage_area_.height = nr_y2 - c.damage_area_.y;
    }
    c.damaged_ = true;
    if (!c.on_damage_list_ && c.display_) {
        c.display_->rep()->needs_repair(c.window_);
        c.on_damage_list_ = true;
    }
}

void Canvas::damage_all() {
    CanvasRep& c = *rep();
    c.damage_area_.x = 0; c.damage_area_.y = 0;
    c.damage_area_.width  = c.pwidth_;
    c.damage_area_.height = c.pheight_;
    c.damaged_ = true;
    if (!c.on_damage_list_ && c.display_) {
        c.display_->rep()->needs_repair(c.window_);
        c.on_damage_list_ = true;
    }
}

boolean Canvas::damaged(Coord& l, Coord& b, Coord& r, Coord& t) const {
    CanvasRep& c = *rep();
    if (!c.damaged_) return false;
    Display* d = c.display_;
    l = d->to_coord(c.damage_area_.x);
    b = d->to_coord(c.pheight_ - c.damage_area_.y - c.damage_area_.height);
    r = d->to_coord(c.damage_area_.x + c.damage_area_.width);
    t = d->to_coord(c.pheight_ - c.damage_area_.y);
    return true;
}

boolean Canvas::start_repair() {
    CanvasRep& c = *rep();
    if (!c.damaged_) return false;
    c.repairing_ = true;
    /* Ensure there's a drawing context pointing to the offscreen surface */
    if (c.cr_ && c.surface_) {
        cairo_save(c.cr_);
        /* Set up Cairo clip to the damage rectangle */
        cairo_rectangle(c.cr_,
            c.damage_area_.x, c.damage_area_.y,
            c.damage_area_.width, c.damage_area_.height);
        cairo_clip(c.cr_);
    }
    return true;
}

void Canvas::finish_repair() {
    CanvasRep& c = *rep();
    if (c.cr_) cairo_restore(c.cr_);

    /* Blit the damaged area from the offscreen surface to the widget context */
    if (c.widget_cr_ && c.surface_) {
        cairo_save(c.widget_cr_);
        cairo_rectangle(c.widget_cr_,
            c.damage_area_.x, c.damage_area_.y,
            c.damage_area_.width, c.damage_area_.height);
        cairo_clip(c.widget_cr_);
        cairo_set_source_surface(c.widget_cr_, c.surface_, 0, 0);
        cairo_paint(c.widget_cr_);
        cairo_restore(c.widget_cr_);
    }

    c.damaged_        = false;
    c.on_damage_list_ = false;
    c.repairing_      = false;
}

/* ------------------------------------------------------------------ */
/* CanvasRep helpers                                                   */
/* ------------------------------------------------------------------ */

Transformer& CanvasRep::matrix() {
    return *transformers_->item(transformers_->count() - 1);
}

void CanvasRep::flush() {
    /* Cairo does not buffer operations; nothing to flush */
}

void CanvasRep::apply_clip() {
    if (!cr_) return;
    cairo_reset_clip(cr_);
    if (cairo_region_is_empty(clip_region_)) return;
    int n = cairo_region_num_rectangles(clip_region_);
    for (int i = 0; i < n; i++) {
        cairo_rectangle_int_t r;
        cairo_region_get_rectangle(clip_region_, i, &r);
        cairo_rectangle(cr_, r.x, r.y, r.width, r.height);
    }
    cairo_clip(cr_);
}

void CanvasRep::bind(boolean double_buffered) {
    if (!pwidth_ || !pheight_) return;
    unbind();
    double_buffered_ = double_buffered;

    surface_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pwidth_, pheight_);
    cr_      = cairo_create(surface_);
    /* Clear to white */
    cairo_set_source_rgb(cr_, 1, 1, 1);
    cairo_paint(cr_);
}

void CanvasRep::unbind() {
    if (cr_)      { cairo_destroy(cr_);            cr_      = nullptr; }
    if (surface_) { cairo_surface_destroy(surface_); surface_ = nullptr; }
}

void CanvasRep::clear_damage() {
    damaged_        = false;
    on_damage_list_ = false;
    repairing_      = false;
    damage_area_    = { 0, 0, 0, 0 };
}

/* ================================================================== */
/* Drawing operations                                                  */
/* ================================================================== */

/* Apply current transform matrix to the Cairo context */
static void apply_transform(cairo_t* cr, const Transformer& m,
                            PixelCoord pheight, Display* d)
{
    /* InterViews Transformer is a 2D affine matrix.
       Map it to a cairo_matrix_t.
       Note: IV uses bottom-up Y; Cairo uses top-down Y.
       We apply the Y-flip here. */
    double a, b, c, e, f;   /* ax,bx,ay,by,tx,ty */
    double tx_val, ty_val;
    m.matrix(a, b, c, e, tx_val, ty_val);
    /* Flip Y: y_cairo = pheight - y_iv */
    cairo_matrix_t cm;
    cm.xx =  a;   cm.xy = -b;
    cm.yx = -c;   cm.yy =  e;   /* negate both y-related terms */
    cm.x0 = d->to_pixels(tx_val);
    cm.y0 = pheight - d->to_pixels(ty_val);
    cairo_set_matrix(cr, &cm);
}

/* Set colour and operator on the Cairo context */
static void set_color(cairo_t* cr, const Color* color, WindowVisual* wv) {
    if (!color || !cr) return;
    ColorRep* cr_rep = color->rep(wv);
    cairo_set_operator(cr, cr_rep->cairo_op_);
    cairo_set_source_rgba(cr, cr_rep->rgba_.red, cr_rep->rgba_.green,
                              cr_rep->rgba_.blue, cr_rep->rgba_.alpha);
}

/* Apply brush (line width and dash) */
static void set_brush(cairo_t* cr, const Brush* brush, Display* d) {
    if (!brush || !cr) return;
    BrushRep* br = brush->rep(d);
    cairo_set_line_width(cr, br->width_ > 0 ? (double)br->width_ : 1.0);
    if (br->dash_count_ > 0) {
        cairo_set_dash(cr, br->dash_list_, br->dash_count_, 0.0);
    } else {
        cairo_set_dash(cr, nullptr, 0, 0.0);
    }
}

void Canvas::stroke(const Color* c, const Brush* b) {
    CanvasRep* rep = this->rep();
    cairo_t*   cr  = rep->cr_;
    if (!cr) return;
    WindowVisual* wv = rep->window_ ?
        rep->window_->rep()->visual_ :
        (rep->display_ ? rep->display_->rep()->default_visual_ : nullptr);
    set_color(cr, c, wv);
    set_brush(cr, b, rep->display_);
    cairo_stroke_preserve(cr);
}

void Canvas::fill(const Color* c) {
    CanvasRep* rep = this->rep();
    cairo_t*   cr  = rep->cr_;
    if (!cr) return;
    WindowVisual* wv = rep->window_ ?
        rep->window_->rep()->visual_ :
        (rep->display_ ? rep->display_->rep()->default_visual_ : nullptr);
    set_color(cr, c, wv);
    cairo_fill_preserve(cr);
}

void Canvas::fill_rect(Coord l, Coord b, Coord r, Coord t, const Color* c) {
    CanvasRep* rep = this->rep();
    cairo_t*   cr  = rep->cr_;
    if (!cr) return;

    Display* d = rep->display_;
    int px_l = d->to_pixels(l);
    int px_b = rep->pheight_ - d->to_pixels(b);
    int px_r = d->to_pixels(r);
    int px_t = rep->pheight_ - d->to_pixels(t);

    WindowVisual* wv = rep->window_ ?
        rep->window_->rep()->visual_ :
        (rep->display_ ? rep->display_->rep()->default_visual_ : nullptr);
    set_color(cr, c, wv);
    cairo_rectangle(cr, px_l, px_t, px_r - px_l, px_b - px_t);
    cairo_fill(cr);
}

void Canvas::character(const Font* f, long ch, Coord width, const Color* c, Coord x, Coord y)
{
    CanvasRep* rep = this->rep();
    cairo_t*   cr  = rep->cr_;
    if (!cr) return;

    Display* d   = rep->display_;
    FontRep* fr  = f->rep(d);
    WindowVisual* wv = rep->window_ ?
        rep->window_->rep()->visual_ :
        (rep->display_ ? rep->display_->rep()->default_visual_ : nullptr);

    set_color(cr, c, wv);

    /* Build a one-character UTF-8 string */
    char utf8[8]; int n = 0;
    if (ch < 0x80)       utf8[n++] = (char)ch;
    else if (ch < 0x800) { utf8[n++]=char(0xC0|(ch>>6)); utf8[n++]=char(0x80|(ch&0x3F)); }
    else { utf8[n++]=char(0xE0|(ch>>12)); utf8[n++]=char(0x80|((ch>>6)&0x3F)); utf8[n++]=char(0x80|(ch&0x3F)); }
    utf8[n] = '\0';

    double px_x = d->to_pixels(x);
    double px_y = rep->pheight_ - d->to_pixels(y);

    cairo_save(cr);
    /* Y-flip for text: Pango lays out text top-down */
    cairo_translate(cr, px_x, px_y - fr->ascent_);
    pango_layout_set_text(fr->layout_, utf8, n);
    pango_cairo_show_layout(cr, fr->layout_);
    cairo_restore(cr);
    (void)width;
}

void Canvas::stencil(const Bitmap* mask, const Color* c, Coord x, Coord y)
{
    CanvasRep* rep = this->rep();
    cairo_t*   cr  = rep->cr_;
    if (!cr || !mask) return;

    BitmapRep* br = mask->rep();
    if (!br || !br->surface_) return;

    Display* d = rep->display_;
    WindowVisual* wv = rep->window_ ?
        rep->window_->rep()->visual_ :
        (rep->display_ ? rep->display_->rep()->default_visual_ : nullptr);

    double px_x = d->to_pixels(x) + d->to_pixels(br->left_);
    double px_y = rep->pheight_ - d->to_pixels(y) - br->pheight_
                  + d->to_pixels(-br->bottom_);

    cairo_save(cr);
    set_color(cr, c, wv);
    cairo_mask_surface(cr, br->surface_, px_x, px_y);
    cairo_restore(cr);
}

void Canvas::image(const Raster* r, Coord x, Coord y)
{
    CanvasRep* rep = this->rep();
    cairo_t*   cr  = rep->cr_;
    if (!cr || !r) return;

    RasterRep* rr = r->rep();
    if (!rr || !rr->surface_) return;

    Display* d = rep->display_;
    double px_x = d->to_pixels(x) + d->to_pixels(rr->left_);
    double px_y = rep->pheight_ - d->to_pixels(y) - rr->pheight_
                  + d->to_pixels(-rr->bottom_);

    cairo_save(cr);
    cairo_set_source_surface(cr, rr->surface_, px_x, px_y);
    cairo_rectangle(cr, px_x, px_y, rr->pwidth_, rr->pheight_);
    cairo_fill(cr);
    cairo_restore(cr);
}

/* ================================================================== */
/* Compatibility: old Painter-based API                                */
/* ================================================================== */

void Canvas::damage(Coord l, Coord b, Coord r, Coord t) {
    redraw(l, b, r, t);
}

void Canvas::damage(const Extension& ext) {
    redraw(ext.left(), ext.bottom(), ext.right(), ext.top());
}
