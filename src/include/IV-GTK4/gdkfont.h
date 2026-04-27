/*
 * GTK4 backend: replaces IV-X11/xfont.h
 * Defines FontRep using PangoFontDescription instead of XFontStruct.
 */

#ifndef iv_gdkfont_h
#define iv_gdkfont_h

#include <InterViews/boolean.h>
#include <OS/enter-scope.h>
#include <IV-GTK4/gdklib.h>
#include <InterViews/resource.h>

class Display;
class KnownFonts;
class String;

class FontRep : public Resource {
public:
    FontRep(Display*, PangoFontDescription*, float);
    ~FontRep();

    Display*             display_;
    PangoFontDescription* desc_;     /* replaces XFontStruct* font_ */
    PangoContext*        context_;   /* Pango context for this display */
    PangoLayout*         layout_;    /* shared layout for metric queries */
    float                scale_;
    boolean              unscaled_;
    String*              name_;
    String*              encoding_;
    float                size_;
    KnownFonts*          entry_;

    /* Font metrics cached from PangoFontMetrics */
    double ascent_;
    double descent_;
    double max_advance_width_;
};

class FontFamilyRep {
public:
    Display* display_;
    int      count_;
    int      min_weight_;
    int      max_weight_;
    int      min_width_;
    int      max_width_;
    int      min_slant_;
    int      max_slant_;
    int      min_size_;
    int      max_size_;

    char**   names_;
    int*     weights_;
    int*     slants_;
    int*     widths_;
    int*     sizes_;
};

#include <InterViews/_leave.h>

#endif /* iv_gdkfont_h */
