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
 * GTK4 backend: replaces IV-X11/Xlib.h
 *
 * Includes GTK4 and GDK headers, and defines type aliases that map
 * the X11 naming convention used throughout the codebase to GDK equivalents.
 */

#ifndef iv_gdklib_h
#define iv_gdklib_h

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>

/*
 * GDK-based type aliases replacing core Xlib types.
 * "XDisplay*" throughout the codebase maps to GdkDisplay*.
 * "XWindow" maps to GdkSurface*.
 * "XDrawable" maps to cairo_surface_t*.
 */
typedef GdkDisplay   XDisplay;
typedef GdkDisplay   XDisplayType;   /* opaque display connection */
typedef GdkSurface * XWindow;
typedef void *       XDrawable;
typedef cairo_t *    GC;             /* graphics context is now a cairo context */
typedef unsigned long XFont;
typedef int XScreen;

/* Atom: GDK doesn't use numeric atoms; use string-keyed GdkAtom */
typedef const char * Atom;
#define None   nullptr
#define NoSymbol 0UL

/* CurrentTime equivalent */
#define CurrentTime ((guint32)GDK_CURRENT_TIME)

/* Pixel value: colours are represented as GdkRGBA, not indices */
typedef unsigned long Pixel;

/* Dummy XColor replacement – carries RGBA-compatible component values. */
struct XColor {
    unsigned short red;
    unsigned short green;
    unsigned short blue;
    unsigned char  flags;
    unsigned char  pad;
    unsigned long  pixel;
};

#define DoRed   1
#define DoGreen 2
#define DoBlue  4

/* Coordinate types */
typedef int          PixelCoord;
typedef int          IntCoord;

/* GDK does not use colormaps; keep the symbol to avoid #ifdef cascades */
typedef int          XColormap;
#define DefaultColormap(d,s) 0

/* Boolean */
#ifndef True
#define True  TRUE
#define False FALSE
#endif

#endif /* iv_gdklib_h */
