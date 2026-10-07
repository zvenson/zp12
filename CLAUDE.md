# zp12

A second firmware for the M-VAVE FM-1: a 12-bit sampler drum machine in the spirit of the SP-12 / SP-1200, built on
SLOOP 2.3 (isod89/sloop-fm1, remote `upstream`; based on Felucca). GPL-3.0. Repo github.com/zvenson/zp12 (`main`).
Sister project: sloopDX in `../sloopdx` (its installer and effects are reused here). Plan: CONCEPT.md. Open work: TODO.md.

## Hard rules

- Only CC0 or own samples in the kit (`tools/gen_kit.py`); the E-piano is CC BY 3.0 (Greg Sullivan), keep its credit.
- No E-mu / SP-1200 trademarks in the product name: "inspired by", the marks belong to their owners.
- Keep the SLOOP / Felucca credits. "Only an SP-12": no other synth engines.
- Target: JieLi AC791N, no FPU, no libgcc: **no 64-bit division** (link fails on __udivdi3), integer DSP only.
  `tools/build.py check()` fails on any constant in 0xFFC00000-0xFFD00000 (ROM range; e.g. -32767<<7). RAM ≤ 96 KB.

## Where things are (firmware/src)

- `zp12.c`: main, audio ISR (output `sp_clamp(v >> 1, ...) << 8`, i.e. -6 dB).
- `sp_core.c`: 12-bit packed samples (2 in 3 bytes) at 26.04 / 27.5 kHz, zero-order-hold pitch, 8 channels
  (1-2 resonant 4-pole LP following the decay, 3-6 fixed LP 9/12 kHz, 7-8 none), mono choke, DRIVE, 3 sends,
  click; `sp_trigger_at(k, vel, semis)`.
- `sp_fx.c`: chorus / delay / reverb (sloopDX's fx_buses).
- `sp_seq.c`: 96 PPQ, 16 segments ("loops") of 1-32 bars or AUTO (bars=0: first take sets the length),
  `sq_ev_t` {t:14, pad:5, lvl:3, skip:1, semis:6}, 512 events per segment; requests via `sq_post(op,pad,arg)`
  (RQ_HIT PLAY STOP REC STEP WIPE CLEAR COPY LOOP) processed in the ISR `sq_block()`; AUTO CORRECT, swing,
  count-in, click off/rec/on; black keys = loops 1-11 (switch at loop end); 4 songs `sq_songs[4][32]`.
- `sp_store.c`: two 40 KiB copies at 0xC4000 / 0xCE000 (gen + CRC32), `ZS_VER` 2; `ZS_KIT_ID` resets the pads
  when the factory kit changes (bump it then). Autosave 3 s after stop; SAVE saves now.
- `sp_ui.c`: panel look (header, big LCD line + 4 columns for KNOB 1-4, faders, pads, LEDs via `ui_leds()`).
  HOME: KNOB 1-4 = pad volumes 1-4, SEL held 5-8. EDIT: WAVE, SOUND, TRUNC, OUT, SENDS. FX: CHORUS, DELAY,
  REVERB. SEQ tap: LOOP, TOOLS (CLEAR, COPY>, COPY: turn twice), SONG. GLO: SETUP (TEMPO, CLICK, VER, RESET).
  Pages return after 6 s. White keys 1-8/9-16 = banks A/B (OCT+ C/D), ARP = MULTI PITCH (last pad over 27 keys,
  semis recorded), LFO held + pad = erase, ENV = tap tempo. Version text from FELUCCA_ID[7].

## Build, test, release

- `./build.sh --release X.Y` -> `build/zp12-X.Y.fwsc`, identity `FM-1_97Y` (zp12 = 97N; sloopDX 9XY, Doom 98N).
- Host tests (from the repo root):
  `cc -O2 -w -o build/host/sp_core_test tests/sp_core_test.c -lm && build/host/sp_core_test build/host/core.wav`
  `cc -O2 -Wall -Wno-unused-function -Ifirmware/src -Ibuild/gen -o build/host/sp_seq_test tests/sp_seq_test.c -lm && build/host/sp_seq_test`
  `cc -O2 -w -Ifirmware/src -Ifirmware/gen -Ibuild/gen -o build/host/zp12_ui_test tests/zp12_ui_test.c -lm && build/host/zp12_ui_test`
- Site: `python3 tools/make_site.py ../sloopdx build/zp12-X.Y.fwsc X.Y [video.mp4]` -> `docs/` (start page
  web/landing.html, installer docs/install/, cheat sheet web/cheatsheet.html + PDF via headless Chrome, impressum).
  Bump the version in README, landing, cheat sheet when releasing.
- Deploy: commit, push `main`, then `ssh -o BatchMode=yes pi-remote '~/docker/sloopdx-site/update.sh'`
  (pulls this repo into the Pi's zp12repo; served as zp12.designburgapps.com and dx7.designburgapps.com/zp12/).
  sloopDX's site copies docs/zp12 too: rebuild it there when /zp12/ should change.
- Videos: C harness renders the real UI + audio per frame (`../video/zp12av*.c`), compositor `../video/make_video*.py`
  (panel, knobs, keys, cold open, cuts on bar lines, loudnorm -14 LUFS). Latest: `zp12-1.0-downbeat.mp4`.

## Style

As SLOOP: C99, 4 spaces, short comments that say why, unity build. Commit messages: one line what changed.
