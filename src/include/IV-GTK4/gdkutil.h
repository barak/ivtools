/*
 * GTK4 backend: replaces IV-X11/Xutil.h
 *
 * Provides Pango font / WM hint types that replace the Xlib Xutil.h types.
 */

#ifndef iv_gdkutil_h
#define iv_gdkutil_h

#include <IV-GTK4/gdklib.h>

/* Window-manager hint structs – minimal stubs sufficient for compilation */
struct XSizeHints {
    long flags;
    int  x, y;
    int  width, height;
    int  min_width, min_height;
    int  max_width, max_height;
    int  base_width, base_height;
};

/* flags values for XSizeHints */
#define USPosition  (1L << 0)
#define USSize      (1L << 1)
#define PPosition   (1L << 2)
#define PSize       (1L << 3)
#define PMinSize    (1L << 4)
#define PMaxSize    (1L << 5)
#define PResizeInc  (1L << 6)
#define PAspect     (1L << 7)
#define PBaseSize   (1L << 8)

/* XWMHints */
struct XWMHints {
    long flags;
    int  input;
    int  initial_state;
    unsigned long icon_pixmap;  /* unused in GTK4 port */
    GdkSurface * icon_window;
    int  icon_x, icon_y;
    unsigned long icon_mask;
    GdkSurface * window_group;
};

#define InputHint        (1L << 0)
#define StateHint        (1L << 1)
#define IconPixmapHint   (1L << 2)
#define IconWindowHint   (1L << 3)
#define IconPositionHint (1L << 4)
#define IconMaskHint     (1L << 5)
#define WindowGroupHint  (1L << 6)

#define NormalState  1
#define IconicState  3

/* XClassHint */
struct XClassHint {
    char * res_name;
    char * res_class;
};

/* XTextItem (plain 8-bit) */
struct XTextItem {
    char * chars;
    int    nchars;
    int    delta;
    void * font;  /* unused; Pango owns the font */
};

/* XChar2b: 16-bit character cell */
struct XChar2b {
    unsigned char byte1;
    unsigned char byte2;
};

/* Character metrics returned by Pango – mirrors XCharStruct */
struct XCharStruct {
    short lbearing;
    short rbearing;
    short width;
    short ascent;
    short descent;
};

/* XFontStruct stub – actual font metrics come from PangoFontMetrics */
struct XFontStruct {
    PangoFontDescription * description;
    PangoContext         * context;
    double                 ascent;
    double                 descent;
    double                 max_width;
    int                    per_char_count;
    XCharStruct          * per_char;
};

/* Geometry parsing – retained for ApplicationWindow::compute_geometry */
/* Returns the same bitmask as XParseGeometry */
#define XValue    (1 << 0)
#define YValue    (1 << 1)
#define WidthValue  (1 << 2)
#define HeightValue (1 << 3)
#define XNegative  (1 << 4)
#define YNegative  (1 << 5)

int XParseGeometry(const char* str, int* x, int* y,
                   unsigned int* w, unsigned int* h);

/* XLookupKeysym / XK_ key symbols: map to GDK equivalents */
#include <gdk/gdkkeysyms.h>

/* XK_VoidSymbol */
#ifndef XK_VoidSymbol
#define XK_VoidSymbol GDK_KEY_VoidSymbol
#endif

/* XLookupString replacement – implemented in gdkevent.cc */
int XLookupString(void* key_event, char* buf, int nbytes,
                  unsigned long* keysym_return, void* status_return);

/* XInternAtom replacement – atoms are just string constants in GTK4 */
static inline const char* XInternAtom(GdkDisplay*, const char* name, int)
{ return name; }

#endif /* iv_gdkutil_h */
