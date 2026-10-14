#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# zp12 in the browser: the firmware's sources (firmware/src, unchanged) on a fake FM-1 (sim/hal, sim/sim.c)
# -> WebAssembly, plus the page.
#   sh sim/build.sh        ->  docs/sim/{index.html, sim.js, sim.css, worklet.js, zp12.wasm, favicon.svg}
#   then: python3 -m http.server -d docs/sim 8737   and open http://localhost:8737
# Tools (once, nothing system-wide; override with ZIG= / WASM_OPT=), the same as OberVave's web/sim/build.sh:
#   zig cc (wasm32-freestanding, as winampFM's firmware/build-wasm.sh):
#     python3 -m venv ~/.local/share/obervave-wasm && ~/.local/share/obervave-wasm/bin/pip install ziglang
#   Binaryen's wasm-opt (Asyncify: the main loop never returns, so it runs as a coroutine; see sim/sim.c):
#     https://github.com/WebAssembly/binaryen/releases (binaryen-version_133-x86_64-linux.tar.gz),
#     unpacked into ~/.local/share/obervave-wasm/
# The version shown is ZP12_VER (default: the README's "Status: X.Y").
set -e
cd "$(dirname "$0")/.."
T="$HOME/.local/share/obervave-wasm"
if [ -z "$ZIG" ]; then
    if [ -x "$T/bin/python" ] && "$T/bin/python" -c "import ziglang" 2>/dev/null; then ZIG="$T/bin/python -m ziglang"
    elif command -v zig >/dev/null 2>&1; then ZIG=zig
    else echo "sim: no zig (see the top of sim/build.sh)"; exit 1; fi
fi
if [ -z "$WASM_OPT" ]; then
    WASM_OPT=$(ls -d "$T"/binaryen-*/bin/wasm-opt 2>/dev/null | sort | tail -n 1)
    [ -n "$WASM_OPT" ] || WASM_OPT=$(command -v wasm-opt || true)
    [ -n "$WASM_OPT" ] || { echo "sim: no wasm-opt (see the top of sim/build.sh)"; exit 1; }
fi
VER=${ZP12_VER:-$(sed -n 's/.*\*\*Status: \([0-9][0-9.]*\)\*\*.*/\1/p' README.md | head -n 1)}
[ -n "$VER" ] || VER=0.0
mkdir -p build/sim build/gen docs/sim
[ -f build/gen/zp12_kit.h ] || python3 -c "import sys; sys.path.insert(0, 'tools'); import gen_kit; gen_kit.main('build/gen/zp12_kit.h')"
# -Isim/hal in place of firmware/hal: the fake FM-1. -fno-builtin: libc.c brings memset / memcpy / memcmp.
nice -n 15 $ZIG cc --target=wasm32-freestanding -mcpu=generic+bulk_memory -O2 -fno-builtin -ffreestanding -nostdlib \
    -w -DZP12_VER="\"$VER\"" -Isim/hal -Ifirmware/src -Ifirmware/gen -Ibuild/gen \
    -Wl,--no-entry -Wl,--export-memory -Wl,--export-dynamic -Wl,-z,stack-size=262144 \
    -o build/sim/raw.wasm sim/sim.c
nice -n 15 "$WASM_OPT" build/sim/raw.wasm --enable-bulk-memory --asyncify -O2 --strip-debug --strip-producers \
    -o docs/sim/zp12.wasm
cp sim/index.html sim/sim.js sim/sim.css sim/worklet.js docs/sim/
cp docs/favicon.svg docs/sim/favicon.svg                # (its own copy: the page also works elsewhere, e.g. on the hub)
echo "sim: docs/sim, zp12 $VER ($(wc -c < docs/sim/zp12.wasm | tr -d ' ') B wasm)"
