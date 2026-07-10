MRuby::Build.new do |conf|
    toolchain :gcc
    conf.gembox 'default'
end

MRuby::CrossBuild.new('wasm32-unknown-gnu') do |conf|
    toolchain :clang
  
    conf.gembox 'default'
    # Pin mruby-onig-regexp (a transitive dep of pulsejet/mruby-marshal) to the
    # commit that was `master` when mkxp-web last built (Apr 2023). Newer master
    # (2025+) adds `#include <mruby/presym.h>`, which does not exist in mruby 2.1.2
    # and breaks the build. Declared BEFORE marshal so its add_dependency resolves
    # to this pinned commit instead of re-cloning master.
    conf.gem :github => 'mattn/mruby-onig-regexp', :checksum_hash => '074325207f9181ad242ffb8de34072607164f57f'
    conf.gem :github => 'pulsejet/mruby-marshal'
    conf.gem :github => 'monochromegane/mruby-time-strftime'
    conf.gem :core => 'mruby-eval'
    conf.cc.command = 'emcc'
    # WEB PORT: enable C++ exception catching so it matches mkxp's build. Without
    # this, libmruby.a's setjmp landing pads are compiled out and mruby's own
    # longjmp-based raise/rescue can't be caught when mkxp enables exceptions,
    # surfacing as "Uncaught int". Must be consistent across mruby + mkxp.
    # WEB PORT (JSPI): full wasm-native EH. -fwasm-exceptions (native wasm exception
    # handling) + SUPPORT_LONGJMP=wasm (native wasm setjmp/longjmp for mruby's
    # raise/rescue). Replaces emscripten-mode (-sDISABLE_EXCEPTION_CATCHING=0 +
    # ASYNCIFY-based setjmp), which is incompatible with JSPI's native stack
    # switching ("undefined symbol: saveSetjmp"). emcc rejects wasm-longjmp mixed
    # with emscripten-EH, so both must be wasm-mode. Must match mkxp's link flags.
    # WEB PORT PERF: -flto (link-time optimization). Compiles mruby's C sources to
    # LLVM bitcode so the final emcc link can inline/optimize across the whole VM +
    # mkxp together (opcode dispatch, hot helpers). emar/llvm-ar handle bitcode
    # archives fine. Must match mkxp's link flags (CMakeLists EMS_FLAGS also gets -flto).
    conf.cc.flags = %W(-O3 -g0 -flto -fwasm-exceptions -sSUPPORT_LONGJMP=wasm)
    conf.cxx.command = 'em++'
    conf.cxx.flags = %W(-O3 -g0 -flto -std=c++14 -fwasm-exceptions -sSUPPORT_LONGJMP=wasm)

    conf.linker.command = 'emcc'
    conf.archiver.command = 'emar'
end
