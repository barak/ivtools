/*
 * GTK4 backend: Selection management.
 * Replaces IV-X11/xselection.cc.
 *
 * Uses GDK4's GdkClipboard API for clipboard/selection operations.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <InterViews/display.h>
#include <InterViews/selection.h>
#include <InterViews/session.h>
#include <InterViews/window.h>
#include <IV-GTK4/gdklib.h>
#include <IV-GTK4/gdkdisplay.h>
#include <IV-GTK4/gtkselection.h>
#include <IV-GTK4/gtkwindow.h>
#include <OS/string.h>
#include <string.h>

/* ================================================================== */
/* SelectionManagerRep                                                 */
/* ================================================================== */

SelectionManagerRep::SelectionManagerRep(Display* d, const String& name)
: clipboard_(nullptr), owner_(nullptr),
  convert_(nullptr), lose_(nullptr), done_(nullptr), ok_(nullptr), fail_(nullptr)
{
    name_ = new CopyString(name);

    GdkDisplay* gdpy = d ? d->rep()->display_ : gdk_display_get_default();
    if (name == "PRIMARY") {
        clipboard_ = gdk_display_get_primary_clipboard(gdpy);
    } else {
        clipboard_ = gdk_display_get_clipboard(gdpy);
    }
    if (clipboard_) g_object_ref(clipboard_);
    xdisplay_ = gdpy;
    memset(&x_req_, 0, sizeof(x_req_));
}

SelectionManagerRep::~SelectionManagerRep() {
    if (clipboard_) { g_object_unref(clipboard_); clipboard_ = nullptr; }
    delete name_;
    Resource::unref(convert_);
    Resource::unref(lose_);
    Resource::unref(done_);
    Resource::unref(ok_);
    Resource::unref(fail_);
    delete owner_;
}

/* ================================================================== */
/* SelectionManager                                                    */
/* ================================================================== */

SelectionManager::SelectionManager(Display* d, const char* name) {
    rep_ = new SelectionManagerRep(d, String(name ? name : "CLIPBOARD"));
}

SelectionManager::SelectionManager(Display* d, const String& name) {
    rep_ = new SelectionManagerRep(d, name);
}

SelectionManager::~SelectionManager() {
    delete rep_;
}

SelectionManagerRep* SelectionManager::rep() const { return rep_; }

void SelectionManager::own(
    SelectionHandler* convert,
    SelectionHandler* lose,
    SelectionHandler* done)
{
    SelectionManagerRep& r = *rep_;
    Resource::ref(convert);
    Resource::unref(r.convert_);
    r.convert_ = convert;
    Resource::ref(lose);
    Resource::unref(r.lose_);
    r.lose_ = lose;
    Resource::ref(done);
    Resource::unref(r.done_);
    r.done_ = done;
}

void SelectionManager::put_value(const void* data, int len, int format)
{
    SelectionManagerRep& r = *rep_;
    if (!r.clipboard_) return;
    if (data && len > 0) {
        gchar* text = g_strndup((const gchar*)data, (gsize)len);
        gdk_clipboard_set_text(r.clipboard_, text);
        g_free(text);
    }
    (void)format;
}

void SelectionManager::retrieve(
    const String& /*type*/,
    SelectionHandler* ok,
    SelectionHandler* fail)
{
    SelectionManagerRep& r = *rep_;
    Resource::ref(ok);
    Resource::unref(r.ok_);
    r.ok_ = ok;
    Resource::ref(fail);
    Resource::unref(r.fail_);
    r.fail_ = fail;
}

void SelectionManager::get_value(
    String*& /*type*/, void*& data, int& length, int& format)
{
    /* Synchronous clipboard read is not supported in GTK4 - return empty */
    data   = nullptr;
    length = 0;
    format = 8;
}

void SelectionManagerRep::request(SelectionManager*, const XEvent& /*xe*/) {
    /* In GTK4 there are no SelectionRequest events; handled by GDK */
}

void SelectionManagerRep::notify(SelectionManager*, const XEvent& /*xe*/) {
    /* In GTK4 there are no SelectionNotify events; handled by GDK */
}

/* class SelectionHandler */

SelectionHandler::SelectionHandler() {}
SelectionHandler::~SelectionHandler() {}
