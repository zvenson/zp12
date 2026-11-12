#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# ZP12 for the VIBE (native): built by the VIBE SDK's tools/build.sh as firmware/<id>/build.sh, from the SDK's root.
# The sound is zp12's own (../firmware/src: sp_core.c, sp_fx.c, sp_seq.c – the same files the FM-1 builds) and its
# factory kit (../build/gen/zp12_kit.h, made by ../tools/gen_kit.py when missing).
set -e
D=$(dirname "$0")
N=$(basename "$(cd "$D" && pwd -L)")          # the id: the link's name in the SDK (firmware/zp12n)
R=$(cd "$D" && pwd -P)                         # this folder, the real one (zp12's repo)
[ -f "$R/../build/gen/zp12_kit.h" ] || { mkdir -p "$R/../build/gen"; python3 "$R/../tools/gen_kit.py" "$R/../build/gen/zp12_kit.h"; }
T=build/obj/$N; rm -rf "${T:?}"; mkdir -p "$T"
for f in "$R/main.c" sdk/src/vibe_sdk.c sdk/src/vibe_font.c; do
  "$ZIG" cc -target wasm32-freestanding -O2 -ffast-math -fno-builtin-memset -matomics -mbulk-memory \
    -Isdk/include -Isdk/src -I"$R" -I"$R/../firmware/src" -I"$R/../build/gen" -Wall -Wno-unused-function \
    -c -o "$T/$(basename "$f" .c).o" "$f"
done
MB=$(grep -o '"memory_mb": *[0-9]*' "$R/firmware.json" | grep -o '[0-9]*$'); MB=${MB:-64}
"$ZIG" wasm-ld --no-entry --export-dynamic --export=__heap_base --export=__data_end \
  --shared-memory --max-memory=$((MB << 20)) -z stack-size=262144 -o "build/$N.wasm" "$T"/*.o
