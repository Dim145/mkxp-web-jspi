/*
** tilemapvx-binding.cpp
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

/* WEB PORT (VX): binds the VX tilemap renderer (src/tilemapvx.cpp) as "Tilemap",
 * selected by mrbBindingInit when rgssVer >= 2. The XP Tilemap (tilemap-binding.cpp)
 * cannot render VX map data (5-bitmap A1-A5/B-E atlas + 4 layers). Modelled on
 * tilemap-binding.cpp; the "autotiles" proxy becomes the VX "bitmaps" proxy (9 slots). */

#include "tilemapvx.h"
#include "viewport.h"
#include "bitmap.h"
#include "table.h"

#include "disposable-binding.h"
#include "binding-util.h"
#include "binding-types.h"

#include <mruby/array.h>

static const mrb_data_type TilemapVXBitmapArrayType =
{
    "TilemapVXBitmapArray",
	0
};

MRB_METHOD(tilemapVXBitmapsSet)
{
	TilemapVX::BitmapArray *a = getPrivateData<TilemapVX::BitmapArray>(mrb, self);

	mrb_int i;
	mrb_value bitmapObj;

	mrb_get_args(mrb, "io", &i, &bitmapObj);

	Bitmap *bitmap = getPrivateDataCheck<Bitmap>(mrb, bitmapObj, BitmapType);

	a->set(i, bitmap);

	mrb_value ary = mrb_iv_get(mrb, self, getMrbData(mrb)->symbols[CSarray]);
	mrb_ary_set(mrb, ary, i, bitmapObj);

	return self;
}

MRB_METHOD(tilemapVXBitmapsGet)
{
	mrb_int i;

	mrb_get_args(mrb, "i", &i);

	if (i < 0 || i >= 9)
		return mrb_nil_value();

	mrb_value ary = mrb_iv_get(mrb, self, getMrbData(mrb)->symbols[CSarray]);

	return mrb_ary_entry(ary, i);
}

DEF_TYPE(TilemapVX);

MRB_METHOD(tilemapVXInitialize)
{
	TilemapVX *t;

	mrb_value viewportObj = mrb_nil_value();
	Viewport *viewport = 0;

	mrb_get_args(mrb, "|o", &viewportObj);

	if (!mrb_nil_p(viewportObj))
		viewport = getPrivateDataCheck<Viewport>(mrb, viewportObj, ViewportType);

	t = new TilemapVX(viewport);

	setPrivateData(self, t, TilemapVXType);

	setProperty(mrb, self, CSviewport, viewportObj);

	wrapProperty(mrb, self, &t->getBitmapArray(), CSbitmaps, TilemapVXBitmapArrayType);

	MrbData &mrbData = *getMrbData(mrb);
	mrb_value bitmapsObj = mrb_iv_get(mrb, self, mrbData.symbols[CSbitmaps]);

	mrb_value ary = mrb_ary_new_capa(mrb, 9);
	for (int i = 0; i < 9; ++i)
		mrb_ary_push(mrb, ary, mrb_nil_value());

	mrb_iv_set(mrb, bitmapsObj, mrbData.symbols[CSarray], ary);

	/* Circular reference so both objects are always alive at the same time */
	mrb_iv_set(mrb, bitmapsObj, mrbData.symbols[CStilemap], self);

	return self;
}

MRB_METHOD(tilemapVXGetBitmaps)
{
	checkDisposed<TilemapVX>(mrb, self);

	return getProperty(mrb, self, CSbitmaps);
}

MRB_METHOD(tilemapVXUpdate)
{
	TilemapVX *t = getPrivateData<TilemapVX>(mrb, self);

	t->update();

	return mrb_nil_value();
}

MRB_METHOD(tilemapVXGetViewport)
{
	checkDisposed<TilemapVX>(mrb, self);

	return getProperty(mrb, self, CSviewport);
}

DEF_PROP_OBJ_REF(TilemapVX, Table, MapData,   CSmap_data)
DEF_PROP_OBJ_REF(TilemapVX, Table, FlashData, CSflash_data)
DEF_PROP_OBJ_REF(TilemapVX, Table, Flags,     CSflags)

DEF_PROP_B(TilemapVX, Visible)

DEF_PROP_I(TilemapVX, OX)
DEF_PROP_I(TilemapVX, OY)

void
tilemapVXBindingInit(mrb_state *mrb)
{
	RClass *klass = defineClass(mrb, "TilemapBitmaps");

	mrb_define_method(mrb, klass, "[]=", tilemapVXBitmapsSet, MRB_ARGS_REQ(2));
	mrb_define_method(mrb, klass, "[]",  tilemapVXBitmapsGet, MRB_ARGS_REQ(1));
	mrb_define_method(mrb, klass, "inspect", inspectObject, MRB_ARGS_NONE());

	klass = defineClass(mrb, "Tilemap");

	disposableBindingInit<TilemapVX>(mrb, klass);

	mrb_define_method(mrb, klass, "initialize", tilemapVXInitialize, MRB_ARGS_OPT(1));
	mrb_define_method(mrb, klass, "bitmaps",    tilemapVXGetBitmaps, MRB_ARGS_NONE());
	mrb_define_method(mrb, klass, "update",     tilemapVXUpdate,     MRB_ARGS_NONE());
	mrb_define_method(mrb, klass, "viewport",   tilemapVXGetViewport, MRB_ARGS_NONE());

	INIT_PROP_BIND( TilemapVX, MapData,   "map_data"   );
	INIT_PROP_BIND( TilemapVX, FlashData, "flash_data" );
	INIT_PROP_BIND( TilemapVX, Flags,     "flags"      );
	INIT_PROP_BIND( TilemapVX, Visible,   "visible"    );
	INIT_PROP_BIND( TilemapVX, OX,        "ox"         );
	INIT_PROP_BIND( TilemapVX, OY,        "oy"         );

	mrb_define_method(mrb, klass, "inspect", inspectObject, MRB_ARGS_NONE());
}
