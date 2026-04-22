/*
 * GTK4 backend: replaces IV-X11/Xundefs.h
 *
 * Undoes the macro definitions from gdkdefs.h to prevent leakage
 * into code that includes IV-GTK4 headers from a controlled scope.
 * In the GTK4 backend most of these macros are harmless to leave defined
 * (they refer to GDK/Cairo constants), but this file mirrors the X11
 * convention so existing scope-guarding #include patterns still compile.
 */

#ifndef iv_gdkundefs_h
#define iv_gdkundefs_h

/* Intentionally empty in the GTK4 backend.
 * The Xundefs.h pattern existed to #undef macros that X11 headers defined
 * and that conflicted with InterViews class/member names (e.g. "None",
 * "True", "False", "Window", "Display", "Cursor").
 *
 * In the GTK4 backend these conflicts are avoided because:
 *  - GDK does not define "Display" or "Window" as macros.
 *  - "None" is redefined to nullptr in gdkdefs.h (harmless).
 *  - "True" / "False" are re-mapped to TRUE / FALSE.
 *
 * Code that calls #include <IV-X11/Xundefs.h> (or the GTK4 equivalent)
 * after a scope that used <IV-X11/Xdefs.h> should not need to do
 * anything here.
 */

#endif /* iv_gdkundefs_h */
