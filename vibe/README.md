# zp12 for the VIBE (native)

zp12's sound on the VIBE FM: the same engine files the FM-1 builds (`../firmware/src/sp_core.c`, `sp_fx.c`, `sp_seq.c`),
with the VIBE's inputs, its 480 × 320 screen and its controls. Not a second ZVAMPLER: few pages, everything at a glance,
the character first (12-bit waves at 26.04 / 27.5 kHz, pitch by the playback rate without interpolation, eight output
channels with their filters, the master's soft knee).

| File | |
| --- | --- |
| `main.c` | `vibe_on_*`: init (kit, saved state or a demo beat in loop 1), events, the sound in zp12's 32-frame blocks, the draw's chores (finishing a take, LOAD, writing to the card) |
| `zv_core.h` | the engine with the VIBE's switches (`SP_FS` = the host's rate, `SP_WIDE` 16-bit waves, `SP_TR` 60000 for fine START / END, `SQ_TICKS_K`), the own samples' pool and its time budget, `state.bin` / `seq.bin` / `lk.bin` / `wNNN-CCC.raw` |
| `zv_rec.h` | sampling (IN 1 LINE / MIC / HI-Z, IN 1+2, the computer's USB L / R – SDK 26), threshold with 20 ms before it, a windowed-sinc resampler to 26.04 / 27.5 kHz, normalising, LOAD of a WAV through the same path |
| `zv_ui.h` | the pages SAMPLE · EDIT · MIX · FX · SEQ · SONG, SETUP, the media browser, HOME, the picture |
| `cheatsheet.html` | the emulator's cheat sheet |

**12 / 16 bit:** own samples are kept as int16; a wave plays at 12 bits (the low four dropped, as the kit's packing does)
or at 16. The budget counts 1.5 bytes a sample at 12 bits and 2 at 16 (SETUP → TIME: 10 s, 30 s, 2 min, 5 min), so 16
bits costs a quarter more time. Switching is lossless (EDIT F1). The kit is 12-bit and free.

**The engine stays the FM-1's:** the switches default to the FM-1's values; `tests/sp_core_test.c` and
`tests/sp_seq_test.c` render bit for bit as before.

## Build and test (from the VIBE SDK)

`firmware/zp12n` in `~/VibeFMmk1/vibe-sdk` is a link to this folder (hidden there by `.git/info/exclude`; zp12 is not
part of the SDK's repo). `sh tools/build.sh zp12n` runs `build.sh` here (it makes `../build/gen/zp12_kit.h` with
`tools/gen_kit.py` when missing). Then `node tools/wasmtest.mjs build/zp12n.wasm <script.json> <out>` and
`node tools/vibe-check.mjs build/zp12n.wasm --manifest firmware/zp12n/firmware.json`.
