#!/usr/bin/env sh
# build.sh — compile the ytcui-dl InnerTube client to WebAssembly.
#
# Run this from the dev/ folder. It writes ytfast.js + ytfast.wasm to the repo
# ROOT (one level up), where index.html and GitHub Pages expect them.
#
# Requires the Emscripten SDK (emcc/em++) on PATH:
#   git clone https://github.com/emscripten-core/emsdk
#   cd emsdk && ./emsdk install latest && ./emsdk activate latest
#   source ./emsdk_env.sh
#
# Then:  cd dev && ./build.sh
set -e
cd "$(dirname "$0")"          # always run from this script's dir (dev/)
OUT=..                        # repo root

em++ -std=c++17 -O2 -fexceptions \
  -Iinclude -Iinclude/ytcui-dl \
  src/ytfast_bindings.cpp -o "$OUT/ytfast.js" \
  -sASYNCIFY \
  -sMODULARIZE=1 \
  -sEXPORT_ES6=1 \
  -sEXPORT_NAME=createYtFast \
  -sEXPORTED_RUNTIME_METHODS='["UTF8ToString","stringToUTF8","lengthBytesUTF8"]' \
  -sEXPORTED_FUNCTIONS='["_malloc","_free"]' \
  -sDISABLE_EXCEPTION_CATCHING=0 \
  -sALLOW_MEMORY_GROWTH=1 \
  -sENVIRONMENT=web \
  -sASYNCIFY_STACK_SIZE=131072 \
  --bind

echo "built $OUT/ytfast.js + $OUT/ytfast.wasm"

# Why these flags matter:
#   -sASYNCIFY               lets the synchronous C++ InnerTube code call the
#                            browser's async fetch() (see ytfast_http.h).
#   -fexceptions             + -sDISABLE_EXCEPTION_CATCHING=0: without these,
#                            emscripten compiles with -fignore-exceptions, so a
#                            C++ `throw` ABORTS the whole module instead of
#                            being caught — errors must come back as {ok:false}.
#   -Iinclude/ytcui-dl       so the vendored `#include <nlohmann/json.hpp>`
#                            (angle-bracket) resolves.
#   -sENVIRONMENT=web        target browsers; the .wasm is loaded via fetch().
