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
 * GTK4 backend: Font implementation using Pango.
 * Replaces IV-X11/xfont.cc.
 *
 * Font names are interpreted as Pango font description strings
 * (e.g. "Sans 12", "Monospace Bold 10") rather than X XLFD strings.
 * When an XLFD-like name is passed we attempt a best-effort conversion
 * by extracting the family and size fields.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <InterViews/display.h>
#include <InterViews/font.h>
#include <InterViews/session.h>
#include <IV-GTK4/gdklib.h>
#include <IV-GTK4/gdkdisplay.h>
#include <IV-GTK4/gdkfont.h>
#include <OS/list.h>
#include <OS/math.h>
#include <OS/string.h>
#include <OS/ustring.h>
#include <OS/table.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

/*
 * Attempt to strip XLFD-style font names to something Pango can parse.
 * An XLFD looks like "-foundry-family-weight-slant-width-style-pixels-
 * decipoints-resx-resy-spacing-avgwidth-registry-encoding".
 * We extract fields 2 (family) and 8 (decipoints) and build a simple
 * Pango description like "Family pointsize".
 */
static PangoFontDescription* xlfd_to_pango(const char* name, float scale)
{
    PangoFontDescription* desc = nullptr;

    /* First try it as a Pango description directly */
    if (name && name[0] != '-') {
        desc = pango_font_description_from_string(name);
        if (desc) {
            if (scale != 1.0f) {
                double sz = pango_font_description_get_size(desc);
                if (!pango_font_description_get_size_is_absolute(desc))
                    sz *= PANGO_SCALE;
                pango_font_description_set_size(desc, (gint)(sz * scale / PANGO_SCALE));
            }
            return desc;
        }
    }

    /* Try XLFD parsing */
    if (name && name[0] == '-') {
        /* split on '-' */
        char buf[512];
        strncpy(buf, name, sizeof(buf)-1); buf[sizeof(buf)-1] = '\0';
        char* fields[15];
        int nf = 0;
        char* p = buf + 1; /* skip leading '-' */
        fields[nf++] = p;
        while (*p && nf < 15) {
            if (*p == '-') { *p = '\0'; fields[nf++] = p + 1; }
            p++;
        }
        /* field[0]=foundry, [1]=family, [2]=weight, [3]=slant,
           [4]=setwidth, [5]=addstyle, [6]=pixels, [7]=decipoints, … */
        const char* family = (nf > 1 && fields[1][0]) ? fields[1] : "Sans";
        int decipoints = (nf > 7 && fields[7][0] && fields[7][0]!='*')
                         ? atoi(fields[7]) : 120; /* default 12pt */
        double pt = decipoints / 10.0 * scale;
        if (pt <= 0) pt = 12.0;

        char pango_str[256];
        const char* weight_str = "";
        if (nf > 2) {
            const char* w = fields[2];
            if (!strcasecmp(w,"bold"))   weight_str = " Bold";
            else if (!strcasecmp(w,"light"))  weight_str = " Light";
        }
        const char* style_str = "";
        if (nf > 3) {
            const char* s = fields[3];
            if (*s == 'i' || *s == 'I') style_str = " Italic";
            else if (*s == 'o' || *s == 'O') style_str = " Oblique";
        }
        snprintf(pango_str, sizeof(pango_str), "%s%s%s %g",
                 family, weight_str, style_str, pt);
        desc = pango_font_description_from_string(pango_str);
        return desc;
    }

    /* Fallback: use Sans 12 */
    return pango_font_description_from_string("Sans 12");
}

/* Create a temporary Pango context for metric queries */
static PangoContext* make_pango_context(Display* d)
{
    GdkDisplay* gdpy = d ? d->rep()->display_ : gdk_display_get_default();
    PangoFontMap* fm = pango_cairo_font_map_get_default();
    PangoContext* ctx = pango_font_map_create_context(fm);
    (void)gdpy;
    return ctx;
}

/* ------------------------------------------------------------------ */
/* KnownFonts / FontImpl internals                                     */
/* ------------------------------------------------------------------ */

declarePtrList(FontList, Font)
implementPtrList(FontList, Font)

declarePtrList(FontRepList, FontRep)
implementPtrList(FontRepList, FontRep)

inline unsigned long key_to_hash(UniqueString& s) { return s.hash(); }

class KnownFonts {
public:
    FontList    fonts;
    FontRepList fontreps;
    void delta() { }
};

declareTable(NameToKnownFonts, UniqueString, KnownFonts*)
implementTable(NameToKnownFonts, UniqueString, KnownFonts*)

class FontImpl {
    friend class Font;
    friend class FontRep;

    FontImpl(const String&, float);
    ~FontImpl();

    void remove(const Font*);
    static NameToKnownFonts* fonts();
    FontRep* rep(Display*);
    FontRep* default_rep();
    void new_rep(KnownFonts*, FontRep*);
    void attach(FontRep*);

    static const Font* lookup(Display*, const String&, float);
    static FontRep* find_rep(FontRepList&, Display*, float);
    static FontRep* create(Display*, const String&, float);
    static const Font* new_font(const String&, float, KnownFonts*, FontRep*);
    static KnownFonts* known(KnownFonts*, const UniqueString&);

    UniqueString*  name_;
    float          scale_;
    FontRepList*   replist_;
    KnownFonts*    entry_;

    static NameToKnownFonts* fonts_;
};

NameToKnownFonts* FontImpl::fonts_;

FontImpl::FontImpl(const String& s, float scale)
: name_(new UniqueString(s)), scale_(scale), entry_(nil),
  replist_(new FontRepList) { }

FontImpl::~FontImpl() {
    for (ListItr(FontRepList) i(*replist_); i.more(); i.next())
        Resource::unref(i.cur());
    delete replist_;
    delete name_;
}

void FontImpl::remove(const Font* f) {
    if (entry_) {
        for (ListUpdater(FontList) i(entry_->fonts); i.more(); i.next()) {
            if (i.cur() == f) { i.remove_cur(); break; }
        }
        if (entry_->fonts.count() == 0 && entry_->fontreps.count() == 0) {
            fonts_->remove(*name_);
            delete entry_;
        }
    }
    entry_ = nil;
}

NameToKnownFonts* FontImpl::fonts() {
    if (!fonts_) fonts_ = new NameToKnownFonts(256);
    return fonts_;
}

FontRep* FontImpl::rep(Display* d) {
    FontRep* r;
    for (ListItr(FontRepList) i(*replist_); i.more(); i.next()) {
        r = i.cur();
        if (r->display_ == d) return r;
    }
    KnownFonts* k = nil;
    if (fonts()->find(k, *name_)) {
        r = find_rep(k->fontreps, d, scale_);
        if (r) { attach(r); return r; }
    }
    r = create(d, *name_, scale_);
    if (r) new_rep(known(k, *name_), r);
    return r;
}

FontRep* FontImpl::default_rep() {
    long n = replist_->count();
    if (n) return replist_->item(n - 1);
    return rep(Session::instance()->default_display());
}

void FontImpl::new_rep(KnownFonts* k, FontRep* r) {
    r->entry_ = k;
    k->fontreps.append(r);
    attach(r);
}

void FontImpl::attach(FontRep* r) {
    replist_->append(r);
    Resource::ref(r);
}

const Font* FontImpl::lookup(Display* d, const String& name, float scale) {
    const Font* f;
    FontRep* r;
    KnownFonts* k = nil;
    UniqueString uname(name);
    if (fonts()->find(k, uname)) {
        for (ListItr(FontList) i(k->fonts); i.more(); i.next()) {
            f = i.cur();
            if (Math::equal(f->impl_->scale_, scale, float(0.0001))) return f;
        }
        r = find_rep(k->fontreps, d, scale);
        if (r) return new_font(uname, scale, k, r);
    }
    r = create(d, uname, scale);
    if (!r) return nil;
    k = known(k, uname);
    f = new_font(uname, scale, k, r);
    f->impl_->new_rep(k, r);
    f->impl_->entry_ = k;
    return f;
}

FontRep* FontImpl::find_rep(FontRepList& list, Display* d, float s) {
    for (ListItr(FontRepList) i(list); i.more(); i.next()) {
        FontRep* r = i.cur();
        if (r->display_ == d && Math::equal(r->scale_, s, float(0.0001)))
            return r;
    }
    return nil;
}

FontRep* FontImpl::create(Display* d, const String& name, float scale) {
    NullTerminatedString ns(name);
    PangoFontDescription* desc = xlfd_to_pango(ns.string(), scale);
    if (!desc) return nil;

    FontRep* f = new FontRep(d, desc, scale);
    f->name_ = new CopyString(ns.string());
    f->encoding_ = nil;

    /* Approximate point size from the description */
    int sz_pango = pango_font_description_get_size(desc);
    if (pango_font_description_get_size_is_absolute(desc))
        f->size_ = (float)(sz_pango / PANGO_SCALE) * scale;
    else
        f->size_ = (float)(sz_pango / PANGO_SCALE) * scale;

    return f;
}

const Font* FontImpl::new_font(const String& name, float scale,
                               KnownFonts* k, FontRep* r)
{
    Font* f = new Font(name, scale);
    f->impl_->attach(r);
    f->impl_->entry_ = k;
    k->fonts.append(f);
    return f;
}

KnownFonts* FontImpl::known(KnownFonts* old_k, const UniqueString& name) {
    if (old_k) return old_k;
    KnownFonts* k = new KnownFonts;
    fonts_->insert(name, k);
    return k;
}

/* ================================================================== */
/* class FontRep                                                        */
/* ================================================================== */

FontRep::FontRep(Display* d, PangoFontDescription* desc, float scale)
: display_(d), desc_(desc), scale_(scale),
  unscaled_(scale > 0.9999f && scale < 1.0001f),
  name_(nil), encoding_(nil), size_(0.0f), entry_(nil)
{
    context_ = make_pango_context(d);
    layout_  = pango_layout_new(context_);
    pango_layout_set_font_description(layout_, desc);

    /* Cache metrics */
    PangoFontMetrics* metrics = pango_context_get_metrics(
        context_, desc, pango_language_get_default());
    if (metrics) {
        ascent_  = pango_font_metrics_get_ascent(metrics)  / (double)PANGO_SCALE;
        descent_ = pango_font_metrics_get_descent(metrics) / (double)PANGO_SCALE;
        max_advance_width_ = pango_font_metrics_get_approximate_char_width(metrics)
                             / (double)PANGO_SCALE;
        pango_font_metrics_unref(metrics);
    } else {
        ascent_ = 12.0; descent_ = 3.0; max_advance_width_ = 8.0;
    }
}

FontRep::~FontRep() {
    if (layout_)  { g_object_unref(layout_);  layout_  = nullptr; }
    if (context_) { g_object_unref(context_); context_ = nullptr; }
    if (desc_)    { pango_font_description_free(desc_); desc_ = nullptr; }
    if (entry_) {
        for (ListUpdater(FontRepList) i(entry_->fontreps); i.more(); i.next()) {
            if (i.cur() == this) { i.remove_cur(); break; }
        }
    }
    delete name_;
    delete encoding_;
}

/* ================================================================== */
/* class Font                                                           */
/* ================================================================== */

Font::Font(const String& name, float scale) {
    impl_ = new FontImpl(name, scale);
}

Font::Font(const char* name, float scale) {
    impl_ = new FontImpl(String(name), scale);
}

Font::Font(FontImpl* i) { impl_ = i; }

Font::~Font() { delete impl_; }

void Font::cleanup() { impl_->remove(this); }

const Font* Font::lookup(const String& name) {
    return FontImpl::lookup(Session::instance()->default_display(), name, 1.0f);
}

const Font* Font::lookup(const char* name) {
    return FontImpl::lookup(Session::instance()->default_display(),
                            String(name), 1.0f);
}

boolean Font::exists(Display* d, const String& name) {
    return FontImpl::lookup(d, name, 1.0f) != nil;
}

boolean Font::exists(Display* d, const char* name) {
    return FontImpl::lookup(d, String(name), 1.0f) != nil;
}

FontRep* Font::rep(Display* d) const { return impl_->rep(d); }

const char* Font::name() const {
    FontRep* f = impl_->default_rep();
    return f->name_ ? f->name_->string() : "";
}

const char* Font::encoding() const {
    FontRep* f = impl_->default_rep();
    return f->encoding_ ? f->encoding_->string() : nil;
}

Coord Font::size() const { return impl_->default_rep()->size_; }

void Font::font_bbox(FontBoundingBox& b) const {
    FontRep* f = impl_->default_rep();
    float scale = f->scale_;
    Display* d  = f->display_;

    double asc  = f->ascent_;
    double des  = f->descent_;
    double adv  = f->max_advance_width_;

    /* Convert from Pango pixel units to IV coords */
    double ppd = d ? d->to_coord(1) : 1.0;
    b.ascent_       = (Coord)(scale * asc  * ppd);
    b.descent_      = (Coord)(scale * des  * ppd);
    b.font_ascent_  = b.ascent_;
    b.font_descent_ = b.descent_;
    b.left_bearing_ = 0;
    b.right_bearing_= (Coord)(scale * adv  * ppd);
    b.width_        = b.right_bearing_;
}

void Font::char_bbox(long c, FontBoundingBox& b) const {
    if (c < 0) {
        b.left_bearing_ = b.right_bearing_ = b.width_ = 0;
        b.ascent_ = b.descent_ = b.font_ascent_ = b.font_descent_ = 0;
        return;
    }
    FontRep* f = impl_->default_rep();
    float scale = f->scale_;
    Display* d  = f->display_;

    char utf8[8]; int n = 0;
    if (c < 0x80) { utf8[n++] = (char)c; }
    else if (c < 0x800) {
        utf8[n++] = char(0xC0 | (c >> 6));
        utf8[n++] = char(0x80 | (c & 0x3F));
    } else {
        utf8[n++] = char(0xE0 | (c >> 12));
        utf8[n++] = char(0x80 | ((c >> 6) & 0x3F));
        utf8[n++] = char(0x80 | (c & 0x3F));
    }
    utf8[n] = '\0';

    pango_layout_set_text(f->layout_, utf8, n);
    PangoRectangle ink, logical;
    pango_layout_get_extents(f->layout_, &ink, &logical);

    double ppd = d ? d->to_coord(1) : 1.0;
    double ps  = (double)PANGO_SCALE;
    b.left_bearing_  = (Coord)(scale * (-ink.x / ps) * ppd);
    b.right_bearing_ = (Coord)(scale * ((ink.x + ink.width) / ps) * ppd);
    b.width_         = width(c);
    b.ascent_        = (Coord)(scale * (-ink.y / ps) * ppd);
    b.descent_       = (Coord)(scale * ((ink.y + ink.height) / ps) * ppd);
    b.font_ascent_   = (Coord)(scale * f->ascent_ * ppd);
    b.font_descent_  = (Coord)(scale * f->descent_ * ppd);
}

void Font::string_bbox(const char* s, int len, FontBoundingBox& b) const {
    FontRep* f = impl_->default_rep();
    float scale = f->scale_;
    Display* d  = f->display_;

    pango_layout_set_text(f->layout_, s, len);
    PangoRectangle ink, logical;
    pango_layout_get_extents(f->layout_, &ink, &logical);

    double ppd = d ? d->to_coord(1) : 1.0;
    double ps  = (double)PANGO_SCALE;
    b.left_bearing_  = (Coord)(scale * (-ink.x / ps) * ppd);
    b.right_bearing_ = (Coord)(scale * ((ink.x + ink.width) / ps) * ppd);
    b.width_         = width(s, len);
    b.ascent_        = (Coord)(scale * (-ink.y / ps) * ppd);
    b.descent_       = (Coord)(scale * ((ink.y + ink.height) / ps) * ppd);
    b.font_ascent_   = (Coord)(scale * f->ascent_ * ppd);
    b.font_descent_  = (Coord)(scale * f->descent_ * ppd);
}

Coord Font::width(long c) const {
    if (c < 0) return 0;
    char utf8[8]; int n = 0;
    if (c < 0x80) { utf8[n++] = (char)c; }
    else if (c < 0x800) {
        utf8[n++] = char(0xC0 | (c >> 6));
        utf8[n++] = char(0x80 | (c & 0x3F));
    } else {
        utf8[n++] = char(0xE0 | (c >> 12));
        utf8[n++] = char(0x80 | ((c >> 6) & 0x3F));
        utf8[n++] = char(0x80 | (c & 0x3F));
    }
    utf8[n] = '\0';
    return width(utf8, n);
}

Coord Font::width(const char* s, int len) const {
    FontRep* f = impl_->default_rep();
    Display* d = f->display_;
    pango_layout_set_text(f->layout_, s, len);
    PangoRectangle logical;
    pango_layout_get_extents(f->layout_, nullptr, &logical);
    double ppd = d ? d->to_coord(1) : 1.0;
    return (Coord)(f->scale_ * (logical.width / (double)PANGO_SCALE) * ppd);
}

/* IV-2_6 Font compatibility methods */
int Font::Height() const {
    FontRep* f = rep(nullptr);
    if (!f || !f->layout_) return 0;
    PangoFontMetrics* m = pango_context_get_metrics(
        pango_layout_get_context(f->layout_), nullptr, nullptr);
    int h = PANGO_PIXELS(pango_font_metrics_get_ascent(m) +
                         pango_font_metrics_get_descent(m));
    pango_font_metrics_unref(m);
    return h;
}

int Font::Width(const char* s) const {
    if (!s) return 0;
    return (int)width(s, strlen(s));
}

int Font::Width(const char* s, int len) const {
    if (!s || len <= 0) return 0;
    return (int)width(s, len);
}

int Font::Index(const char* s, int offset, boolean between) const {
    if (!s) return 0;
    return index(s, strlen(s), float(offset), between);
}

int Font::Index(const char* s, int len, int offset, boolean between) const {
    if (!s || len <= 0) return 0;
    return index(s, len, float(offset), between);
}

int Font::index(const char* s, int len, float offset, boolean /*between*/) const {
    if (!s || len <= 0) return 0;
    float sofar = 0.0f;
    for (int i = 0; i < len; i++) {
        float w = (float)width(s[i]);
        if (sofar + w > offset) return i;
        sofar += w;
    }
    return len;
}
