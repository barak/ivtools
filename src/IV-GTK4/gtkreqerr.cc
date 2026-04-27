/*
 * GTK4 backend: Request error handling.
 * Replaces IV-X11/xreqerr.cc.
 *
 * In X11, ReqErr provides a mechanism for catching X protocol errors.
 * In GTK4 there are no synchronous protocol errors; GLib/GDK signals
 * critical and warning messages via the GLib logging system.
 * We hook into g_log() to forward GLib/GDK errors to the IV ReqErr
 * chain.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <InterViews/reqerr.h>
#include <IV-GTK4/gdklib.h>
#include <string.h>

static ReqErr* errhandler;
static GLogFunc prev_log_handler;

static void glib_log_handler(const char*    log_domain,
                              GLogLevelFlags log_level,
                              const char*    message,
                              gpointer       /*user_data*/)
{
    ReqErr* r = errhandler;
    if (r && (log_level & (G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR))) {
        r->msgid   = 0;
        r->code    = (int)log_level;
        r->request = 0;
        r->detail  = 0;
        r->id      = nullptr;
        strncpy(r->message, message ? message : "(null)", sizeof(r->message) - 1);
        r->message[sizeof(r->message) - 1] = '\0';
        r->Error();
        return;  /* swallow the message after handling */
    }
    /* Pass through to the previous handler */
    if (prev_log_handler) {
        prev_log_handler(log_domain, log_level, message, nullptr);
    } else {
        g_log_default_handler(log_domain, log_level, message, nullptr);
    }
}

ReqErr::ReqErr() { }

ReqErr::~ReqErr() {
    if (errhandler == this) errhandler = nil;
}

void ReqErr::Error() { /* default: do nothing */ }

ReqErr* ReqErr::Install() {
    if (!errhandler) {
        /* Install the GLib log hook once */
        prev_log_handler = g_log_set_default_handler(glib_log_handler, nullptr);
    }
    ReqErr* r = errhandler;
    errhandler = this;
    return r;
}
