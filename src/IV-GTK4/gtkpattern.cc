/*
 * GTK4 backend: Pattern implementation.
 * Replaces IV-X11/xpattern.cc.
 *
 * In X11 a Pattern is a 4x4 stipple Pixmap.  In GTK4 we store the
 * pattern as a cairo_pattern_t that is used when painting with
 * cairo_mask().  The 16-bit integer encoding is the same as X11.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <InterViews/pattern.h>
#include <InterViews/display.h>
#include <InterViews/session.h>
#include <IV-GTK4/gdklib.h>
#include <IV-GTK4/gdkdisplay.h>
#include <IV-GTK4/gtkwindow.h>
#include <IV-GTK4/gtkpattern.h>
#include <OS/list.h>

declarePtrList(PatternRepList, PatternRep)
implementPtrList(PatternRepList, PatternRep)

class PatternImpl {
    friend class Pattern;

    int         data_[patternHeight];
    PatternRepList* replist_;
};

Pattern::Pattern(int p0,  int p1,  int p2,  int p3,
                 int p4,  int p5,  int p6,  int p7,
                 int p8,  int p9,  int p10, int p11,
                 int p12, int p13, int p14, int p15)
{
    PatternImpl* pi = new PatternImpl;
    impl_ = pi;
    pi->data_[0]  = p0;  pi->data_[1]  = p1;
    pi->data_[2]  = p2;  pi->data_[3]  = p3;
    pi->data_[4]  = p4;  pi->data_[5]  = p5;
    pi->data_[6]  = p6;  pi->data_[7]  = p7;
    pi->data_[8]  = p8;  pi->data_[9]  = p9;
    pi->data_[10] = p10; pi->data_[11] = p11;
    pi->data_[12] = p12; pi->data_[13] = p13;
    pi->data_[14] = p14; pi->data_[15] = p15;
    pi->replist_ = new PatternRepList;
}

Pattern::Pattern(int data[patternHeight]) {
    PatternImpl* pi = new PatternImpl;
    impl_ = pi;
    for (int i = 0; i < patternHeight; i++) pi->data_[i] = data[i];
    pi->replist_ = new PatternRepList;
}

Pattern::Pattern(float graylevel) {
    PatternImpl* pi = new PatternImpl;
    impl_ = pi;
    /* Build a simple dither pattern from the grey level */
    int level = (int)(graylevel * 16 + 0.5f);
    if (level <= 0)  level = 0;
    if (level >= 16) level = 15;

    static const int stipple[16][4] = {
        {0x0000,0x0000,0x0000,0x0000},
        {0x0100,0x0000,0x0000,0x0000},
        {0x0100,0x0000,0x0400,0x0000},
        {0x0500,0x0000,0x0400,0x0000},
        {0x0500,0x0000,0x0500,0x0000},
        {0x0500,0x0200,0x0500,0x0000},
        {0x0500,0x0200,0x0500,0x0800},
        {0x0500,0x0a00,0x0500,0x0800},
        {0x0500,0x0a00,0x0500,0x0a00},
        {0x0700,0x0a00,0x0500,0x0a00},
        {0x0700,0x0a00,0x0d00,0x0a00},
        {0x0f00,0x0a00,0x0d00,0x0a00},
        {0x0f00,0x0a00,0x0f00,0x0a00},
        {0x0f00,0x0b00,0x0f00,0x0a00},
        {0x0f00,0x0b00,0x0f00,0x0e00},
        {0x0f00,0x0f00,0x0f00,0x0e00}
    };
    for (int i = 0; i < 16; i++) pi->data_[i] = stipple[level][i % 4];
    pi->replist_ = new PatternRepList;
}

Pattern::~Pattern() {
    for (ListItr(PatternRepList) i(*impl_->replist_); i.more(); i.next()) {
        PatternRep* r = i.cur();
        if (r->pixmap_) { cairo_surface_destroy(r->pixmap_); r->pixmap_ = nullptr; }
        delete r;
    }
    delete impl_->replist_;
    delete impl_;
}

PatternRep* Pattern::rep(Display* d) const {
    for (ListItr(PatternRepList) i(*impl_->replist_); i.more(); i.next()) {
        PatternRep* r = i.cur();
        if (r->display_ == d) return r;
    }

    /* Build a 16×16 A1 cairo surface from the 16-int pattern data */
    const int W = 16, H = 16;
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_A1, W, H);
    cairo_surface_flush(surf);
    unsigned char* data   = cairo_image_surface_get_data(surf);
    int            stride = cairo_image_surface_get_stride(surf);

    for (int row = 0; row < H; row++) {
        int bits = impl_->data_[row];
        for (int col = 0; col < W; col++) {
            int bit = (bits >> (15 - col)) & 1;
            int byte_idx = row * stride + col / 8;
            int bit_idx  = col % 8;
            if (bit) data[byte_idx] |=  (1 << bit_idx);
            else     data[byte_idx] &= ~(1 << bit_idx);
        }
    }
    cairo_surface_mark_dirty(surf);

    PatternRep* r = new PatternRep;
    r->display_ = d;
    r->pixmap_  = surf;
    impl_->replist_->append(r);
    return r;
}

int Pattern::info(int row) const {
    if (row < 0 || row >= patternHeight) return 0;
    return impl_->data_[row];
}
