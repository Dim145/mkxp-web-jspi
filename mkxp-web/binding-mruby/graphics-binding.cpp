/*
** graphics-binding.cpp
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

#include "graphics.h"
#include "sharedstate.h"
#include "binding-util.h"
#include "binding-types.h"
#include "bitmap.h"
#include "exception.h"

MRB_FUNCTION(graphicsUpdate)
{
	MRB_FUN_UNUSED_PARAM;

	shState->graphics().update();

	return mrb_nil_value();
}

MRB_FUNCTION(graphicsFreeze)
{
	MRB_FUN_UNUSED_PARAM;

	shState->graphics().freeze();

	return mrb_nil_value();
}

MRB_FUNCTION(graphicsTransition)
{
	mrb_int duration = 8;
	const char *filename = "";
	mrb_int vague = 40;

	mrb_get_args(mrb, "|izi", &duration, &filename, &vague);

	GUARD_EXC( shState->graphics().transition(duration, filename, vague); )

	return mrb_nil_value();
}

MRB_FUNCTION(graphicsFrameReset)
{
	MRB_FUN_UNUSED_PARAM;

	shState->graphics().frameReset();

	return mrb_nil_value();
}

/* WEB PORT (VX): RGSS2/VX Graphics methods absent from this XP-era binding.
 * The C++ Graphics class (src/graphics.h) already implements them all. */
MRB_FUNCTION(graphicsWidth)
{
	MRB_FUN_UNUSED_PARAM;
	return mrb_fixnum_value(shState->graphics().width());
}

MRB_FUNCTION(graphicsHeight)
{
	MRB_FUN_UNUSED_PARAM;
	return mrb_fixnum_value(shState->graphics().height());
}

MRB_FUNCTION(graphicsResizeScreen)
{
	mrb_int width, height;
	mrb_get_args(mrb, "ii", &width, &height);
	shState->graphics().resizeScreen(width, height);
	return mrb_nil_value();
}

MRB_FUNCTION(graphicsWait)
{
	mrb_int duration;
	mrb_get_args(mrb, "i", &duration);
	GUARD_EXC( shState->graphics().wait(duration); )
	return mrb_nil_value();
}

MRB_FUNCTION(graphicsFadeout)
{
	mrb_int duration;
	mrb_get_args(mrb, "i", &duration);
	GUARD_EXC( shState->graphics().fadeout(duration); )
	return mrb_nil_value();
}

MRB_FUNCTION(graphicsFadein)
{
	mrb_int duration;
	mrb_get_args(mrb, "i", &duration);
	GUARD_EXC( shState->graphics().fadein(duration); )
	return mrb_nil_value();
}

MRB_FUNCTION(graphicsSnapToBitmap)
{
	MRB_FUN_UNUSED_PARAM;
	Bitmap *result = 0;
	GUARD_EXC( result = shState->graphics().snapToBitmap(); )
	return wrapObject(mrb, result, BitmapType);
}

MRB_FUNCTION(graphicsPlayMovie)
{
	const char *filename;
	mrb_get_args(mrb, "z", &filename);
	shState->graphics().playMovie(filename);
	return mrb_nil_value();
}

#define DEF_GRA_PROP_I(PropName) \
	MRB_FUNCTION(graphics##Get##PropName) \
	{ \
		MRB_FUN_UNUSED_PARAM; \
		return mrb_fixnum_value(shState->graphics().get##PropName()); \
	} \
	MRB_FUNCTION(graphics##Set##PropName) \
	{ \
		mrb_int value; \
		mrb_get_args(mrb, "i", &value); \
		shState->graphics().set##PropName(value); \
		return mrb_fixnum_value(value); \
	}

#define DEF_GRA_PROP_B(PropName) \
	MRB_FUNCTION(graphics##Get##PropName) \
	{ \
		MRB_FUN_UNUSED_PARAM; \
		return mrb_bool_value(shState->graphics().get##PropName()); \
	} \
	MRB_FUNCTION(graphics##Set##PropName) \
	{ \
		mrb_bool value; \
		mrb_get_args(mrb, "b", &value); \
		shState->graphics().set##PropName(value); \
		return mrb_bool_value(value); \
	}

DEF_GRA_PROP_I(FrameRate)
DEF_GRA_PROP_I(FrameCount)
DEF_GRA_PROP_I(Brightness)

DEF_GRA_PROP_B(Fullscreen)
DEF_GRA_PROP_B(ShowCursor)

#define INIT_GRA_PROP_BIND(PropName, prop_name_s) \
{ \
	mrb_define_module_function(mrb, module, prop_name_s, graphics##Get##PropName, MRB_ARGS_NONE()); \
	mrb_define_module_function(mrb, module, prop_name_s "=", graphics##Set##PropName, MRB_ARGS_REQ(1)); \
}

void graphicsBindingInit(mrb_state *mrb)
{
	RClass *module = mrb_define_module(mrb, "Graphics");

	mrb_define_module_function(mrb, module, "update", graphicsUpdate, MRB_ARGS_NONE());
	mrb_define_module_function(mrb, module, "freeze", graphicsFreeze, MRB_ARGS_NONE());
	mrb_define_module_function(mrb, module, "transition", graphicsTransition, MRB_ARGS_OPT(3));
	mrb_define_module_function(mrb, module, "frame_reset", graphicsFrameReset, MRB_ARGS_NONE());

	/* WEB PORT (VX): RGSS2/VX Graphics API */
	mrb_define_module_function(mrb, module, "width",         graphicsWidth,        MRB_ARGS_NONE());
	mrb_define_module_function(mrb, module, "height",        graphicsHeight,       MRB_ARGS_NONE());
	mrb_define_module_function(mrb, module, "resize_screen", graphicsResizeScreen, MRB_ARGS_REQ(2));
	mrb_define_module_function(mrb, module, "wait",          graphicsWait,         MRB_ARGS_REQ(1));
	mrb_define_module_function(mrb, module, "fadeout",       graphicsFadeout,      MRB_ARGS_REQ(1));
	mrb_define_module_function(mrb, module, "fadein",        graphicsFadein,       MRB_ARGS_REQ(1));
	mrb_define_module_function(mrb, module, "snap_to_bitmap", graphicsSnapToBitmap, MRB_ARGS_NONE());
	mrb_define_module_function(mrb, module, "play_movie",    graphicsPlayMovie,    MRB_ARGS_REQ(1));

	INIT_GRA_PROP_BIND( FrameRate,  "frame_rate"  );
	INIT_GRA_PROP_BIND( FrameCount, "frame_count" );
	INIT_GRA_PROP_BIND( Brightness, "brightness"  );

	INIT_GRA_PROP_BIND( Fullscreen, "fullscreen"  );
	INIT_GRA_PROP_BIND( ShowCursor, "show_cursor" );
}
