/*
 * GTK4 backend: Painter implementation.
 * Replaces IV-X11/xpainter.cc.
 *
 * The Painter class wraps the Canvas drawing API, applying current
 * brush, color, font, and transform state before each drawing call.
 * In the GTK4 backend the underlying rendering is done via Cairo.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <InterViews/bitmap.h>
#include <InterViews/brush.h>
#include <InterViews/canvas.h>
#include <InterViews/color.h>
#include <InterViews/font.h>
#include <InterViews/painter.h>
#include <InterViews/raster.h>
#include <InterViews/transformer.h>
#include <IV-GTK4/gdklib.h>
#include <IV-GTK4/gtkbrush.h>
#include <IV-GTK4/gtkcanvas.h>
#include <IV-GTK4/gdkcolor.h>
#include <IV-GTK4/gdkdisplay.h>
#include <IV-GTK4/gdkfont.h>
#include <IV-GTK4/gtkpainter.h>
#include <OS/math.h>
#include <string.h>

/* ================================================================== */
/* PainterRep                                                          */
/* ================================================================== */

PainterRep::PainterRep()
: foreground_(nil), background_(nil), brush_(nil), font_(nil),
  xor_mode_(false) { }

PainterRep::~PainterRep() {
    Resource::unref(foreground_);
    Resource::unref(background_);
    Resource::unref(brush_);
    Resource::unref(font_);
}

/* ================================================================== */
/* class Painter                                                        */
/* ================================================================== */

Painter::Painter() {
    PainterRep* p = new PainterRep;
    rep_ = p;
}

Painter::Painter(Painter* copy) {
    PainterRep* p = new PainterRep;
    rep_ = p;
    if (copy) {
        Resource::ref(copy->rep_->foreground_);
        Resource::ref(copy->rep_->background_);
        Resource::ref(copy->rep_->brush_);
        Resource::ref(copy->rep_->font_);
        *p = *copy->rep_;
    }
}

Painter::~Painter() {
    delete rep_;
}

void Painter::Copy(Painter* p) {
    if (!p) return;
    PainterRep& r = *rep_;
    Resource::ref(p->rep_->foreground_);  Resource::unref(r.foreground_);
    Resource::ref(p->rep_->background_);  Resource::unref(r.background_);
    Resource::ref(p->rep_->brush_);       Resource::unref(r.brush_);
    Resource::ref(p->rep_->font_);        Resource::unref(r.font_);
    *rep_ = *p->rep_;
}

void Painter::FillBg(boolean b) { rep_->fill_bg_ = b; }
boolean Painter::BgFilled()      { return rep_->fill_bg_; }

void Painter::SetColors(const Color* fg, const Color* bg) {
    Resource::ref(fg); Resource::unref(rep_->foreground_); rep_->foreground_ = const_cast<Color*>(fg);
    Resource::ref(bg); Resource::unref(rep_->background_); rep_->background_ = const_cast<Color*>(bg);
}

const Color* Painter::GetFgColor() { return rep_->foreground_; }
const Color* Painter::GetBgColor() { return rep_->background_; }

void Painter::SetBrush(const Brush* b) {
    Resource::ref(b);
    Resource::unref(rep_->brush_);
    rep_->brush_ = const_cast<Brush*>(b);
}

const Brush* Painter::GetBrush() { return rep_->brush_; }

void Painter::SetFont(const Font* f) {
    Resource::ref(f);
    Resource::unref(rep_->font_);
    rep_->font_ = const_cast<Font*>(f);
}

const Font* Painter::GetFont() { return rep_->font_; }

void Painter::SetPattern(const Pattern*) { /* patterns are unsupported */ }
const Pattern* Painter::GetPattern() { return nullptr; }

void Painter::SetTransformer(Transformer* t) {
    rep_->cur_transformer_ = t;
}

Transformer* Painter::GetTransformer() {
    return rep_->cur_transformer_;
}

void Painter::Transform(float a00, float a01, float a10, float a11,
                         float a20, float a21)
{
    if (!rep_->cur_transformer_) rep_->cur_transformer_ = new Transformer;
    rep_->cur_transformer_->premultiply(
        Transformer(a00, a01, a10, a11, a20, a21));
}

void Painter::Translate(float x, float y) {
    if (!rep_->cur_transformer_) rep_->cur_transformer_ = new Transformer;
    rep_->cur_transformer_->translate(x, y);
}

void Painter::Scale(float x, float y) {
    if (!rep_->cur_transformer_) rep_->cur_transformer_ = new Transformer;
    rep_->cur_transformer_->scale(x, y);
}

void Painter::Rotate(float angle) {
    if (!rep_->cur_transformer_) rep_->cur_transformer_ = new Transformer;
    rep_->cur_transformer_->rotate(angle);
}

/* ================================================================== */
/* Drawing operations                                                  */
/* ================================================================== */

static void setup_canvas(Canvas* c, PainterRep* p) {
    if (p->cur_transformer_) c->transformer(*p->cur_transformer_);
}

void Painter::Line(Canvas* c, Coord x0, Coord y0, Coord x1, Coord y1) {
    if (!c) return;
    setup_canvas(c, rep_);
    c->new_path();
    c->move_to(x0, y0);
    c->line_to(x1, y1);
    c->stroke(rep_->foreground_, rep_->brush_);
}

void Painter::Rect(Canvas* c, Coord x0, Coord y0, Coord x1, Coord y1) {
    if (!c) return;
    setup_canvas(c, rep_);
    c->new_path();
    c->move_to(x0, y0);
    c->line_to(x1, y0);
    c->line_to(x1, y1);
    c->line_to(x0, y1);
    c->line_to(x0, y0);
    c->stroke(rep_->foreground_, rep_->brush_);
}

void Painter::FillRect(Canvas* c, Coord x0, Coord y0, Coord x1, Coord y1) {
    if (!c) return;
    setup_canvas(c, rep_);
    c->fill_rect(x0, y0, x1, y1, rep_->foreground_);
}

void Painter::ClearRect(Canvas* c, Coord x0, Coord y0, Coord x1, Coord y1) {
    if (!c) return;
    setup_canvas(c, rep_);
    c->fill_rect(x0, y0, x1, y1, rep_->background_);
}

void Painter::Circle(Canvas* c, Coord x, Coord y, int r) {
    if (!c) return;
    setup_canvas(c, rep_);
    double rad = (double)r;
    c->new_path();
    CanvasRep* cr = c->rep();
    if (cr->cr_) {
        int px = cr->display_->to_pixels(x);
        int py = cr->pheight_ - cr->display_->to_pixels(y);
        cairo_arc(cr->cr_, px, py, rad, 0, 2 * G_PI);
    }
    c->stroke(rep_->foreground_, rep_->brush_);
}

void Painter::FillCircle(Canvas* c, Coord x, Coord y, int r) {
    if (!c) return;
    setup_canvas(c, rep_);
    double rad = (double)r;
    CanvasRep* cr = c->rep();
    if (cr->cr_) {
        int px = cr->display_->to_pixels(x);
        int py = cr->pheight_ - cr->display_->to_pixels(y);
        cairo_arc(cr->cr_, px, py, rad, 0, 2 * G_PI);
    }
    c->fill(rep_->foreground_);
}

void Painter::Text(Canvas* c, const char* s, int len, Coord x, Coord y) {
    if (!c || !s || !rep_->font_) return;
    setup_canvas(c, rep_);
    for (int i = 0; i < len; i++) {
        Coord w = rep_->font_->width(s[i]);
        c->character(rep_->font_, s[i], w, rep_->foreground_, x, y);
        x += w;
    }
}

void Painter::Text(Canvas* c, const char* s, Coord x, Coord y) {
    if (s) Text(c, s, strlen(s), x, y);
}

void Painter::Stencil(Canvas* c, Coord x, Coord y,
                       const Bitmap* mask, const Bitmap* /*pat*/) {
    if (!c || !mask) return;
    setup_canvas(c, rep_);
    c->stencil(mask, rep_->foreground_, x, y);
}

void Painter::RasterRect(Canvas* c, Coord x, Coord y, const Raster* r) {
    if (!c || !r) return;
    setup_canvas(c, rep_);
    c->image(r, x, y);
}

void Painter::SetMode(int mode) {
    rep_->xor_mode_ = (mode != 0);
}

int Painter::GetMode() { return rep_->xor_mode_ ? 1 : 0; }
