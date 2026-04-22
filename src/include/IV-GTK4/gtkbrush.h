/*
 * GTK4 backend: replaces IV-X11/xbrush.h
 * Defines BrushRep.  The dash_list_ is now a double[] (for cairo_set_dash)
 * rather than a char[].
 */

#ifndef iv_gtkbrush_h
#define iv_gtkbrush_h

class Display;

class BrushRep {
public:
    Display* display_;

    /*
     * In GTK4 cairo_set_dash() takes double[] values.  We convert from
     * the char dash_list stored in BrushImpl at rep() construction time.
     */
    double*  dash_list_;   /* replaces char* dash_list_ */
    int      dash_count_;
    int      width_;       /* pixel width; set in Brush::rep(Display*) */
};

#endif /* iv_gtkbrush_h */
