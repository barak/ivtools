/*
 * GTK4 backend: replaces IV-X11/xpainter.h
 * Defines PainterRep using Cairo instead of X11 GC/XDrawable.
 */

#ifndef iv_gtkpainter_h
#define iv_gtkpainter_h

#include <IV-GTK4/gdklib.h>

class Brush;
class Pattern;
class Transformer;

class PainterRep {
public:
    PainterRep();
    ~PainterRep();

    void PrepareFill(const Pattern*);
    void PrepareDash(const Brush*);

    /*
     * fillgc / dashgc were X11 GC handles.  In GTK4 we hold onto
     * a cairo_t* that is set during the draw callback.  The "GC" typedef
     * in gdklib.h maps to cairo_t*, so the field types still compile.
     */
    cairo_t*    fillgc;     /* cairo context for fill operations */
    cairo_t*    dashgc;     /* cairo context for dash/stroke operations */
    boolean     fillbg;
    boolean     overwrite;
    boolean     x_or;
    boolean     clipped;
    Display*    display;

    /* Clip rectangle (replaces XRectangle xclip[1]) */
    cairo_rectangle_int_t xclip[1];

    /* Cairo-specific: fill pattern (replaces X11 pattern stipple) */
    cairo_pattern_t* fill_pattern_;
    /* Dash array for dashed lines */
    double* dashes_;
    int     n_dashes_;
    double  dash_offset_;
};

/*
 * DrawTransformedImage
 *
 * Replaces the X11 version which used XImage* and manual pixel blitting.
 * In GTK4 we use Cairo affine transforms and cairo_surface_t* sources.
 */
void DrawTransformedImage(
    cairo_surface_t* src, int sx0, int sy0,
    cairo_surface_t* mask, int mx0, int my0,
    cairo_surface_t* dst, unsigned int height, int dx0, int dy0,
    boolean stencil, GdkRGBA fg, GdkRGBA bg,
    cairo_t* cr, const Transformer& matrix
);

#endif /* iv_gtkpainter_h */
