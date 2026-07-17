/*
** windowvx-binding.cpp
**
** This file is part of mkxp.
**
** Copyright (C) 2013 Jonas Kulla <Nyocurio@gmail.com>
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** mkxp is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
*/

/* WEB PORT (VX): this fork's binding-mruby only bound the RGSS1/XP Window (see
 * window-binding.cpp). RPG Maker VX (RGSS2) games use the WindowVX renderer
 * (src/windowvx.cpp), which was implemented in C++ but never exposed to mruby.
 * This file binds it as "Window" and is selected by mrbBindingInit when
 * rgssVer >= 2. Modelled on window-binding.cpp plus the VX-only attributes
 * (openness, tone, padding, arrows_visible) declared in windowvx.h. */

#include "windowvx.h"
#include "disposable-binding.h"
#include "viewportelement-binding.h"
#include "binding-util.h"
#include "binding-types.h"

DEF_TYPE(WindowVX);

MRB_METHOD(windowVXInitialize)
{
	WindowVX *w = viewportElementInitialize<WindowVX>(mrb, self);

	setPrivateData(self, w, WindowVXType);

	w->initDynAttribs();

	wrapProperty(mrb, self, &w->getCursorRect(), CScursor_rect, RectType);

	/* WEB PORT (VX) FIX: WindowVX::initDynAttribs() only heap-allocates the tone for rgssVer>=3
	 * (VX Ace). At rgssVer 2 (this game) p->tone keeps its ctor default &tmp.tone -- an INTERIOR
	 * pointer into WindowVXPrivate's embedded EtcTemps member. Wrapping that in an OWNING ToneType
	 * made mruby GC eventually `delete` non-heap memory -> emscripten heap corruption -> a later GC
	 * sweep frees a Color whose vtable got clobbered -> "indirect call to null" (the freeze that hit
	 * after a few minutes of Window churn). Guard the wrap exactly like initDynAttribs above and the
	 * canonical binding-mri/windowvx-binding.cpp. (RGSS2 Window has no `tone` anyway.) */
	if (rgssVer >= 3)
		wrapProperty(mrb, self, &w->getTone(), CStone, ToneType);

	return self;
}

MRB_METHOD(windowVXUpdate)
{
	WindowVX *w = getPrivateData<WindowVX>(mrb, self);

	w->update();

	return mrb_nil_value();
}

MRB_METHOD(windowVXMove)
{
	WindowVX *w = getPrivateData<WindowVX>(mrb, self);

	mrb_int x, y, width, height;
	mrb_get_args(mrb, "iiii", &x, &y, &width, &height);

	w->move(x, y, width, height);

	return mrb_nil_value();
}

MRB_METHOD(windowVXIsOpen)
{
	WindowVX *w = getPrivateData<WindowVX>(mrb, self);

	return mrb_bool_value(w->isOpen());
}

MRB_METHOD(windowVXIsClosed)
{
	WindowVX *w = getPrivateData<WindowVX>(mrb, self);

	return mrb_bool_value(w->isClosed());
}

DEF_PROP_OBJ_REF(WindowVX, Bitmap, Windowskin, CSwindowskin)
DEF_PROP_OBJ_REF(WindowVX, Bitmap, Contents,   CScontents)
DEF_PROP_OBJ_VAL(WindowVX, Rect,   CursorRect, CScursor_rect)
DEF_PROP_OBJ_VAL(WindowVX, Tone,   Tone,       CStone)

DEF_PROP_B(WindowVX, Active)
DEF_PROP_B(WindowVX, ArrowsVisible)
DEF_PROP_B(WindowVX, Pause)

DEF_PROP_I(WindowVX, X)
DEF_PROP_I(WindowVX, Y)
DEF_PROP_I(WindowVX, Width)
DEF_PROP_I(WindowVX, Height)
DEF_PROP_I(WindowVX, OX)
DEF_PROP_I(WindowVX, OY)
DEF_PROP_I(WindowVX, Padding)
DEF_PROP_I(WindowVX, PaddingBottom)
DEF_PROP_I(WindowVX, Opacity)
DEF_PROP_I(WindowVX, BackOpacity)
DEF_PROP_I(WindowVX, ContentsOpacity)
DEF_PROP_I(WindowVX, Openness)

void
windowVXBindingInit(mrb_state *mrb)
{
	RClass *klass = defineClass(mrb, "Window");

	disposableBindingInit     <WindowVX>(mrb, klass);
	viewportElementBindingInit<WindowVX>(mrb, klass);

	mrb_define_method(mrb, klass, "initialize", windowVXInitialize, MRB_ARGS_OPT(1));
	mrb_define_method(mrb, klass, "update",     windowVXUpdate,     MRB_ARGS_NONE());
	mrb_define_method(mrb, klass, "move",       windowVXMove,       MRB_ARGS_REQ(4));
	mrb_define_method(mrb, klass, "open?",      windowVXIsOpen,     MRB_ARGS_NONE());
	mrb_define_method(mrb, klass, "close?",     windowVXIsClosed,   MRB_ARGS_NONE());

	INIT_PROP_BIND( WindowVX, Windowskin,      "windowskin"       );
	INIT_PROP_BIND( WindowVX, Contents,        "contents"         );
	INIT_PROP_BIND( WindowVX, CursorRect,      "cursor_rect"      );
	INIT_PROP_BIND( WindowVX, Tone,            "tone"             );
	INIT_PROP_BIND( WindowVX, Active,          "active"           );
	INIT_PROP_BIND( WindowVX, ArrowsVisible,   "arrows_visible"   );
	INIT_PROP_BIND( WindowVX, Pause,           "pause"            );
	INIT_PROP_BIND( WindowVX, X,               "x"                );
	INIT_PROP_BIND( WindowVX, Y,               "y"                );
	INIT_PROP_BIND( WindowVX, Width,           "width"            );
	INIT_PROP_BIND( WindowVX, Height,          "height"           );
	INIT_PROP_BIND( WindowVX, OX,              "ox"               );
	INIT_PROP_BIND( WindowVX, OY,              "oy"               );
	INIT_PROP_BIND( WindowVX, Padding,         "padding"          );
	INIT_PROP_BIND( WindowVX, PaddingBottom,   "padding_bottom"   );
	INIT_PROP_BIND( WindowVX, Opacity,         "opacity"          );
	INIT_PROP_BIND( WindowVX, BackOpacity,     "back_opacity"     );
	INIT_PROP_BIND( WindowVX, ContentsOpacity, "contents_opacity" );
	INIT_PROP_BIND( WindowVX, Openness,        "openness"         );

	mrb_define_method(mrb, klass, "inspect", inspectObject, MRB_ARGS_NONE());
}
