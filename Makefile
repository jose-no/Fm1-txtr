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
CFLAGS  += -MMD -MP -O2 -std=c99 -Wall -Wextra -Wno-unused-function -Icore -Icore/gen -Ithird_party/fm6
LDLIBS  += -lm

CORE_C   := $(wildcard core/*.c) core/gen/tx_params.c
OBJ      := $(patsubst %,build/test/%.o,$(CORE_C))

all: test

gen core/gen/tx_params.c core/gen/tx_params.h web/gen/params.js: params/params.py tools/gen_params.py
	$(PY) tools/gen_params.py
	$(PY) tools/gen_fm6_tables.py

build/test/%.c.o: %.c core/gen/tx_params.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/test_core: build/test/tests/test_core.c.o $(OBJ)
	$(CC) $^ -o $@ $(LDLIBS)

build/render: build/test/tools/render.c.o $(OBJ)
	$(CC) $^ -o $@ $(LDLIBS)

test: build/test_core
	./build/test_core

render: build/render
	@mkdir -p out
	./build/render out/demo.wav 30

# ---------------------------------------------------------------- browser (WebAssembly via Zig)
ZIG       ?= $(PY) -m ziglang
WASM_COMMON = -target wasm32-wasi -O2 -Icore -Icore/gen -Ithird_party/fm6 -DTX_WEB -Wno-unused-function
WASM_OBJ  := $(patsubst %,build/wasm/%.o,$(CORE_C) web/txtr_web.c)

build/wasm/%.c.o: %.c core/gen/tx_params.h
	@mkdir -p $(dir $@)
	$(ZIG) cc $(WASM_COMMON) -std=c99 -MMD -MP -c $< -o $@

web: web/txtr.wasm

web/txtr.wasm: $(WASM_OBJ)
	$(ZIG) cc $(WASM_COMMON) -mexec-model=reactor -Wl,--export-dynamic -Wl,--strip-all $^ -o $@

clean:
	rm -rf build out

.PHONY: all gen test render web clean

-include $(shell find build -name '*.d' 2>/dev/null)
