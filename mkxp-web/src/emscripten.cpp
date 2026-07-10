#include "emscripten.hpp"

#ifdef __EMSCRIPTEN__

// WEB PORT: EM_ASYNC_JS (was EM_JS + Asyncify.handleSleep) so this suspending
// import works under BOTH -sASYNCIFY=1 and -sJSPI. Under JSPI the wasm stack is
// switched by the VM (no Asyncify instrumentation), eliminating the ~50% whole-
// program overhead. `await`-ing the Promise suspends until drive.js calls wakeUp.
EM_ASYNC_JS(void, load_file_async_js, (const char* fullPathC, int bitmap), {
	await new Promise(function(wakeUp) {
		window.loadFileAsync(UTF8ToString(fullPathC), bitmap, wakeUp);
	});
});

EM_JS(void, save_file_async_js, (const char* fullPathC), {
	if (window.saveFile) window.saveFile(UTF8ToString(fullPathC));
});

EM_JS(int, file_is_cached, (const char* fullPathC), {
	const fullPath = UTF8ToString(fullPathC);
	const mappingKey = getMappingKey(fullPath);
	return window.fileAsyncCache.hasOwnProperty(mappingKey) ? 1 : 0;
});

// WEB PORT: BGM is played by a JS Web Audio player (js/webbgm.js) instead of mkxp's
// OpenAL, because Emscripten's OpenAL uses a main-thread ScriptProcessorNode -- so BGM
// froze whenever the main thread blocked (e.g. the synchronous Marshal save). Web Audio
// runs on the browser's own audio thread, keeping music playing through main-thread stalls.
EM_JS(void, web_bgm_play_js, (const char* pathC, int volume, int pitch), {
	if (window.webBgmPlay) window.webBgmPlay(UTF8ToString(pathC), volume, pitch);
});

EM_JS(void, web_bgm_stop_js, (void), {
	if (window.webBgmStop) window.webBgmStop();
});

EM_JS(void, web_bgm_fade_js, (int ms), {
	if (window.webBgmFade) window.webBgmFade(ms);
});

#endif

