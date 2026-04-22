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
 * GTK4-dependent window and display implementation.
 * Replaces IV-X11/xwindow.cc.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "wtable.h"
#include <InterViews/bitmap.h>
#include <InterViews/canvas.h>
#include <InterViews/color.h>
#include <InterViews/cursor.h>
#include <InterViews/display.h>
#include <InterViews/event.h>
#include <InterViews/handler.h>
#include <InterViews/glyph.h>
#include <InterViews/hit.h>
#include <InterViews/selection.h>
#include <InterViews/session.h>
#include <InterViews/style.h>
#include <InterViews/window.h>
#include <IV-GTK4/gdklib.h>
#include <IV-GTK4/gdkdefs.h>
#include <IV-GTK4/gdkutil.h>
#include <IV-GTK4/gdkbitmap.h>
#include <IV-GTK4/gtkcanvas.h>
#include <IV-GTK4/gdkcursor.h>
#include <IV-GTK4/gdkdisplay.h>
#include <IV-GTK4/gdkevent.h>
#include <IV-GTK4/gtkselection.h>
#include <IV-GTK4/gtkwindow.h>
#include <OS/host.h>
#include <OS/list.h>
#include <OS/math.h>
#include <OS/string.h>
#include <OS/table.h>
#include <iostream.h>

using std::cerr;

implementTable(WindowTable, GtkWidgetKey, Window*)
implementPtrList(WindowVisualList, WindowVisual)

declarePtrList(WindowCursorStack, Cursor)
implementPtrList(WindowCursorStack, Cursor)

/* ------------------------------------------------------------------ */
/* Forward declarations for GTK4 signal callbacks                      */
/* ------------------------------------------------------------------ */

static void on_draw(GtkDrawingArea*, cairo_t*, int w, int h, gpointer data);
static void on_resize(GtkWidget*, int w, int h, gpointer data);
static gboolean on_key_pressed(GtkEventControllerKey*, guint keyval,
                                guint keycode, GdkModifierType state,
                                gpointer data);
static gboolean on_key_released(GtkEventControllerKey*, guint keyval,
                                 guint keycode, GdkModifierType state,
                                 gpointer data);
static void on_button_pressed(GtkGestureClick*, int n_press,
                              double x, double y, gpointer data);
static void on_button_released(GtkGestureClick*, int n_press,
                               double x, double y, gpointer data);
static void on_motion(GtkEventControllerMotion*, double x, double y,
                      gpointer data);
static void on_enter(GtkEventControllerMotion*, double x, double y,
                     gpointer data);
static void on_leave(GtkEventControllerMotion*, gpointer data);
static void on_focus_in(GtkEventControllerFocus*, gpointer data);
static void on_focus_out(GtkEventControllerFocus*, gpointer data);
static gboolean on_close_request(GtkWindow*, gpointer data);

/* ------------------------------------------------------------------ */
/* Helper: dispatch a synthesized event to the InterViews machinery   */
/* ------------------------------------------------------------------ */

static void dispatch_event(Window* window, IVGdkEvent& xev)
{
    if (!window) return;
    Display* d = window->display();
    if (!d) return;

    Event e;
    EventRep* er = e.rep();
    er->xevent_ = xev;
    er->display_ = d;
    er->window_ = window;
    er->clear();

    window->receive(e);
    Handler* h = e.handler();
    if (h) {
        boolean b = Resource::defer(true);
        h->ref();
        h->event(e);
        h->unref();
        Resource::flush();
        Resource::defer(b);
    }
}

/* ------------------------------------------------------------------ */
/* Helper: attach all event controllers to a drawing area             */
/* ------------------------------------------------------------------ */

static void attach_controllers(GtkWidget* area, Window* window)
{
    /* Key events */
    GtkEventController* key = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(key, GTK_PHASE_CAPTURE);
    g_signal_connect(key, "key-pressed",  G_CALLBACK(on_key_pressed),  window);
    g_signal_connect(key, "key-released", G_CALLBACK(on_key_released), window);
    gtk_widget_add_controller(area, key);

    /* Button events */
    GtkGesture* click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0); /* all buttons */
    g_signal_connect(click, "pressed",  G_CALLBACK(on_button_pressed),  window);
    g_signal_connect(click, "released", G_CALLBACK(on_button_released), window);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(click));

    /* Motion / enter / leave */
    GtkEventController* motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_motion), window);
    g_signal_connect(motion, "enter",  G_CALLBACK(on_enter),  window);
    g_signal_connect(motion, "leave",  G_CALLBACK(on_leave),  window);
    gtk_widget_add_controller(area, motion);

    /* Focus */
    GtkEventController* focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "enter", G_CALLBACK(on_focus_in),  window);
    g_signal_connect(focus, "leave", G_CALLBACK(on_focus_out), window);
    gtk_widget_add_controller(area, focus);
}

/* ================================================================== */
/* class Window                                                        */
/* ================================================================== */

Window::Window(Glyph* g) {
    WindowRep* w = new WindowRep;
    rep_ = w;
    w->glyph_ = g;
    w->glyph_->ref();
    w->style_ = nil;
    w->display_ = nil;
    w->visual_ = nil;
    w->left_ = 0;
    w->bottom_ = 0;
    w->focus_in_ = nil;
    w->focus_out_ = nil;
    w->wm_delete_ = nil;
    w->widget_ = nullptr;
    w->gtkwindow_ = nullptr;
    w->toplevel_widget_ = nullptr;
    w->xpos_ = 0;
    w->ypos_ = 0;
    w->placed_ = false;
    w->aligned_ = false;
    w->override_redirect_ = false;
    w->clear_mapping_info();
    w->cursor_ = defaultCursor;
    w->cursor_stack_ = new WindowCursorStack;
    w->toplevel_ = this;
    w->canvas_ = new Canvas;
    w->canvas_->rep()->window_ = this;
}

Window::Window(WindowRep* w) {
    rep_ = w;
}

Window::~Window() {
    Window::unbind();
    WindowRep& w = *rep();
    Resource::unref_deferred(w.glyph_);
    Resource::unref_deferred(w.style_);
    Resource::unref_deferred(w.focus_in_);
    Resource::unref_deferred(w.focus_out_);
    Resource::unref_deferred(w.wm_delete_);
    delete w.canvas_;
    delete w.cursor_stack_;
    delete rep_;
    rep_ = nil;
}

Glyph* Window::glyph() const { return rep()->glyph_; }

void Window::style(Style* s) {
    WindowRep& w = *rep();
    if (w.style_ != s) {
        Resource::ref(s);
        Resource::unref(w.style_);
        w.style_ = s;
        w.check_binding(this);
    }
}

Style* Window::style() const { return rep()->style_; }

void Window::display(Display* d) {
    WindowRep& w = *rep();
    if (w.display_ != d) {
        w.check_binding(this);
        w.display_ = d;
        w.canvas_->rep()->display_ = d;
    }
}

Display* Window::display() const { return rep()->display_; }

Canvas* Window::canvas() const { return rep()->canvas_; }

void Window::cursor(Cursor* c) {
    WindowRep& w = *rep();
    if (w.cursor_ != c) {
        w.check_binding(this);
        w.cursor_ = c;
        GtkWidget* widget = w.widget_;
        if (widget) {
            if (c == nil) {
                gtk_widget_set_cursor(widget, nullptr);
            } else {
                GdkCursor* gdk_cur = c->rep()->xid(w.display_, w.visual_);
                gtk_widget_set_cursor(widget, gdk_cur);
            }
        }
    }
}

Cursor* Window::cursor() const { return rep()->cursor_; }

void Window::push_cursor() {
    WindowRep& w = *rep();
    w.cursor_stack_->prepend(w.cursor_);
}

void Window::pop_cursor() {
    WindowRep& w = *rep();
    WindowCursorStack& s = *w.cursor_stack_;
    if (s.count() > 0) {
        cursor(s.item(0));
        s.remove(0);
    }
}

void Window::place(Coord left, Coord bottom) {
    WindowRep& w = *rep();
    if (!w.placed_ ||
        !Math::equal(left, w.left_, float(1e-3)) ||
        !Math::equal(bottom, w.bottom_, float(1e-3)))
    {
        w.check_binding(this);
        w.placed_ = true;
        w.left_ = left;
        w.bottom_ = bottom;
    }
}

void Window::pplace(IntCoord pleft, IntCoord pbottom) {
    WindowRep& w = *rep();
    w.check_binding(this);
    w.placed_ = true;
    Display& d = *w.display_;
    w.left_ = d.to_coord(pleft);
    w.bottom_ = d.to_coord(pbottom);
}

void Window::align(float x, float y) {
    WindowRep& w = *rep();
    if (!w.aligned_ ||
        !Math::equal(x, w.xalign_, float(1e-3)) ||
        !Math::equal(y, w.yalign_, float(1e-3)))
    {
        w.check_binding(this);
        w.aligned_ = true;
        w.xalign_ = x;
        w.yalign_ = y;
    }
}

Coord Window::left() const {
    WindowRep& w = *rep();
    Display* d = w.display_;
    if (!d) return w.left_;
    w.check_position(this);
    return d->to_coord(w.xpos_);
}

Coord Window::bottom() const {
    WindowRep& w = *rep();
    Display* d = w.display_;
    if (!d) return w.bottom_;
    w.check_position(this);
    return d->height() - d->to_coord(w.ypos_) - height();
}

Coord Window::width() const  { return rep()->canvas_->width(); }
Coord Window::height() const { return rep()->canvas_->height(); }

void Window::map() {
    WindowRep& w = *rep();
    if (w.map_pending_ || is_mapped()) return;

    w.unmapped_ = false;
    if (bound()) {
        GtkWidgetKey key = widget_to_key(w.widget_);
        w.display_->rep()->wtable_->insert(key, this);
    } else {
        unbind();
        if (!w.display_) display(Session::instance()->default_display());
        if (!w.style_)   style(new Style(w.display_->style()));
        configure();
        default_geometry();
        compute_geometry();
        bind();
        set_props();
    }
    do_map();
}

void Window::configure() { }

void Window::default_geometry() {
    WindowRep& w = *rep();
    const Display& d = *w.display_;
    w.glyph_->request(w.shape_);
    Coord cwidth  = w.shape_.requirement(Dimension_X).natural();
    Coord cheight = w.shape_.requirement(Dimension_Y).natural();
    w.canvas_->size(cwidth, cheight);
    w.xpos_ = d.to_pixels(w.left_);
    w.ypos_ = d.pheight() - d.to_pixels(w.bottom_) - w.canvas_->pheight();
    if (w.aligned_) {
        w.xpos_ -= d.to_pixels(w.xalign_ * cwidth);
        w.ypos_ += d.to_pixels(w.yalign_ * cheight);
    }
}

void Window::compute_geometry() { }

void Window::bind() {
    WindowRep& w = *rep();
    w.do_bind(this, nullptr, w.xpos_, w.ypos_);
}

void Window::unbind() {
    WindowRep& w = *rep();
    Display* d = w.display_;
    if (d && w.widget_) {
        DisplayRep& r = *d->rep();
        GtkWidgetKey key = widget_to_key(w.widget_);
        r.wtable_->remove(key);
        r.remove(this);
        if (w.toplevel_ == this) {
            w.glyph_->undraw();
            if (w.gtkwindow_) {
                gtk_window_destroy(GTK_WINDOW(w.gtkwindow_));
                w.gtkwindow_ = nullptr;
            }
        }
        w.widget_ = nullptr;
    }
    w.clear_mapping_info();
    CanvasRep& c = *w.canvas_->rep();
    c.unbind();
    c.clear_damage();
}

boolean Window::bound() const {
    WindowRep& w = *rep();
    if (!w.widget_) return false;
    if (w.toplevel_ == this) return true;
    /* check that the top-level window is still registered */
    GtkWidgetKey top_key = widget_to_key(w.toplevel_widget_);
    Window* toplevel_found;
    if (w.display_ &&
        w.display_->rep()->wtable_->find(toplevel_found, top_key) &&
        toplevel_found == w.toplevel_)
    {
        return true;
    }
    return false;
}

void Window::set_attributes() {
    WindowRep& w = *rep();
    if (!w.visual_) {
        w.visual_ = WindowVisual::find_visual(w.display_, w.style_);
    }
    /* GTK4 has no XSetWindowAttributes equivalent; attributes are set via
       GTK API calls at bind/map time. */
}

void Window::set_props() { }

void Window::do_map() {
    WindowRep& w = *rep();
    if (w.gtkwindow_) {
        gtk_window_present(GTK_WINDOW(w.gtkwindow_));
        w.map_pending_ = true;
    }
}

void Window::unmap() {
    WindowRep& w = *rep();
    if (!w.map_pending_ && !is_mapped()) return;
    DisplayRep& d = *w.display_->rep();
    w.glyph_->undraw();
    if (w.widget_) gtk_widget_hide(w.widget_);
    GtkWidgetKey key = widget_to_key(w.widget_);
    d.wtable_->remove(key);
    d.remove(this);
    w.canvas_->rep()->clear_damage();
    w.unmapped_ = true;
    w.wm_mapped_ = false;
    w.map_pending_ = false;
}

boolean Window::is_mapped() const { return rep()->wm_mapped_; }

void Window::receive(const Event& e) {
    WindowRep& w = *rep();
    const XEvent& xe = e.rep()->xevent_;
    Handler* handler = nil;
    SelectionManager* s;
    switch (xe.type) {
    case MapNotify:
        w.map_notify(this, const_cast<XMapEvent&>(xe));
        break;
    case UnmapNotify:
        w.unmap_notify(this, const_cast<XUnmapEvent&>(xe));
        break;
    case Expose:
        w.expose(this, const_cast<XExposeEvent&>(xe));
        break;
    case ConfigureNotify:
        w.configure_notify(this, const_cast<XConfigureEvent&>(xe));
        break;
    case MotionNotify:
        e.rep()->acknowledge_motion();
        break;
    case FocusIn:
        handler = w.focus_in_;
        break;
    case FocusOut:
        handler = w.focus_out_;
        break;
    case ClientMessage:
        if (xe.xclient.message_type == w.wm_protocols_atom() &&
            (Atom)xe.xclient.data.l[0] == w.wm_delete_atom())
        {
            handler = w.wm_delete_;
            if (!handler) Session::instance()->quit();
        }
        break;
    case SelectionRequest:
        s = w.display_->primary_selection();
        s->rep()->request(s, xe);
        break;
    case SelectionNotify:
        s = w.display_->primary_selection();
        s->rep()->notify(s, xe);
        break;
    }
    if (handler) {
        Event writable_e(e);
        handler->event(writable_e);
    }
}

Handler* Window::target(const Event& e) const {
    EventRep& r = *e.rep();
    const XEvent& xe = r.xevent_;
    if (xe.type == LeaveNotify || !r.has_pointer_location()) return nil;

    WindowRep& w = *rep();
    Hit hit(&e);
    w.glyph_->pick(w.canvas_, w.allocation_, 0, hit);
    Handler* h = hit.handler();
    if (h && (e.grabber() == nil || e.type() == Event::key ||
              e.is_grabbing(h)))
    {
        return h;
    }
    return nil;
}

void Window::grab_pointer(Cursor* c) const {
    WindowRep& w = *rep();
    if (!w.widget_) return;
    GdkDisplay* gdpy = gdk_widget_get_display(w.widget_);
    GdkSeat* seat = gdk_display_get_default_seat(gdpy);
    GdkCursor* gdk_cur = nullptr;
    if (c) gdk_cur = c->rep()->xid(w.display_, w.visual_);
    GdkSurface* surface = gtk_native_get_surface(
        gtk_widget_get_native(w.widget_));
    if (surface) {
        gdk_seat_grab(seat, surface,
                      GDK_SEAT_CAPABILITY_ALL_POINTING,
                      FALSE, gdk_cur, nullptr, nullptr, nullptr);
    }
}

void Window::ungrab_pointer() const {
    WindowRep& w = *rep();
    if (!w.widget_) return;
    GdkDisplay* gdpy = gdk_widget_get_display(w.widget_);
    GdkSeat* seat = gdk_display_get_default_seat(gdpy);
    gdk_seat_ungrab(seat);
}

void Window::repair() {
    WindowRep& w = *rep();
    CanvasRep& c = *w.canvas_->rep();
    if (c.start_repair()) {
        w.glyph_->draw(w.canvas_, w.allocation_);
        c.finish_repair();
    }
}

void Window::raise() {
    WindowRep& w = *rep();
    if (w.gtkwindow_) gtk_window_present(GTK_WINDOW(w.gtkwindow_));
}

void Window::lower() {
    /* GTK4/Wayland does not support explicit z-order lowering */
}

void Window::move(Coord left, Coord bottom) {
    /* Wayland does not allow applications to set window position.
       On X11-backed GDK we could use gdk_x11_surface_move(); for now
       we record the intended position and let the WM decide. */
    WindowRep& w = *rep();
    Display& d = *w.display_;
    w.xpos_ = d.to_pixels(left);
    w.ypos_ = d.pheight() - d.to_pixels(bottom) - w.canvas_->pheight();
}

void Window::resize() {
    WindowRep& w = *rep();
    CanvasRep& c = *w.canvas_->rep();
    if (w.gtkwindow_) {
        gtk_window_set_default_size(GTK_WINDOW(w.gtkwindow_),
                                    c.pwidth_, c.pheight_);
    }
    w.needs_resize_ = true;
}

void Window::offset_from_toplevel(PixelCoord& dx, PixelCoord& dy) {
    dx = 0;
    dy = 0;
    /* In GTK4 child widgets are positioned by layout managers, not via
       coordinate translation.  Return (0,0) as a safe default. */
}

/* ================================================================== */
/* class ManagedWindow                                                 */
/* ================================================================== */

ManagedWindow::ManagedWindow(Glyph* g) : Window(g) {
    ManagedWindowRep* w = new ManagedWindowRep;
    rep_ = w;
    w->group_leader_   = nil;
    w->transient_for_  = nil;
    w->icon_           = nil;
    w->icon_bitmap_    = nil;
    w->icon_mask_      = nil;
}

ManagedWindow::~ManagedWindow() {
    ManagedWindowRep* w = rep_;
    Resource::unref(w->icon_bitmap_);
    Resource::unref(w->icon_mask_);
    delete w;
}

ManagedWindow* ManagedWindow::icon()        const { return rep()->icon_; }
Bitmap* ManagedWindow::icon_bitmap()        const { return rep()->icon_bitmap_; }
Bitmap* ManagedWindow::icon_mask()          const { return rep()->icon_mask_; }

void ManagedWindow::icon(ManagedWindow* i) {
    rep()->icon_ = i;
    rep()->do_set(this, &ManagedWindowRep::set_icon);
}

void ManagedWindow::icon_bitmap(Bitmap* b) {
    ManagedWindowRep& w = *rep();
    Resource::ref(b);
    Resource::unref(w.icon_bitmap_);
    w.icon_bitmap_ = b;
    w.do_set(this, &ManagedWindowRep::set_icon_bitmap);
}

void ManagedWindow::icon_mask(Bitmap* b) {
    ManagedWindowRep& w = *rep();
    Resource::ref(b);
    Resource::unref(w.icon_mask_);
    w.icon_mask_ = b;
    w.do_set(this, &ManagedWindowRep::set_icon_mask);
}

void ManagedWindow::iconify() {
    /* GTK4 does not expose a direct iconify API.
       gtk_window_minimize() (GTK ≥ 4.2) is a best-effort hint. */
    WindowRep& w = *Window::rep();
    if (w.gtkwindow_) gtk_window_minimize(GTK_WINDOW(w.gtkwindow_));
}

void ManagedWindow::deiconify() {
    WindowRep& w = *Window::rep();
    if (w.gtkwindow_) gtk_window_unminimize(GTK_WINDOW(w.gtkwindow_));
}

void ManagedWindow::resize() {
    default_geometry();
    ManagedWindowRep& w = *rep();
    w.wm_normal_hints(this);
    Window::resize();
}

void ManagedWindow::focus_event(Handler* in, Handler* out) {
    WindowRep& w = *Window::rep();
    Resource::ref(in);
    Resource::ref(out);
    Resource::unref(w.focus_in_);
    Resource::unref(w.focus_out_);
    w.focus_in_  = in;
    w.focus_out_ = out;
}

void ManagedWindow::wm_delete(Handler* h) {
    WindowRep& w = *Window::rep();
    Resource::ref(h);
    Resource::unref(w.wm_delete_);
    w.wm_delete_ = h;
}

void ManagedWindow::compute_geometry() {
    WindowRep& wr = *Window::rep();
    CanvasRep& c  = *wr.canvas_->rep();
    Display& d    = *wr.display_;
    if (c.pwidth_ <= 0) {
        c.width_  = 72;
        c.pwidth_ = d.to_pixels(c.width_);
    }
    if (c.pheight_ <= 0) {
        c.height_  = 72;
        c.pheight_ = d.to_pixels(c.height_);
    }
}

void ManagedWindow::set_props() {
    ManagedWindowRep& w = *rep();
    w.wm_normal_hints(this);
    w.wm_name(this);
    w.wm_class(this);
    w.wm_protocols(this);
    w.wm_colormap_windows(this);
    w.wm_hints(this);
}

/* ================================================================== */
/* class ApplicationWindow                                             */
/* ================================================================== */

ApplicationWindow::ApplicationWindow(Glyph* g)
: ManagedWindow(g)
{
    _otherdisplay = nil;
}

ApplicationWindow::ApplicationWindow(Glyph* g, const char* display)
: ManagedWindow(g)
{
    _otherdisplay = display;
}

ApplicationWindow::~ApplicationWindow() { }

void ApplicationWindow::compute_geometry() {
    WindowRep& wr = *Window::rep();
    CanvasRep& c  = *wr.canvas_->rep();
    Display& d    = *wr.display_;
    unsigned int spec = 0;
    String v;
    if (wr.style_ && wr.style_->find_attribute("geometry", v)) {
        NullTerminatedString g(v);
        unsigned int xw = 0, xh = 0;
        spec = XParseGeometry(g.string(), &wr.xpos_, &wr.ypos_, &xw, &xh);
        const unsigned int userplace = XValue | YValue;
        if ((spec & userplace) == userplace) wr.placed_ = true;
        if (spec & WidthValue)  { c.pwidth_  = PixelCoord(xw); c.width_  = d.to_coord(c.pwidth_); }
        if (spec & HeightValue) { c.pheight_ = PixelCoord(xh); c.height_ = d.to_coord(c.pheight_); }
        if ((spec & XValue) && (spec & XNegative)) wr.xpos_ = d.pwidth()  + wr.xpos_ - c.pwidth_;
        if ((spec & YValue) && (spec & YNegative)) wr.ypos_ = d.pheight() + wr.ypos_ - c.pheight_;
    }
    ManagedWindow::compute_geometry();
}

void ApplicationWindow::set_props() {
    ManagedWindow::set_props();
}

/* ================================================================== */
/* class TopLevelWindow                                                */
/* ================================================================== */

TopLevelWindow::TopLevelWindow(Glyph* g) : ManagedWindow(g) { }
TopLevelWindow::~TopLevelWindow() { }

void TopLevelWindow::group_leader(Window* primary) {
    ManagedWindowRep& w = *rep();
    w.group_leader_ = primary;
    w.do_set(this, &ManagedWindowRep::set_group_leader);
}

Window* TopLevelWindow::group_leader() const { return rep()->group_leader_; }

void TopLevelWindow::set_props() { ManagedWindow::set_props(); }

/* ================================================================== */
/* class TransientWindow                                               */
/* ================================================================== */

TransientWindow::TransientWindow(Glyph* g) : TopLevelWindow(g) { }
TransientWindow::~TransientWindow() { }

void TransientWindow::transient_for(Window* primary) {
    ManagedWindowRep& w = *rep();
    w.transient_for_ = primary;
    w.do_set(this, &ManagedWindowRep::set_transient_for);
}

Window* TransientWindow::transient_for() const { return rep()->transient_for_; }

void TransientWindow::configure() { Window::configure(); }

void TransientWindow::set_attributes() {
    Style& s = *style();
    s.alias("TransientWindow");
    TopLevelWindow::set_attributes();
}

/* ================================================================== */
/* class PopupWindow                                                   */
/* ================================================================== */

PopupWindow::PopupWindow(Glyph* g) : Window(g) { }
PopupWindow::~PopupWindow() { }

void PopupWindow::set_attributes() {
    Style& s = *style();
    s.alias("PopupWindow");
    Window::set_attributes();
    rep()->override_redirect_ = true;
}

/* ================================================================== */
/* class IconWindow                                                    */
/* ================================================================== */

IconWindow::IconWindow(Glyph* g) : ManagedWindow(g) { }
IconWindow::~IconWindow() { }

void IconWindow::do_map() { /* icons are not shown as separate windows */ }

/* ================================================================== */
/* class WindowRep                                                     */
/* ================================================================== */

Atom WindowRep::wm_delete_atom_    = nullptr;
Atom WindowRep::wm_protocols_atom_ = nullptr;

Atom WindowRep::wm_delete_atom() {
    if (!wm_delete_atom_) wm_delete_atom_ = "WM_DELETE_WINDOW";
    return wm_delete_atom_;
}

Atom WindowRep::wm_protocols_atom() {
    if (!wm_protocols_atom_) wm_protocols_atom_ = "WM_PROTOCOLS";
    return wm_protocols_atom_;
}

void WindowRep::clear_mapping_info() {
    toplevel_widget_ = nullptr;
    needs_resize_ = false;
    resized_      = false;
    moved_        = false;
    unmapped_     = false;
    wm_mapped_    = false;
    map_pending_  = false;
}

void WindowRep::map_notify(Window*, XMapEvent&) {
    needs_resize_ = true;
    wm_mapped_    = true;
    map_pending_  = false;
    canvas_->rep()->status_ = Canvas::mapped;
}

void WindowRep::unmap_notify(Window*, XUnmapEvent&) {
    glyph_->undraw();
    wm_mapped_ = false;
    canvas_->rep()->status_ = Canvas::unmapped;
}

void WindowRep::expose(Window* w, XExposeEvent& xe) {
    unsigned int pw = canvas_->pwidth();
    unsigned int ph = canvas_->pheight();
    if (needs_resize_) {
        needs_resize_ = false;
        resize(w, pw, ph);
    } else {
        Display* d = display_;
        Coord l = d->to_coord(xe.xexpose.x);
        Coord r = l + d->to_coord(xe.xexpose.width);
        Coord t = d->to_coord(ph - xe.xexpose.y);
        Coord b = t - d->to_coord(xe.xexpose.height);
        canvas_->redraw(l, b, r, t);
    }
}

void WindowRep::configure_notify(Window* w, XConfigureEvent& xe) {
    moved_ = true;
    if (resized_) {
        if ((unsigned)xe.xconfigure.width  != canvas_->pwidth() ||
            (unsigned)xe.xconfigure.height != canvas_->pheight())
        {
            resize(w, xe.xconfigure.width, xe.xconfigure.height);
        }
    } else {
        canvas_->psize(xe.xconfigure.width, xe.xconfigure.height);
        needs_resize_ = true;
    }
}

void WindowRep::move(Window*, int x, int y) {
    xpos_  = x;
    ypos_  = y;
    moved_ = false;
}

void WindowRep::resize(Window* w, unsigned int xwidth, unsigned int xheight) {
    canvas_->psize(xwidth, xheight);
    canvas_->damage_all();
    const Requirement& rx = shape_.requirement(Dimension_X);
    const Requirement& ry = shape_.requirement(Dimension_Y);
    Coord xsize = canvas_->width();
    Coord ysize = canvas_->height();
    Coord ox = xsize * rx.alignment();
    Coord oy = ysize * ry.alignment();
    allocation_.allot(Dimension_X, Allotment(ox, xsize, ox / xsize));
    allocation_.allot(Dimension_Y, Allotment(oy, ysize, oy / ysize));
    Extension ext;
    ext.clear();
    init_renderer(w);
    if (resized_) glyph_->undraw();
    glyph_->allocate(canvas_, allocation_, ext);
    resized_ = true;
}

void WindowRep::check_position(const Window*) {
    /* GTK4 does not expose surface coordinates programmatically on Wayland.
       On X11-backed GDK we could query via gdk_x11_surface_get_xid().
       For now we keep the last-known position from configure events. */
    moved_ = false;
}

void WindowRep::check_binding(Window* w) {
    if (unmapped_) w->unbind();
}

/*
 * do_bind(): create the GTK4 window + drawing area hierarchy and register
 * the new widget in the window table.
 */
void WindowRep::do_bind(Window* w, GtkWidget* /*parent*/, int left, int top) {
    CanvasRep& c  = *canvas_->rep();
    DisplayRep& d = *display_->rep();
    WindowTable& t = *d.wtable_;

    /* Remove old registration if re-binding */
    if (widget_) {
        GtkWidgetKey old_key = widget_to_key(widget_);
        t.remove(old_key);
    }

    w->set_attributes();

    /* Create the top-level window shell if this is a top-level window */
    if (toplevel_ == w) {
        GtkWidget* win = gtk_window_new();
        gtkwindow_ = win;
        widget_ = win;

        /* Title */
        if (style_) {
            String v;
            if (style_->find_attribute("name", v) ||
                style_->find_attribute("title", v))
            {
                NullTerminatedString ns(v);
                gtk_window_set_title(GTK_WINDOW(win), ns.string());
            }
        }

        /* For override-redirect windows (popups), use a plain GtkPopover or
           GtkPopoverMenu equivalent.  GTK4 provides GtkPopup semantics via
           GtkPopover; for simplicity we fall back to a decorated GtkWindow
           with no title bar for now. */
        if (override_redirect_) {
            gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
            /* Popups need to be kept on top */
            gtk_window_set_transient_for(GTK_WINDOW(win), nullptr);
        }

        /* Default size from canvas */
        if (c.pwidth_ > 0 && c.pheight_ > 0) {
            gtk_window_set_default_size(GTK_WINDOW(win), c.pwidth_, c.pheight_);
        }

        /* Position hint (Wayland ignores it; X11-backed GDK honours it) */
        if (placed_) {
            /* Best effort: we cannot force position on Wayland */
            (void)left; (void)top;
        }

        /* Create drawing area as the window's content */
        GtkWidget* da = gtk_drawing_area_new();
        gtk_window_set_child(GTK_WINDOW(win), da);
        gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(da),  c.pwidth_);
        gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(da), c.pheight_);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da), on_draw, w, nullptr);

        /* GTK4 resize: listen to size-allocate on the drawing area */
        g_signal_connect(da, "resize", G_CALLBACK(on_resize), w);

        /* Attach input event controllers */
        attach_controllers(da, w);
        gtk_widget_set_focusable(da, TRUE);
        gtk_widget_set_can_focus(da, TRUE);

        /* Close-request (replaces WM_DELETE_WINDOW) */
        g_signal_connect(win, "close-request", G_CALLBACK(on_close_request), w);

        /* Register the drawing area widget in the table */
        GtkWidgetKey key = widget_to_key(da);
        t.insert(key, w);

        /* Update internal pointers */
        widget_          = da;   /* use the drawing area as the primary widget */
        toplevel_widget_ = da;
    } else {
        /* Sub-window: create a drawing area inside the parent's container.
           GTK4 compositing means we do not have sub-window XID hierarchy.
           For simplicity, treat as a floating overlay widget.
           This is sufficient for popups that are children of a window. */
        GtkWidget* da = gtk_drawing_area_new();
        gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(da),  c.pwidth_);
        gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(da), c.pheight_);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da), on_draw, w, nullptr);
        g_signal_connect(da, "resize", G_CALLBACK(on_resize), w);
        attach_controllers(da, w);
        gtk_widget_set_focusable(da, TRUE);

        widget_ = da;
        if (toplevel_->rep()->gtkwindow_) {
            toplevel_widget_ = toplevel_->rep()->widget_;
        }

        GtkWidgetKey key = widget_to_key(da);
        t.insert(key, w);
    }
}

void WindowRep::init_renderer(Window* w) {
    CanvasRep& c = *w->canvas()->rep();
    c.unbind();
    c.bind(style_ && style_->value_is_on("double_buffered"));
}

Window* WindowRep::find(GtkWidget* widget, WindowTable* t) {
    Window* window;
    GtkWidgetKey key = widget_to_key(widget);
    if (t->find(window, key)) {
        WindowRep& w = *window->rep();
        if (w.toplevel_ == window) return window;
        /* Check the top-level is still in the table */
        GtkWidgetKey top_key = widget_to_key(w.toplevel_widget_);
        Window* toplevel;
        if (t->find(toplevel, top_key) && toplevel == w.toplevel_) return window;
    }
    return nil;
}

/* ================================================================== */
/* GTK4 signal callbacks                                               */
/* ================================================================== */

static void on_draw(GtkDrawingArea*, cairo_t* cr, int w, int h, gpointer data)
{
    Window* win = static_cast<Window*>(data);
    if (!win) return;
    CanvasRep* c = win->canvas()->rep();
    if (!c) return;

    /* Expose: synthesize and dispatch, then blit our offscreen surface */
    IVGdkEvent xe;
    memset(&xe, 0, sizeof(xe));
    xe.type = Expose;
    xe.xexpose.x = 0;
    xe.xexpose.y = 0;
    xe.xexpose.width  = w;
    xe.xexpose.height = h;

    /* Store the GTK4 draw context so finish_repair() can blit to it */
    c->widget_cr_ = cr;
    dispatch_event(win, xe);
    c->widget_cr_ = nullptr;
}

static void on_resize(GtkWidget* widget, int w, int h, gpointer data)
{
    Window* win = static_cast<Window*>(data);
    if (!win) return;

    IVGdkEvent xe;
    memset(&xe, 0, sizeof(xe));
    xe.type = ConfigureNotify;
    xe.xconfigure.x      = 0;
    xe.xconfigure.y      = 0;
    xe.xconfigure.width  = w;
    xe.xconfigure.height = h;

    /* Resize the underlying Cairo surface to match */
    CanvasRep* c = win->canvas()->rep();
    if (c && c->surface_) {
        /* unbind/rebind to reallocate surfaces at the new size */
        WindowRep& wr = *win->rep();
        wr.resize(win, (unsigned)w, (unsigned)h);
        return; /* resize already calls init_renderer which calls bind/unbind */
    }
    dispatch_event(win, xe);
}

static gboolean on_key_pressed(GtkEventControllerKey* ctrl,
                                guint keyval, guint keycode,
                                GdkModifierType state, gpointer data)
{
    Window* win = static_cast<Window*>(data);
    if (!win) return FALSE;

    IVGdkEvent xe;
    memset(&xe, 0, sizeof(xe));
    xe.type = KeyPress;
    xe.xkey.keycode = keycode;
    xe.xkey.state   = (unsigned int)state;
    xe.xkey.keysym  = (unsigned long)keyval;
    /* Convert keyval to ASCII if possible */
    if (keyval < 0x100) {
        xe.xkey.buf[0] = (char)keyval;
        xe.xkey.buflen = 1;
    }
    xe.time = GDK_CURRENT_TIME;

    dispatch_event(win, xe);
    return TRUE;
}

static gboolean on_key_released(GtkEventControllerKey*, guint keyval,
                                  guint keycode, GdkModifierType state,
                                  gpointer data)
{
    Window* win = static_cast<Window*>(data);
    if (!win) return FALSE;

    IVGdkEvent xe;
    memset(&xe, 0, sizeof(xe));
    xe.type = KeyRelease;
    xe.xkey.keycode = keycode;
    xe.xkey.state   = (unsigned int)state;
    xe.xkey.keysym  = (unsigned long)keyval;
    xe.time = GDK_CURRENT_TIME;

    dispatch_event(win, xe);
    return FALSE;
}

static void on_button_pressed(GtkGestureClick* gesture, int /*n_press*/,
                               double x, double y, gpointer data)
{
    Window* win = static_cast<Window*>(data);
    if (!win) return;
    guint button = gtk_gesture_single_get_current_button(
        GTK_GESTURE_SINGLE(gesture));

    GdkModifierType state = GDK_NO_MODIFIER_MASK;
    GdkEvent* gdk_ev = gtk_gesture_get_last_event(GTK_GESTURE(gesture), nullptr);
    if (gdk_ev) state = gdk_event_get_modifier_state(gdk_ev);

    IVGdkEvent xe;
    memset(&xe, 0, sizeof(xe));
    xe.type = ButtonPress;
    xe.xbutton.x      = (int)x;
    xe.xbutton.y      = (int)y;
    xe.xbutton.button = button;
    xe.xbutton.state  = (unsigned int)state;
    xe.time = GDK_CURRENT_TIME;

    dispatch_event(win, xe);
}

static void on_button_released(GtkGestureClick* gesture, int /*n_press*/,
                                double x, double y, gpointer data)
{
    Window* win = static_cast<Window*>(data);
    if (!win) return;
    guint button = gtk_gesture_single_get_current_button(
        GTK_GESTURE_SINGLE(gesture));

    GdkModifierType state = GDK_NO_MODIFIER_MASK;
    GdkEvent* gdk_ev = gtk_gesture_get_last_event(GTK_GESTURE(gesture), nullptr);
    if (gdk_ev) state = gdk_event_get_modifier_state(gdk_ev);

    IVGdkEvent xe;
    memset(&xe, 0, sizeof(xe));
    xe.type = ButtonRelease;
    xe.xbutton.x      = (int)x;
    xe.xbutton.y      = (int)y;
    xe.xbutton.button = button;
    xe.xbutton.state  = (unsigned int)state;
    xe.time = GDK_CURRENT_TIME;

    dispatch_event(win, xe);
}

static void on_motion(GtkEventControllerMotion* ctrl, double x, double y,
                       gpointer data)
{
    Window* win = static_cast<Window*>(data);
    if (!win) return;

    GdkModifierType state = GDK_NO_MODIFIER_MASK;
    GdkEvent* gdk_ev = gtk_event_controller_get_current_event(
        GTK_EVENT_CONTROLLER(ctrl));
    if (gdk_ev) state = gdk_event_get_modifier_state(gdk_ev);

    IVGdkEvent xe;
    memset(&xe, 0, sizeof(xe));
    xe.type = MotionNotify;
    xe.xmotion.x     = (int)x;
    xe.xmotion.y     = (int)y;
    xe.xmotion.state = (unsigned int)state;
    xe.time = GDK_CURRENT_TIME;

    dispatch_event(win, xe);
}

static void on_enter(GtkEventControllerMotion*, double x, double y,
                      gpointer data)
{
    Window* win = static_cast<Window*>(data);
    if (!win) return;

    IVGdkEvent xe;
    memset(&xe, 0, sizeof(xe));
    xe.type = EnterNotify;
    xe.xcrossing.x = (int)x;
    xe.xcrossing.y = (int)y;
    dispatch_event(win, xe);
}

static void on_leave(GtkEventControllerMotion*, gpointer data)
{
    Window* win = static_cast<Window*>(data);
    if (!win) return;

    IVGdkEvent xe;
    memset(&xe, 0, sizeof(xe));
    xe.type = LeaveNotify;
    dispatch_event(win, xe);
}

static void on_focus_in(GtkEventControllerFocus*, gpointer data)
{
    Window* win = static_cast<Window*>(data);
    if (!win) return;

    /* Post a synthetic MapNotify first if not yet reported as mapped */
    WindowRep& wr = *win->rep();
    if (!wr.wm_mapped_) {
        IVGdkEvent map_xe;
        memset(&map_xe, 0, sizeof(map_xe));
        map_xe.type = MapNotify;
        dispatch_event(win, map_xe);
    }

    IVGdkEvent xe;
    memset(&xe, 0, sizeof(xe));
    xe.type = FocusIn;
    dispatch_event(win, xe);
}

static void on_focus_out(GtkEventControllerFocus*, gpointer data)
{
    Window* win = static_cast<Window*>(data);
    if (!win) return;

    IVGdkEvent xe;
    memset(&xe, 0, sizeof(xe));
    xe.type = FocusOut;
    dispatch_event(win, xe);
}

static gboolean on_close_request(GtkWindow*, gpointer data)
{
    Window* win = static_cast<Window*>(data);
    if (!win) return FALSE;

    WindowRep& wr = *win->rep();
    IVGdkEvent xe;
    memset(&xe, 0, sizeof(xe));
    xe.type = ClientMessage;
    xe.xclient.message_type = wr.wm_protocols_atom();
    xe.xclient.format = 32;
    xe.xclient.data.l[0] = (long)(intptr_t)wr.wm_delete_atom();

    dispatch_event(win, xe);
    /* Return TRUE to prevent GTK from destroying the window immediately;
       the application's wm_delete handler (or Session::quit()) will do it. */
    return TRUE;
}

/* ================================================================== */
/* class ManagedWindowRep                                              */
/* ================================================================== */

void ManagedWindowRep::do_set(Window* window, HintFunction f) {
    WindowRep& w = *window->rep();
    ManagedWindowHintInfo info;
    info.widget_   = w.widget_;
    if (info.widget_) {
        info.style_   = w.style_;
        info.dpy_     = gdk_display_get_default();
        info.pwidth_  = w.canvas_->pwidth();
        info.pheight_ = w.canvas_->pheight();
        info.display_ = w.display_;
        info.hints_   = nullptr;
        (this->*f)(info);
    }
}

boolean ManagedWindowRep::set_name(ManagedWindowHintInfo& info) {
    if (info.style_) {
        Style& s = *info.style_;
        String v;
        if (s.find_attribute("name", v) || s.find_attribute("title", v)) {
            NullTerminatedString ns(v);
            GtkWidget* gtkwin = gtk_widget_get_ancestor(
                info.widget_, GTK_TYPE_WINDOW);
            if (gtkwin) gtk_window_set_title(GTK_WINDOW(gtkwin), ns.string());
        }
    }
    return false;
}

boolean ManagedWindowRep::set_geometry(ManagedWindowHintInfo&) { return false; }

boolean ManagedWindowRep::set_group_leader(ManagedWindowHintInfo& info) {
    if (group_leader_ && group_leader_->rep()->gtkwindow_) {
        GtkWidget* gtkwin = gtk_widget_get_ancestor(
            info.widget_, GTK_TYPE_WINDOW);
        if (gtkwin) {
            gtk_window_set_transient_for(
                GTK_WINDOW(gtkwin),
                GTK_WINDOW(group_leader_->rep()->gtkwindow_));
        }
    }
    return false;
}

boolean ManagedWindowRep::set_transient_for(ManagedWindowHintInfo& info) {
    if (transient_for_ && transient_for_->rep()->gtkwindow_) {
        GtkWidget* gtkwin = gtk_widget_get_ancestor(
            info.widget_, GTK_TYPE_WINDOW);
        if (gtkwin) {
            gtk_window_set_transient_for(
                GTK_WINDOW(gtkwin),
                GTK_WINDOW(transient_for_->rep()->gtkwindow_));
        }
    }
    return false;
}

boolean ManagedWindowRep::set_icon_name(ManagedWindowHintInfo&) { return false; }
boolean ManagedWindowRep::set_icon_geometry(ManagedWindowHintInfo&) { return false; }
boolean ManagedWindowRep::set_icon(ManagedWindowHintInfo&) { return false; }
boolean ManagedWindowRep::set_icon_bitmap(ManagedWindowHintInfo&) { return false; }
boolean ManagedWindowRep::set_icon_mask(ManagedWindowHintInfo&) { return false; }

boolean ManagedWindowRep::set_all(ManagedWindowHintInfo& info) {
    set_name(info);
    set_geometry(info);
    set_group_leader(info);
    set_transient_for(info);
    set_icon_name(info);
    set_icon_geometry(info);
    set_icon(info);
    set_icon_bitmap(info);
    set_icon_mask(info);
    return true;
}

void ManagedWindowRep::wm_normal_hints(Window* window) {
    WindowRep& w = *window->rep();
    if (!w.gtkwindow_) return;

    const Display& d  = *w.display_;
    Requirement& rx = w.shape_.requirement(Dimension_X);
    Requirement& ry = w.shape_.requirement(Dimension_Y);

    const Coord smallest  = d.to_coord(2);
    const Coord x_largest = d.width();
    const Coord y_largest = d.height();

    int min_w = d.to_pixels(Math::min(x_largest, Math::max(smallest,
                                rx.natural() - rx.shrink())));
    int min_h = d.to_pixels(Math::min(y_largest, Math::max(smallest,
                                ry.natural() - ry.shrink())));
    Coord max_width  = Math::max(smallest, rx.natural() + rx.stretch());
    Coord max_height = Math::max(smallest, ry.natural() + ry.stretch());

    gtk_widget_set_size_request(w.widget_, min_w, min_h);

    /* Maximum size hint via GdkGeometry */
    if (max_width < x_largest || max_height < y_largest) {
        GdkGeometry geom;
        geom.max_width  = d.to_pixels(Math::min(max_width,  x_largest));
        geom.max_height = d.to_pixels(Math::min(max_height, y_largest));
        geom.min_width  = min_w;
        geom.min_height = min_h;
        gtk_window_set_geometry_hints(GTK_WINDOW(w.gtkwindow_),
                                      nullptr, &geom,
                                      GDK_HINT_MIN_SIZE | GDK_HINT_MAX_SIZE);
    }
}

void ManagedWindowRep::wm_name(Window* window) {
    WindowRep& w = *window->rep();
    Style* s = w.style_;
    if (!s) return;
    String v;
    if (!s->find_attribute("name", v) && !s->find_attribute("title", v)) {
        s->attribute("name", Session::instance()->name());
    }
    if (w.gtkwindow_) {
        NullTerminatedString ns(v);
        gtk_window_set_title(GTK_WINDOW(w.gtkwindow_), ns.string());
    }
}

void ManagedWindowRep::wm_class(Window* window) {
    WindowRep& w = *window->rep();
    if (!w.gtkwindow_) return;
    Style* s = w.style_;
    String v("Noname");
    if (s && !s->find_attribute("name", v)) s->find_attribute("title", v);
    /* GTK4 uses GApplication ID for the WM class; set it as title fallback */
    NullTerminatedString ns(v);
    gtk_window_set_title(GTK_WINDOW(w.gtkwindow_), ns.string());
}

void ManagedWindowRep::wm_protocols(Window* window) {
    /* Protocols are handled via the close-request signal in do_bind(). */
    (void)window;
}

void ManagedWindowRep::wm_colormap_windows(Window*) { /* not applicable */ }

void ManagedWindowRep::wm_hints(Window* window) {
    do_set(window, &ManagedWindowRep::set_all);
}

/* ================================================================== */
/* class WindowVisual                                                  */
/* ================================================================== */

WindowVisual::WindowVisual(const WindowVisualInfo& info) : info_(info), xor_(0) { }

WindowVisual::~WindowVisual() { }

WindowVisual* WindowVisual::find_visual(Display* d, Style*) {
    WindowVisualInfo info;
    info.display_ = d ? d->rep()->display_ : gdk_display_get_default();
    info.screen_  = 0;
    info.depth_   = 24; /* assume 24-bit colour */
    return new WindowVisual(info);
}

void WindowVisual::init_color_tables() { }

void WindowVisual::find_color(unsigned long, XColor& xc) {
    /* Cannot reverse-map a pixel value to RGB without a colormap.
       Return white as a safe fallback. */
    xc.red = xc.green = xc.blue = 0xffff;
    xc.pixel = 0;
}

void WindowVisual::find_color(unsigned short r, unsigned short g, unsigned short b,
                               XColor& xc)
{
    xc.red   = r;
    xc.green = g;
    xc.blue  = b;
    xc.pixel = 0; /* unused in GTK4 */
}

unsigned long WindowVisual::x_or(const Style& s) const { return x_or_(s); }

unsigned long WindowVisual::x_or_(const Style&) const {
    /* In X11 this returned the XOR colour to flip between fg/bg.
       In GTK4 we use CAIRO_OPERATOR_XOR; return 0 as a placeholder. */
    return 0;
}

/* ================================================================== */
/* class DisplayRep                                                    */
/* ================================================================== */

/* (DisplayRep::init and the rest of Display are implemented in
   the InterViews/session.cc and InterViews/display.cc which call
   Display::open() → DisplayRep::init().  We implement init() here.) */

void DisplayRep::init(GdkDisplay* dpy) {
    display_ = dpy;
    screen_  = 0;
    root_    = nullptr; /* no root surface concept on Wayland */

    /* Physical screen dimensions */
    GdkMonitor* monitor = gdk_display_get_monitor_at_surface(dpy, nullptr);
    if (!monitor) {
        GListModel* monitors = gdk_display_get_monitors(dpy);
        if (g_list_model_get_n_items(monitors) > 0)
            monitor = GDK_MONITOR(g_list_model_get_item(monitors, 0));
    }

    if (monitor) {
        GdkRectangle geom;
        gdk_monitor_get_geometry(monitor, &geom);
        pwidth_  = (unsigned int)geom.width;
        pheight_ = (unsigned int)geom.height;
    } else {
        pwidth_  = 1280;
        pheight_ = 1024;
    }

    /* Resolution: assume 96 dpi unless monitor provides physical size */
    double dpi = 96.0;
    if (monitor) {
        int width_mm  = gdk_monitor_get_width_mm(monitor);
        int height_mm = gdk_monitor_get_height_mm(monitor);
        if (width_mm > 0 && pwidth_ > 0) {
            dpi = (double)pwidth_ * 25.4 / (double)width_mm;
        }
    }
    set_dpi(dpi > 0 ? *(Coord*)&dpi : *(Coord*)nullptr);

    width_  = to_coord(pwidth_);
    height_ = to_coord(pheight_);

    wtable_     = new WindowTable(256);
    grabbers_   = nullptr;
    damaged_    = nullptr;
    selections_ = nullptr;
    style_      = nullptr;

    /* Create the default WindowVisual */
    WindowVisualInfo vi;
    vi.display_ = dpy;
    vi.screen_  = 0;
    vi.depth_   = 24;
    default_visual_ = new WindowVisual(vi);
    visuals_.append(default_visual_);
}

void DisplayRep::set_dpi(Coord& /*dpi*/) {
    /* dpi is configured via Display::set_dpi() in display.cc */
}

void DisplayRep::needs_repair(Window* w) {
    /* Trigger a GTK queue-draw on the window's drawing area */
    GtkWidget* da = w->rep()->widget_;
    if (da) gtk_widget_queue_draw(da);
}

void DisplayRep::remove(Window* w) {
    GtkWidget* da = w->rep()->widget_;
    if (!da) return;
    GtkWidgetKey key = widget_to_key(da);
    if (wtable_) wtable_->remove(key);
}
