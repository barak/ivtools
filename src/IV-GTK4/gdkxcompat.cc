#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <IV-GTK4/gdklib.h>
#include <IV-GTK4/gdkdefs.h>
#include <IV-GTK4/gdkutil.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static PangoFontDescription* gtk4_compat_font_desc(const char* name) {
    if (name == nullptr || *name == '\0') {
        return pango_font_description_from_string("Sans 12");
    }
    return pango_font_description_from_string(name);
}

int XSync(XDisplay* display, int) {
    if (display != nullptr) {
        gdk_display_flush(display);
    }
    return 0;
}

int XFree(void* ptr) {
    g_free(ptr);
    return 0;
}

char* XGetAtomName(XDisplay*, Atom atom) {
    if (atom == nullptr) {
        return nullptr;
    }
    return g_strdup(atom);
}

XFontStruct* XLoadQueryFont(XDisplay*, const char* name) {
    PangoFontDescription* desc = gtk4_compat_font_desc(name);
    if (desc == nullptr) {
        return nullptr;
    }

    PangoFontMap* font_map = pango_cairo_font_map_get_default();
    PangoContext* context = pango_font_map_create_context(font_map);
    PangoFont* font = pango_font_map_load_font(font_map, context, desc);
    if (font == nullptr) {
        g_object_unref(context);
        pango_font_description_free(desc);
        return nullptr;
    }

    PangoFontMetrics* metrics = pango_font_get_metrics(font, nullptr);

    XFontStruct* xf = g_new0(XFontStruct, 1);
    xf->description = desc;
    xf->context = context;
    xf->ascent = pango_font_metrics_get_ascent(metrics) / (double)PANGO_SCALE;
    xf->descent = pango_font_metrics_get_descent(metrics) / (double)PANGO_SCALE;
    xf->max_width = pango_font_metrics_get_approximate_char_width(metrics) / (double)PANGO_SCALE;
    xf->family_name = g_strdup(pango_font_description_get_family(desc));
    xf->full_name = pango_font_description_to_string(desc);

    int size = pango_font_description_get_size(desc);
    if (size <= 0) {
        size = 12 * PANGO_SCALE;
    }
    xf->point_size = (unsigned long)((size * 10) / PANGO_SCALE);

    pango_font_metrics_unref(metrics);
    g_object_unref(font);
    return xf;
}

int XGetFontProperty(XFontStruct* font, Atom atom, unsigned long* value) {
    if (font == nullptr || value == nullptr || atom == nullptr) {
        return 0;
    }
    if (strcmp(atom, XA_FULL_NAME) == 0) {
        *value = (unsigned long)(uintptr_t)(font->full_name ? font->full_name : "");
        return 1;
    }
    if (strcmp(atom, XA_FONT_NAME) == 0 || strcmp(atom, XA_FAMILY_NAME) == 0) {
        *value = (unsigned long)(uintptr_t)(font->family_name ? font->family_name : "");
        return 1;
    }
    if (strcmp(atom, XA_POINT_SIZE) == 0) {
        *value = font->point_size;
        return 1;
    }
    return 0;
}

int XDestroyImage(XImage* image) {
    delete image;
    return 0;
}

static unsigned long gtk4_compat_pack_pixel(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    return ((unsigned long)a << 24)
         | ((unsigned long)r << 16)
         | ((unsigned long)g << 8)
         | (unsigned long)b;
}

static unsigned char gtk4_compat_a1_mask(int x) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
    return (unsigned char)(1u << (x % 8));
#else
    return (unsigned char)(1u << (7 - (x % 8)));
#endif
}

static cairo_surface_t* gtk4_compat_surface(XDrawable drawable) {
    return static_cast<cairo_surface_t*>(drawable);
}

static void gtk4_compat_set_source_pixel(cairo_t* cr, unsigned long pixel) {
    double a = ((pixel >> 24) & 0xff) / 255.0;
    if (a == 0.0) {
        a = 1.0;
    }
    double r = ((pixel >> 16) & 0xff) / 255.0;
    double g = ((pixel >> 8) & 0xff) / 255.0;
    double b = (pixel & 0xff) / 255.0;
    cairo_set_source_rgba(cr, r, g, b, a);
}

unsigned long XGetPixel(XImage* image, int x, int y) {
    if (image == nullptr || image->surface_ == nullptr) {
        return 0;
    }
    cairo_surface_flush(image->surface_);
    if (x < 0 || y < 0 || x >= image->width || y >= image->height) {
        return 0;
    }

    unsigned char* data = cairo_image_surface_get_data(image->surface_);
    int stride = cairo_image_surface_get_stride(image->surface_);
    cairo_format_t format = cairo_image_surface_get_format(image->surface_);
    if (format == CAIRO_FORMAT_A1) {
        const unsigned char* row = data + y * stride;
        return (row[x / 8] & gtk4_compat_a1_mask(x)) ? 1UL : 0UL;
    }
    unsigned char* pix = data + y * stride + x * 4;
    return gtk4_compat_pack_pixel(pix[2], pix[1], pix[0], pix[3]);
}

int XPutPixel(XImage* image, int x, int y, unsigned long pixel) {
    if (image == nullptr || image->surface_ == nullptr) {
        return 0;
    }
    cairo_surface_flush(image->surface_);
    if (x < 0 || y < 0 || x >= image->width || y >= image->height) {
        return 0;
    }

    unsigned char* data = cairo_image_surface_get_data(image->surface_);
    int stride = cairo_image_surface_get_stride(image->surface_);
    cairo_format_t format = cairo_image_surface_get_format(image->surface_);
    if (format == CAIRO_FORMAT_A1) {
        unsigned char* row = data + y * stride;
        unsigned char mask = gtk4_compat_a1_mask(x);
        if (pixel != 0) {
            row[x / 8] |= mask;
        } else {
            row[x / 8] &= (unsigned char)~mask;
        }
        cairo_surface_mark_dirty(image->surface_);
        return 0;
    }
    unsigned char* pix = data + y * stride + x * 4;
    pix[0] = (unsigned char)(pixel & 0xff);
    pix[1] = (unsigned char)((pixel >> 8) & 0xff);
    pix[2] = (unsigned char)((pixel >> 16) & 0xff);
    pix[3] = (unsigned char)(((pixel >> 24) & 0xff) ? ((pixel >> 24) & 0xff) : 0xff);
    cairo_surface_mark_dirty(image->surface_);
    return 0;
}

int XCopyArea(XDisplay*, XDrawable src, XDrawable dst, GC, int src_x, int src_y,
              unsigned int width, unsigned int height, int dst_x, int dst_y) {
    cairo_surface_t* src_surface = gtk4_compat_surface(src);
    cairo_surface_t* dst_surface = gtk4_compat_surface(dst);
    if (src_surface == nullptr || dst_surface == nullptr) {
        return 0;
    }
    cairo_t* cr = cairo_create(dst_surface);
    cairo_rectangle(cr, dst_x, dst_y, width, height);
    cairo_clip(cr);
    cairo_set_source_surface(cr, src_surface, dst_x - src_x, dst_y - src_y);
    cairo_paint(cr);
    cairo_destroy(cr);
    return 0;
}

Pixmap XCreatePixmap(XDisplay*, XDrawable, unsigned int width, unsigned int height, unsigned int depth) {
    cairo_format_t format = depth <= 1 ? CAIRO_FORMAT_A1 : CAIRO_FORMAT_ARGB32;
    cairo_surface_t* surface = cairo_image_surface_create(format, (int)width, (int)height);
    cairo_t* cr = cairo_create(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_destroy(cr);
    return surface;
}

int XFreePixmap(XDisplay*, Pixmap pixmap) {
    if (pixmap != nullptr) {
        cairo_surface_destroy(pixmap);
    }
    return 0;
}

GC XCreateGC(XDisplay*, XDrawable drawable, unsigned long, void*) {
    cairo_surface_t* surface = gtk4_compat_surface(drawable);
    return surface ? cairo_create(surface) : nullptr;
}

int XFreeGC(XDisplay*, GC gc) {
    if (gc != nullptr) {
        cairo_destroy(gc);
    }
    return 0;
}

XImage* XGetImage(XDisplay*, XDrawable drawable, int x, int y, unsigned int width, unsigned int height, unsigned long, int) {
    cairo_surface_t* surface = gtk4_compat_surface(drawable);
    if (surface == nullptr) {
        return nullptr;
    }

    cairo_format_t format = cairo_image_surface_get_format(surface);
    cairo_surface_t* copy = cairo_image_surface_create(format, (int)width, (int)height);
    cairo_t* cr = cairo_create(copy);
    cairo_set_source_surface(cr, surface, -x, -y);
    cairo_paint(cr);
    cairo_destroy(cr);

    XImage* image = new XImage;
    image->surface_ = copy;
    image->width = (int)width;
    image->height = (int)height;
    return image;
}

int XPutImage(XDisplay*, XDrawable drawable, GC, XImage* image, int src_x, int src_y, int dest_x, int dest_y, unsigned int width, unsigned int height) {
    cairo_surface_t* surface = gtk4_compat_surface(drawable);
    if (surface == nullptr || image == nullptr || image->surface_ == nullptr) {
        return 0;
    }

    cairo_t* cr = cairo_create(surface);
    cairo_rectangle(cr, dest_x, dest_y, width, height);
    cairo_clip(cr);
    cairo_set_source_surface(cr, image->surface_, dest_x - src_x, dest_y - src_y);
    cairo_paint(cr);
    cairo_destroy(cr);
    return 0;
}

Region XCreateRegion(void) {
    return cairo_region_create();
}

int XDestroyRegion(Region region) {
    if (region != nullptr) {
        cairo_region_destroy(region);
    }
    return 0;
}

int XUnionRectWithRegion(const XRectangle* rect, Region, Region dst) {
    if (rect == nullptr || dst == nullptr) {
        return 0;
    }
    cairo_rectangle_int_t cairo_rect = { rect->x, rect->y, rect->width, rect->height };
    cairo_region_union_rectangle(dst, &cairo_rect);
    return 0;
}

int XIntersectRegion(Region src1, Region src2, Region dst) {
    if (src1 == nullptr || src2 == nullptr || dst == nullptr) {
        return 0;
    }
    cairo_rectangle_int_t r1;
    cairo_rectangle_int_t r2;
    cairo_region_get_extents(src1, &r1);
    cairo_region_get_extents(src2, &r2);
    cairo_region_subtract(dst, dst);
    cairo_rectangle_int_t ri;
    ri.x = r1.x > r2.x ? r1.x : r2.x;
    ri.y = r1.y > r2.y ? r1.y : r2.y;
    int right = (r1.x + r1.width < r2.x + r2.width) ? (r1.x + r1.width) : (r2.x + r2.width);
    int bottom = (r1.y + r1.height < r2.y + r2.height) ? (r1.y + r1.height) : (r2.y + r2.height);
    ri.width = right > ri.x ? right - ri.x : 0;
    ri.height = bottom > ri.y ? bottom - ri.y : 0;
    if (ri.width > 0 && ri.height > 0) {
        cairo_region_union_rectangle(dst, &ri);
    }
    return 0;
}

int XClipBox(Region region, XRectangle* rect) {
    if (region == nullptr || rect == nullptr) {
        return 0;
    }
    cairo_rectangle_int_t extents;
    cairo_region_get_extents(region, &extents);
    rect->x = (short)extents.x;
    rect->y = (short)extents.y;
    rect->width = (unsigned short)extents.width;
    rect->height = (unsigned short)extents.height;
    return 0;
}

Region XPolygonRegion(XPoint* points, int npoints, int) {
    Region region = cairo_region_create();
    if (points == nullptr || npoints <= 0) {
        return region;
    }
    short min_x = points[0].x, max_x = points[0].x;
    short min_y = points[0].y, max_y = points[0].y;
    for (int i = 1; i < npoints; ++i) {
        if (points[i].x < min_x) min_x = points[i].x;
        if (points[i].x > max_x) max_x = points[i].x;
        if (points[i].y < min_y) min_y = points[i].y;
        if (points[i].y > max_y) max_y = points[i].y;
    }
    cairo_rectangle_int_t rect = { min_x, min_y, max_x - min_x + 1, max_y - min_y + 1 };
    cairo_region_union_rectangle(region, &rect);
    return region;
}

int XSetRegion(XDisplay*, GC gc, Region region) {
    if (gc == nullptr) {
        return 0;
    }
    cairo_reset_clip(gc);
    if (region != nullptr) {
        cairo_rectangle_int_t extents;
        cairo_region_get_extents(region, &extents);
        cairo_rectangle(gc, extents.x, extents.y, extents.width, extents.height);
        cairo_clip(gc);
    }
    return 0;
}

int XSetGraphicsExposures(XDisplay*, GC, int) {
    return 0;
}

int XSetClipRectangles(XDisplay*, GC gc, int clip_x_origin, int clip_y_origin, XRectangle* rects, int n, int) {
    if (gc == nullptr) {
        return 0;
    }
    cairo_reset_clip(gc);
    for (int i = 0; i < n; ++i) {
        cairo_rectangle(gc, clip_x_origin + rects[i].x, clip_y_origin + rects[i].y, rects[i].width, rects[i].height);
    }
    cairo_clip(gc);
    return 0;
}

int XSetForeground(XDisplay*, GC gc, unsigned long pixel) {
    if (gc != nullptr) {
        gtk4_compat_set_source_pixel(gc, pixel);
    }
    return 0;
}

int XSetLineAttributes(XDisplay*, GC gc, unsigned int width, int, int, int) {
    if (gc != nullptr) {
        cairo_set_line_width(gc, width == 0 ? 1.0 : (double)width);
    }
    return 0;
}

int XFillRectangle(XDisplay*, XDrawable drawable, GC gc, int x, int y, unsigned int width, unsigned int height) {
    cairo_t* cr = gc ? gc : cairo_create(gtk4_compat_surface(drawable));
    if (cr == nullptr) {
        return 0;
    }
    cairo_rectangle(cr, x, y, width, height);
    cairo_fill(cr);
    if (gc == nullptr) {
        cairo_destroy(cr);
    }
    return 0;
}

int XDrawRectangle(XDisplay*, XDrawable drawable, GC gc, int x, int y, unsigned int width, unsigned int height) {
    cairo_t* cr = gc ? gc : cairo_create(gtk4_compat_surface(drawable));
    if (cr == nullptr) {
        return 0;
    }
    cairo_rectangle(cr, x, y, width, height);
    cairo_stroke(cr);
    if (gc == nullptr) {
        cairo_destroy(cr);
    }
    return 0;
}

int XFillPolygon(XDisplay*, XDrawable drawable, GC gc, XPoint* points, int npoints, int, int) {
    cairo_t* cr = gc ? gc : cairo_create(gtk4_compat_surface(drawable));
    if (cr == nullptr || points == nullptr || npoints <= 0) {
        return 0;
    }
    cairo_move_to(cr, points[0].x, points[0].y);
    for (int i = 1; i < npoints; ++i) {
        cairo_line_to(cr, points[i].x, points[i].y);
    }
    cairo_close_path(cr);
    cairo_fill(cr);
    if (gc == nullptr) {
        cairo_destroy(cr);
    }
    return 0;
}

int XGetGeometry(XDisplay*, XDrawable drawable, XWindow*, int* x_return, int* y_return, unsigned int* width_return, unsigned int* height_return, unsigned int* border_return, unsigned int* depth_return) {
    cairo_surface_t* surface = gtk4_compat_surface(drawable);
    if (x_return) *x_return = 0;
    if (y_return) *y_return = 0;
    if (border_return) *border_return = 0;
    if (depth_return) *depth_return = 32;
    if (surface == nullptr) {
        if (width_return) *width_return = 0;
        if (height_return) *height_return = 0;
        return 0;
    }
    if (width_return) *width_return = (unsigned int)cairo_image_surface_get_width(surface);
    if (height_return) *height_return = (unsigned int)cairo_image_surface_get_height(surface);
    return 1;
}

int XAllocColorCells(XDisplay*, XColormap, int, unsigned long*, unsigned int, unsigned long* pixels, unsigned int npixels) {
    if (pixels == nullptr) {
        return 0;
    }
    for (unsigned int i = 0; i < npixels; ++i) {
        pixels[i] = i;
    }
    return 1;
}

int XFreeColors(XDisplay*, XColormap, unsigned long*, int, unsigned long) {
    return 1;
}

int XStoreColor(XDisplay*, XColormap, XColor* color) {
    if (color == nullptr) {
        return 0;
    }
    color->pixel = ((unsigned long)(color->red >> 8) << 16)
                 | ((unsigned long)(color->green >> 8) << 8)
                 | (unsigned long)(color->blue >> 8);
    return 1;
}

unsigned long XBlackPixel(XDisplay*, int) {
    return 0x00000000UL;
}
