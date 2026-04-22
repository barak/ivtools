/*
 * GTK4 backend: replaces IV-X11/xcursor.h
 * Defines CursorRep hierarchy using GdkCursor* instead of XCursor (XID).
 */

#ifndef iv_gdkcursor_h
#define iv_gdkcursor_h

#include <IV-GTK4/gdklib.h>
#include <IV-GTK4/gdkdefs.h>

#include <InterViews/_enter.h>

class Bitmap;
class Color;
class Display;
class Font;
class Style;
class WindowVisual;

class CursorRep {
public:
    const Color* fg_;
    const Color* bg_;
    Display*     display_;
    GdkCursor*   gdkcursor_;   /* replaces XCursor xcursor_ */

    CursorRep(const Color* fg, const Color* bg);
    virtual ~CursorRep();

    /* Returns the GdkCursor*, creating it if necessary */
    GdkCursor* gdk_cursor(Display*, WindowVisual*);

    virtual void make_cursor(Display*, WindowVisual*) = 0;

    const Color* make_color(
        Display*, Style*,
        const char* str1, const char* str2, const char* str3,
        const char* default_value
    );

    /* Compatibility accessor – used by xwindow code that calls xid() */
    GdkCursor* xid(Display* d, WindowVisual* wv) { return gdk_cursor(d, wv); }
};

class CursorRepData : public CursorRep {
public:
    short x_, y_;
    const int* pat_;
    const int* mask_;

    CursorRepData(
        short x_hot, short y_hot, const int* pat, const int* mask,
        const Color* fg, const Color* bg
    );
    virtual ~CursorRepData();

    virtual void make_cursor(Display*, WindowVisual*);

    /* Build a 1-bit Cairo surface from a 16-int scanline array */
    cairo_surface_t* make_cursor_surface(const int* scanline,
                                         int width, int height);
};

class CursorRepBitmap : public CursorRep {
public:
    const Bitmap* pat_;
    const Bitmap* mask_;

    CursorRepBitmap(
        const Bitmap* pat, const Bitmap* mask,
        const Color* fg, const Color* bg
    );
    virtual ~CursorRepBitmap();

    virtual void make_cursor(Display*, WindowVisual*);
};

class CursorRepFont : public CursorRep {
public:
    const Font* font_;
    int pat_;
    int mask_;

    CursorRepFont(
        const Font*, int pat, int mask,
        const Color* fg, const Color* bg
    );
    virtual ~CursorRepFont();

    virtual void make_cursor(Display*, WindowVisual*);
};

class CursorRepXFont : public CursorRep {
public:
    int code_;

    CursorRepXFont(int code, const Color* fg, const Color* bg);
    virtual ~CursorRepXFont();

    virtual void make_cursor(Display*, WindowVisual*);
};

#include <InterViews/_leave.h>

#endif /* iv_gdkcursor_h */
