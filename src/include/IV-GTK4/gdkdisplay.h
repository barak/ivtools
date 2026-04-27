/*
 * GTK4 backend: replaces IV-X11/xdisplay.h
 * Defines DisplayRep using GdkDisplay* rather than XDisplay*.
 */

#ifndef iv_gdkdisplay_h
#define iv_gdkdisplay_h

#include <InterViews/display.h>
#include <OS/enter-scope.h>
#include <IV-GTK4/gdklib.h>
#include <IV-GTK4/gdkutil.h>
#include <IV-GTK4/gtkwindow.h>

#include <InterViews/_enter.h>

class DamageList;
class GrabList;
class SelectionList;
class String;
class Transformer;
class Window;
class WindowTable;

class DisplayRep {
public:
    GdkDisplay*      display_;        /* replaces XDisplay* */
    unsigned int     screen_;
    GdkSurface*      root_;           /* root surface (nullptr on Wayland) */
    WindowVisualList visuals_;
    WindowVisual*    default_visual_;
    Coord            width_;
    Coord            height_;
    unsigned int     pwidth_;
    unsigned int     pheight_;
    Style*           style_;

    GrabList*        grabbers_;
    DamageList*      damaged_;
    SelectionList*   selections_;
    WindowTable*     wtable_;

    void set_dpi(Coord&);

    void needs_repair(Window*);
    void remove(Window*);

    void init(GdkDisplay*);
};

#include <InterViews/_leave.h>

#endif /* iv_gdkdisplay_h */
