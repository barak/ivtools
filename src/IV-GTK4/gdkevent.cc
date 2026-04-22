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
 * GTK4 backend: event-reading implementation.
 * Replaces IV-X11/xevent.cc.
 *
 * The Event and EventRep classes are implemented here.  The IVGdkEvent
 * synthetic struct mirrors the X11 XEvent field layout so that all the
 * switch/field-access code in event.cc and window.cc compiles unchanged.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "wtable.h"
#include <InterViews/display.h>
#include <InterViews/event.h>
#include <InterViews/handler.h>
#include <InterViews/session.h>
#include <InterViews/window.h>
#include <IV-GTK4/gdklib.h>
#include <IV-GTK4/gdkdefs.h>
#include <IV-GTK4/gdkutil.h>
#include <IV-GTK4/gtkcanvas.h>
#include <IV-GTK4/gdkdisplay.h>
#include <IV-GTK4/gtkdrag.h>
#include <IV-GTK4/gdkevent.h>
#include <IV-GTK4/gtkwindow.h>
#include <string.h>
#include <stdio.h>

event_tracker_ptr Event::_event_tracker = nil;

Event::Event() {
    if (sizeof(EventRep) <= sizeof(free_store_)) {
        rep_ = (EventRep*)free_store_;
    } else {
        rep_ = new EventRep;
    }
    EventRep& e = *rep_;
    e.display_  = nil;
    e.window_   = nil;
    e.xevent_.type = LASTEvent;
    e.pointer_x_      = 0;
    e.pointer_y_      = 0;
    e.pointer_root_x_ = 0;
    e.pointer_root_y_ = 0;
    e.clear();

    /* backward compatibility fields */
    target = nil;
    timestamp = 0;
    eventType = undefined;
    x = 0;
    y = 0;
    control    = false;
    meta       = false;
    shift      = false;
    shiftlock  = false;
    leftmouse  = false;
    middlemouse= false;
    rightmouse = false;
    button = 0;
    len = 0;
    keystring = keydata;
    w = nil;
    wx = 0;
    wy = 0;
}

Event::Event(const Event& e) {
    if (sizeof(EventRep) <= sizeof(free_store_)) {
        rep_ = (EventRep*)free_store_;
    } else {
        rep_ = new EventRep;
    }
    *this = e;
}

Event::~Event() {
    if (rep_ != (EventRep*)free_store_) delete rep_;
}

Event& Event::operator=(const Event& e) {
    copy_rep(e);
    target      = e.target;
    timestamp   = e.timestamp;
    eventType   = e.eventType;
    x           = e.x;
    y           = e.y;
    control     = e.control;
    meta        = e.meta;
    shift       = e.shift;
    shiftlock   = e.shiftlock;
    leftmouse   = e.leftmouse;
    middlemouse = e.middlemouse;
    rightmouse  = e.rightmouse;
    button      = e.button;
    len         = e.len;
    if (e.keystring == e.keydata) {
        keystring = keydata;
        strncpy(keydata, e.keydata, e.len);
    } else {
        keystring = e.keystring;
    }
    w  = e.w;
    wx = e.wx;
    wy = e.wy;
    return *this;
}

void Event::copy_rep(const Event& e) { *rep_ = *e.rep_; }

void Event::display(Display* d) { rep()->display_ = d; }
Display* Event::display() const { return rep()->display_; }
void Event::window(Window* w)   { rep()->window_ = w; }
Window* Event::window() const   { return rep()->window_; }

boolean Event::pending() const {
    Event e;
    if (rep()->display_->get(e)) {
        rep()->display_->put(e);
        return true;
    }
    return false;
}

void Event::read() { Session::instance()->read(*this); }

boolean Event::read(long s, long u) {
    return Session::instance()->read(s, u, *this);
}

void Event::unread() { rep()->display_->put(*this); }

void Event::poll() {
    /* In GTK4 there is no XQueryPointer equivalent independent of a window.
       We synthesize a MotionNotify at (0,0) with the current modifier state
       from the seat.  This is a best-effort implementation. */
    EventRep& e = *rep();
    if (!e.display_) {
        e.display_ = e.window_
            ? e.window_->display()
            : Session::instance()->default_display();
    }
    IVGdkEvent& m = e.xevent_;
    memset(&m, 0, sizeof(m));
    m.type = MotionNotify;

    /* Try to get current pointer position via GDK */
    GdkDisplay* gdpy = e.display_->rep()->display_;
    if (gdpy) {
        GdkSeat* seat = gdk_display_get_default_seat(gdpy);
        GdkDevice* pointer = gdk_seat_get_pointer(seat);
        if (pointer) {
            double px = 0, py = 0;
            GdkSurface* surface = nullptr;
            if (e.window_ && e.window_->rep()->widget_) {
                surface = gtk_native_get_surface(
                    gtk_widget_get_native(e.window_->rep()->widget_));
            }
            if (surface) {
                gdk_surface_get_device_position(surface, pointer, &px, &py, nullptr);
            }
            m.xmotion.x = (int)px;
            m.xmotion.y = (int)py;
        }
    }
    e.clear();
}

Handler* Event::handler() const {
    Handler* h = nil;
    Window* w = rep()->window_;
    if (w) h = w->target(*this);
    return h;
}

void Event::handle() {
    if (event_tracker()) (*event_tracker())(*this);
    Handler* h = nil;
    if (rep()->xevent_.type != KeyPress) h = grabber();
    if (!h) h = handler();
    if (h) {
        boolean b = Resource::defer(true);
        h->ref();
        h->event(*this);
        h->unref();
        Resource::flush();
        Resource::defer(b);
    }
}

void Event::grab(Handler* h) const {
    EventRep& e = *rep();
    e.display_->grab(e.window_, h);
}

void Event::ungrab(Handler* h) const  { rep()->display_->ungrab(h); }
Handler* Event::grabber() const { return rep()->display_->grabber(); }

boolean Event::is_grabbing(Handler* h) const {
    return rep()->display_->is_grabbing(h);
}

EventType Event::type() const {
    switch (rep()->xevent_.type) {
    case MotionNotify: case EnterNotify: case LeaveNotify: return motion;
    case ButtonPress:   return down;
    case ButtonRelease: return up;
    case KeyPress:      return key;
    case SelectionNotify: return selection_notify;
    default:            return other_event;
    }
}

const char* Event::typestr() const {
    switch (rep()->xevent_.type) {
    case KeyPress:          return "KeyPress";
    case KeyRelease:        return "KeyRelease";
    case ButtonPress:       return "ButtonPress";
    case ButtonRelease:     return "ButtonRelease";
    case MotionNotify:      return "MotionNotify";
    case EnterNotify:       return "EnterNotify";
    case LeaveNotify:       return "LeaveNotify";
    case FocusIn:           return "FocusIn";
    case FocusOut:          return "FocusOut";
    case KeymapNotify:      return "KeymapNotify";
    case Expose:            return "Expose";
    case GraphicsExpose:    return "GraphicsExpose";
    case NoExpose:          return "NoExpose";
    case VisibilityNotify:  return "VisibilityNotify";
    case CreateNotify:      return "CreateNotify";
    case DestroyNotify:     return "DestroyNotify";
    case UnmapNotify:       return "UnmapNotify";
    case MapNotify:         return "MapNotify";
    case MapRequest:        return "MapRequest";
    case ReparentNotify:    return "ReparentNotify";
    case ConfigureNotify:   return "ConfigureNotify";
    case ConfigureRequest:  return "ConfigureRequest";
    case GravityNotify:     return "GravityNotify";
    case ResizeRequest:     return "ResizeRequest";
    case CirculateNotify:   return "CirculateNotify";
    case CirculateRequest:  return "CirculateRequest";
    case PropertyNotify:    return "PropertyNotify";
    case SelectionClear:    return "SelectionClear";
    case SelectionRequest:  return "SelectionRequest";
    case SelectionNotify:   return "SelectionNotify";
    case ColormapNotify:    return "ColormapNotify";
    case ClientMessage:     return "ClientMessage";
    case MappingNotify:     return "MappingNotify";
    case GenericEvent:      return "GenericEvent";
    case LASTEvent:         return "LASTEvent";
    default:                return "NADAEvent";
    }
}

unsigned long Event::time() const {
    const XEvent& xe = rep()->xevent_;
    switch (xe.type) {
    case MotionNotify: case EnterNotify: case LeaveNotify: return xe.time;
    case ButtonPress:  case ButtonRelease: return xe.time;
    case KeyPress:     return xe.time;
    default:           return (unsigned long)GDK_CURRENT_TIME;
    }
}

Coord Event::pointer_x() const {
    EventRep& e = *rep();
    e.locate();
    return e.pointer_x_;
}

Coord Event::pointer_y() const {
    EventRep& e = *rep();
    e.locate();
    return e.pointer_y_;
}

Coord Event::pointer_root_x() const {
    EventRep& e = *rep();
    e.locate();
    return e.pointer_root_x_;
}

Coord Event::pointer_root_y() const {
    EventRep& e = *rep();
    e.locate();
    return e.pointer_root_y_;
}

EventButton Event::pointer_button() const {
    const XEvent& xe = rep()->xevent_;
    switch (xe.type) {
    case ButtonPress: case ButtonRelease:
        switch (xe.xbutton.button) {
        case GDK_BUTTON_PRIMARY:   return left;
        case GDK_BUTTON_MIDDLE:    return middle;
        case GDK_BUTTON_SECONDARY: return right;
        default:                   return other_button;
        }
    default: return none;
    }
}

unsigned int Event::keymask() const {
    const XEvent& xe = rep()->xevent_;
    switch (xe.type) {
    case MotionNotify:                return xe.xmotion.state;
    case ButtonPress: case ButtonRelease: return xe.xbutton.state;
    case KeyPress:                    return xe.xkey.state;
    case EnterNotify: case LeaveNotify:   return xe.xcrossing.state;
    default:                          return 0;
    }
}

static boolean check_key(const Event* e, unsigned int mask) {
    return (e->keymask() & mask) != 0;
}

boolean Event::control_is_down() const { return check_key(this, (unsigned)GDK_CONTROL_MASK); }
boolean Event::meta_is_down()    const { return check_key(this, (unsigned)GDK_ALT_MASK); }
boolean Event::shift_is_down()   const { return check_key(this, (unsigned)GDK_SHIFT_MASK); }
boolean Event::capslock_is_down()const { return check_key(this, (unsigned)GDK_LOCK_MASK); }
boolean Event::left_is_down()    const { return check_key(this, (unsigned)GDK_BUTTON1_MASK); }
boolean Event::middle_is_down()  const { return check_key(this, (unsigned)GDK_BUTTON2_MASK); }
boolean Event::right_is_down()   const { return check_key(this, (unsigned)GDK_BUTTON3_MASK); }

unsigned char Event::keycode() const {
    const XEvent& xe = rep()->xevent_;
    if (xe.type == KeyPress) return (unsigned char)xe.xkey.keycode;
    return 0;
}

unsigned long Event::keysym() const {
    const XEvent& xe = rep()->xevent_;
    if (xe.type == KeyPress) {
        /* xkey.keysym was stored by on_key_pressed from the GDK keyval */
        return xe.xkey.keysym;
    }
    return (unsigned long)GDK_KEY_VoidSymbol;
}

unsigned int Event::mapkey(char* buf, unsigned int len) const {
    unsigned int n = 0;
    const XEvent& xe = rep()->xevent_;
    if (xe.type == KeyPress) {
        /* xkey.buf was filled in by the key-pressed signal handler */
        n = (unsigned int)xe.xkey.buflen;
        if (n > len) n = len;
        strncpy(buf, xe.xkey.buf, n);
        if (meta_is_down()) {
            for (unsigned int i = 0; i < n; i++) buf[i] |= 0200;
        }
    }
    return n;
}

/* ================================================================== */
/* EventRep                                                            */
/* ================================================================== */

void EventRep::clear() {
    location_valid_      = false;
    has_pointer_location_ = false;
}

void EventRep::locate() {
    if (location_valid_ || !window_) return;

    PixelCoord x = 0, y = 0, root_x = 0, root_y = 0;
    boolean has_root_location = false;
    const XEvent& xe = xevent_;

    switch (xe.type) {
    case MotionNotify:
        x = xe.xmotion.x;
        y = xe.xmotion.y;
        /* In GTK4 we don't have root coordinates in the synthetic event;
           approximate them as widget-local for now. */
        root_x = x;
        root_y = y;
        has_root_location = true;
        break;
    case ButtonPress:
    case ButtonRelease:
        x = xe.xbutton.x;
        y = xe.xbutton.y;
        root_x = x;
        root_y = y;
        has_root_location = true;
        break;
    case KeyPress:
        /* Key events don't carry pointer position in GTK4.
           Query the pointer from the display. */
        {
            x = 0; y = 0;
            if (display_) {
                GdkDisplay* gdpy = display_->rep()->display_;
                GdkSeat* seat = gdk_display_get_default_seat(gdpy);
                GdkDevice* ptr = gdk_seat_get_pointer(seat);
                if (ptr && window_ && window_->rep()->widget_) {
                    GdkSurface* surf = gtk_native_get_surface(
                        gtk_widget_get_native(window_->rep()->widget_));
                    double px = 0, py = 0;
                    if (surf) {
                        gdk_surface_get_device_position(surf, ptr, &px, &py, nullptr);
                        x = (PixelCoord)px;
                        y = (PixelCoord)py;
                    }
                }
            }
        }
        root_x = x; root_y = y;
        has_root_location = false;
        break;
    case EnterNotify:
    case LeaveNotify:
        x = xe.xcrossing.x;
        y = xe.xcrossing.y;
        root_x = x; root_y = y;
        has_root_location = true;
        break;
    case ClientMessage:
        if (!GtkDrag::isDrag(xe)) {
            has_pointer_location_ = false;
            return;
        }
        GtkDrag::locate(xe, x, y);
        has_root_location = false;
        break;
    default:
        has_pointer_location_ = false;
        return;
    }

    has_pointer_location_ = true;
    pointer_x_ = display_->to_coord(x);
    pointer_y_ = display_->to_coord(window_->canvas()->pheight() - y);
    pointer_root_x_ = display_->to_coord(root_x);
    pointer_root_y_ = display_->to_coord(display_->pheight() - root_y);
    location_valid_ = true;

    if (has_root_location) {
        window_->rep()->move(window_, root_x - x, root_y - y);
    }
}

boolean EventRep::has_pointer_location() {
    locate();
    return has_pointer_location_;
}

void EventRep::acknowledge_motion() {
    /* In X11 this called XQueryPointer to get an updated position.
       In GTK4 motions are event-driven; nothing to acknowledge. */
}

/* ================================================================== */
/* XLookupString / XParseGeometry replacements (declared in gdkutil.h) */
/* ================================================================== */

int XLookupString(void* /*key_event*/, char* buf, int nbytes,
                  unsigned long* keysym_return, void* /*status*/)
{
    /* In the GTK4 backend the IVGdkEvent struct already has the key string
       filled in by on_key_pressed.  Callers that still call XLookupString
       pass the xkey sub-struct; we return an empty result as a fallback. */
    if (buf && nbytes > 0) buf[0] = '\0';
    if (keysym_return) *keysym_return = GDK_KEY_VoidSymbol;
    return 0;
}

int XParseGeometry(const char* str, int* x, int* y,
                   unsigned int* w, unsigned int* h)
{
    if (!str) return 0;
    int rx = 0, ry = 0;
    unsigned int rw = 0, rh = 0;
    int result = 0;

    /* Parse "[<width>x<height>][{+-}<xoffset>{+-}<yoffset>]" */
    const char* p = str;

    /* Optional width x height */
    if (*p >= '0' && *p <= '9') {
        rw = (unsigned int)atoi(p);
        while (*p >= '0' && *p <= '9') p++;
        if (*p == 'x' || *p == 'X') {
            p++;
            rh = (unsigned int)atoi(p);
            while (*p >= '0' && *p <= '9') p++;
            result |= WidthValue | HeightValue;
        }
    }

    /* Optional x offset */
    if (*p == '+' || *p == '-') {
        boolean neg = (*p == '-');
        p++;
        rx = atoi(p);
        while (*p >= '0' && *p <= '9') p++;
        result |= XValue;
        if (neg) { result |= XNegative; rx = -rx; }
    }

    /* Optional y offset */
    if (*p == '+' || *p == '-') {
        boolean neg = (*p == '-');
        p++;
        ry = atoi(p);
        result |= YValue;
        if (neg) { result |= YNegative; ry = -ry; }
    }

    if (result & XValue)      *x = rx;
    if (result & YValue)      *y = ry;
    if (result & WidthValue)  *w = rw;
    if (result & HeightValue) *h = rh;
    return result;
}
