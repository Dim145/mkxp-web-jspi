# Changes from upstream mkxp-web

This fork (RGSS-Web / mkxp-web-jspi) modifies pulsejet/mkxp-web. Per GPL-2.0 §2(a),
the significant changes and the date they were made:

## 2026 — run script-heavy RGSS1 games (e.g. Pokémon Essentials) in the browser

**Event loop (the key change)**
- `src/graphics.cpp` — `FPSLimiter::delayTicks()` now calls `emscripten_sleep()` so a
  game's blocking RGSS render loops (`loop { Graphics.update }`) yield to the browser
  once per frame via ASYNCIFY/JSPI, unwinding the whole C+Ruby stack. This removes the
  need to rewrite a game's event loop into `main_update_loop` callbacks.
- `binding-mruby/binding-mruby.cpp` — removed `emscripten_set_main_loop()`; the game's
  own loop drives frames via the yield above.

**Toolchain**
- `CMakeLists.txt`, `extra/build_config.rb` — migrated to JSPI (`-s JSPI` / `ASYNCIFY=2`),
  wasm-native exceptions (`-fwasm-exceptions`) + wasm `setjmp`/`longjmp`
  (`-s SUPPORT_LONGJMP=wasm`); pinned emsdk 3.1.61; added `-flto` to the mruby build.

**mruby 2.1.2 compatibility (`deps/mruby/src/vm.c`, applied via `extra/vm.c.patch`)**
- Escaping-proc top-level env force-unshare in `mrb_top_run` — fixes "undefined method"
  from stored `Events.on* += proc{}` callbacks after later script loads / GC.
- `super` + `alias` `MRB_PROC_TARGET_CLASS` guard — fixes spurious "self has wrong type
  to call super" through deep alias chains.
- `super` garbage-mid guard — no-op instead of crashing when an alias-corrupted method id
  is not a valid symbol.
- Integer-division fixnum fix (original mkxp-web patch).

**Input / rendering**
- `src/input.cpp` — static keyboard bindings + `web_set_scancode` for browser keyboard input.
- `src/config.cpp`, `src/main.cpp` — request WebGL2 / GLES3; frame-skip and skip-storm fixes.

**Audio (frame-independent BGM)**
- `src/emscripten.cpp/.hpp`, `binding-mruby/audio-binding.cpp` — added `Audio.web_bgm_play/
  stop/fade`, which hand BGM to a JS Web Audio player (`build/js/webbgm.js`). mkxp's OpenAL
  renders on a main-thread `ScriptProcessorNode`, so BGM stutters/freezes whenever the main
  thread blocks (e.g. a synchronous Marshal save, a long map load); Web Audio plays on the
  browser's own audio thread and keeps going through those stalls. Intro→loop points are
  pluggable via `window.BGM_LOOP_TABLE`. SE/BGS/ME stay on mkxp's OpenAL (short / unnoticeable).
- `extra/rgss.rb` — Audio shim: tolerant play (swallows non-OGG decode errors), arity guard
  (forwards only name/volume/pitch when a game passes an extra `position` arg), and the
  BGM→Web-Audio routing with a native-OpenAL fallback when the `web_bgm_*` binding is absent.

**Browser harness & tooling**
- `build/index.html`, `extra/shell.html`, `build/js/*`, `build/sw.js` — unthrottled Web
  Worker timers, keyboard + on-screen dpad input, fullscreen toggle, Service Worker
  offline cache, async hashed-asset loading, and dropping the WebGL context on unload so a
  reload reclaims GPU memory promptly.
- `extra/rgss.rb` — generic mruby/RGSS compatibility shims (Win32API stub, Thread/Mutex
  no-ops, ENV/Dir stubs, streaming Zlib, ObjectSpace stubs, File byte helpers).
- `deploy/` — containerized static serving (Caddy: Brotli, correct wasm MIME, caching).
- `scripts_tool.rb`, `regen-mapping.sh`, `gen-bitmap-map.rb`, `gen-preload.rb`,
  `import-game.sh` — the "bring your own game" asset pipeline.
