# Porting a script-heavy RGSS1 game from MRI/CRuby to mruby

This guide describes how to get a heavily-scripted RPG Maker XP (RGSS1) game — the
motivating example being a Pokémon Essentials fan-game — to boot and run under
**RGSS-Web** (this fork, `mkxp-web-jspi`). The engine embeds **mruby 2.1.2**, not
CRuby/MRI. RGSS scripts written and tested against RPG Maker XP's bundled MRI
interpreter will not run unchanged: the language dialect, standard library, and C
extensions all differ.

Be honest about the shape of the work:

- **Bounded, mechanical** (this doc, sections 3–4): syntax and dialect differences you
  can grep for and fix in a pass. Finite, predictable.
- **Unbounded, semantic** (sections 5–6): missing standard-library and Win32 features.
  Some are covered by generic shims in `extra/rgss.rb`; the rest surface one at a time
  as you exercise the game and must be ported per-game. There is **no pre-flight compat
  linter** — you find these by running the game until it stops.

For vanilla or lightly-scripted RGSS1 games, most of this is unnecessary — light
mechanical porting plus asset processing gets close to "just works" (not exhaustively
tested across arbitrary games). For Essentials-class games, expect substantial
per-game porting even with the engine fixes and shims below.

The engine itself is game-agnostic: `mkxp.wasm` reads `Game.ini` and
`Data/Scripts.rxdata` at **runtime**. Porting scripts is a re-packaging task
(`import-game.sh`, then repack `Scripts.rxdata`) — you do **not** rebuild the engine
to port a game. You only rebuild (`rebuild-*.sh`) when you change the C++/mruby.

---

## 1. The event loop needs NO rewrite here

RGSS games drive their own frames with a blocking loop:

```ruby
loop do
  Graphics.update
  Input.update
  # ... scene logic ...
end
```

Under stock Emscripten this deadlocks the browser: the loop never returns to the JS
event loop, so the canvas never repaints and no keyboard/timer events are delivered.
Upstream `pulsejet/mkxp-web` worked around this by requiring you to rewrite the game's
top-level loop into an Emscripten `main_update_loop` callback that returns once per
frame — an invasive change that fights every scene the game defines.

**This fork removes that requirement.** `src/graphics.cpp`,
`FPSLimiter::delayTicks()` calls `emscripten_sleep()` (ASYNCIFY / JSPI) once per frame:

```c++
// src/graphics.cpp — delayTicks()
uint64_t ms = ticks / tickFreqMS;
if (ms < 1) ms = 1;   /* always yield at least once per frame */
emscripten_sleep((unsigned int)ms);
```

`emscripten_sleep()` unwinds the **entire** C+Ruby call stack — including intervening
C frames such as `Proc#call` — hands control to the browser for the frame interval,
then rewinds and resumes exactly where it left off. Every `Graphics.update` therefore
yields a frame to the browser, so a game's native blocking `loop { Graphics.update }`
runs one frame per iteration with **no source changes**. `binding-mruby.cpp` no longer
installs an `emscripten_set_main_loop()` callback — the game's own loop drives frames.

Practical consequences:

- Do **not** restructure scenes into callbacks. Leave `Scene_*#main`,
  `pbUpdateSceneMap`, message-box wait loops, etc. as-is.
- Any code path that spins waiting on input **must** call `Graphics.update` (or
  `Input.update`, which updates graphics) inside the loop — that is the yield point.
  A busy `loop { break if condition }` with no `Graphics.update` will hang the tab.
  This is already how correct RGSS code is written, so ordinary games are fine.

---

## 2. The two mruby-2.1.2 VM fixes (`extra/vm.c.patch`)

mruby 2.1.2 has two VM bugs that heavily-aliased RGSS/Essentials code trips over.
Both are patched into `deps/mruby/src/vm.c` via `extra/vm.c.patch`, applied during the
mruby build (`rebuild-jspi.sh` / `rebuild-lto.sh` run
`patch -p0 --forward < ../../extra/vm.c.patch`). You do **not** edit game scripts for
these — they are engine fixes — but knowing the symptoms saves hours of misdiagnosis.
The patch has four hunks; two are the substantive fixes below, plus a related `super`
no-op guard and the original mkxp-web fixnum-division fix.

### 2a. `super` + `alias` target-class guard

**Symptom:** `TypeError: self has wrong type to call super in this context`, raised
from methods reached through deep `alias`/redefine chains. Essentials plugins do this
constantly (`alias old_dispose dispose; def dispose; ...; old_dispose; end` on
`Sprite_Character`, scenes, etc., stacked many layers deep).

**Cause:** mruby 2.1.2 mis-computes `MRB_PROC_TARGET_CLASS` through those chains, so the
`super` receiver fails an `mrb_obj_is_kind_of` check that should pass.

**Fix:** instead of raising, fall back to the receiver's real class
(`mrb_class(mrb, recv)`) and let the super search walk `self`'s actual ancestry.

### 2b. Escaping-proc top-level env force-unshare

**Symptom:** `NoMethodError: undefined method '...'` raised from a **stored callback**
(not at definition time) — typically after later script sections load or after GC. The
classic trigger is Essentials registering event handlers at script top level:

```ruby
Events.onMapUpdate    += proc { |sender, e| ... }
Events.onStepTaken    += proc { ... }
```

**Cause:** each `Scripts.rxdata` section is loaded as a separate top-level run. mruby's
`OP_STOP` never unshares the top-level environment, and `mrb_env_unshare` deliberately
skips `cibase->env`. The escaping `proc` captures a `self`/locals living in that
shared on-stack env; once the section returns and the VM stack is reused for the next
section or for gameplay, that captured `self` becomes garbage, and bare top-level calls
from inside the stored proc fail.

**Fix:** after each top-level load returns, heap-copy the top-level env (mirroring
`mrb_env_unshare` but **without** its `cibase` skip) so escaping procs stay valid for
the whole session.

---

## 3. Mechanical dialect fixes (grep-able)

These are finite, syntactic differences between MRI-era RGSS Ruby and mruby 2.1.2. Fix
them in the extracted `scripts_src/*.rb` before repacking. All are greppable.

### 3a. CR / CRLF line endings

RMXP stores scripts with Windows line endings. Stray `\r` can confuse mruby's lexer on
some constructs. Normalize to LF:

```bash
# in scripts_src/
find . -name '*.rb' -exec perl -i -pe 's/\r\n/\n/g; s/\r/\n/g' {} +
```

### 3b. `when X:` → `when X then`

MRI once accepted a colon as the `when`/`then` separator; mruby does not.

```ruby
# before
case n
when 1: foo
when 2: bar
end

# after
case n
when 1 then foo
when 2 then bar
end
```

Grep: `grep -rnE 'when .+:\s*\S' scripts_src` (inspect hits — skip hash/ternary colons).

### 3c. `begin ... end until COND` → `loop`

mruby does not support the do/while form where the modifier runs the body at least once.

```ruby
# before
begin
  step
end until done?

# after
loop do
  step
  break if done?
end
```

Same for `begin ... end while COND` (invert the break condition). Grep:
`grep -rnE 'end (until|while) ' scripts_src`.

### 3d. `defined?(X)`

mruby 2.1.2 does **not** implement the `defined?` keyword. Rewrite each use to the
concrete check the code actually wanted:

```ruby
defined?(SomeConst)              # -> Object.const_defined?(:SomeConst)
defined?(@ivar)                  # -> instance_variable_defined?(:@ivar)
defined?(some_method)            # -> respond_to?(:some_method)  (or a rescue)
defined?($global)                # -> !$global.nil?  (globals default to nil)
if defined?(X) && X.something    # -> guard with the const/respond_to? form above
```

There is no single mechanical substitution — you must read each call site to see which
kind of "is this defined" it means. Grep: `grep -rn 'defined?' scripts_src`.

### 3e. `$!` → explicit `rescue => e`

mruby does not populate the `$!` global with the current exception. Bind it explicitly:

```ruby
# before
begin
  risky
rescue
  log($!.message)
end

# after
begin
  risky
rescue => e
  log(e.message)
end
```

Grep: `grep -rn '\$!' scripts_src`. Also audit `$@` (backtrace global) similarly.

---

## 4. "too big code block" — split oversized methods

mruby's compiler has a hard per-`irep` limit and rejects very large method/block
bodies with a **`too big code block`** parse error. Essentials has a few monster
methods (giant `case` dispatchers, event-command interpreters) that exceed it.

There is no flag to raise the limit — you split the method:

```ruby
# before: one 900-line method
def pbExecuteCommand(cmd)
  case cmd
  when 1 ; ...            # hundreds of branches
  when 2 ; ...
  # ...
  end
end

# after: split by range into helpers, each under the limit
def pbExecuteCommand(cmd)
  return pbExecuteCommandA(cmd) if cmd < 200
  return pbExecuteCommandB(cmd) if cmd < 400
  pbExecuteCommandC(cmd)
end
def pbExecuteCommandA(cmd) ; case cmd ; when 1 ; ... ; end ; end
def pbExecuteCommandB(cmd) ; case cmd ; when 200 ; ... ; end ; end
def pbExecuteCommandC(cmd) ; case cmd ; when 400 ; ... ; end ; end
```

Keep the split purely mechanical (preserve branch order and behavior). Large top-level
`begin/end` blocks and giant array/hash literals can hit the same limit — the same
"break it into pieces" approach applies.

---

## 5. Standard-library / native gaps covered by `extra/rgss.rb`

`extra/rgss.rb` is loaded **before** the game's `Scripts.rxdata` sections
(`binding-mruby.cpp` reads `rgss.rb` first, then each script section in order).
`import-game.sh` copies it to `build/gameasync/rgss.rb`. It provides the RGSS1 `RPG::`
data classes plus **generic** shims for mruby/browser gaps. What each covers, and what
is left to you:

| Feature | Shim in `rgss.rb` | What remains game-specific |
|---|---|---|
| `Win32API` | Permissive stub. Records the declared return type; `#call` returns `""` for pointer/string returns (`p`/`P`/`s`/`S`) and `0` otherwise, so construction and calls don't raise. | Any call site that depends on the **real** result (mouse position, MP3 playback, INI read/write, clipboard, screenshots) does nothing. Port those call sites to a real alternative or remove them. |
| `Thread` / `Mutex` | Synchronous no-ops. `Thread.new` runs its block inline; `Thread.critical`, `join`, `Mutex#synchronize` are stubs. | Anything relying on **actual** concurrency (background asset threads, timed threads). The browser build is single-threaded; restructure to synchronous. |
| `ObjectSpace` | `define_finalizer`/`undefine_finalizer` no-op; `each_object` yields nothing; `_id2ref` raises `RangeError` (behaves like a collected ref); `garbage_collect` calls `GC.start`. | Logic that truly needs to enumerate live objects. Best-effort sweeps degrade gracefully; hard dependencies must be rewritten. |
| `Zlib` streaming | `Zlib::Deflate.new(...) << data ... finish` and `Zlib::Inflate` wrappers on top of the native one-shot `Zlib::Deflate.deflate` / `Zlib::Inflate.inflate` provided by the binding, plus the `Zlib::*` flush/level constants. | Usually nothing — covers the PNG/save streaming API Essentials uses. |
| `ENV` | Empty hash so `ENV["X"]` returns `nil` instead of raising. | Anything that needs real environment values. |
| `Dir` | Benign stubs: `glob`/`[]`/`entries` return `[]`, `pwd` returns `"."`, `mkdir`/`chdir`/`foreach` no-op. | Code that genuinely needs to enumerate files at runtime (e.g. globbing animation frames) gets nothing back — hardcode lists or preprocess. |
| `Errno` / `SystemCallError` | Minimal `Errno::ENOENT/EINVAL/EACCES/...` classes so `rescue Errno::ENOENT` clauses resolve. | The rescues catch, but the underlying I/O still isn't real. |
| `Time` marshalling | `Time#_dump` / `Time._load` so `Time` round-trips as an integer epoch through Marshal (in-game saves that carry live `Time` objects). | Sub-second precision / timezone fidelity is lost. |
| Audio tolerance | `Audio.se_play/bgm_play/bgs_play/me_play` wrapped to swallow decode errors so an unconvertible file plays silently instead of crashing gameplay. | This is a **safety net, not a fix** — convert MIDI/WMA to OGG at import time (see below). Files that error stay silent. |

> The audio shim exists because native mkxp decodes OGG (Vorbis). MP3/WAV/OGG play at
> runtime; **MIDI and WMA must be converted to OGG** during import (`import-game.sh`
> step 2 runs `extra/convert_audio.sh`). The shim only prevents a missing/undecodable
> file from crashing the game.

**Not covered anywhere** (mruby-only / browser limits — remove or re-implement per
game): MRI/CRuby-only C extensions, video playback, native Win32 features beyond the
`Win32API` stub above, and extremely large bitmaps.

Games differ, so `extra/rgss.rb` won't be exhaustive for yours. When you hit a
NameError/NoMethodError for a class or method that is genuinely generic (not
game-specific logic), the right move is often to add a small shim to a **copy** of
`rgss.rb` for your build rather than editing hundreds of call sites. Keep truly
game-specific fixes in the game's own scripts.

---

## 6. The debugging loop

Porting is iterative: run, hit the next error, fix, repack, run again. The tools:

### Extract / edit / repack

```bash
# one-time (import-game.sh step 3 already does this): extract to editable .rb
ruby scripts_tool.rb extract build/gameasync/Data/Scripts.rxdata scripts_src

# ... edit scripts_src/*.rb (order + section names preserved in index.json) ...

# repack after each round of edits
ruby scripts_tool.rb pack scripts_src build/gameasync/Data/Scripts.rxdata
```

After repacking, the loader must refetch the new `Scripts.rxdata`. Either re-run
`regen-mapping.sh` or bump the `?h=` hash on the `Data/Scripts.rxdata` entry in
`build/gameasync/mapping.js`, otherwise the browser (and Service Worker) will serve the
stale copy.

### Parse-check with `mrbc` inside the container

Runtime NameErrors need the browser, but **parse/compile** errors (`syntax error`,
`too big code block`, the `when X:` / `defined?` classes above) can be caught far
faster with mruby's compiler, `mrbc`, without booting the game. Run it inside the
build container (where the pinned emsdk-built mruby toolchain lives):

```bash
# syntax-check every script section without executing it
for f in scripts_src/*.rb; do
  mrbc -c "$f" || echo "PARSE FAIL: $f"
done
```

`mrbc -c` compiles and reports errors but does not run. This flushes out the entire
class of mechanical errors in section 3–4 in one pass, so your slower in-browser runs
are spent on genuine runtime/semantic issues.

> The mruby bin tools (`mrbc`, `mirb`, `mruby`) may fail to *link* during the
> wasm-EH engine build (they can't resolve wasm exception personality symbols); the
> rebuild scripts treat that as non-fatal because only `libmruby.a` matters for the
> engine. For parse-checking, use a host/native `mrbc` from the same mruby 2.1.2, or
> build the tools separately — the goal is only to reject bad syntax early.

### Suggested order per game

1. `import-game.sh <namespace> ["Title"]` (assets + extract + shims + maps).
2. Mechanical pass on `scripts_src` (section 3), then `find … perl` for line endings.
3. `mrbc -c` loop until all sections parse; split any `too big code block` methods (§4).
4. `pack`, refresh the hash, run in the browser.
5. Fix each runtime error in turn: add generic shims to your `rgss.rb` copy (§5) or
   port the specific call site. Repack and repeat.

That is the honest workflow: a **bounded** mechanical/parse phase (2–4) followed by an
**unbounded** semantic phase (5) whose size depends entirely on how much CRuby-only and
Win32-only behavior your game assumed.
