# zp12

A 12-bit sampling drum machine for the **M-VAVE FM-1**: 32 sounds at 26.04 kHz (or 27.5 kHz), pitched
without interpolation, eight output channels with their filters, a panel-style screen. Inspired by the
12-bit samplers of the 80s; their names are trademarks of their owners, no affiliation.

> **Status: 0.5, an early test build.** The factory kit on the keys (bank D: E-piano chords, horns, vibes, bass,
> scratches), eleven loops on the black keys, the sequencer (loops of 1–32 bars or AUTO, song, real-time recording with count-in and AUTO CORRECT, step editing, swing, erase, tap tempo), sloopDX's
> chorus, delay and reverb, saved in flash (0xC4000.., a room sloopDX leaves free). Own samples (a web editor) next.
> Plan: [CONCEPT.md](CONCEPT.md).

Install from Chrome or Edge: https://dx7.designburgapps.com/zp12/ (later zp12.designburgapps.com).
Back to sloopDX or SLOOP any time with their installers; if the FM-1 does not answer, hold OCT− while
switching it on (USB rescue).

## Playing (0.4)

| | |
| --- | --- |
| White keys 1–8 · 9–16 | bank A · B pads (OCT+: C · D, OCT−: back) |
| Black keys | loops 1–11 (segments): stopped at once, playing from the end of the loop |
| KNOB 1–4 | the faders of channels 1–4 (SEL held: 5–8); a page takes them, 6 s untouched or HOME gives them back |
| EDIT | WAVE (the pad's sample · COPY> · COPY the sound to a pad), SOUND (TUNE · FINE · DECAY · LEVEL), TRUNC (START · END · DIR · SPEED 45/33), OUT (CHAN · PAN · CUT · RESO), SENDS (DRIVE · CHO · DLY · REV) of the last pad |
| FX | CHORUS (RATE · DEPTH · MIX), DELAY (TIME · FDBK · COLOR · MIX), REVERB (SIZE · DAMP · PRE) |
| SEQ tapped | SEGMENT (SEG · BARS 1–32 / AUTO · QUANT · SWING), SEG TOOLS (CLEAR · COPY> · COPY, turn twice), SONG (STEP · SEG · REPEAT, 0 ends · MODE) |
| SEQ held | the last pad's 16 steps of a bar on the white keys (lit = a hit); OCT− / OCT+: the bars |
| PLAY · REC | run / stop · record (stopped: armed, PLAY counts a bar in; playing: overdub on / off) |
| LFO held + pad | erase that pad's hits as the playhead passes (stopped: at once) |
| ENV · GLO · SAVE | tap tempo · TEMPO and CLICK · save now (it saves by itself when stopped and quiet) |
| ARP | MULTI PITCH: the last sound over all 27 keys (F4 as tuned) |
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
`SLOOP-README.md`. Factory sounds: CC0 (VSCO-2 CE, VCSL, Sonic Pi; `assets/samples-cc0`); bank D from SLOOP's hip-hop pack, the
E-piano by Greg Sullivan (CC BY 3.0, `assets/samples-cc0/HIPHOP/CREDITS.txt`). Not affiliated
with M-VAVE.
