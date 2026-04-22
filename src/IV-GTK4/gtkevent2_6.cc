/*
 * GTK4 backend: IV-2.6 event compatibility layer.
 * Replaces IV-2_6/xevent2_6.cc.
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

/* X11 event mask constants (numeric values) used by Sensor::mask */
typedef unsigned long Mask;
Mask motionmask  = 0x0040L;  /* PointerMotionMask */
Mask keymask     = 0x0001L;  /* KeyPressMask      */
Mask entermask   = 0x0010L;  /* EnterWindowMask   */
Mask leavemask   = 0x0020L;  /* LeaveWindowMask   */
Mask focusmask   = 0x0200L;  /* FocusChangeMask   */
Mask upmask      = 0x0008L;  /* ButtonReleaseMask */
Mask downmask    = 0x0004L;  /* ButtonPressMask   */
Mask initmask    = 0x0080L;  /* PointerMotionHintMask */

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
    XEvent& xe = rep()->xevent_;
    eventType = MotionEvent;
    timestamp = 0;
    x  = xe.xmotion.x;
    y  = xe.xmotion.y;
    wx = xe.xmotion.x;
    wy = xe.xmotion.y;
    GetKeyState(xe.xmotion.state);
}

void Event::GetButtonInfo(EventType t) {
    XEvent& xe = rep()->xevent_;
    eventType = t;
    timestamp = 0;
    x  = xe.xbutton.x;
    y  = xe.xbutton.y;
    wx = xe.xbutton.x;
    wy = xe.xbutton.y;
    button = (int)xe.xbutton.button - 1;
    len = 0;
    GetKeyState(xe.xbutton.state | (Button1Mask << button));
}

void Event::GetKeyInfo() {
    XEvent& xe = rep()->xevent_;
    eventType = KeyEvent;
    timestamp = 0;
    x  = 0;
    y  = 0;
    wx = 0;
    wy = 0;
    button = (int)xe.xkey.keycode;

    int buflen = xe.xkey.buflen;
    if (buflen > 0 && buflen < (int)sizeof(keydata)) {
        keystring = keydata;
        strncpy(keydata, xe.xkey.buf, buflen);
        keydata[buflen] = '\0';
        len = buflen;
    } else if (buflen > 0) {
        keystring = new char[buflen + 1];
        strncpy(keystring, xe.xkey.buf, buflen);
        keystring[buflen] = '\0';
        len = buflen;
    } else {
        keystring = keydata;
        keydata[0] = '\0';
        len = 0;
    }
    GetKeyState(xe.xkey.state);
}

void Event::GetKeyState(unsigned state) {
    shift       = (state & ShiftMask)   != 0;
    control     = (state & ControlMask) != 0;
    meta        = (state & Mod1Mask)    != 0;
    shiftlock   = (state & LockMask)    != 0;
    leftmouse   = (state & Button1Mask) != 0;
    middlemouse = (state & Button2Mask) != 0;
    rightmouse  = (state & Button3Mask) != 0;
}

void Event::GetCrossingInfo(EventType t) {
    XEvent& xe = rep()->xevent_;
    eventType = t;
    timestamp = 0;
    x  = xe.xcrossing.x;
    y  = xe.xcrossing.y;
    wx = xe.xcrossing.x;
    wy = xe.xcrossing.y;
    GetKeyState(xe.xcrossing.state);
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
