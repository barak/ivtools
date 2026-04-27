/*
 * GTK4 backend: replaces IV-X11/xevent.h
 * Defines EventRep using GDK event types.
 */

#ifndef iv_gdkevent_h
#define iv_gdkevent_h

#include <InterViews/boolean.h>
#include <InterViews/coord.h>
#include <IV-GTK4/gdklib.h>
#include <IV-GTK4/gdkdefs.h>

#include <InterViews/_enter.h>

class Display;
class Window;

class EventRep {
public:
    /*
     * xevent_ holds our synthetic IVGdkEvent which mirrors the XEvent layout.
     * All existing switch(xe.type) / xe.xbutton.x / xe.xkey.state access
     * continues to compile unchanged.
     */
    XEvent   xevent_;
    Display* display_;
    Window*  window_;
    Coord    pointer_x_;
    Coord    pointer_y_;
    Coord    pointer_root_x_;
    Coord    pointer_root_y_;

    void clear();
    void locate();
    boolean has_pointer_location();
    void acknowledge_motion();

private:
    boolean location_valid_;
    boolean has_pointer_location_;
};

#include <InterViews/_leave.h>

#endif /* iv_gdkevent_h */
