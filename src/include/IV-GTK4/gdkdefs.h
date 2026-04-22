/*
 * GTK4 backend: replaces IV-X11/Xdefs.h
 *
 * Type definitions that allow code including both GTK4 headers and
 * InterViews headers to coexist.  The Display* macro collision is
 * the most common one: X11 uses Display as a typedef, while InterViews
 * has a class named Display.  The GTK4 backend avoids this entirely
 * because GdkDisplay is a distinct name.
 */

#ifndef iv_gdkdefs_h
#define iv_gdkdefs_h

/* Pull in the core GTK4/GDK types */
#include <IV-GTK4/gdklib.h>

/*
 * GDK event type constants – mirror the X11 event type integers that
 * appear in switch statements throughout the existing codebase.
 * Actual GDK event dispatch is handled differently (signals / callbacks),
 * but we keep integer IDs here so that the EventRep event-type switch
 * can be compiled without change.
 */
enum GdkIVEventType {
    IV_GDK_NoEvent          =  0,
    IV_GDK_KeyPress         =  2,
    IV_GDK_KeyRelease       =  3,
    IV_GDK_ButtonPress      =  4,
    IV_GDK_ButtonRelease    =  5,
    IV_GDK_MotionNotify     =  6,
    IV_GDK_EnterNotify      =  7,
    IV_GDK_LeaveNotify      =  8,
    IV_GDK_FocusIn          =  9,
    IV_GDK_FocusOut         = 10,
    IV_GDK_Expose           = 12,
    IV_GDK_ConfigureNotify  = 22,
    IV_GDK_MapNotify        = 19,
    IV_GDK_UnmapNotify      = 18,
    IV_GDK_ClientMessage    = 33,
    IV_GDK_SelectionRequest = 30,
    IV_GDK_SelectionNotify  = 31,
    IV_GDK_SelectionClear   = 29,
    IV_GDK_LASTEvent        = 64
};

/* Mirror the X11 event-type constant names */
#define KeyPress         IV_GDK_KeyPress
#define KeyRelease       IV_GDK_KeyRelease
#define ButtonPress      IV_GDK_ButtonPress
#define ButtonRelease    IV_GDK_ButtonRelease
#define MotionNotify     IV_GDK_MotionNotify
#define EnterNotify      IV_GDK_EnterNotify
#define LeaveNotify      IV_GDK_LeaveNotify
#define FocusIn          IV_GDK_FocusIn
#define FocusOut         IV_GDK_FocusOut
#define Expose           IV_GDK_Expose
#define ConfigureNotify  IV_GDK_ConfigureNotify
#define MapNotify        IV_GDK_MapNotify
#define UnmapNotify      IV_GDK_UnmapNotify
#define ClientMessage    IV_GDK_ClientMessage
#define SelectionRequest IV_GDK_SelectionRequest
#define SelectionNotify  IV_GDK_SelectionNotify
#define SelectionClear   IV_GDK_SelectionClear
#define LASTEvent        IV_GDK_LASTEvent

/* Additional event types used in typestr() */
#define KeymapNotify        64
#define GraphicsExpose      65
#define NoExpose            66
#define VisibilityNotify    67
#define CreateNotify        68
#define DestroyNotify       69
#define MapRequest          70
#define ReparentNotify      71
#define ConfigureRequest    72
#define GravityNotify       73
#define ResizeRequest       74
#define CirculateNotify     75
#define CirculateRequest    76
#define PropertyNotify      77
#define ColormapNotify      78
#define MappingNotify       79
#define GenericEvent        80

/* Modifier mask bits (mirrors X11 ShiftMask, ControlMask, …) */
#define ShiftMask    GDK_SHIFT_MASK
#define LockMask     GDK_LOCK_MASK
#define ControlMask  GDK_CONTROL_MASK
#define Mod1Mask     GDK_ALT_MASK
#define Button1Mask  GDK_BUTTON1_MASK
#define Button2Mask  GDK_BUTTON2_MASK
#define Button3Mask  GDK_BUTTON3_MASK

/* Mouse-button numbers */
#define Button1  GDK_BUTTON_PRIMARY
#define Button2  GDK_BUTTON_MIDDLE
#define Button3  GDK_BUTTON_SECONDARY

/* Window class (XCreateWindow wclass argument – unused in GTK4) */
#define InputOutput  1
#define InputOnly    2

/* Cairo drawing operator that replaces GXcopy / GXxor / GXnoop */
#define GXcopy  CAIRO_OPERATOR_OVER
#define GXxor   CAIRO_OPERATOR_XOR
#define GXnoop  CAIRO_OPERATOR_DEST
#define GXand   CAIRO_OPERATOR_IN

/* AllPlanes */
#define AllPlanes  (~0UL)

/* XEvent structure replacement – EventRep holds a synthetic struct */
struct IVGdkEvent {
    int type;          /* one of IV_GDK_* above */
    GdkEvent * gdk;   /* underlying GDK event (may be nullptr) */
    /* Synthetic fields populated by gdkevent.cc */
    struct {
        int x, y;
        unsigned int state;
        unsigned int button;
    } xbutton;
    struct {
        int x, y;
        unsigned int state;
    } xmotion;
    struct {
        int x, y;
        unsigned int state;
    } xcrossing;
    struct {
        unsigned int keycode;
        unsigned int state;
        unsigned long keysym;
        char buf[32];
        int buflen;
    } xkey;
    struct {
        int x, y;
        int width, height;
    } xexpose;
    struct {
        int x, y;
        int width, height;
    } xconfigure;
    struct {
        /* for MapNotify / UnmapNotify */
        GdkSurface * window;
    } xmap, xunmap;
    struct {
        /* for ClientMessage (WM_DELETE_WINDOW) */
        Atom message_type;
        int format;
        union { long l[5]; } data;
    } xclient;
    struct {
        /* for SelectionRequest */
        GdkSurface * owner;
        Atom          selection;
        Atom          target;
        Atom          property;
    } xselectionrequest;
    struct {
        /* for SelectionNotify */
        Atom selection;
        Atom target;
        Atom property;
    } xselection;
    /* Timestamp */
    guint32 time;
};

typedef IVGdkEvent XEvent;

/* Map / Unmap event sub-types */
typedef IVGdkEvent XMapEvent;
typedef IVGdkEvent XUnmapEvent;
typedef IVGdkEvent XExposeEvent;
typedef IVGdkEvent XConfigureEvent;
typedef IVGdkEvent XKeyEvent;
typedef IVGdkEvent XButtonEvent;
typedef IVGdkEvent XMotionEvent;
typedef IVGdkEvent XCrossingEvent;
typedef IVGdkEvent XFocusChangeEvent;
typedef IVGdkEvent XSelectionRequestEvent;
typedef IVGdkEvent XSelectionEvent;
typedef IVGdkEvent XClientMessageEvent;

/* Pixmap type (1-bit surfaces in GTK4 are just cairo_surface_t*) */
typedef cairo_surface_t * Pixmap;

/* Region type (XRegion → cairo_region_t*) */
typedef cairo_region_t * Region;

/* Cursor (XCursor → GdkCursor*) */
typedef GdkCursor * XCursor;
typedef unsigned long XID;

/* Visual / depth */
struct _IVWindowVisual;  /* forward */

/* GrabMode */
#define GrabModeSync  0
#define GrabModeAsync 1

/* SubstructureNotify masks – used in iconify/deiconify; ignored in GTK4 */
#define SubstructureRedirectMask 0L
#define SubstructureNotifyMask   0L

/* IconicState / NormalState – kept for wm_hints compatibility */
#ifndef NormalState
#define NormalState 1
#define IconicState 3
#endif

/* CoordMode */
#define CoordModeOrigin 0

/* PolyShape */
#define Complex  0
#define EvenOddRule 0

/* FillStyle */
#define FillSolid     0
#define FillStippled  1

/* ZPixmap */
#define ZPixmap 2

/* PropModeReplace / XA_WM_CLIENT_MACHINE / XA_STRING – unused stubs */
#define PropModeReplace  0
#define XA_WM_CLIENT_MACHINE  "WM_CLIENT_MACHINE"
#define XA_STRING  "STRING"

/* BitmapSuccess */
#define BitmapSuccess 0

/* WhenMapped – backing store hint, ignored in GTK4 */
#define WhenMapped 1

#endif /* iv_gdkdefs_h */
