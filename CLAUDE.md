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
  (2.1: a pad's channel is its position, pad % 8, as on the SP-1200; tools/gen_kit.py lays the kit out by role,
  KIT_MOVED moves a 2.0 save's pads and hits; 1-2 resonant 4-pole LP: CUT at the hit, two octaves down with the decay; 3-6 fixed LP 9/12 kHz, 7-8 none),
  order sample > VCA > DRIVE > filter > fader, mono choke, 3 sends, click; `sp_mute` / `sp_solo` (bit = channel, ramped
  ~3 ms by `quiet`, not saved); `sp_trigger_at(k, vel, semis)`;
  `sp_out(v)`: the master's 16 bits (-6 dB, soft knee above 3/4). Own-sample flags: bit0 45->33, bit1 x2 (TUNE -12).
- `sp_fx.c`: chorus / delay / reverb (sloopDX's fx_buses).
- `sp_punch.c`: FX held + white 1-4: ROLL 1/8, ROLL 1/16, REVERSE, TAPE STOP on the whole mix (after SLOOP's punch.c),
  mono ring of 0.74 s at 22 kHz in .pool (pool is then nearly full: ~0.8 KB left); test tests/sp_punch_test.c.
- `sp_seq.c`: 96 PPQ, 16 segments ("loops") of 1-32 bars or AUTO (bars=0: first take sets the length),
  `sq_ev_t` {t:14, pad:5, lvl:3, skip:1, semis:6}, 512 events per segment; requests via `sq_post(op,pad,arg)`
  (RQ_HIT PLAY STOP REC STEP WIPE CLEAR COPY LOOP) processed in the ISR `sq_block()`; AUTO CORRECT, swing,
  count-in 0/1/2 bars (`cin_bars`), DUB (`dub_bar`: an overdub waits for the next 1, `dub_wait`), click off/rec/on; black keys = loops 1-11 (switch at loop end); 4 songs `sq_songs[4][32]`.
- `sp_store.c`: two 40 KiB copies at 0xC4000 / 0xCE000 (gen + CRC32), `ZS_VER` 2; `ZS_KIT_ID` resets the pads
  when the factory kit changes (bump it then). Autosave 3 s after stop; SAVE saves now.
- `sp_samples.c`: own samples from the web editor: directory 0xD8000 / 0xD9000 (gen + CRC, 24 slots), data in
  0xDA000-0xDBFFF, 0xE5000-0xE8FFF, 0xEA000-0xFBFFF (+ sloopDX's bank room 0xA0000-0xC3FFF if chosen), read via the
  plain XIP window; waves KIT_NWAVE + slot; names in sp_ui.c `ui_uname`. Editor: web/editor.html (also samples: getUserMedia + an inline AudioWorklet, threshold start,
  stop when the room is full).
- `sp_link.c`: USB link for the web tools, SysEx F0 pack7(7D 'Z' 'P' cmd addr len data sum) F7: HELLO, READ, ERASE,
  WRITE (only 0xA0000-0xDBFFF, 0xE5000-0xE8FFF, 0xEA000-0xFBFFF: sloopDX's banks + zp12's rooms), HOLD, REBOOT, RELOAD, ASSIGN, PADS. The
  logic is in `web/zp12link.js` (backup / restore; the sample editor goes on top). Image ~565 of 582 KB (FONT_S cut to ASCII saved 12.9 KB; -Oz would save 4.3 KB more but slows the audio ISR: untried on the device).
- `sp_ui.c`: panel look (header, big LCD line + 4 columns for KNOB 1-4, faders, pads, LEDs via `ui_leds()`).
  HOME: KNOB 1-4 = pad volumes 1-4, SEL held 5-8. EDIT: WAVE, SOUND, TRUNC, OUT, SENDS. FX: CHORUS, DELAY,
  REVERB. SEQ tap: LOOP, TOOLS (CLEAR, COPY>, COPY: turn twice), SONG. GLO tap: SETUP (TEMPO, BACK, RESET), CLICK (CLICK,
  COUNT, DUB), OUTPUT (SMOOTH); GLO held + white 1-8 mute / 9-16 solo. REC: a press under 2 s is a tap, held 2 s clears.
  Pages return after 6 s. White keys 1-8/9-16 = banks A/B (OCT+ C/D), ARP = MULTI PITCH (last pad over 27 keys,
  semis recorded), LFO held + pad = erase, ENV = tap tempo. Version text from FELUCCA_ID[7].

## Build, test, release

- `./build.sh --release X.Y` -> `build/zp12-X.Y.fwsc`, identity `FM-1_97Y` (zp12 = 97N; sloopDX 9XY, Doom 98N).
- Host tests (from the repo root):
  `cc -O2 -w -o build/host/sp_core_test tests/sp_core_test.c -lm && build/host/sp_core_test build/host/core.wav`
  `cc -O2 -Wall -Wno-unused-function -Ifirmware/src -Ibuild/gen -o build/host/sp_seq_test tests/sp_seq_test.c -lm && build/host/sp_seq_test`
  `cc -O2 -w -Ifirmware/src -Ifirmware/gen -Ibuild/gen -o build/host/zp12_ui_test tests/zp12_ui_test.c -lm && build/host/zp12_ui_test`
- Start animation (sp_ui.c ui_splash, SPLASH_HIT): `cc -O2 -w -Ifirmware/src -Ifirmware/gen -Ibuild/gen -o build/host/zp12_splash tests/zp12_splash_render.c -lm && build/host/zp12_splash build/host/splash` (frames + WAV)
- Link: `cc -O2 -Wall -Wno-unused-function -Ifirmware/src -o build/host/zp12_link_host tests/zp12_link_host.c -lm && node tests/zp12_link_test.mjs`
- Site: `python3 tools/make_site.py ../sloopdx build/zp12-X.Y.fwsc X.Y [video.mp4]` -> `docs/` (start page
  web/landing.html with the cheat sheet embedded from web/cheatsheet.html (its scoped `cs-style` and `<!--cs-->` block;
  no own page), installer docs/install/, backup docs/backup/, impressum). Always pass the video (it is deleted
  otherwise). PDF / screenshots: `flatpak-spawn --host flatpak run --filesystem=$PWD com.google.Chrome --headless=new ...`.
  Bump the version in README, landing, cheat sheet when releasing.
- Deploy: commit, push `main`, then `ssh -o BatchMode=yes pi-remote '~/docker/sloopdx-site/update.sh'`
  (pulls this repo into the Pi's zp12repo, purges Cloudflare, syncs the hub; served as zp12.designburgapps.com,
  dx7.designburgapps.com/zp12 only redirects there).
- Videos: C harness renders the real UI + audio per frame (`../video/zp12av*.c`), compositor `../video/make_video*.py`
  (panel, knobs, keys, cold open, cuts on bar lines, loudnorm -14 LUFS). Latest: `zp12-2.0-groove.mp4` (`zp12av7.c`, `make_video13.py`, v13/: boom bap at 90 in one take, sampling in the browser, count-in, mute / solo, punch-in; FX / GLO held driven as zp12.c does, sp_punch after sp_render). Before: `zp12-1.6-samples.mp4` (`zp12av6.c`, `make_video12.py`, v12/; then loudnorm -14 LUFS with a limiter from
  `v12/track.wav`). Its storyboard (the user's): the finished groove, the web editor with two own samples (v12/own/*.wav,
  the editor's `?demo=` states screenshotted with `--allow-file-access-from-files`), the build, the own sample in it, a
  jam, double time, the end cards.
  Every video ends with "Please always check the latest firmware version!" + zp12.designburgapps.com/install.

## Style

As SLOOP: C99, 4 spaces, short comments that say why, unity build. Commit messages: one line what changed.
