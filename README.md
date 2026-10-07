# zp12

A 12-bit sampling drum machine for the **M-VAVE FM-1**: 32 sounds at 26.04 kHz (or 27.5 kHz), pitched
without interpolation, eight output channels with their filters, a panel-style screen. Inspired by the
12-bit samplers of the 80s; their names are trademarks of their owners, no affiliation.

> **Status: 0.2, an early test build.** The factory kit on the keys, the sequencer (segments, song, real-time
> recording, AUTO CORRECT, swing, erase, tap tempo) and sloopDX's chorus, delay and reverb. Not saved over power-off yet;
> screen and the knobs. The sequencer (segments and song) and your own samples (a web editor) come next.
> Plan: [CONCEPT.md](CONCEPT.md).

Install from Chrome or Edge: https://dx7.designburgapps.com/zp12/ (later zp12.designburgapps.com).
Back to sloopDX or SLOOP any time with their installers; if the FM-1 does not answer, hold OCT− while
switching it on (USB rescue).

## Playing (0.1)

| | |
| --- | --- |
| White keys 1–8 · 9–16 | bank A · B pads (OCT+: C · D, OCT−: back) |
| ARP | MULTI PITCH: the last sound over all 27 keys (F4 as tuned) |
| KNOB 1–4 | SOUND: TUNE · FINE · DECAY · LEVEL |
| EDIT | next page: TRUNC (START · END · REVERSE · 45/33), OUT (CHANNEL · PAN · CUTOFF · RESO) |
| GLO | MIX: channel levels 1–4, then 5–8 (the faders) |
| HOME | back to SOUND |
| SELECT · ALGORITHM | tempo · the sound to edit |
| USB MIDI | notes 36–67 play pads A1–D8 |

## Sound

- 12-bit linear samples, packed, at 26.04 / 27.5 kHz; pitch by the playback rate with a zero-order hold
  (no interpolation): the aliasing is the sound. 45→33: stored fast, played slow.
- Channels 1–2: a 4-pole resonant low-pass whose cutoff follows the decay; 3–6: a fixed low-pass; 7–8:
  none. One sound per channel at a time (a new hit cuts the last).
- `firmware/src/sp_core.c`; host test `tests/sp_core_test.c` renders a demo WAV.

## Build

`sh build.sh [--release X.Y]` (JieLi toolchain and AC79 SDK as for SLOOP: `tools/get_toolchain.sh`).
Host: `cc -O2 -o build/host/sp_core_test tests/sp_core_test.c -lm`, `tests/zp12_ui_test.c` (screens as PPM).

## Credits and licence

GPL-3.0. Built on **SLOOP** by isod89 (github.com/isod89/sloop-fm1) and **Felucca** by Leo Kuroshita
(Hügelton Instruments), with the FM-1 platform as **sloopDX** carries it; SLOOP's README is kept as
`SLOOP-README.md`. Factory sounds: CC0 (VSCO-2 CE, VCSL, Sonic Pi; `assets/samples-cc0`). Not affiliated
with M-VAVE.
