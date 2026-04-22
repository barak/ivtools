/*
 * GTK4 backend: replaces IV-X11/xselection.h
 * Defines SelectionManagerRep using GdkClipboard instead of X11 selection atoms.
 */

#ifndef iv_gtkselection_h
#define iv_gtkselection_h

#include <InterViews/selection.h>
#include <IV-GTK4/gdklib.h>
#include <IV-GTK4/gdkdefs.h>

class PopupWindow;

class SelectionManagerRep {
public:
    GdkDisplay*      xdisplay_;   /* name kept for source compat */
    String*          name_;

    /*
     * x_req_ held the XSelectionRequestEvent in X11.  In GTK4 selection
     * requests are handled via GdkContentProvider callbacks; we keep a
     * stub struct for source compatibility.
     */
    XEvent           x_req_;

    PopupWindow*     owner_;
    SelectionHandler* convert_;
    SelectionHandler* lose_;
    SelectionHandler* done_;
    SelectionHandler* ok_;
    SelectionHandler* fail_;

    /* GDK clipboard backing this selection */
    GdkClipboard*    clipboard_;

    SelectionManagerRep(Display*, const String&);
    ~SelectionManagerRep();

    void request(SelectionManager*, const XEvent& req);
    void notify(SelectionManager*, const XEvent& note);
};

#endif /* iv_gtkselection_h */
