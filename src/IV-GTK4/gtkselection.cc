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

SelectionManagerRep::SelectionManagerRep(Display* d, const char* name)
: display_(d), clipboard_(nullptr), handler_(nullptr),
  owned_(false), data_(nullptr), data_len_(0)
{
    name_ = new CopyString(name ? name : "CLIPBOARD");

    GdkDisplay* gdpy = d ? d->rep()->display_ : gdk_display_get_default();
    if (strcmp(name_, "PRIMARY") == 0) {
        clipboard_ = gdk_display_get_primary_clipboard(gdpy);
    } else {
        clipboard_ = gdk_display_get_clipboard(gdpy);
    }
    /* We keep a ref to the clipboard object */
    if (clipboard_) g_object_ref(clipboard_);
}

SelectionManagerRep::~SelectionManagerRep() {
    if (clipboard_) { g_object_unref(clipboard_); clipboard_ = nullptr; }
    if (data_)      { delete[] data_; data_ = nullptr; }
    delete name_;
}

/* ================================================================== */
/* SelectionManager                                                    */
/* ================================================================== */

SelectionManager::SelectionManager(Display* d, const char* name) {
    SelectionManagerRep* r = new SelectionManagerRep(d, name);
    rep_ = r;
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
    r.handler_  = convert;
    r.lose_     = lose;
    r.done_     = done;
    r.owned_    = true;

    /* Nothing to do explicitly; ownership is implicit from setting the
       content on the clipboard below. */
}

void SelectionManager::put_value(const void* data, int len, int format)
{
    SelectionManagerRep& r = *rep_;
    if (!r.clipboard_) return;

    if (r.data_) { delete[] r.data_; r.data_ = nullptr; }
    r.data_    = new char[len];
    memcpy(r.data_, data, len);
    r.data_len_ = len;

    /* Store as plain text (the most common case).
       A proper implementation would use a ContentProvider that handles
       multiple MIME types.  For now, assume UTF-8 text. */
    GBytes* bytes = g_bytes_new(data, (gsize)len);
    gdk_clipboard_set_bytes(r.clipboard_, "text/plain;charset=utf-8", bytes);
    g_bytes_unref(bytes);
    (void)format;
}

void SelectionManager::get_value(
    const char* /*type*/, SelectionHandler* handler)
{
    SelectionManagerRep& r = *rep_;
    if (!r.clipboard_) { if (handler) handler->handle(r.display_, nullptr, 0); return; }

    /* Asynchronous GDK read — for simplicity we use a synchronous helper */
    struct ReadCtx {
        SelectionHandler* handler;
        Display* display;
    };
    ReadCtx* ctx = new ReadCtx{ handler, r.display_ };

    gdk_clipboard_read_text_async(r.clipboard_, nullptr,
        [](GObject* src, GAsyncResult* res, gpointer user_data) {
            ReadCtx* ctx = static_cast<ReadCtx*>(user_data);
            char* text = gdk_clipboard_read_text_finish(
                GDK_CLIPBOARD(src), res, nullptr);
            if (ctx->handler && text) {
                ctx->handler->handle(ctx->display_,
                                     (void*)text, strlen(text));
            }
            g_free(text);
            delete ctx;
        }, ctx);
}

void SelectionManagerRep::request(SelectionManager*, const XEvent& /*xe*/) {
    /* In GTK4 there are no SelectionRequest events; handled by GDK */
}

void SelectionManagerRep::notify(SelectionManager*, const XEvent& /*xe*/) {
    /* In GTK4 there are no SelectionNotify events; handled by GDK */
}
