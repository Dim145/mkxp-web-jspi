#ifndef EMSCRIPTEN_HPP
#define EMSCRIPTEN_HPP

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

extern "C" {
	void load_file_async_js(const char* fullPathC, int bitmap=0);

	void save_file_async_js(const char* fullPathC);

	int file_is_cached(const char* fullPathC);

	void web_bgm_play_js(const char* pathC, int volume, int pitch);
	void web_bgm_stop_js(void);
	void web_bgm_fade_js(int ms);
}

#endif

#endif

