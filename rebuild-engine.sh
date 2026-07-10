#!/bin/bash
# Recompile ONLY the mkxp engine (deps are cached) and drop the new
# mkxp.wasm + mkxp.js into build/. Does NOT touch index.html (keeps our
# __log debug shim) or gameasync/ (keeps the integrated game).
set -e
source /opt/emsdk/emsdk_env.sh
cd /src/mkxp-web
emcmake cmake .
emmake make -j"$(nproc)"
cp -f mkxp.wasm mkxp.js build/
echo ">>> engine rebuilt; wasm size:"; ls -la build/mkxp.wasm
