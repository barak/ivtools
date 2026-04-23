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
 * GTK4 backend: replaces IV-X11/xwindow.h
 * Defines WindowRep, WindowVisual, and ManagedWindowRep using GDK/GTK4 types.
 */

#ifndef iv_gtkwindow_h
#define iv_gtkwindow_h

#include <InterViews/boolean.h>
#include <InterViews/geometry.h>
#include <IV-GTK4/gdklib.h>
#include <IV-GTK4/gdkdefs.h>
#include <IV-GTK4/gdkutil.h>
#include <OS/list.h>

#include <InterViews/_enter.h>

class Bitmap;
class Canvas;
class Cursor;
class Display;
class Glyph;
class Handler;
class ManagedWindow;
class ManagedWindowRep;
class String;
class Style;
class Window;
class WindowCursorStack;

/* WindowTable maps GdkSurface* → Window* */
class WindowTable;

/*
 * WindowVisual
 *
 * In X11 this held an XVisual + XColormap.  In GTK4 it holds a reference
 * to the GdkDisplay and provides the colour-finding interface used by
 * ColorRep.  The GdkRGBA values are 0.0–1.0 floats, so "find_color"
 * simply converts the 16-bit unsigned short channels.
 */
struct WindowVisualInfo {
    GdkDisplay * display_;
    int          screen_;     /* screen index (always 0 on Wayland) */
    int          depth_;      /* colour depth in bits */
};

/* Forward declaration needed for declarePtrList */
class WindowVisual;

declarePtrList(WindowVisualList, WindowVisual)

class WindowVisual {
public:
    WindowVisual(const WindowVisualInfo&);
    ~WindowVisual();

    static WindowVisual* find_visual(Display*, Style*);

    void init_color_tables();
    void find_color(unsigned long, XColor&);
    void find_color(unsigned short r, unsigned short g, unsigned short b,
                    XColor& xc);

    /* Returns an XOR colour value.  In GTK4 this is just white ^ bg. */
    unsigned long x_or(const Style&) const;
    unsigned long x_or_(const Style&) const;

    GdkDisplay* display() const;
    int screen() const;
    int depth() const;
    /* No Visual* / XColormap in GTK4 */
    int colormap() const { return 0; }

private:
    WindowVisualInfo info_;
    unsigned long xor_;
};

inline GdkDisplay* WindowVisual::display() const { return info_.display_; }
inline int WindowVisual::screen() const { return info_.screen_; }
inline int WindowVisual::depth() const { return info_.depth_; }

/*
 * WindowRep
 *
 * Replaces the X11 WindowRep.  The XWindow (unsigned long xid) is replaced
 * by a GtkWidget* for the drawing area, and a GtkWindow* for the top-level
 * window shell.
 */
class WindowRep {
public:
    Glyph*      glyph_;
    Style*      style_;
    Display*    display_;
    WindowVisual* visual_;
    Canvas*     canvas_;
    Requisition shape_;
    Allocation  allocation_;
    Cursor*     cursor_;
    WindowCursorStack* cursor_stack_;
    Coord       left_;
    Coord       bottom_;
    float       xalign_;
    float       yalign_;

    Handler*    focus_in_;
    Handler*    focus_out_;
    Handler*    wm_delete_;

    /* GTK4 widget pointers replacing XWindow xwindow_ / xtoplevel_ */
    GtkWidget*  widget_;          /* drawing area for this window */
    GtkWidget*  gtkwindow_;       /* top-level GtkWindow (may == widget_) */
    XWindow     xwindow_;
    XWindow     xtoplevel_;

    /* Position / size – kept in pixel coords as before */
    int         xpos_;
    int         ypos_;

    Window*     toplevel_;
    GtkWidget*  toplevel_widget_;

    /* Mapping state */
    boolean placed_       : 1;
    boolean aligned_      : 1;
    boolean needs_resize_ : 1;
    boolean resized_      : 1;
    boolean moved_        : 1;
    boolean unmapped_     : 1;
    boolean wm_mapped_    : 1;
    boolean map_pending_  : 1;
    boolean override_redirect_ : 1;

    /* "unbound" sentinel (replaces XWindow == 0 check) */
    enum { unbound = 0 };

    /* Inline accessor so code like w.dpy() keeps compiling */
    GdkDisplay* dpy() {
        return gdk_display_get_default();
    }

    /* WM protocol atoms (string constants in GTK4) */
    static Atom wm_delete_atom_;
    static Atom wm_protocols_atom_;
    Atom wm_delete_atom();
    Atom wm_protocols_atom();

    void clear_mapping_info();
    void map_notify(Window*, XMapEvent&);
    void unmap_notify(Window*, XUnmapEvent&);
    void expose(Window*, XExposeEvent&);
    void configure_notify(Window*, XConfigureEvent&);
    void move(Window*, int x, int y);
    void resize(Window*, unsigned int w, unsigned int h);
    void check_position(const Window*);
    void check_binding(Window*);
    void do_bind(Window*, GtkWidget* parent, int left, int top);
    void init_renderer(Window*);

    static Window* find(GtkWidget*, WindowTable*);
};

/*
 * ManagedWindowHintInfo
 *
 * Carries WM hint data during set_props() calls.  In GTK4 many of these
 * fields are set via gtk_window_set_*() calls.
 */
class ManagedWindowHintInfo {
    friend class ManagedWindowRep;

    Style*       style_;
    GtkWidget*   widget_;         /* replaces xwindow_ */
    GdkDisplay*  dpy_;
    unsigned int pwidth_;
    unsigned int pheight_;
    Display*     display_;
    XWMHints*    hints_;          /* kept for API compat; may be nullptr */
};

typedef boolean (ManagedWindowRep::*HintFunction)(ManagedWindowHintInfo&);

class ManagedWindowRep {
public:
    ManagedWindow* icon_;
    Bitmap*        icon_bitmap_;
    Bitmap*        icon_mask_;
    Window*        group_leader_;
    Window*        transient_for_;

    void do_set(Window*, HintFunction);

    boolean set_name(ManagedWindowHintInfo&);
    boolean set_geometry(ManagedWindowHintInfo&);
    boolean set_group_leader(ManagedWindowHintInfo&);
    boolean set_transient_for(ManagedWindowHintInfo&);
    boolean set_icon_name(ManagedWindowHintInfo&);
    boolean set_icon_geometry(ManagedWindowHintInfo&);
    boolean set_icon(ManagedWindowHintInfo&);
    boolean set_icon_bitmap(ManagedWindowHintInfo&);
    boolean set_icon_mask(ManagedWindowHintInfo&);
    boolean set_all(ManagedWindowHintInfo&);

    void wm_normal_hints(Window*);
    void wm_name(Window*);
    void wm_class(Window*);
    void wm_protocols(Window*);
    void wm_colormap_windows(Window*);
    void wm_hints(Window*);
};

#include <InterViews/_leave.h>

#endif /* iv_gtkwindow_h */
