# RPG Maker VX (RGSS2) support

RGSS-Web began as an RGSS1 (RPG Maker XP) engine. It now also runs **RPG Maker VX
(RGSS2)** games. Both RGSS versions are compiled into a single `mkxp.wasm`; the active
version is chosen at build time (see below). This document covers what VX support
consists of, how to select it, and the VX-specific gotchas.

> **Scope:** RPG Maker **VX (RGSS2)** only. RPG Maker **VX Ace (RGSS3)** is a different
> runtime — Features-based data classes, `.rvdata2` — and is **not** supported.

## Selecting the RGSS version

The engine does **not** parse `Game.ini` at runtime — the RGSS version, default screen
size, and scripts path are pinned in `mkxp-web/src/config.cpp`:

| Setting | XP (RGSS1) | VX (RGSS2) |
| --- | --- | --- |
| `rgssVersion` | `1` | `2` |
| `defScreenW` × `defScreenH` | `640` × `480` | `544` × `416` |
| `game.scripts` | `Data/Scripts.rxdata` | `Data/Scripts.rvdata` |

The repository currently defaults to **VX (2)**. To target XP, set those three back to
the RGSS1 values and rebuild the engine ([BUILDING.md](BUILDING.md)). A game may still
call `Graphics.resize_screen(w, h)` at boot to override the resolution.

Both renderers are always compiled in; `binding-mruby/binding-mruby.cpp` binds the
version-appropriate `Window` / `Tilemap` from `rgssVer` at boot:

```cpp
if (rgssVer >= 2) { windowVXBindingInit(mrb); tilemapVXBindingInit(mrb); }  // VX
else              { windowBindingInit(mrb);   tilemapBindingInit(mrb);   }   // XP
```

## What VX support consists of

- **Renderers.** `binding-mruby/windowvx-binding.cpp` and `tilemapvx-binding.cpp` bind the
  RGSS2 `Window` and `Tilemap` (the multi-layer VX tilemap; VX window skin / arrows / pause
  / `openness`) to mruby, backed by the existing `src/windowvx.cpp` and `src/tilemapvx.cpp`
  engine classes. `binding-util` exposes the extra `flags` / `bitmaps` symbols the VX
  tilemap needs.
- **Config defaults.** VX render size (544×416), `rgssVersion = 2`, and
  `Data/Scripts.rvdata` — see the table above.
- **Data files.** VX Marshals `.rvdata` (Scripts, Maps, System, Actors, …) where XP used
  `.rxdata`. The on-demand asset loader (`mkxp-web/extra/js/drive.js`) accepts both
  `.rxdata` and `.rvdata` map files.
- **`viewport=` setter.** `binding-mruby/viewportelement-binding.h` binds the `viewport=`
  setter on Sprite / Window / Plane. VX default menu and battle scenes commonly reassign a
  drawable's viewport at runtime; upstream exposed only the getter, so those reassignments
  raised `undefined method 'viewport='`.
- **VX-shaped compat shim.** The `RPG::` data classes in `mkxp-web/extra/rgss.rb` expose
  the VX-shaped fields Marshalled from `.rvdata` (see the gotchas below).
- **Japanese filenames.** `src/filesystem.cpp` resolves NFC/NFD Unicode forms — VX RTP and
  many VX games ship Japanese asset names, which browsers/filesystems may normalise
  differently.
- **Bigger wasm stack + diagnostics.** `CMakeLists.txt` sets an 8 MB wasm stack (deeply
  nested RGSS/mruby script recursion plus the event interpreter keep a large native call
  stack live across every JSPI suspend; the 64 KB emscripten default overflows and, without
  SAFE_HEAP, silently corrupts memory). It also adds a `MKXP_WEB_DEBUG` build toggle
  (`ASSERTIONS=2` / `SAFE_HEAP=1` / `STACK_OVERFLOW_CHECK=2` + `-g2`, keeps `-O3`) that turns
  silent native faults into printed aborts, plus boot script-load tracing and script-error
  output to stderr.

## Porting gotchas specific to VX

VX games are RGSS2, written for **Ruby 1.8**, so the general mruby porting guide applies —
see [PORTING-mruby.md](PORTING-mruby.md). Two issues are specific to VX:

### The RPG data-class shim must expose VX fields

`extra/rgss.rb` defines the `RPG::` data classes. Some were originally **XP-shaped** (XP
field names), but VX `.rvdata` Marshals **VX field names**. `Marshal.load` sets the
`@ivars` directly (bypassing `initialize`), so if a class has no reader method for a VX
field, reads of it fail — in one of two ways:

- **Crash** — when nothing else defines the method: `undefined method 'X'` the first time a
  screen reads it (instantiating an enemy, drawing a skill, rendering a hit animation, …).
- **Silent wrong value** — when an add-on script's note-tag module (mixed into
  `RPG::BaseItem`) happens to define a method of the same name, the missing accessor falls
  through to the note-tag value (usually `0` / `false`), quietly corrupting combat or menu
  math with **no error**.

The shim already exposes the common VX fields: weapon / armor combat params and feature
flags (`hit`, `def`/`spi`/`agi`, `prevent_critical`, `half_mp_cost`, `double_exp_gain`,
`auto_hp_recover`, …), skill / item `icon_index` and skill `message1`/`message2`, enemy
`maxmp` / `def` / `spi` / `hit` / drops / action conditions, animation `animation1_name…`,
and state fields. If a VX screen crashes with `undefined method 'X'` on an `RPG::*` object,
or shows a stat that is wrong-but-not-crashing, `X` is almost always a VX field the shim
doesn't expose yet — add an `attr_accessor` (or a `def X; @X || default; end` reader) for
it in the relevant `RPG::` class.

### Standard mruby / Ruby-1.8 stdlib gaps

Identical to XP: methods that existed in Ruby 1.8 but were removed in 1.9/2.0 and are absent
from mruby (`Hash#index`, `Object#id`, `NilClass#to_i`/`#to_f`/`#to_a`, …). All are shimmed
in `extra/rgss.rb`. See [PORTING-mruby.md](PORTING-mruby.md) for the full list and how to
hunt new ones.

## Status

VX support is newer and less exercised than the XP path. The renderers, version dispatch,
config defaults, and VX data-class shim are in place, and a full, script-heavy VX game
builds and runs. Expect the same "find the wall by hitting it" porting reality described in
the README's compatibility section — plus new `RPG::*` field gaps as you reach screens that
read VX fields the shim hasn't needed yet.
