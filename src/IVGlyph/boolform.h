/*
 * Copyright (c) 1994-1996 Vectaport Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the names of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any purpose.
 * It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL,
 * INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING
 * FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 * 
 */

#ifndef boolform_h
#define boolform_h

#include <InterViews/monoglyph.h>
#include <IVGlyph/observables.h>

class ObservableBoolean;
class Button;
class Patch;

class CheckBooleanEditor : public MonoGlyph, public Observer {
public:
    CheckBooleanEditor(ObservableBoolean* obs, const char* labl);
    virtual ~CheckBooleanEditor();

    void edit();
    virtual void update(Observable*);
protected:
    Button* _checkbox;
    ObservableBoolean* _obs;
};

class PaletteBooleanEditor : public MonoGlyph, public Observer {
public:
    PaletteBooleanEditor(ObservableBoolean* obs, const char* labl);
    virtual ~PaletteBooleanEditor();

    void edit();
    virtual void update(Observable*);
protected:
    Button* _palette;
    ObservableBoolean* _obs;
};

class BooleanObserver : public MonoGlyph, public Observer {
public:
    BooleanObserver(ObservableBoolean*, const char* labl);
    virtual ~BooleanObserver();

    virtual void update(Observable*);
protected:
    ObservableBoolean* _obs;
    Patch* _view;
};

#endif
