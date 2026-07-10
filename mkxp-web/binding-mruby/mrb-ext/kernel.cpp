/*
** kernel.cpp
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

#include <mruby.h>
#include <mruby/string.h>
#include <mruby/compile.h>

#include <stdlib.h>
#include <sys/time.h>

#include <SDL_messagebox.h>

#include "../binding-util.h"
#include "marshal.h"
#include "file-helper.h"
#include "sharedstate.h"
#include "eventthread.h"
#include "exception.h"
#include "filesystem.h"
#include "binding.h"

#ifdef __EMSCRIPTEN__
#include "emscripten.hpp"
#endif

void mrbBindingTerminate();

MRB_FUNCTION(kernelLoadData)
{
	const char *filename;
	mrb_get_args(mrb, "z", &filename);

	mrb_value obj;
	try {
		SDL_rw_file_helper fileHelper;
		fileHelper.filename = filename;
		char * contents = fileHelper.read();
		mrb_value rawdata = mrb_str_new_static(mrb, contents, fileHelper.length);
		obj = mrb_marshal_load(mrb, rawdata);
	}
	catch (const Exception &e)
	{
		raiseMrbExc(mrb, e);
	}

	return obj;
}

MRB_FUNCTION(kernelSaveData)
{
	mrb_value obj;
	const char *filename;

	mrb_get_args(mrb, "oz", &obj, &filename);

	try {
		mrb_value dumped = mrb_nil_value();
		mrb_marshal_dump(mrb, obj, dumped);
		SDL_rw_file_helper fileHelper;
		fileHelper.filename = filename;
		fileHelper.write(RSTRING_PTR(dumped));
	}
	catch (const Exception &e)
	{
		raiseMrbExc(mrb, e);
	}

	return mrb_nil_value();
}

MRB_FUNCTION(kernelSaveAsync)
{
	mrb_value obj;
	const char *filename;

	mrb_get_args(mrb, "z", &filename);

#ifdef __EMSCRIPTEN__
	save_file_async_js(filename);

#endif
	return mrb_nil_value();
}

void kernelBindingInit(mrb_state *mrb)
{
	RClass *module = mrb->kernel_module;

	mrb_define_module_function(mrb, module, "load_data", kernelLoadData, MRB_ARGS_REQ(1));
	mrb_define_module_function(mrb, module, "save_data", kernelSaveData, MRB_ARGS_REQ(2));
	mrb_define_module_function(mrb, module, "save_file_async", kernelSaveAsync, MRB_ARGS_REQ(1));
}
