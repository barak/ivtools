/*
 * GTK4 backend: Canvas implementation using Cairo.
 * Replaces IV-X11/xcanvas.cc.
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

static void gtk4_canvasrep_sync_compat(CanvasRep* c) {
    c->xdrawable_ = c->surface_;
    c->drawbuffer_ = c->surface_;
    c->copybuffer_ = c->copysurface_ ? c->copysurface_ : c->surface_;
    c->copygc_ = c->cr_;
}

XDisplay* CanvasRep::dpy() const {
    return (display_ && display_->rep()) ? display_->rep()->display_ : nullptr;
}

/* ================================================================== */
/* class Canvas                                                         */
/* ================================================================== */

Canvas::Canvas() {
    CanvasRep* c = new CanvasRep;
    rep_ = c;

    PathRenderInfo* p = &CanvasRep::path_;
    if (!p->point_) {
        p->point_     = new GtkPoint[10];
        p->cur_point_ = p->point_;
        p->end_point_ = p->point_ + 10;
    }
    TextRenderInfo* t = &CanvasRep::text_;
    if (!t->text_) {
        t->text_     = new char[1000];
        t->cur_text_ = t->text_;
    }

    c->surface_        = nullptr;
    c->xdrawable_      = nullptr;
    c->drawbuffer_     = nullptr;
    c->cr_             = nullptr;
    c->copygc_         = nullptr;
    c->widget_cr_      = nullptr;
    c->copysurface_    = nullptr;
    c->copybuffer_     = nullptr;
    c->clipping_       = cairo_region_create();
    c->empty_          = cairo_region_create();
    c->clippers_       = new ClipStack;
    c->transformers_   = new TransformerStack;
    c->transformed_    = false;
    c->double_buffered_= false;
    c->clip_.x = c->clip_.y = 0;
    c->clip_.width = c->clip_.height = 0;

    Transformer* identity = new Transformer;
    c->transformers_->append(identity);

    c->display_  = nil;
    c->window_   = nil;

    c->width_    = 0;
    c->height_   = 0;
    c->pwidth_   = 0;
    c->pheight_  = 0;

    c->brush_       = nil;
    c->color_       = nil;
    c->font_        = nil;
    c->stipple_     = nullptr;
    c->playout_     = nullptr;
    c->dash_list_   = nullptr;
    c->dash_count_  = 0;
    c->brush_width_ = 0;
    c->pixel_       = 0;
    c->op_          = 0;
    c->text_twobyte_   = false;
    c->text_reencode_  = false;
    c->font_is_scaled_ = false;

    c->damaged_        = false;
    c->on_damage_list_ = false;
    c->repairing_      = false;
    c->status_         = Canvas::unmapped;

    c->damage_.left = c->damage_.bottom = 0;
    c->damage_.right = c->damage_.top = 0;
}

Canvas::~Canvas() {
    CanvasRep* c = rep();
    c->unbind();
    for (ListItr(TransformerStack) i(*c->transformers_); i.more(); i.next())
        delete i.cur();
    delete c->transformers_;
    if (c->clipping_) { cairo_region_destroy(c->clipping_); c->clipping_ = nullptr; }
    if (c->empty_)    { cairo_region_destroy(c->empty_);    c->empty_    = nullptr; }
    for (ListItr(ClipStack) j(*c->clippers_); j.more(); j.next())
        cairo_region_destroy(j.cur());
    delete c->clippers_;
    delete c;
    rep_ = nil;
}

/* CanvasRep helpers */

Transformer& CanvasRep::matrix() {
    return *transformers_->item(transformers_->count() - 1);
}

void CanvasRep::flush() { /* Cairo does not need flushing */ }

void CanvasRep::apply_clip() {
    if (!cr_) return;
    cairo_reset_clip(cr_);
    if (cairo_region_is_empty(clipping_)) return;
    int n = cairo_region_num_rectangles(clipping_);
    for (int i = 0; i < n; i++) {
        cairo_rectangle_int_t r;
        cairo_region_get_rectangle(clipping_, i, &r);
        cairo_rectangle(cr_, r.x, r.y, r.width, r.height);
    }
    cairo_clip(cr_);
}

void CanvasRep::clear_damage() {
    damaged_        = false;
    on_damage_list_ = false;
    repairing_      = false;
    damage_.left = damage_.bottom = damage_.right = damage_.top = 0;
}

boolean CanvasRep::start_repair() {
    if (!damaged_) return false;
    repairing_ = true;
    if (cr_) {
        cairo_save(cr_);
        int px_l = display_ ? display_->to_pixels(damage_.left) : 0;
        int px_b = display_ ? display_->to_pixels(damage_.bottom) : 0;
        int px_r = display_ ? display_->to_pixels(damage_.right) : pwidth_;
        int px_t = display_ ? display_->to_pixels(damage_.top) : pheight_;
        int px_w = px_r - px_l;
        int px_h = pheight_ - px_b - (pheight_ - (display_ ? display_->to_pixels(damage_.top) : pheight_));
        if (px_w < 0) px_w = 0;
        if (px_h < 0) px_h = 0;
        cairo_rectangle(cr_, px_l, pheight_ - px_t, px_w, px_t - px_b);
        cairo_clip(cr_);
    }
    return true;
}

void CanvasRep::finish_repair() {
    if (cr_) cairo_restore(cr_);
    if (widget_cr_ && surface_) {
        cairo_save(widget_cr_);
        cairo_set_source_surface(widget_cr_, surface_, 0, 0);
        cairo_paint(widget_cr_);
        cairo_restore(widget_cr_);
    }
    damaged_        = false;
    on_damage_list_ = false;
    repairing_      = false;
}

void CanvasRep::bind(boolean double_buffered) {
    if (!pwidth_ || !pheight_) return;
    unbind();
    double_buffered_ = double_buffered;
    surface_ = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pwidth_, pheight_);
    cr_      = cairo_create(surface_);
    cairo_set_source_rgb(cr_, 1, 1, 1);
    cairo_paint(cr_);
    gtk4_canvasrep_sync_compat(this);
}

void CanvasRep::unbind() {
    if (cr_)          { cairo_destroy(cr_);              cr_          = nullptr; }
    if (surface_)     { cairo_surface_destroy(surface_); surface_     = nullptr; }
    if (copysurface_) { cairo_surface_destroy(copysurface_); copysurface_ = nullptr; }
    gtk4_canvasrep_sync_compat(this);
}

void CanvasRep::needs_repair(Window*) {
    /* In GTK4 we request a redraw via gtk_widget_queue_draw() on the
       associated widget.  That callback is wired in gtkwindow.cc. */
    /* Stub: actual wiring is done by WindowRep */
}

void CanvasRep::swapbuffers() {
    if (widget_cr_ && surface_) {
        cairo_set_source_surface(widget_cr_, surface_, 0, 0);
        cairo_paint(widget_cr_);
    }
}

/* ================================================================== */
/* Canvas public API                                                   */
/* ================================================================== */

Window* Canvas::window() const { return rep()->window_; }

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

PixelCoord Canvas::to_pixels(Coord p) const {
    return rep()->display_ ? rep()->display_->to_pixels(p) : (PixelCoord)p;
}
Coord Canvas::to_coord(PixelCoord p) const {
    return rep()->display_ ? rep()->display_->to_coord(p) : (Coord)p;
}
Coord Canvas::to_pixels_coord(Coord p) const {
    if (!rep()->display_) return p;
    const Display& d = *rep()->display_;
    return d.to_coord(d.to_pixels(p));
}

CanvasLocation Canvas::status() const { return rep()->status_; }

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
    if (i == 0) return;
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
    cairo_region_t* old_clip = c->clipping_;
    cairo_region_t* new_clip = cairo_region_copy(old_clip);
    c->clippers_->append(old_clip);
    c->clipping_ = new_clip;
}

void Canvas::pop_clipping() {
    CanvasRep* c = rep();
    c->flush();
    ClipStack& s = *c->clippers_;
    long n = s.count();
    if (n == 0) return;
    cairo_region_destroy(c->clipping_);
    c->clipping_ = s.item(n - 1);
    s.remove(n - 1);
    if (c->cr_) c->apply_clip();
}

void Canvas::clip() {
    CanvasRep* c = rep();
    if (!c->cr_) return;
    cairo_clip(c->cr_);
    double x1, y1, x2, y2;
    cairo_clip_extents(c->cr_, &x1, &y1, &x2, &y2);
    cairo_rectangle_int_t r;
    r.x = (int)x1; r.y = (int)y1;
    r.width  = (int)(x2 - x1);
    r.height = (int)(y2 - y1);
    c->clip_.x = (short)r.x;
    c->clip_.y = (short)r.y;
    c->clip_.width = (unsigned short)r.width;
    c->clip_.height = (unsigned short)r.height;
    if (c->clipping_) cairo_region_destroy(c->clipping_);
    c->clipping_ = cairo_region_create_rectangle(&r);
}

void Canvas::clip_rect(Coord l, Coord b, Coord r, Coord t) {
    CanvasRep* c = rep();
    if (!c->cr_ || !c->display_) return;
    int px_l = c->display_->to_pixels(l);
    int px_b = c->pheight_ - c->display_->to_pixels(t);
    int px_w = c->display_->to_pixels(r - l);
    int px_h = c->display_->to_pixels(t - b);
    cairo_rectangle(c->cr_, px_l, px_b, px_w, px_h);
    clip();
}

void Canvas::front_buffer() { }
void Canvas::back_buffer()  { }

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

static GtkPoint* next_point(PathRenderInfo* p) {
    if (p->cur_point_ == p->end_point_) {
        int old_size = (int)(p->cur_point_ - p->point_);
        int new_size = 2 * old_size;
        GtkPoint* np = new GtkPoint[new_size];
        for (int i = 0; i < old_size; i++) np[i] = p->point_[i];
        delete[] p->point_;
        p->point_     = np;
        p->cur_point_ = p->point_ + old_size;
        p->end_point_ = p->point_ + new_size;
    }
    return p->cur_point_++;
}

static PixelCoord iv_to_pixels(Display* d, Coord c) {
    return d ? d->to_pixels(c) : (PixelCoord)c;
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

    Coord tx = x, ty = y;
    if (c->transformed_) c->matrix().transform(x, y, tx, ty);

    GtkPoint* pt = p->point_;
    pt->x = (short)iv_to_pixels(c->display_, tx);
    pt->y = (short)(c->pheight_ - iv_to_pixels(c->display_, ty));
    p->cur_point_ = pt + 1;

    if (c->cr_) cairo_move_to(c->cr_, pt->x, pt->y);
}

void Canvas::line_to(Coord x, Coord y) {
    CanvasRep* c = rep();
    PathRenderInfo* p = &CanvasRep::path_;
    p->curx_ = x; p->cury_ = y;

    Coord tx = x, ty = y;
    if (c->transformed_) c->matrix().transform(x, y, tx, ty);

    GtkPoint* pt = next_point(p);
    pt->x = (short)iv_to_pixels(c->display_, tx);
    pt->y = (short)(c->pheight_ - iv_to_pixels(c->display_, ty));

    if (c->cr_) cairo_line_to(c->cr_, pt->x, pt->y);
}

void Canvas::curve_to(Coord x, Coord y,
    Coord x1, Coord y1, Coord x2, Coord y2)
{
    CanvasRep* c = rep();
    PathRenderInfo* p = &CanvasRep::path_;
    if (straight(c->matrix(), p->curx_, p->cury_, x1, y1, x2, y2, x, y)) {
        line_to(x, y);
        return;
    }
    Coord cx = p->curx_, cy = p->cury_;
    Coord midx1 = mid(cx, x1); Coord midy1 = mid(cy, y1);
    Coord midx2 = mid(x1, x2); Coord midy2 = mid(y1, y2);
    Coord midx3 = mid(x2, x);  Coord midy3 = mid(y2, y);
    Coord midx4 = mid(midx1, midx2); Coord midy4 = mid(midy1, midy2);
    Coord midx5 = mid(midx2, midx3); Coord midy5 = mid(midy2, midy3);
    Coord midx6 = mid(midx4, midx5); Coord midy6 = mid(midy4, midy5);
    curve_to(midx6, midy6, midx1, midy1, midx4, midy4);
    curve_to(x, y, midx5, midy5, midx3, midy3);
}

void Canvas::close_path() {
    if (rep()->cr_) cairo_close_path(rep()->cr_);
}

/* ------------------------------------------------------------------ */
/* Damage                                                              */
/* ------------------------------------------------------------------ */

static void extend_damage(CanvasDamage& d, Coord l, Coord b, Coord r, Coord t,
                           boolean& damaged)
{
    if (!damaged) {
        d.left = l; d.bottom = b; d.right = r; d.top = t;
        damaged = true;
    } else {
        if (l < d.left)   d.left   = l;
        if (b < d.bottom) d.bottom = b;
        if (r > d.right)  d.right  = r;
        if (t > d.top)    d.top    = t;
    }
}

void Canvas::damage(Coord l, Coord b, Coord r, Coord t) {
    CanvasRep& c = *rep();
    boolean dmg = c.damaged_; /* copy bit-field to ordinary bool */
    extend_damage(c.damage_, l, b, r, t, dmg);
    c.damaged_ = dmg;
    if (!c.on_damage_list_ && c.display_) {
        c.needs_repair(c.window_);
        c.on_damage_list_ = true;
    }
}

void Canvas::damage(const Extension& ext) {
    damage(ext.left(), ext.bottom(), ext.right(), ext.top());
}

boolean Canvas::damaged(const Extension& ext) const {
    return damaged(ext.left(), ext.bottom(), ext.right(), ext.top());
}

boolean Canvas::damaged(Coord l, Coord b, Coord r, Coord t) const {
    CanvasRep& c = *rep();
    if (!c.damaged_) return false;
    return !(r < c.damage_.left || l > c.damage_.right ||
             t < c.damage_.bottom || b > c.damage_.top);
}

void Canvas::damage_area(Extension& ext) {
    CanvasRep& c = *rep();
    ext.set_xy(this, c.damage_.left, c.damage_.bottom,
                     c.damage_.right, c.damage_.top);
}

void Canvas::damage_all() {
    CanvasRep& c = *rep();
    if (c.display_) {
        c.damage_.left   = 0;
        c.damage_.bottom = 0;
        c.damage_.right  = c.width_;
        c.damage_.top    = c.height_;
    }
    c.damaged_ = true;
    if (!c.on_damage_list_ && c.display_) {
        c.needs_repair(c.window_);
        c.on_damage_list_ = true;
    }
}

boolean Canvas::any_damage() const { return rep()->damaged_; }

void Canvas::restrict_damage(const Extension& ext) {
    restrict_damage(ext.left(), ext.bottom(), ext.right(), ext.top());
}

void Canvas::restrict_damage(Coord l, Coord b, Coord r, Coord t) {
    CanvasRep& c = *rep();
    if (!c.damaged_) return;
    if (l > c.damage_.left)   c.damage_.left   = l;
    if (b > c.damage_.bottom) c.damage_.bottom = b;
    if (r < c.damage_.right)  c.damage_.right  = r;
    if (t < c.damage_.top)    c.damage_.top    = t;
}

void Canvas::redraw(Coord l, Coord b, Coord r, Coord t) {
    damage(l, b, r, t);
}

void Canvas::repair() {
    CanvasRep& c = *rep();
    if (c.start_repair()) {
        /* Actual repaint happens via the window's glyph redraw */
        c.finish_repair();
    }
}

/* ------------------------------------------------------------------ */
/* Misc anachronisms                                                   */
/* ------------------------------------------------------------------ */

unsigned int Canvas::Width()  const { return (unsigned int)pwidth(); }
unsigned int Canvas::Height() const { return (unsigned int)pheight(); }
void Canvas::SetBackground(const Color*) { }

/* ================================================================== */
/* Drawing operations                                                  */
/* ================================================================== */

static WindowVisual* canvas_visual(CanvasRep* c) {
    if (c->window_ && c->window_->rep())
        return c->window_->rep()->visual_;
    if (c->display_ && c->display_->rep())
        return c->display_->rep()->default_visual_;
    return nullptr;
}

static void set_color_on_cr(cairo_t* cr, const Color* color, WindowVisual* wv) {
    if (!color || !cr) return;
    ColorRep* cr_rep = color->rep(wv);
    cairo_set_operator(cr, cr_rep->cairo_op_);
    cairo_set_source_rgba(cr,
        cr_rep->rgba_.red, cr_rep->rgba_.green,
        cr_rep->rgba_.blue, cr_rep->rgba_.alpha);
}

static void set_brush_on_cr(cairo_t* cr, const Brush* brush, Display* d) {
    if (!brush || !cr) return;
    BrushRep* br = brush->rep(d);
    cairo_set_line_width(cr, br->width_ > 0 ? (double)br->width_ : 1.0);
    if (br->dash_count_ > 0)
        cairo_set_dash(cr, br->dash_list_, br->dash_count_, 0.0);
    else
        cairo_set_dash(cr, nullptr, 0, 0.0);
}

void Canvas::stroke(const Color* c, const Brush* b) {
    CanvasRep* rep = this->rep();
    cairo_t* cr = rep->cr_;
    if (!cr) return;
    set_color_on_cr(cr, c, canvas_visual(rep));
    set_brush_on_cr(cr, b, rep->display_);
    cairo_stroke_preserve(cr);
}

void Canvas::line(Coord x1, Coord y1, Coord x2, Coord y2,
                  const Color* c, const Brush* b)
{
    new_path();
    move_to(x1, y1);
    line_to(x2, y2);
    stroke(c, b);
}

void Canvas::rect(Coord l, Coord b, Coord r, Coord t,
                  const Color* c, const Brush* br)
{
    new_path();
    move_to(l, b);
    line_to(r, b);
    line_to(r, t);
    line_to(l, t);
    close_path();
    stroke(c, br);
}

void Canvas::fill(const Color* c) {
    CanvasRep* rep = this->rep();
    cairo_t* cr = rep->cr_;
    if (!cr) return;
    set_color_on_cr(cr, c, canvas_visual(rep));
    cairo_fill_preserve(cr);
}

void Canvas::fill_rect(Coord l, Coord b, Coord r, Coord t, const Color* c) {
    CanvasRep* rep = this->rep();
    cairo_t* cr = rep->cr_;
    if (!cr) return;

    Display* d = rep->display_;
    int px_l = d ? d->to_pixels(l) : (int)l;
    int px_t = rep->pheight_ - (d ? d->to_pixels(t) : (int)t);
    int px_w = d ? d->to_pixels(r - l) : (int)(r - l);
    int px_h = d ? d->to_pixels(t - b) : (int)(t - b);

    set_color_on_cr(cr, c, canvas_visual(rep));
    cairo_rectangle(cr, px_l, px_t, px_w, px_h);
    cairo_fill(cr);
}

void Canvas::character(const Font* f, long ch, Coord width,
                        const Color* c, Coord x, Coord y)
{
    CanvasRep* rep = this->rep();
    cairo_t* cr = rep->cr_;
    if (!cr || !f) return;

    Display* d   = rep->display_;
    FontRep* fr  = f->rep(d);

    char utf8[8]; int n = 0;
    if (ch < 0x80)       utf8[n++] = (char)ch;
    else if (ch < 0x800) { utf8[n++]=char(0xC0|(ch>>6)); utf8[n++]=char(0x80|(ch&0x3F)); }
    else { utf8[n++]=char(0xE0|(ch>>12)); utf8[n++]=char(0x80|((ch>>6)&0x3F)); utf8[n++]=char(0x80|(ch&0x3F)); }
    utf8[n] = '\0';

    double px_x = d ? d->to_pixels(x) : (double)x;
    double px_y = rep->pheight_ - (d ? d->to_pixels(y) : (double)y);

    set_color_on_cr(cr, c, canvas_visual(rep));
    cairo_save(cr);
    cairo_translate(cr, px_x, px_y - fr->ascent_);
    pango_layout_set_text(fr->layout_, utf8, n);
    pango_cairo_show_layout(cr, fr->layout_);
    cairo_restore(cr);
    (void)width;
}

void Canvas::stencil(const Bitmap* mask, const Color* c, Coord x, Coord y) {
    CanvasRep* rep = this->rep();
    cairo_t* cr = rep->cr_;
    if (!cr || !mask) return;

    BitmapRep* br = mask->rep();
    if (!br || !br->surface_) return;

    Display* d = rep->display_;
    double px_x = (d ? d->to_pixels(x) : (double)x) + (d ? d->to_pixels(br->left_) : br->left_);
    double px_y = rep->pheight_ - (d ? d->to_pixels(y) : (double)y) - br->pheight_;

    set_color_on_cr(cr, c, canvas_visual(rep));
    cairo_save(cr);
    cairo_mask_surface(cr, br->surface_, px_x, px_y);
    cairo_restore(cr);
}

void Canvas::image(const Raster* r, Coord x, Coord y) {
    CanvasRep* rep = this->rep();
    cairo_t* cr = rep->cr_;
    if (!cr || !r) return;

    RasterRep* rr = r->rep();
    if (!rr || !rr->surface_) return;

    Display* d = rep->display_;
    double px_x = (d ? d->to_pixels(x) : (double)x) + (d ? d->to_pixels(rr->left_) : rr->left_);
    double px_y = rep->pheight_ - (d ? d->to_pixels(y) : (double)y) - rr->pheight_;

    cairo_save(cr);
    cairo_set_source_surface(cr, rr->surface_, px_x, px_y);
    cairo_rectangle(cr, px_x, px_y, rr->pwidth_, rr->pheight_);
    cairo_fill(cr);
    cairo_restore(cr);
}
