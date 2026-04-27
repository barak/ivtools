/*
 * GTK4 backend: IV-2.6 Interactor/Window/Scene/World compatibility layer.
 * Replaces IV-2_6/xinter.cc.
 *
 * The Window/Canvas placement operations that were done via X11 calls
 * (XMoveResizeWindow, XMapRaised, etc.) are adapted to GTK4.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "wtable.h"
#include <InterViews/canvas.h>
#include <InterViews/cursor.h>
#include <InterViews/display.h>
#include <InterViews/event.h>
#include <InterViews/font.h>
#include <InterViews/handler.h>
#include <InterViews/hit.h>
#include <InterViews/style.h>
#include <IV-2_6/InterViews/ihandler.h>
#include <IV-2_6/InterViews/interactor.h>
#include <IV-2_6/InterViews/iwindow.h>
#include <IV-2_6/InterViews/painter.h>
#include <IV-2_6/InterViews/scene.h>
#include <IV-2_6/InterViews/sensor.h>
#include <IV-2_6/InterViews/shape.h>
#include <IV-2_6/InterViews/world.h>
#include <IV-GTK4/gdklib.h>
#include <IV-GTK4/gdkdefs.h>
#include <IV-GTK4/gtkcanvas.h>
#include <IV-GTK4/gtkwindow.h>
#include <IV-GTK4/gdkevent.h>
#include <IV-GTK4/gdkdisplay.h>
#include <OS/math.h>

boolean Interactor::ValidCanvas(Canvas* c) {
    boolean b = false;
    if (c != nil) {
        Window* w = c->window();
        if (w != nil) {
            b = w->bound();
        }
    }
    return b;
}

void Interactor::request(Requisition& r) const {
    if (output_ == nil) {
        Interactor* i = (Interactor*)this;
        i->Config(World::current());
    }
    Display* d = GetWorld()->display();
    float align = 0.0;
    Requirement rx(
        d->to_coord(shape->width),
        d->to_coord(shape->hstretch), d->to_coord(shape->hshrink), align
    );
    Requirement ry(
        d->to_coord(shape->height),
        d->to_coord(shape->vstretch), d->to_coord(shape->vshrink), align
    );
    r.require(Dimension_X, rx);
    r.require(Dimension_Y, ry);
}

void Interactor::allocate(Canvas* c, const Allocation& a, Extension& ext) {
    ext.set(c, a);
}

void Interactor::draw(Canvas* c, const Allocation& a) const {
    Interactor* i = (Interactor*)this;
    const Allotment& ax = a.allotment(Dimension_X);
    const Allotment& ay = a.allotment(Dimension_Y);
    Coord width  = ax.span();
    Coord height = ay.span();
    unsigned int pwidth  = c->to_pixels(width);
    unsigned int pheight = c->to_pixels(height);
    int x0 = c->to_pixels(ax.origin());
    int y0 = c->rep()->pheight_ - c->to_pixels(ay.origin()) - (int)pheight;

    if (window != nil && window->bound()) {
        CanvasRep& cr = *canvas->rep();
        WindowRep& wr = *window->Window::rep();
        if (x0 != wr.xpos_ || y0 != wr.ypos_ ||
            cr.pwidth_ != pwidth || cr.pheight_ != pheight)
        {
            cr.width_   = width;
            cr.height_  = height;
            cr.pwidth_  = pwidth;
            cr.pheight_ = pheight;
            cr.status_  = Canvas::unmapped;
            wr.xpos_ = x0;
            wr.ypos_ = y0;
            Allotment& w_ax = wr.allocation_.x_allotment();
            w_ax.origin(0.0); w_ax.span(width); w_ax.alignment(0.0);
            Allotment& w_ay = wr.allocation_.y_allotment();
            w_ay.origin(0.0); w_ay.span(height); w_ay.alignment(0.0);
            if (wr.widget_) {
                gtk_widget_set_size_request(wr.widget_,
                    (int)pwidth, (int)pheight);
                gtk_widget_queue_allocate(wr.widget_);
            }
            /* Recreate the drawing context at the new position/size on the
               (possibly reallocated) top-level backing surface. */
            wr.init_renderer(i->window);
            i->xmax = (int)pwidth  - 1;
            i->ymax = (int)pheight - 1;
            i->Resize();
        }
        if (cr.status_ == Canvas::unmapped) {
            if (wr.widget_) gtk_widget_set_visible(wr.widget_, TRUE);
            cr.status_ = Canvas::mapped;
        }
        return;
    }
    Window* cw = c->window();
    Display* d = cw->rep()->display_;
    delete i->window;
    i->window = new InteractorWindow(i, cw);
    i->window->display(d);
    style->attribute("double_buffered", "false");
    style->attribute("overlay", "false");
    i->window->style(style);
    i->canvas = window->canvas();

    CanvasRep& cr = *i->canvas->rep();
    cr.width_   = width;
    cr.height_  = height;
    cr.pwidth_  = pwidth;
    cr.pheight_ = pheight;

    WindowRep& w = *i->window->Window::rep();
    w.xpos_ = x0;
    w.ypos_ = y0;
    Allotment& w_ax = w.allocation_.x_allotment();
    w_ax.origin(0.0); w_ax.span(width); w_ax.alignment(0.0);
    Allotment& w_ay = w.allocation_.y_allotment();
    w_ay.origin(0.0); w_ay.span(height); w_ay.alignment(0.0);

    i->window->bind();
    i->xmax = (int)pwidth  - 1;
    i->ymax = (int)pheight - 1;
    cr.status_ = Canvas::mapped;
    i->Resize();
    if (w.widget_) gtk_widget_set_visible(w.widget_, TRUE);
}

void Interactor::undraw() {
    if (window != nil) {
        WindowRep& w = *window->rep();
        if (w.widget_ != nullptr) {
            if (window->bound()) {
                gtk_widget_set_visible(w.widget_, FALSE);
                canvas->rep()->status_ = Canvas::unmapped;
            } else {
                window->unbind();
            }
        }
    }
}

Handler* InteractorWindow::target(const Event& e) const {
    if (!e.rep()->has_pointer_location()) {
        return nil;
    }
    WindowRep* w = Window::rep();
    Hit h(&e);
    w->glyph_->pick(w->canvas_, w->allocation_, 0, h);
    return h.handler();
}

static boolean grabbing = false;

void Interactor::pick(Canvas*, const Allocation& a, int depth, Hit& h) {
    const Event* ep = h.event();
    if ((ep != nil && parent != nil) ||
        (h.left() < a.right() && h.right() >= a.left() &&
         h.bottom() < a.top() && h.top() >= a.bottom()))
    {
        Event& e = *(Event*)ep;
        e.GetInfo();
        Sensor* s = cursensor == nil ? input_ : cursensor;
        if ((s != nil && s->Caught(e)) || grabbing) {
            e.target = this;
            e.y = ymax - e.y;
            if (e.eventType == DownEvent) {
                grabbing = true;
            } else if (e.eventType == UpEvent) {
                grabbing = false;
            }
            h.target(depth, this, 0, handler_);
        }
    }
}

InteractorHandler::InteractorHandler(Interactor* i) {
    interactor_ = i;
}

InteractorHandler::~InteractorHandler() { }

boolean InteractorHandler::event(Event& e) {
    Interactor* i = interactor_;
    XEvent& xe = e.rep()->xevent_;
    switch (xe.type) {
    case FocusIn:
        e.eventType = FocusInEvent;
        break;
    case FocusOut:
        e.eventType = FocusOutEvent;
        break;
    default:
        break;
    }
    Sensor* s = i->cursensor == nil ? i->input_ : i->cursensor;
    if (s != nil && s->Caught(e)) {
        i->Handle(e);
    }
    return true;
}

void Interactor::Listen(Sensor* s) {
    cursensor = s;
    /* GTK4 event filtering is handled through event controllers */
}

int Interactor::CheckQueue() {
    /* No X11 event queue in GTK4; return 0 */
    return 0;
}

void Interactor::Poll(Event& e) {
    e.window(nil);
    e.poll();
    auto& m = e.rep()->xevent_.xmotion;
    e.w = World::current();
    e.wx = m.x;
    e.wy = m.y;
    e.GetKeyState(m.state);
    IntCoord x, y;
    GetPosition(x, y);
    e.x = m.x - x;
    e.y = (e.display() ? e.display()->pheight() : 0) - 1 - m.y - y;
}

void Interactor::GetPosition(IntCoord& left, IntCoord& bottom) const {
    if (window == nil) {
        left = 32767;
        bottom = 32767;
        return;
    }
    WindowRep* w = window->rep();
    left   = w->xpos_;
    bottom = (w->display_ ? w->display_->pheight() : 0)
             - w->ypos_ - (int)window->canvas()->pheight();
}

InteractorWindow::InteractorWindow(Interactor* i) : Window(i) {
    interactor_ = i;
    parent_ = nil;
}

InteractorWindow::InteractorWindow(Interactor* i, Window* w) : Window(i) {
    interactor_ = i;
    parent_ = w;
}

InteractorWindow::~InteractorWindow() { }

void InteractorWindow::set_attributes() {
    /* In GTK4 window attributes are configured through the widget API */
    Window::set_attributes();
}

void InteractorWindow::bind() {
    if (parent_ == nil) {
        if (interactor_->parent != nil)
            parent_ = interactor_->parent->window;
    }
    WindowRep& w = *Window::rep();
    if (parent_ != nil) {
        /* Sub-window: share the top-level's Cairo backing surface rather
           than creating an orphaned GtkWidget that would never be rendered.
           Populate only the fields that bound(), init_renderer() and
           needs_repair() rely on; skip do_bind() entirely. */
        WindowRep& pw = *parent_->Window::rep();
        w.toplevel_        = pw.toplevel_;
        w.display_         = pw.display_;
        w.canvas_->rep()->display_ = pw.display_;
        w.toplevel_widget_ = pw.toplevel_widget_;
        w.visual_          = pw.visual_;
        if (!w.style_)
            w.style_ = pw.style_;
        w.parent_window_   = parent_;
        /* Sub-windows have no GtkWidget of their own. */
        w.widget_          = nullptr;
        w.gtkwindow_       = nullptr;
    } else {
        w.do_bind(this, nullptr, w.xpos_, w.ypos_);
    }
    w.init_renderer(this);
}

void InteractorWindow::unbind() {
    Window::unbind();
    interactor_->Orphan();
}

void InteractorWindow::receive(const Event& e) {
    int ymax = canvas()->pheight() - 1;
    XEvent& xe = e.rep()->xevent_;
    switch (xe.type) {
    case Expose: {
        int itop = ymax - xe.xexpose.y;
        interactor_->Redraw(
            xe.xexpose.x, itop - xe.xexpose.height + 1,
            xe.xexpose.x + xe.xexpose.width - 1, itop
        );
        break;
    }
    case MotionNotify:
        e.rep()->acknowledge_motion();
        break;
    }
}

/* ================================================================ */
/* Helpers                                                           */
/* ================================================================ */

static void AlignPosition(Window* w, Alignment a) {
    float xalign = 0.0, yalign = 0.0;
    boolean needs_align = true;
    switch (a) {
    case BottomRight:
    case Right:
        xalign = 1.0; break;
    case Top:
    case TopLeft:
        yalign = 1.0; break;
    case BottomCenter:
    case HorizCenter:
        xalign = 0.5; break;
    case CenterLeft:
    case VertCenter:
        yalign = 0.5; break;
    case TopCenter:
        xalign = 0.5; yalign = 1.0; break;
    case TopRight:
        xalign = 1.0; yalign = 1.0; break;
    case _lib_iv2_6(Center):
        xalign = 0.5; yalign = 0.5; break;
    case CenterRight:
        xalign = 1.0; yalign = 0.5; break;
    case Bottom:
    case Left:
    case BottomLeft:
        needs_align = false; break;
    }
    if (needs_align) {
        w->align(xalign, yalign);
    }
}

/* ================================================================ */
/* World::Insert* methods                                            */
/* ================================================================ */

void World::InsertApplication(Interactor* i) {
    delete i->insert_window;
    ApplicationWindow* w = new ApplicationWindow(i);
    i->insert_window = w;
    i->managed_window = w;
    w->display(display_);
    w->map();
    Handler* h = i->handler_;
    w->focus_event(h, h);
}

void World::InsertApplication(
    Interactor* i, IntCoord left, IntCoord bottom, Alignment a)
{
    delete i->insert_window;
    ApplicationWindow* w = new ApplicationWindow(i);
    i->insert_window = w;
    i->managed_window = w;
    w->display(display_);
    w->pplace(left, bottom);
    AlignPosition(w, a);
    w->map();
    Handler* h = i->handler_;
    w->focus_event(h, h);
}

void World::InsertToplevel(Interactor* i, Interactor* leader) {
    delete i->insert_window;
    TopLevelWindow* w = new TopLevelWindow(i);
    i->insert_window = w;
    i->managed_window = w;
    w->display(display_);
    Window* g = (leader == i) ? (Window*)w : leader->window;
    w->group_leader(g);
    w->map();
    Handler* h = i->handler_;
    w->focus_event(h, h);
}

void World::InsertToplevel(
    Interactor* i, Interactor* leader,
    IntCoord left, IntCoord bottom, Alignment a)
{
    delete i->insert_window;
    TopLevelWindow* w = new TopLevelWindow(i);
    i->insert_window = w;
    i->managed_window = w;
    w->display(display_);
    w->pplace(left, bottom);
    AlignPosition(w, a);
    Window* g = (leader == i) ? (Window*)w : leader->window;
    w->group_leader(g);
    w->map();
    Handler* h = i->handler_;
    w->focus_event(h, h);
}

class InteractorPopupWindow : public Window {
public:
    InteractorPopupWindow(Glyph*);
    virtual ~InteractorPopupWindow();
protected:
    virtual void set_attributes();
};

InteractorPopupWindow::InteractorPopupWindow(Glyph* g) : Window(g) { }
InteractorPopupWindow::~InteractorPopupWindow() { }

void InteractorPopupWindow::set_attributes() {
    Window::set_attributes();
    WindowRep& w = *rep();
    w.override_redirect_ = true;
}

void World::InsertPopup(Interactor* i) {
    delete i->insert_window;
    Window* w = new InteractorPopupWindow(i);
    i->insert_window = w;
    i->managed_window = nil;
    w->display(display_);
    w->map();
}

void World::InsertPopup(
    Interactor* i, IntCoord left, IntCoord bottom, Alignment a)
{
    delete i->insert_window;
    Window* w = new InteractorPopupWindow(i);
    i->insert_window = w;
    i->managed_window = nil;
    w->display(display_);
    w->pplace(left, bottom);
    AlignPosition(w, a);
    w->map();
}

void World::InsertTransient(Interactor* i, Interactor* primary) {
    delete i->insert_window;
    TransientWindow* w = new TransientWindow(i);
    i->insert_window = w;
    i->managed_window = w;
    w->display(display_);
    Window* pw = (primary == i) ? (Window*)w : primary->managed_window;
    w->group_leader(pw);
    w->transient_for(pw);
    w->map();
    Handler* h = i->handler_;
    w->focus_event(h, h);
}

void World::InsertTransient(
    Interactor* i, Interactor* primary,
    IntCoord left, IntCoord bottom, Alignment a)
{
    delete i->insert_window;
    TransientWindow* w = new TransientWindow(i);
    i->insert_window = w;
    i->managed_window = w;
    w->display(display_);
    w->pplace(left, bottom);
    AlignPosition(w, a);
    Window* pw = (primary == i) ? (Window*)w : primary->managed_window;
    w->group_leader(pw);
    w->transient_for(pw);
    w->map();
    Handler* h = i->handler_;
    w->focus_event(h, h);
}

void World::InsertIcon(Interactor* i) {
    delete i->insert_window;
    IconWindow* w = new IconWindow(i);
    i->insert_window = w;
    i->managed_window = w;
    w->display(display_);
    w->map();
    Handler* h = i->handler_;
    w->focus_event(h, h);
}

void World::InsertIcon(
    Interactor* i, IntCoord left, IntCoord bottom, Alignment a)
{
    delete i->insert_window;
    IconWindow* w = new IconWindow(i);
    i->insert_window = w;
    i->managed_window = w;
    w->display(display_);
    w->pplace(left, bottom);
    AlignPosition(w, a);
    w->map();
    Handler* h = i->handler_;
    w->focus_event(h, h);
}

void World::Move(Interactor* i, IntCoord x, IntCoord y) {
    if (i->window != nil) {
        i->window->move(display_->to_coord(x), display_->to_coord(y));
    }
}

void World::Raise(Interactor* i) {
    if (i->window != nil) {
        i->window->raise();
    }
}

void World::Lower(Interactor* i) {
    if (i->window != nil) {
        i->window->lower();
    }
}

void World::Change(Interactor* i) {
    Window* iw = i->insert_window;
    if (iw != nil) {
        WindowRep* w = iw->rep();
        if (w->widget_ != nullptr) {
            CanvasRep* c = i->canvas->rep();
            Shape* s = i->GetShape();
            if (c->pwidth_ != (unsigned int)s->width ||
                c->pheight_ != (unsigned int)s->height)
            {
                iw->resize();
            } else {
                i->Resize();
            }
        }
    }
}

void World::Remove(Interactor* i) {
    i->parent = nil;
    if (i->insert_window != nil) {
        delete i->insert_window;
        i->insert_window = nil;
        i->managed_window = nil;
    }
    if (i->window != nil) {
        i->window->unbind();
        i->Deactivate();
    }
}

/* ================================================================ */
/* Scene operations                                                   */
/* ================================================================ */

void Scene::Place(
    Interactor* i,
    IntCoord l, IntCoord b, IntCoord r, IntCoord t,
    boolean map)
{
    IntCoord x = l;
    IntCoord y = ymax - t;
    unsigned int width  = (unsigned int)(r - l + 1);
    unsigned int height = (unsigned int)(t - b + 1);
    if (width  == 0) width  = (unsigned int)Math::round(ivinch);
    if (height == 0) height = (unsigned int)Math::round(ivinch);

    Display* d = window->display();
    InteractorWindow* iw = i->window;
    GtkWidget* old_widget = nullptr;
    if (iw != nil && iw->bound()) {
        old_widget = iw->Window::rep()->widget_;
    } else {
        iw = new InteractorWindow(i, canvas->window());
        i->window = iw;
        i->canvas = iw->canvas();
    }
    iw->display(d);
    iw->style(i->style);

    WindowRep* w = iw->Window::rep();
    CanvasRep* c = i->canvas->rep();
    w->xpos_ = x;
    w->ypos_ = y;
    c->pwidth_  = width;
    c->pheight_ = height;
    c->width_   = d->to_coord(width);
    c->height_  = d->to_coord(height);

    if (old_widget == nullptr) {
        iw->bind();
    } else {
        gtk_widget_set_size_request(old_widget, (int)width, (int)height);
        gtk_widget_queue_allocate(old_widget);
    }
    i->xmax = (int)width  - 1;
    i->ymax = (int)height - 1;
    c->status_ = Canvas::mapped;
    i->Resize();
    if (map && w->widget_) {
        gtk_widget_set_visible(w->widget_, TRUE);
    }
}

void Scene::Map(Interactor* i, boolean /*raised*/) {
    if (window == nil || !window->bound() || i->window == nil) return;
    WindowRep* w = i->window->rep();
    if (w->widget_) {
        gtk_widget_set_visible(w->widget_, TRUE);
        i->canvas->rep()->status_ = Canvas::mapped;
    }
}

void Scene::Unmap(Interactor* i) {
    if (window == nil || !window->bound() || i->window == nil) return;
    WindowRep* w = i->window->rep();
    if (w->widget_) {
        gtk_widget_set_visible(w->widget_, FALSE);
        i->canvas->rep()->status_ = Canvas::unmapped;
    }
}

void Scene::Move(Interactor* i, IntCoord x, IntCoord y, Alignment a) {
    if (window == nil || !window->bound() || i->window == nil) return;
    IntCoord ax = x, ay = y;
    DoAlign(i, a, ax, ay);
    DoMove(i, ax, ay);
    Display* d = window->rep()->display_;
    i->window->move(d->to_coord(ax), d->to_coord(ay));
}
