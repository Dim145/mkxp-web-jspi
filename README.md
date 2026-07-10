# RGSS-Web (`mkxp-web-jspi`)

Run RPG Maker XP (RGSS1) games in the browser — no plugin, no streaming server, no per-player backend CPU — by compiling the open-source **mkxp** engine to WebAssembly.

## What is this

RGSS-Web is a fork of [pulsejet/mkxp-web](https://github.com/pulsejet/mkxp-web) (itself based on [Ancurio/mkxp](https://github.com/Ancurio/mkxp)), a clean-room, open-source reimplementation of Enterbrain's RGSS runtime. mkxp is compiled to WebAssembly with Emscripten and runs Ruby through the embedded **mruby** VM (not CRuby/MRI).

The key change over upstream is in `src/graphics.cpp`: `FPSLimiter::delayTicks()` now calls `emscripten_sleep()`. Via ASYNCIFY/JSPI this unwinds the entire C+Ruby call stack once per frame, so a game's own blocking RGSS render loop (`loop { Graphics.update }`) yields to the browser naturally. You no longer have to rewrite the game's event loop into engine callbacks the way upstream mkxp-web required. On top of that, this fork migrates the build to JSPI and patches two mruby 2.1.2 VM bugs (escaping-proc environment unshare; a `super` + `alias` guard) in `extra/vm.c.patch`.

The practical result: a full, heavily-scripted RGSS1 fan-game booted and played in the browser — something the stock upstream demo never demonstrated. This repository ships **no game and no assets** — it is a toolkit plus a porting guide for bringing your own game.

### Features

- mkxp (C++ RGSS1 reimplementation) compiled to WASM via Emscripten, pinned emsdk **3.1.61**
- JSPI / `emscripten_sleep()` frame-yield — run a game's native RGSS loop unmodified
- Patched mruby 2.1.2 for RGSS-heavy scripts
- Reproducible Docker build; static-only hosting (zero server CPU per player)
- "Bring your own game" pipeline (`import-game.sh`) — no engine rebuild to add a game
- Browser harness: keyboard + on-screen d-pad, fullscreen, Service Worker offline cache
- Generic mruby/RGSS compatibility shims (`extra/rgss.rb`)

## Compatibility (honest)

This is **not** a drag-and-drop "drop your game in and it runs" solution. Expect work that scales with how heavily your game is scripted:

| Tier | Games | What it takes |
| --- | --- | --- |
| **Green** | Vanilla / lightly-scripted RGSS1 games | Light mechanical porting + asset processing. Close to "just works." Not exhaustively tested across arbitrary games. |
| **Yellow** | Heavily-scripted frameworks (e.g. Pokémon Essentials-class) | **Substantial per-game mruby porting is still required**, even with the engine fixes and `rgss.rb` shims. This repo gives you a toolkit and a porting guide, not automation. |
| **Red** | Plugin-heavy / custom-script games | Unknown per-game mruby walls. There is no pre-flight compatibility linter — you find the walls by hitting them. |

**Structurally unsupported:** MRI/CRuby-only scripts (mruby only), video playback, WMA audio, native Win32API features (mouse hooks, INI, clipboard beyond the `rgss.rb` stub), and extremely large bitmaps.

## Quick start

The build runs entirely inside Docker. `--platform linux/amd64` is required — the pinned emsdk ships no arm64-linux binaries, so Apple Silicon runs it under emulation (the wasm output is identical either way).

### 1. Build the builder image

```bash
docker build --platform linux/amd64 -t mkxp-web-jspi .
```

### 2. Build the engine

Compiled `mkxp.wasm` / `mkxp.js` are **not** committed (gitignored) — build them. `build-mkxp.sh` fetches and compiles the deps (libsigc++, pixman, physfs, mruby 2.1.2) and the engine, then packages the stock open-source demo game so you have something runnable:

```bash
docker run --rm --platform linux/amd64 \
  -v "$PWD/mkxp-web:/src/mkxp-web" \
  -v "$PWD/build-mkxp.sh:/build-mkxp.sh" \
  mkxp-web-jspi /build-mkxp.sh
```

Output lands in `mkxp-web/build/`. You only rebuild the engine (`rebuild-jspi.sh`, `rebuild-lto.sh`, `rebuild-full.sh`, `rebuild-engine.sh`) when you change the C++ or mruby — **never** just to add a game. See [docs/BUILDING.md](docs/BUILDING.md).

### 3. Import your game

The engine WASM is game-agnostic: it reads `Game.ini` + `Data/Scripts.rxdata` at **runtime**, so adding a game is a re-packaging task, not a recompile. Mount your RMXP game read-only at `/game` and run:

```bash
docker run --rm -it --platform linux/amd64 \
  -v "$PWD/mkxp-web:/src/mkxp-web" \
  -v "$PWD/import-game.sh:/import-game.sh" \
  -v "/path/to/YourRMXPGame:/game:ro" \
  mkxp-web-jspi /import-game.sh <namespace> "Window Title"
```

`<namespace>` is the per-game IndexedDB save-key prefix (keep it unique). This step copies + strips assets, converts audio (MIDI/WMA must be converted; MP3/WAV/OGG already play at runtime), generates the loader maps (`mapping.js` with directory markers, `bitmap-map.js`, per-map preloads), extracts `Scripts.rxdata` to `scripts_src/` for porting, installs the `rgss.rb` shim, and sets the IndexedDB namespace + window title in `index.html`. See [docs/BRING-YOUR-OWN-GAME.md](docs/BRING-YOUR-OWN-GAME.md).

For a script-heavy game, port `scripts_src/*.rb` to mruby ([docs/PORTING-mruby.md](docs/PORTING-mruby.md)), then repack and force the loader to refetch:

```bash
ruby scripts_tool.rb pack scripts_src build/gameasync/Data/Scripts.rxdata
# then re-run regen-mapping.sh (or bump the Scripts.rxdata ?h= hash in mapping.js)
```

Games that need a non-stock internal resolution call `Graphics.resize_screen(w, h)` at boot; the default is stock RGSS1 640×480.

### 4. Serve

Host `mkxp-web/build/` as static files. Requirements:

- Serve `.wasm` with `Content-Type: application/wasm` (enables `WebAssembly.instantiateStreaming`).
- Brotli/gzip + `Cache-Control` are recommended.
- **Do not** add COOP/COEP headers. This build is single-threaded (no pthreads / no `SharedArrayBuffer`), so cross-origin isolation is unnecessary.
- Production should be **HTTPS**: the Service Worker (offline + fast reloads) needs a secure context.

`deploy/` ships a Caddy container (`deploy/Dockerfile`, `deploy/Caddyfile`, `deploy/docker-compose.yml`) with correct wasm MIME, Brotli, and caching preconfigured; `deploy/apache.htaccess` is an Apache alternative. See [docs/DEPLOY.md](docs/DEPLOY.md).

## How it works

**mkxp → WASM + mruby.** mkxp is a C++ reimplementation of the RGSS1 interface (Graphics, Sprite, Bitmap, Table, RPG::* … backed by SDL2 / OpenGL ES / pixman). Emscripten compiles it to WebAssembly, and the embedded mruby VM executes the game's Ruby scripts. There is no CRuby: everything the game does runs through mruby, which is the source of the porting constraint above.

**JSPI frame-yield.** A native RMXP game spins a blocking Ruby loop that never returns to the host — fine on Windows, fatal in a browser's single event loop. This fork's `FPSLimiter::delayTicks()` calls `emscripten_sleep()`, which under JSPI (`-s JSPI`) suspends and unwinds the whole C+Ruby stack back to the browser each frame, then resumes it on the next tick. The game keeps its own `loop { Graphics.update }` untouched; the engine handles the yielding. This is why you don't rewrite the event loop.

**Game read at runtime = no per-game recompile.** The engine loads `Game.ini` and `Data/Scripts.rxdata` (plus Graphics/Audio/Data) at runtime from a virtual filesystem fed by the generated asset maps. Swapping games means re-running `import-game.sh`, not rebuilding `mkxp.wasm`.

## Project status / maintenance

This is a revival of an abandoned 2023 upstream proof-of-concept, brought to the point of actually running a real, script-heavy game. It is currently **single-maintainer**. A few things worth knowing before you rely on it:

- The toolchain is pinned to **emsdk 3.1.61** and **mruby 2.1.2** with local patches; upgrading either is known to be fragile.
- **JSPI is a young browser feature** (needs a recent Chromium-based browser); expect rough edges.
- **mruby-only is structural**, not a TODO — there is no plan to run MRI in the browser.

Contributions welcome — issues, ports, shims, and compatibility notes especially.

## License & attribution

Licensed under the **GNU General Public License, version 2 or later** (GPL-2.0-or-later). See [COPYING](COPYING).

- **mkxp** © 2013–2023 Jonas Kulla (Ancurio) and contributors — GPL-2.0-or-later
- **mkxp-web** © 2021–2023 pulsejet and contributors — GPL-2.0-or-later
- **this fork** — GPL-2.0-or-later

See [NOTICE](NOTICE) for third-party components and their licenses, and [CHANGES.md](CHANGES.md) for the list of modifications from upstream (per GPLv2 §2a).

## Legal note — bring your own game

This repository contains **no game content**: no RPG Maker XP RTP assets, no Enterbrain RGSS runtime DLLs, no proprietary fonts, and no third-party game or franchise content of any kind. mkxp is an independent open-source reimplementation and uses none of Enterbrain's code.

You supply your own RMXP game files, and **you are solely responsible for their licensing and rights**. RPG Maker XP RTP assets are proprietary (Enterbrain EULA), and many fan-games carry both their framework's licensing and third-party intellectual property. Do not redistribute content you don't have the rights to through this engine.
