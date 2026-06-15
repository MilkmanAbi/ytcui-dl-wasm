# ytcui-dl WebAssembly build
# Requires Emscripten SDK (emcc). Install: https://emscripten.org/docs/getting_started/
#
# Usage:
#   make                      # build with default CORS proxy (corsproxy.io)
#   make CORS_PROXY=""        # build without proxy (direct — will fail on YouTube)
#   make CORS_PROXY="https://myproxy.example.com/?url="   # custom proxy
#   make clean
#
# Output: dist/ytfast.js + dist/ytfast.wasm
# Copy both files to your GitHub Pages project. Load with:
#   <script src="ytfast.js"></script>

CORS_PROXY ?= https://corsproxy.io/?

EMCC      := emcc
CXX_STD   := -std=c++17
OPTFLAGS  := -O2
INCLUDES  := -Iinclude

# emscripten_fetch needs -sFETCH=1. We use synchronous fetch on a pthread
# worker, so -sUSE_PTHREADS=1 is also needed.
# ALLOW_MEMORY_GROWTH so large search-result JSON doesn't OOM.
# MODULARIZE wraps everything in a factory function — cleaner to load.
EMFLAGS   := \
    -sFETCH=1 \
    -sUSE_PTHREADS=1 \
    -sPTHREAD_POOL_SIZE=2 \
    -sALLOW_MEMORY_GROWTH=1 \
    -sEXPORT_ES6=1 \
    -sMODULARIZE=1 \
    -sEXPORT_NAME=YtFastModule \
    -sEXPORTED_RUNTIME_METHODS="['ccall','cwrap']" \
    --bind \
    -sENVIRONMENT=web,worker \
    -sINITIAL_MEMORY=33554432

# Encode the CORS proxy URL so the shell doesn't choke on ? & =
PROXY_DEFINE := -DYTFAST_CORS_PROXY='"$(CORS_PROXY)"'

CXXFLAGS := $(CXX_STD) $(OPTFLAGS) $(INCLUDES) $(EMFLAGS) $(PROXY_DEFINE)

SRC      := ytfast_wasm.cpp
OUT_DIR  := dist
TARGET_JS  := $(OUT_DIR)/ytfast.js
TARGET_WA  := $(OUT_DIR)/ytfast.wasm

.PHONY: all clean info

all: info $(OUT_DIR) $(TARGET_JS)

info:
	@echo "Building ytcui-dl WASM (CORS proxy: $(if $(CORS_PROXY),$(CORS_PROXY),(none — direct)))"

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

$(TARGET_JS): $(SRC) include/ytfast_innertube.h include/ytfast_http.h include/ytfast_types.h
	$(EMCC) $(CXXFLAGS) $(SRC) -o $(TARGET_JS)
	@echo "Built: $(TARGET_JS) + $(TARGET_WA)"
	@ls -lh $(TARGET_JS) $(TARGET_WA)

clean:
	rm -rf $(OUT_DIR)
