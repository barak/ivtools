/*
 * GTK4 backend: IV-2.6 event compatibility layer.
 * Replaces IV-2_6/xevent2_6.cc.
 *
 * The IVGdkEvent struct in gdkdefs.h mirrors the X11 XEvent layout, so
 * field accesses (xe.xbutton.x, xe.xkey.state, etc.) compile unchanged.
 * The event-type constants (KeyPress, MotionNotify, …) are #defined in
 * gdkdefs.h to the corresponding IV_GDK_* enum values.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <InterViews/display.h>
#include <InterViews/event.h>
#include <IV-2_6/InterViews/sensor.h>
#include <IV-2_6/InterViews/world.h>
#include <IV-GTK4/gdklib.h>
#include <IV-GTK4/gdkdefs.h>
#include <IV-GTK4/gdkevent.h>
#include <IV-GTK4/gdkdisplay.h>
#include <string.h>

/* Event masks — numeric values match the GDK modifier/event-mask bits */
typedef unsigned long Mask;
static Mask motionmask  = GDK_BUTTON_MOTION_MASK | GDK_POINTER_MOTION_MASK;
static Mask keymask     = GDK_KEY_PRESS_MASK;
static Mask entermask   = GDK_ENTER_NOTIFY_MASK;
static Mask leavemask   = GDK_LEAVE_NOTIFY_MASK;
static Mask focusmask   = GDK_FOCUS_CHANGE_MASK;
static Mask upmask      = GDK_BUTTON_RELEASE_MASK;
static Mask downmask    = GDK_BUTTON_PRESS_MASK;

/* NotifyInferior – crossing detail constant not present in GTK4 */
#ifndef NotifyInferior
#define NotifyInferior 2
#endif

boolean Sensor::Caught(const Event& e) const {
    XEvent& xe = e.rep()->xevent_;
    switch (xe.type) {
    case MotionNotify:
        return (mask & motionmask) != 0;
    case FocusIn:
    case FocusOut:
        return (mask & focusmask) != 0;
    case KeyPress:
    case ButtonPress:
        return ButtonIsSet(down, e.button);
    case ButtonRelease:
        return ButtonIsSet(up, e.button);
    case EnterNotify:
        return (mask & entermask) != 0;
    case LeaveNotify:
        return (mask & leavemask) != 0;
    }
    return false;
}

void Event::GetInfo() {
    EventRep& e = *rep();
    w = World::current();
    y = 0;
    XEvent& xe = e.xevent_;
    switch (xe.type) {
    case MotionNotify:
        GetMotionInfo();
        break;
    case KeyPress:
        GetKeyInfo();
        break;
    case ButtonPress:
        GetButtonInfo(DownEvent);
        break;
    case ButtonRelease:
        GetButtonInfo(UpEvent);
        break;
    case FocusIn:
        eventType = FocusInEvent;
        break;
    case FocusOut:
        eventType = FocusOutEvent;
        break;
    case EnterNotify:
        GetCrossingInfo(EnterEvent);
        break;
    case LeaveNotify:
        GetCrossingInfo(LeaveEvent);
        break;
    }
}

void Event::GetMotionInfo() {
    rep()->acknowledge_motion();

    XMotionEvent& m = rep()->xevent_.xmotion;
    eventType = MotionEvent;
    timestamp = 0;
    x  = m.x;
    y  = m.y;
    wx = m.x;
    wy = m.y;
    GetKeyState(m.state);
}

void Event::GetButtonInfo(EventType t) {
    XButtonEvent& b = rep()->xevent_.xbutton;
    eventType  = t;
    timestamp  = 0;
    x  = b.x;
    y  = b.y;
    wx = b.x;
    wy = b.y;
    button = (int)b.button - 1;
    len = 0;
    GetKeyState(b.state | (Button1Mask << button));
}

void Event::GetKeyInfo() {
    XKeyEvent& k = rep()->xevent_.xkey;

    eventType = KeyEvent;
    timestamp = 0;
    x  = k.x;
    y  = k.y;
    wx = k.x;
    wy = k.y;
    button = (int)k.keycode;

    int buflen = k.buflen;
    if (buflen > 0 && buflen < (int)sizeof(keydata)) {
        keystring = keydata;
        strncpy(keydata, k.buf, buflen);
        keydata[buflen] = '\0';
        len = buflen;
    } else if (buflen > 0) {
        keystring = new char[buflen + 1];
        strncpy(keystring, k.buf, buflen);
        keystring[buflen] = '\0';
        len = buflen;
    } else {
        keystring = keydata;
        keydata[0] = '\0';
        len = 0;
    }
    GetKeyState(k.state);
}

void Event::GetKeyState(unsigned state) {
    shift      = (state & ShiftMask)   != 0;
    control    = (state & ControlMask) != 0;
    meta       = (state & Mod1Mask)    != 0;
    shiftlock  = (state & LockMask)    != 0;
    leftmouse  = (state & Button1Mask) != 0;
    middlemouse= (state & Button2Mask) != 0;
    rightmouse = (state & Button3Mask) != 0;
}

void Event::GetCrossingInfo(EventType t) {
    XCrossingEvent& c = rep()->xevent_.xcrossing;
    eventType = t;
    timestamp = 0;
    x  = c.x;
    y  = c.y;
    wx = c.x;
    wy = c.y;
    GetKeyState(c.state);
}

void Event::GetAbsolute(_lib_iv2_6(Coord)& absx, _lib_iv2_6(Coord)& absy) {
    absx = wx;
    absy = (rep()->display_ ? rep()->display_->pheight() : 0) - wy;
}

void Event::GetAbsolute(
    World*& wd, _lib_iv2_6(Coord)& absx, _lib_iv2_6(Coord)& absy)
{
    wd   = w;
    absx = wx;
    absy = (rep()->display_ ? rep()->display_->pheight() : 0) - wy;
}
