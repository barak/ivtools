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
 * GTK4 backend: Color implementation.
 * Replaces IV-X11/xcolor.cc.
 *
 * Colours are stored as GdkRGBA (0.0–1.0 float channels).  There is no
 * colormap; alpha transparency is handled via Cairo compositing operators
 * rather than X11 stipple pixmaps.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <InterViews/color.h>
#include <InterViews/display.h>
#include <InterViews/session.h>
#include <InterViews/window.h>
#include <IV-GTK4/gdkcolor.h>
#include <IV-GTK4/gdkdisplay.h>
#include <IV-GTK4/gtkwindow.h>
#include <OS/list.h>
#include <OS/math.h>
#include <OS/string.h>
#include <OS/ustring.h>
#include <OS/table2.h>

declarePtrList(ColorRepList, ColorRep)
implementPtrList(ColorRepList, ColorRep)

inline unsigned long key_to_hash(String& s) { return s.hash(); }

declareTable2(NameToColor, Display*, UniqueString, const Color*)
implementTable2(NameToColor, Display*, UniqueString, const Color*)

class ColorImpl {
    friend class Color;

    ColorIntensity red;
    ColorIntensity green;
    ColorIntensity blue;
    float alpha;
    ColorOp op;
    ColorRepList* replist;
    UniqueString ctable_name;
    Display* ctable_display;

    static NameToColor* ctable_;
};

NameToColor* ColorImpl::ctable_;

Color::Color(ColorIntensity r, ColorIntensity g, ColorIntensity b,
             float alpha, ColorOp op)
{
    ColorImpl* c = new ColorImpl;
    impl_ = c;
    c->red   = r;
    c->green = g;
    c->blue  = b;
    c->alpha = alpha;
    c->op    = op;
    c->ctable_display = nil;
    c->replist = new ColorRepList;
}

Color::Color(const Color& color, float alpha, ColorOp op) {
    ColorImpl* c = new ColorImpl;
    impl_ = c;
    c->red   = color.impl_->red;
    c->green = color.impl_->green;
    c->blue  = color.impl_->blue;
    c->alpha = alpha;
    c->op    = op;
    c->ctable_display = nil;
    c->replist = new ColorRepList;
}

Color::~Color() {
    ColorImpl* c = impl_;
    if (c->ctable_display) c->ctable_->remove(c->ctable_display, c->ctable_name);
    for (ListItr(ColorRepList) i(*c->replist); i.more(); i.next()) {
        destroy(i.cur());
    }
    delete c->replist;
    delete c;
}

const Color* Color::lookup(Display* d, const String& s) {
    NameToColor* t = ColorImpl::ctable_;
    if (!t) { t = new NameToColor(128); ColorImpl::ctable_ = t; }
    UniqueString u(s);
    const Color* c;
    if (t->find(c, d, u)) return c;
    ColorIntensity r, g, b;
    if (find(d, u, r, g, b)) {
        c = new Color(r, g, b);
        t->insert(d, u, c);
        c->impl_->ctable_display = d;
        c->impl_->ctable_name    = u;
        return c;
    }
    return nil;
}

const Color* Color::lookup(Display* d, const char* name) {
    return lookup(d, String(name));
}

boolean Color::distinguished(const Color* c) const {
    return distinguished(Session::instance()->default_display(), c);
}

int Color::PixelValue() const {
    ColorIntensity r, g, b;
    intensities(r, g, b);
    unsigned long red = (unsigned long)(Math::min(r, 1.0f) * 255.0f);
    unsigned long green = (unsigned long)(Math::min(g, 1.0f) * 255.0f);
    unsigned long blue = (unsigned long)(Math::min(b, 1.0f) * 255.0f);
    return (int)((red << 16) | (green << 8) | blue);
}

void Color::intensities(ColorIntensity& r, ColorIntensity& g, ColorIntensity& b) const {
    intensities(Session::instance()->default_display(), r, g, b);
}

float Color::alpha() const { return impl_->alpha; }
ColorOp Color::op()  const { return impl_->op; }

ColorRep* Color::rep(WindowVisual* wv) const {
    for (ListItr(ColorRepList) i(*impl_->replist); i.more(); i.next()) {
        ColorRep* c = i.cur();
        if (c->visual_ == wv) return c;
    }
    ColorImpl* c = impl_;
    ColorRep* r = create(wv, c->red, c->green, c->blue, c->alpha, c->op);
    impl_->replist->append(r);
    return r;
}

void Color::remove(WindowVisual* wv) const {
    for (ListUpdater(ColorRepList) i(*impl_->replist); i.more(); i.next()) {
        ColorRep* c = i.cur();
        if (c->visual_ == wv) { i.remove_cur(); break; }
    }
}

/*
 * create() – allocate a ColorRep for a given visual.
 *
 * In GTK4 there is no colormap allocation.  We simply store the RGBA
 * values and choose the cairo_operator_t based on ColorOp.
 */
ColorRep* Color::create(
    WindowVisual* wv,
    ColorIntensity r, ColorIntensity g, ColorIntensity b,
    float alpha, ColorOp op) const
{
    ColorRep* cr = new ColorRep;
    cr->visual_ = wv;
    cr->op_     = op;
    cr->masking_ = false;

    /* Store as GdkRGBA */
    cr->rgba_.red   = (double)r;
    cr->rgba_.green = (double)g;
    cr->rgba_.blue  = (double)b;
    cr->rgba_.alpha = (double)alpha;

    /* Populate the legacy XColor struct for compatibility */
    cr->xcolor_.red   = (unsigned short)Math::round(r * float(0xffff));
    cr->xcolor_.green = (unsigned short)Math::round(g * float(0xffff));
    cr->xcolor_.blue  = (unsigned short)Math::round(b * float(0xffff));
    cr->xcolor_.pixel = 0; /* unused */

    /* Cairo operator */
    switch (op) {
    case Copy:
        cr->cairo_op_ = CAIRO_OPERATOR_OVER;
        break;
    case Xor:
        cr->cairo_op_ = CAIRO_OPERATOR_XOR;
        break;
    case Invisible:
        cr->cairo_op_ = CAIRO_OPERATOR_DEST;
        break;
    default:
        cr->cairo_op_ = CAIRO_OPERATOR_OVER;
        break;
    }

    /*
     * Stipple pattern for semi-transparent colours.
     * In X11 a 4×4 1-bit pixmap was used to dither alpha.
     * In GTK4 we use cairo_paint_with_alpha() instead, so no stipple needed.
     * We set stipple_ to nullptr to signal "use alpha compositing".
     */
    cr->stipple_ = nullptr;

    return cr;
}

/*
 * find() – parse a colour name into RGB intensities.
 * GDK provides gdk_rgba_parse() which accepts CSS colour syntax.
 */
boolean Color::find(const Display* display, const String& name,
                    ColorIntensity& r, ColorIntensity& g, ColorIntensity& b)
{
    NullTerminatedString ns(name);
    GdkRGBA rgba;
    if (gdk_rgba_parse(&rgba, ns.string())) {
        r = (ColorIntensity)rgba.red;
        g = (ColorIntensity)rgba.green;
        b = (ColorIntensity)rgba.blue;
        return true;
    }
    (void)display;
    return false;
}

boolean Color::find(const Display* display, const char* name,
                    ColorIntensity& r, ColorIntensity& g, ColorIntensity& b)
{
    return find(display, String(name), r, g, b);
}

void Color::destroy(ColorRep* r) {
    if (r->stipple_) {
        cairo_pattern_destroy(r->stipple_);
        r->stipple_ = nullptr;
    }
    delete r;
}

boolean Color::distinguished(Display* d, const Color* color) const {
    WindowVisual* wv = d->rep()->default_visual_;
    ColorRep* r1 = rep(wv);
    ColorRep* r2 = color->rep(wv);
    return (r1->rgba_.red   != r2->rgba_.red   ||
            r1->rgba_.green != r2->rgba_.green  ||
            r1->rgba_.blue  != r2->rgba_.blue);
}

void Color::intensities(Display* d,
                        ColorIntensity& r, ColorIntensity& g, ColorIntensity& b) const
{
    WindowVisual* wv = d->rep()->default_visual_;
    ColorRep* cr = rep(wv);
    r = (ColorIntensity)cr->rgba_.red;
    g = (ColorIntensity)cr->rgba_.green;
    b = (ColorIntensity)cr->rgba_.blue;
}

const Color* Color::brightness(float adjust) const {
    ColorIntensity r, g, b;
    intensities(r, g, b);
    if (adjust >= 0) {
        r += (1 - r) * adjust;
        g += (1 - g) * adjust;
        b += (1 - b) * adjust;
    } else {
        float f = adjust + 1.0f;
        r *= f;
        g *= f;
        b *= f;
    }
    return new Color(r, g, b);
}
