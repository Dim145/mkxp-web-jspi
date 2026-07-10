/*
** binding-mruby.cpp
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

#include "binding.h"

#include <mruby.h>
#include <mruby/string.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/irep.h>
#include <mruby/compile.h>
#include <mruby/proc.h>
#include <mruby/dump.h>
#include <mruby/error.h>

#include <stdio.h>
#include <zlib.h>

#include <string>

#include <SDL_messagebox.h>
#include <SDL_rwops.h>
#include <SDL_timer.h>

#include "sharedstate.h"
#include "texpool.h"
#include "eventthread.h"
#include "filesystem.h"
#include "exception.h"

#include "binding-util.h"
#include "binding-types.h"
#include "mrb-ext/marshal.h"
#include "mrb-ext/file-helper.h"

#include <stdio.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static void mrbBindingExecute();
static void mrbBindingTerminate();
static void mrbBindingReset();

ScriptBinding scriptBindingImpl =
{
    mrbBindingExecute,
    mrbBindingTerminate,
    mrbBindingReset
};

ScriptBinding *scriptBinding = &scriptBindingImpl;

void kernelBindingInit(mrb_state *);

void tableBindingInit(mrb_state *);
void etcBindingInit(mrb_state *);
void fontBindingInit(mrb_state *);
void bitmapBindingInit(mrb_state *);
void spriteBindingInit(mrb_state *);
void planeBindingInit(mrb_state *);
void viewportBindingInit(mrb_state *);
void windowBindingInit(mrb_state *);
void tilemapBindingInit(mrb_state *);

void inputBindingInit(mrb_state *);
void audioBindingInit(mrb_state *);
void graphicsBindingInit(mrb_state *);

/* WEB PORT: mruby has no Zlib stdlib, but many RGSS games rely on it at
 * runtime — most critically pbSetUpSystem inflates Data/Constants.rxdata to
 * define PBSpecies/PBItems/PBMoves/... Expose a minimal Zlib module backed by
 * the zlib C library the engine already links against. */
static mrb_value
zlibInflate(mrb_state *mrb, mrb_value)
{
	char *ptr; mrb_int len;
	mrb_get_args(mrb, "s", &ptr, &len);

	unsigned long bufLen = (len > 0 ? (unsigned long)len * 4 : 256UL) + 256UL;
	std::string out;
	for (;;)
	{
		out.resize(bufLen);
		unsigned long destLen = bufLen;
		int result = uncompress((Bytef*)&out[0], &destLen,
		                        (const Bytef*)ptr, (uLong)len);
		if (result == Z_OK) { out.resize(destLen); break; }
		if (result == Z_BUF_ERROR) { bufLen *= 2; continue; }
		mrb_raisef(mrb, E_RUNTIME_ERROR, "Zlib inflate failed (%S)",
		           mrb_fixnum_value(result));
	}
	return mrb_str_new(mrb, out.data(), out.size());
}

static mrb_value
zlibDeflate(mrb_state *mrb, mrb_value)
{
	char *ptr; mrb_int len; mrb_int level = Z_DEFAULT_COMPRESSION;
	mrb_get_args(mrb, "s|i", &ptr, &len, &level);

	uLong bound = compressBound((uLong)len);
	std::string out;
	out.resize(bound ? bound : 1);
	uLongf destLen = (uLongf)out.size();
	int result = compress2((Bytef*)&out[0], &destLen,
	                       (const Bytef*)ptr, (uLong)len, (int)level);
	if (result != Z_OK)
		mrb_raisef(mrb, E_RUNTIME_ERROR, "Zlib deflate failed (%S)",
		           mrb_fixnum_value(result));
	out.resize(destLen);
	return mrb_str_new(mrb, out.data(), out.size());
}

static mrb_value
zlibCrc32(mrb_state *mrb, mrb_value)
{
	char *ptr = NULL; mrb_int len = 0; mrb_int init = 0;
	mrb_get_args(mrb, "|si", &ptr, &len, &init);
	uLong crc = crc32((uLong)init, (const Bytef*)ptr, (uInt)len);
	return mrb_fixnum_value((mrb_int)crc);
}

static void
zlibBindingInit(mrb_state *mrb)
{
	RClass *zlib = mrb_define_module(mrb, "Zlib");
	mrb_define_module_function(mrb, zlib, "crc32", zlibCrc32, MRB_ARGS_OPT(2));

	RClass *inflate = mrb_define_class_under(mrb, zlib, "Inflate", mrb->object_class);
	mrb_define_class_method(mrb, inflate, "inflate", zlibInflate, MRB_ARGS_REQ(1));

	RClass *deflate = mrb_define_class_under(mrb, zlib, "Deflate", mrb->object_class);
	mrb_define_class_method(mrb, deflate, "deflate", zlibDeflate, MRB_ARGS_ARG(1,1));
}

static void mrbBindingInit(mrb_state *mrb)
{
	int arena = mrb_gc_arena_save(mrb);

	/* Init standard classes */
	kernelBindingInit(mrb);

	/* Init RGSS classes */
	tableBindingInit(mrb);
	etcBindingInit(mrb);
	fontBindingInit(mrb);
	bitmapBindingInit(mrb);
	spriteBindingInit(mrb);
	planeBindingInit(mrb);
	viewportBindingInit(mrb);
	windowBindingInit(mrb);
	tilemapBindingInit(mrb);

	/* Init RGSS modules */
	inputBindingInit(mrb);
	audioBindingInit(mrb);
	graphicsBindingInit(mrb);

	/* WEB PORT: Zlib (needed by games that inflate compiled data at runtime) */
	zlibBindingInit(mrb);

	/* Load global constants */
	mrb_define_global_const(mrb, "MKXP", mrb_true_value());

	mrb_value debug = mrb_bool_value(shState->config().editor.debug);
	if (rgssVer == 1)
		mrb_define_global_const(mrb, "DEBUG", debug);
	else if (rgssVer >= 2)
		mrb_define_global_const(mrb, "TEST", debug);

	mrb_define_global_const(mrb, "BTEST", mrb_bool_value(shState->config().editor.battleTest));

	mrb_gc_arena_restore(mrb, arena);
}

static mrb_value
mkxpTimeOp(mrb_state *mrb, mrb_value)
{
	mrb_int iterations = 1;
	const char *opName = "";
	mrb_value block;

	mrb_get_args(mrb, "|iz&", &iterations, &opName, &block);

	Uint64 start = SDL_GetPerformanceCounter();

	for (int i = 0; i < iterations; ++i)
		mrb_yield(mrb, block, mrb_nil_value());

	Uint64 diff = SDL_GetPerformanceCounter() - start;
	double avg = (double) diff / iterations;

	double sec = avg / SDL_GetPerformanceFrequency();
	float ms = sec * 1000;

	printf("<%s> [%f ms]\n", opName, ms);
	fflush(stdout);

	return mrb_float_value(mrb, ms);
}

static const char *
mrbValueString(mrb_value value)
{
	return mrb_string_p(value) ? RSTRING_PTR(value) : 0;
}

static void
showExcMessageBox(mrb_state *mrb, mrb_value exc)
{
	/* Display actual exception in a message box */
	mrb_value mesg = mrb_funcall(mrb, exc, "message", 0);
	mrb_value line = mrb_attr_get(mrb, exc, mrb_intern_lit(mrb, "line"));
	mrb_value file = mrb_attr_get(mrb, exc, mrb_intern_lit(mrb, "file"));
	const char *excClass = mrb_class_name(mrb, mrb_class(mrb, exc));

	char msgBoxText[512];
	snprintf(msgBoxText, sizeof(msgBoxText), "Script '%s' line %d: %s occured.\n\n%s",
	         mrbValueString(file), mrb_fixnum(line), excClass, mrbValueString(mesg));

	shState->eThread().showMessageBox(msgBoxText, SDL_MESSAGEBOX_ERROR);
}

static void
checkException(mrb_state *mrb)
{
	if (!mrb->exc)
		return;

	mrb_value exc = mrb_obj_value(mrb->exc);
	MrbData &mrbData = *getMrbData(mrb);

	/* Check if an actual exception occured, or just a shutdown was called */
	if (mrb_obj_class(mrb, exc) != mrbData.exc[Shutdown])
		showExcMessageBox(mrb, exc);
}


static void
showError(const std::string &msg)
{
	shState->eThread().showMessageBox(msg.c_str());
}

static void
runCustomScript(mrb_state *mrb, mrbc_context *ctx, const char *filename)
{
	/* Execute custom script */
	FILE *f = fopen(filename, "rb");

	if (!f)
	{
		static char buffer[256];
		snprintf(buffer, sizeof(buffer), "Unable to open script '%s'", filename);
		showError(buffer);

		return;
	}

	ctx->filename = strdup(filename);
	ctx->lineno = 1;

	/* Run code */
	mrb_load_file_cxt(mrb, f, ctx);

	free(ctx->filename);
	fclose(f);
}

static void
runMrbFile(mrb_state *mrb, const char *filename)
{
#if 0
	/* Execute compiled script */
	FILE *f = fopen(filename, "rb");

	if (!f)
	{
		static char buffer[256];
		snprintf(buffer, sizeof(buffer), "Unable to open compiled script '%s'", filename);
		showError(buffer);

		return;
	}

	mrb_irep *irep = mrb_read_irep_file(mrb, f);

	if (!irep)
	{
		static char buffer[256];
		snprintf(buffer, sizeof(buffer), "Unable to read compiled script '%s'", filename);
		showError(buffer);

		return;
	}

	RProc *proc = mrb_proc_new(mrb, irep);
	mrb_run(mrb, proc, mrb_top_self(mrb));

	fclose(f);
#endif
}

mrb_state * static_mrb;

#ifdef __EMSCRIPTEN__
EM_JS(int, jsSkipFrame, (), {
	// Initialize
	const targetRate = window.skfTargetFrameRate || 40;
	if (!window.skfFrameTimings) {
		window.skfFrameTimings = [];
		window.skfPreviousRate = targetRate;
	}

	// Get current time
	const ctime = performance.now();

	// Wait till we have n elements
	if (window.skfFrameTimings.length < (window.skfNumFrames || 8)) {
		window.skfFrameTimings.push(ctime);
		return 0;
	}

	// Get average framerate
	const framerate = 1000 / ((ctime - window.skfFrameTimings[0]) / window.skfFrameTimings.length);

	// Proportional error
	const Pe = framerate - targetRate;

	// Derivative error
	const De = (window.skfPreviousRate - framerate);

	// Set previous rate
	window.skfPreviousRate = framerate;

	// Drop frame if necessary
	if (Pe - De - 1 > 0) {
		return 1;
	}

	// Add the timing
	window.skfFrameTimings.shift();
	window.skfFrameTimings.push(ctime);
	return 0;
});
#endif

void main_update_loop() {
#ifdef __EMSCRIPTEN__
	if (jsSkipFrame() == 1) return;
#endif
	mrb_load_nstring_cxt(static_mrb, "main_update_loop", 16, NULL);
}


static void
runRMXPScripts(mrb_state *mrb, mrbc_context *ctx)
{
	const std::string &scriptPack = shState->rtData().config.game.scripts;

	if (scriptPack.empty())
	{
		showError("No game scripts specified (missing Game.ini?)");
		return;
	}

	if (!shState->fileSystem().exists(scriptPack.c_str()))
	{
		showError(std::string("Unable to open '") + scriptPack + "'");
		return;
	}

	/* Load Ruby Game Scripting System from rgss.rb */
	try
	{
		SDL_rw_file_helper fileHelper;
		fileHelper.filename = "rgss.rb";
		char * contents = fileHelper.read();
		int ai = mrb_gc_arena_save(mrb);
		mrb_load_nstring_cxt(mrb, contents, fileHelper.length, ctx);
		mrb_gc_arena_restore(mrb, ai);
	}
	catch (const Exception &e)
	{
		printf("Failed to read rgss.rb\n");
		return;
	}

	/* We use a secondary util state to unmarshal the scripts */
	mrb_state *scriptMrb = mrb_open();

	mrb_value scriptArray = mrb_nil_value();
	std::string readError;

	try
	{
		SDL_rw_file_helper fileHelper;
		fileHelper.filename = scriptPack.c_str();
		char * contents = fileHelper.read();
		mrb_value rawdata = mrb_str_new_static(mrb, contents, fileHelper.length);
		scriptArray = mrb_marshal_load(mrb, rawdata);
	}
	catch (const Exception &e)
	{
		readError = std::string(": ") + e.msg;
	}

	if (!mrb_array_p(scriptArray))
	{
		showError(std::string("Failed to read script data") + readError);
		mrb_close(scriptMrb);
		return;
	}

	int scriptCount = RARRAY_LEN(scriptArray);

	std::string decodeBuffer;
	decodeBuffer.resize(0x1000);

	for (int i = 0; i < scriptCount; ++i)
	{
		mrb_value script = mrb_ary_entry(scriptArray, i);

		mrb_value scriptChksum = mrb_ary_entry(script, 0);
		mrb_value scriptName   = mrb_ary_entry(script, 1);
		mrb_value scriptString = mrb_ary_entry(script, 2);

		(void) scriptChksum;

		int result = Z_OK;
		unsigned long bufferLen;

		while (true)
		{
			unsigned char *bufferPtr =
			        reinterpret_cast<unsigned char*>(const_cast<char*>(decodeBuffer.c_str()));
			unsigned char *sourcePtr =
			        reinterpret_cast<unsigned char*>(RSTRING_PTR(scriptString));

			bufferLen = decodeBuffer.length();

			result = uncompress(bufferPtr, &bufferLen,
			                    sourcePtr, RSTRING_LEN(scriptString));

			bufferPtr[bufferLen] = '\0';

			if (result != Z_BUF_ERROR)
				break;

			decodeBuffer.resize(decodeBuffer.size()*2);
		}

		if (result != Z_OK)
		{
			static char buffer[256];
			snprintf(buffer, sizeof(buffer), "Error decoding script %d: '%s'",
			         i, RSTRING_PTR(scriptName));

			showError(buffer);

			break;
		}

		ctx->filename = RSTRING_PTR(scriptName);
		ctx->lineno = 1;

		int ai = mrb_gc_arena_save(mrb);

		/* Execute code */
		mrb_load_nstring_cxt(mrb, decodeBuffer.c_str(), bufferLen, ctx);

		mrb_gc_arena_restore(mrb, ai);

		if (mrb->exc) {
			printf("%s - err\n", ctx->filename);
			return;
		}
	}

#ifdef __EMSCRIPTEN__
	/* WEB PORT: the game is driven by its own top-level loop in the final script
	 * section ("Main"), which blocks in `loop { Graphics.update }`. Graphics.update
	 * yields to the browser via emscripten_sleep (ASYNCIFY), so we do NOT install
	 * an emscripten_set_main_loop stepper here — the RGSS main function above has
	 * already entered (and never returns from) the game loop by the time script
	 * evaluation reaches this point. */
	static_mrb = mrb;
	(void)main_update_loop;
#endif

	mrb_close(scriptMrb);
}

static void mrbBindingExecute()
{
	mrb_state *mrb = mrb_open();

	shState->setBindingData(mrb);

	MrbData * mrbData = new MrbData(mrb);
	mrb->ud = mrbData;

	mrb_define_module_function(mrb, mrb->kernel_module, "time_op",
	                           mkxpTimeOp, MRB_ARGS_OPT(2) | MRB_ARGS_BLOCK());

	mrbBindingInit(mrb);

	mrbc_context * ctx = mrbc_context_new(mrb);
	ctx->capture_errors = 1;

	const Config &conf = shState->rtData().config;
	const std::string &customScript = conf.customScript;
	(void) runMrbFile; // FIXME mrbFile support on ice for now

	if (!customScript.empty())
		runCustomScript(mrb, ctx, customScript.c_str());
	else
		runRMXPScripts(mrb, ctx);

	checkException(mrb);

	shState->rtData().rqTermAck.set();
	shState->texPool().disable();

	mrbc_context_free(mrb, ctx);
	mrb_close(mrb);
}

static void mrbBindingTerminate()
{
	mrb_state *mrb = static_cast<mrb_state*>(shState->bindingData());
	MrbData *data = static_cast<MrbData*>(mrb->ud);

	mrb_raise(mrb, data->exc[Shutdown], "");
}

static void mrbBindingReset()
{
	// No idea how to do this with mruby yet
}
