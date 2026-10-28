# SPDX-License-Identifier: GPL-3.0-or-later
# fm1-txtr — host builds (tests, WAV render) and the browser build.
#
#   make            generate tables, build and run the tests
#   make render     render out/demo.wav (30 s with the default patch)
#   make web        build web/txtr.wasm (needs Zig: pip install ziglang)
#   make gen        regenerate core/gen and web/gen from params/params.py

CC      ?= gcc
CXX     ?= g++
PY      ?= python3
CFLAGS  += -MMD -MP -O2 -std=c99 -Wall -Wextra -Icore -Icore/gen -Ithird_party/msfa
CXXFLAGS+= -MMD -MP -O2 -std=c++14 -Wall -Icore -Icore/gen -Ithird_party/msfa
LDLIBS  += -lm

CORE_C   := $(wildcard core/*.c) core/gen/tx_params.c
CORE_CXX := $(wildcard core/*.cpp)
MSFA     := $(filter-out third_party/msfa/EngineMkI.cpp,$(wildcard third_party/msfa/*.cpp))
OBJ      := $(patsubst %,build/host/%.o,$(CORE_C) $(CORE_CXX) $(MSFA))

all: test

gen core/gen/tx_params.c core/gen/tx_params.h web/gen/params.js: params/params.py tools/gen_params.py
	$(PY) tools/gen_params.py

build/host/%.c.o: %.c core/gen/tx_params.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/host/%.cpp.o: %.cpp core/gen/tx_params.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Wno-unused-parameter -c $< -o $@

build/test_core: build/host/tests/test_core.c.o $(OBJ)
	$(CXX) $^ -o $@ $(LDLIBS)

build/render: build/host/tools/render.c.o $(OBJ)
	$(CXX) $^ -o $@ $(LDLIBS)

test: build/test_core
	./build/test_core

render: build/render
	@mkdir -p out
	./build/render out/demo.wav 30

# ---------------------------------------------------------------- browser (WebAssembly via Zig)
ZIG       ?= $(PY) -m ziglang
WASM_COMMON = -target wasm32-wasi -O2 -Icore -Icore/gen -Ithird_party/msfa -DTX_WEB -fno-exceptions
WASM_OBJ  := $(patsubst %,build/wasm/%.o,$(CORE_C) $(CORE_CXX) $(MSFA) web/txtr_web.c)

build/wasm/%.c.o: %.c core/gen/tx_params.h
	@mkdir -p $(dir $@)
	$(ZIG) cc $(WASM_COMMON) -std=c99 -MMD -MP -c $< -o $@

build/wasm/%.cpp.o: %.cpp core/gen/tx_params.h
	@mkdir -p $(dir $@)
	$(ZIG) c++ $(WASM_COMMON) -std=c++14 -fno-rtti -MMD -MP -c $< -o $@

web: web/txtr.wasm

web/txtr.wasm: $(WASM_OBJ)
	$(ZIG) c++ $(WASM_COMMON) -mexec-model=reactor -Wl,--export-dynamic -Wl,--strip-all $^ -o $@

clean:
	rm -rf build out

.PHONY: all gen test render web clean

-include $(shell find build -name '*.d' 2>/dev/null)
