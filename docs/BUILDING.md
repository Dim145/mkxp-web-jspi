# Building the engine (`mkxp.wasm` / `mkxp.js`)

This document explains how to build the **RGSS-Web** engine (a fork of
[pulsejet/mkxp-web](https://github.com/pulsejet/mkxp-web), itself based on
[Ancurio/mkxp](https://github.com/Ancurio/mkxp); GPL-2.0-or-later) reproducibly with
Docker.

The engine is **game-agnostic**: `mkxp.wasm` reads the game's scripts —
`Data/Scripts.rxdata` (XP / RGSS1) or `Data/Scripts.rvdata` (VX / RGSS2) — at *runtime*.
You only need to rebuild the engine when you change its C++ or the mruby VM (or to switch
the pinned RGSS version — see `docs/RGSS2-VX.md`). **Adding or updating a game is a re-packaging task
(`import-game.sh`), not an engine rebuild** — see `docs/BRING-YOUR-OWN-GAME.md`.

---

## Prerequisites

- **Docker**, able to run `linux/amd64` images. On Apple Silicon this works under
  emulation (see below); no other host setup is required.
- **Disk**: budget several GB. The builder image (Ubuntu 22.04 + a pinned Emscripten
  SDK + build/game-processing tools) plus the fetched dependency sources and the
  Emscripten system-port cache add up.
- The engine sources live under `mkxp-web/` in this repo. The repo is bind-mounted into
  the container at build time, so **all artifacts land back on the host**.

No local Emscripten, CMake, or Ruby installation is needed — everything runs inside the
container.

---

## Why Emscripten is pinned to 3.1.61 (and not "latest")

The `Dockerfile` pins `EMSDK_VERSION=3.1.61`. This is deliberate:

- **3.1.61 is the earliest emsdk with the standard JSPI API** (JavaScript Promise
  Integration) that this engine relies on. The build uses:
  - `-s JSPI` (equivalently `ASYNCIFY=2`) — native WebAssembly **stack switching** in
    the VM instead of Binaryen's whole-program ASYNCIFY code rewrite. This is what lets
    a game's blocking RGSS render loop (`loop { Graphics.update }`) yield to the browser
    each frame — `FPSLimiter::delayTicks()` in `src/graphics.cpp` calls
    `emscripten_sleep()`, and the suspend unwinds the whole C+Ruby stack natively.
    JSPI removes ASYNCIFY's large (~50%) speed overhead.
  - `-fwasm-exceptions` — native wasm exception handling, so mkxp's binding
    `try/catch(Exception&)` and mruby's `raise`/`rescue` work.
  - `-s SUPPORT_LONGJMP=wasm` — native wasm `setjmp`/`longjmp`, which mruby's
    longjmp-based `raise`/`rescue` needs.

  These three must be consistent between mruby and mkxp. emcc **rejects** wasm-longjmp
  mixed with emscripten-mode exception handling, so everything is built wasm-native.

- **"latest" is not used** because it breaks this 2023-era mkxp-web codebase, and mixing
  toolchain versions across the dependencies causes ABI mismatches. 3.1.61 is close
  enough to the original codebase to build it while still providing the standard JSPI
  API. Building **all** deps + mkxp with this one toolchain avoids version-mixing bugs
  (e.g. a physfs built with an older libc failing `PHYSFS_init` at runtime).

JSPI requires a JSPI-capable browser at runtime (Chrome 126+).

## `--platform linux/amd64` (Apple Silicon note)

The `Dockerfile` starts with `FROM --platform=linux/amd64 ubuntu:22.04`, and the
`docker run` commands pass `--platform linux/amd64`. The pinned emsdk ships **no
arm64-linux prebuilt binaries**, so on Apple Silicon the toolchain runs under emulation.
The compile target is `wasm32` regardless of host architecture, so **the produced
`.wasm` is byte-for-byte the same** whether you build on x86_64 or an emulated arm64
host.

---

## Step 1 — build the builder image

From the repo root:

```bash
docker build --platform linux/amd64 -t mkxp-web-builder .
```

This produces an image containing Ubuntu 22.04, the build toolchain
(`build-essential`, `cmake`, `git`, `pkg-config`, autotools, `bison`, `ruby`/`rake`
for the mruby build), the game-processing tools (`imagemagick`, `xxd` via `vim-common`,
`file`, `ffmpeg`), and Emscripten **3.1.61** installed and activated on `PATH`.

---

## Step 2 — build the engine

There are several build scripts, all meant to run **inside** the container against the
bind-mounted repo. Pick the one that matches what you changed.

### Full build from clean (deps + engine): `build-mkxp.sh`

```bash
docker run --rm --platform linux/amd64 \
  -v "$PWD:/src" \
  mkxp-web-builder /src/build-mkxp.sh
```

`build-mkxp.sh` does the whole thing from scratch:

1. Fetches and builds the manual dependencies into `mkxp-web/deps/`:
   **libsigc++ 2.12.0**, **pixman 0.42.0**, **physfs 3.0.2**, and **mruby 2.1.2**.
   (Each step is guarded by an "is the `.a` already there?" check, so re-runs are
   incremental.)
2. For mruby, it copies `extra/build_config.rb` and `extra/vm.c.patch` in, applies the
   patch (`patch -p0 --forward`), and runs `make`.
3. Configures and compiles the mkxp engine with `emcmake cmake .` + `emmake make`.
4. Copies `mkxp.wasm`, `mkxp.js`, and the harness assets into `mkxp-web/build/`, then
   downloads and processes the **stock demo game** (Knight Blade) so there is something
   runnable, and renames `mkxp.html` → `index.html`.

Use this for a cold, reproducible build.

> Note: `build-mkxp.sh` builds the manual deps with plain `-O3 -g0` flags. The primary
> JSPI / wasm-EH build flags live in `CMakeLists.txt` and `extra/build_config.rb`; the
> `rebuild-*.sh` scripts below export the matching `-fwasm-exceptions` /
> `-sSUPPORT_LONGJMP=wasm` flags explicitly when they rebuild deps. If you are doing a
> clean JSPI build and hit link/runtime failures from the deps, rebuild them with the
> JSPI flags via `rebuild-jspi.sh REBUILD_DEPS=1` (below).

### Engine-only rebuild: `rebuild-engine.sh`

```bash
docker run --rm --platform linux/amd64 \
  -v "$PWD:/src" \
  mkxp-web-builder /src/rebuild-engine.sh
```

Recompiles **only** the mkxp engine (deps are reused from `mkxp-web/deps/`) and drops
the fresh `mkxp.wasm` + `mkxp.js` into `mkxp-web/build/`. It does **not** touch
`index.html` or `gameasync/`. Fastest loop for iterating on mkxp C++ that does not
change the deps or the mruby VM.

### Engine + mruby rebuild (VM patch changes): `rebuild-full.sh`

```bash
docker run --rm --platform linux/amd64 \
  -v "$PWD:/src" \
  mkxp-web-builder /src/rebuild-full.sh
```

Rebuilds `libmruby.a` **and** mkxp together. It wipes `deps/mruby/build`, re-copies
`extra/build_config.rb`, re-applies `extra/vm.c.patch`, rebuilds mruby, then rebuilds
mkxp. Use this when you edit `extra/vm.c.patch` or `extra/build_config.rb`.

### JSPI build with optional dep rebuild: `rebuild-jspi.sh`

```bash
# reuse existing deps, rebuild only mkxp with -s JSPI
docker run --rm --platform linux/amd64 \
  -v "$PWD:/src" \
  mkxp-web-builder /src/rebuild-jspi.sh

# also rebuild all manual deps with the JSPI/wasm-EH flags (do this if the link
# fails or physfs fails PHYSFS_init at runtime — an ABI mismatch)
docker run --rm --platform linux/amd64 \
  -e REBUILD_DEPS=1 \
  -v "$PWD:/src" \
  mkxp-web-builder /src/rebuild-jspi.sh
```

`rebuild-jspi.sh` (re)activates emsdk 3.1.61, optionally rebuilds pixman / physfs /
libsigc++ / mruby with `-fwasm-exceptions -sSUPPORT_LONGJMP=wasm` so their exception
handling matches mkxp's, then configures and builds mkxp. Before the parallel `make` it
**warms the emcc system-port cache serially** (one throwaway compile pulling in
SDL2/SDL2_image/SDL2_ttf/freetype/harfbuzz/ogg/vorbis/zlib) so that a fresh emsdk does
not have many parallel `emcc` processes racing for the cache lock.

### LTO build (performance): `rebuild-lto.sh`

```bash
docker run --rm --platform linux/amd64 \
  -v "$PWD/mkxp-web:/src/mkxp-web" \
  -v "$PWD/rebuild-lto.sh:/rebuild-lto.sh" \
  mkxp-web-builder /rebuild-lto.sh
```

Builds `libmruby.a` with `-flto` (LLVM bitcode) and links mkxp **natively** against it.
`wasm-ld` still LTO-optimizes the bitcode `libmruby.a` members it pulls in, giving the
mruby VM whole-VM optimization (the VM is the hot path) **without** the full-LTO
breakage described below. Reuses an existing `libmruby.a` if present; delete
`deps/mruby/build` to force a rebuild.

> After an LTO (or any) rebuild that clients have already cached, bump the asset version
> in `mkxp-web/build/index.html` so browsers and the Service Worker refetch the new
> `mkxp.wasm`.

---

## Why LTO is applied to `libmruby.a` only

`CMakeLists.txt` deliberately does **not** put `-flto` on the mkxp link
(`EMS_FLAGS`). LTO is confined to the mruby build (`extra/build_config.rb` sets
`-flto`). The reason, per the comment in `CMakeLists.txt`:

- Linking mkxp itself with `-flto` makes Emscripten swap in the **bitcode** system-lib
  variants (`lto/libc.a`). A JS-library dependency then pulls libc's `fileno` in *after*
  the LTO codegen pass, producing
  `wasm-ld: attempt to add bitcode file after LTO (fileno)`.
- Linking mkxp **natively** against a bitcode `libmruby.a` still LTO-optimizes mruby
  internally (wasm-ld codegens and optimizes the bitcode members it pulls in) *without*
  the bitcode-libc breakage.
- mkxp's own C++ is not the bottleneck (render is ~1.7 ms/frame), so losing
  mkxp-internal / mkxp↔mruby cross-inlining is negligible. emcc still runs Binaryen
  `wasm-opt` at `-O3` on the whole module regardless.

---

## Where the output lands (and what is not committed)

All build scripts copy the compiled engine to:

- `mkxp-web/build/mkxp.wasm`
- `mkxp-web/build/mkxp.js`

**These compiled artifacts are gitignored and are NOT committed.** Build them yourself
with the steps above, or use CI (`.github/`). Also gitignored: `mkxp-web/deps/`, the
CMake scratch files, intermediate `*.o`/`*.a`, and your game content
(`mkxp-web/build/gameasync/`, `mkxp-web/build/preload/`, `scripts_src/`).

Serving requirements (see the deployment docs / `deploy/`): serve `.wasm` with
`Content-Type: application/wasm` (enables `WebAssembly.instantiateStreaming`). This build
is **single-threaded** (no pthreads / no `SharedArrayBuffer`), so **COOP/COEP headers are
not required and should not be added**.

---

## How the mruby patch works (`extra/vm.c.patch`)

mruby is pinned to **2.1.2**. The build applies `extra/vm.c.patch` to
`deps/mruby/src/vm.c` (via `patch -p0 --forward`, so re-applying is a no-op). It was
regenerated by diffing the working `vm.c` against pristine mruby 2.1.2 so that a
from-clean build reproduces the engine exactly. It contains **four hunks**:

1. **`super` + `alias` target-class guard** (~L1577): mruby 2.1.2 mis-computes
   `MRB_PROC_TARGET_CLASS` through the deep multi-alias chains that Pokémon
   Essentials-class plugins build, raising a spurious *"self has wrong type to call
   super"*. The patch falls back to `self`'s real class so the super search walks
   `self`'s actual ancestry instead of aborting.
2. **`super` garbage-`mid` guard** (~L1593): when the alias-corrupted method id is not a
   valid symbol (`mrb_sym_name == NULL`), the intended parent name was already lost;
   make `super` a no-op returning `nil` rather than crashing the game with a bogus
   *"undefined method"*. A normal super to a genuinely missing method still has a valid
   `mid` and proceeds to `method_missing`.
3. **Fixnum integer-division fix** (~L2262): the original upstream mkxp-web patch for the
   integer / integer path.
4. **Escaping-proc top-level env force-unshare** (~L2842): after each top-level load
   finishes, heap-copy the top-level env. `OP_STOP` never unshares it and
   `mrb_env_unshare` deliberately skips `cibase->env`, but Essentials creates *escaping*
   procs at script top level (`Events.onMapUpdate += proc{...}`) whose captured
   self/locals live in that shared on-stack env. Once the load returns and the stack is
   reused, that self becomes garbage and bare top-level method calls from the stored proc
   raise *"undefined method"*. Copying the env to the heap keeps the stored procs valid
   for the whole session.

Hunks 1, 2, and 4 are the fork's novel VM fixes; hunk 3 is inherited from upstream
mkxp-web. `extra/build_config.rb` compiles mruby with the matching
`-fwasm-exceptions -sSUPPORT_LONGJMP=wasm -flto` flags so its exception handling and
`setjmp`/`longjmp` model line up with mkxp's link flags.
