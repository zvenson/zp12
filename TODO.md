# zp12 TODO

## 2.1 (built, not released: build/zp12-2.1-test.fwsc, identity FM-1_971)
- A pad's channel = its position (pad % 8, every bank), as on the SP-1200: fader, mute / solo, the filter (pads 1-2)
  and the choke follow the pad. The kit laid out by role (tools/gen_kit.py LAYOUT_A-D); a 2.0 save's pads and loop
  hits move with KIT_MOVED (tests/sp_seq_test.c). Why: the user found mute / faders by channel confusing and heard
  sounds cut by others (the shaker cut the piano chords on channel 8, the bass the kick on 1).
- To do: test on the device (an old 2.0 save: the loops sound as before; mute 1-8 = the pads' positions), then
  release 2.1 (make_site.py with the video), the cheat sheet PDF and screenshots, the hub picks it up at night.

## 2.2: Chop to pads (in progress, web only so far; not deployed)
- [x] Editor (web/editor.html "Chop to pads", web/zp12link.js chop*): SLOOP 2.5's CHOP (identical to 2.3's) at the
      sound's own rate: TAP (space) + snap, Find hits (sensitivity), Equal 2-16, a tempo grid (BPM from the length),
      markers dragged / nudged, keep, length per chop, Fit to room (whole sectors, the 24 places: `plan`), level
      together / each / as is; chops onto consecutive pads (16 at most), each heard as the FM-1 plays it; channel /
      filter per chop shown; the pads that get overwritten asked first; `uploadMany`: all sectors, then the directory
      once, then the pads (a Stop before the directory leaves the FM-1 as it was). Tests: tests/zp12_chop_test.mjs,
      tests/zp12_link_test.mjs section 6.
- [ ] On the device: 16 chops sent, all playable, channels right, a Stop leaves the old pads. Open: ASSIGN keeps the
      pad's TUNE (only ±12 for ×2 changes): a chop on a tuned kit pad plays detuned; reset TUNE on ASSIGN?
- [ ] Later: a slice mode for one pad (START per key, no extra memory), slice marks in TRUNC; resampling (REC + SEL +
      pad: N bars of the mix to a pad) after the flash write test of sampling in the device.
- [ ] Video "Sample, Chop, Flip": a soul loop sampled in the browser, 8 chops, a boom bap of them (`?demo=loaded&chop=`).
- Checked 2026-10-09 (the user's doubt "the filter acts on all tracks"): in sp_core.c CUT / RESO filter only channels
  1-2 (pads 1-2 of every bank), nothing else is dulled (tests/sp_core_test.c "CUT: pads 1-2 ..."). What does act on
  the whole mix: FX > FILTER (the DJ filter, kept until power-off). Still to listen to on the device.

## Ideas
- USB audio at 48 kHz for phones (sloopDX 3.4's uac_fir.h / usb.c port): recording the FM-1 INTO a phone.
- Sampling IN the device over USB (the phone plays into the FM-1): a UAC OUT interface, recording to flash while the
  audio runs (96 KB RAM: no buffer for 2.5 s): a project of its own, check RAM / flash timing first.

Released: 0.9 (FM-1_979). Open, from the user's tests:

- [x] Parameter changes recordable: TUNE / FINE / DECAY / CUT turned while recording lock onto the pad's hits
      (`sq_lk`, ZS_VER 3). Open: a way to drop a pad's locks again (now: record over them, or erase the hits).
- [x] PRESETS = the last pad's sample; REC held 2 s = clear the loop, EDIT + OCT- = undo / redo; SAVE held + a
      black key = save the loop into that loop (as sloopDX). Tried on the device (1.0).
- [x] 1.3: the backup tool (web/backup.html, web/zp12link.js; firmware sp_link.c: raw flash READ / ERASE / WRITE in zp12's rooms and sloopDX's banks only); the cheat sheet a section of the start page. Next: the sample editor on the same link.
- [x] 1.2: the 8th black key (A#4) is loop 8 again (it and B4 were swapped in white_of).
- [x] 1.1: SEL held = faders 5-8 on any page; the header says where you are (EDIT > SOUND) and a page shows its
      family's tabs; FX > FILTER: sloopDX's DJ filter on the mix (not saved). Image 576 of 582 KB then; 565 KB after FONT_S was cut to ASCII (1.3+).
- [ ] Factory hats quieter in the kit (`tools/gen_kit.py`, then bump `ZS_KIT_ID`).
- [x] 2.0 (roadmap B, C, D, A): GLO > CLICK (CLICK · COUNT off/1/2 bars · DUB now / next 1), a REC press under 2 s
      always a tap (held 2 s: clear), ARMED / COUNT 4 big; GLO held + white 1-8 mute, 9-16 solo (sp_core `quiet`, ~3 ms,
      not saved); OUT: CUT / RESO `--` on CH 3-8; the editor samples (getUserMedia, AudioWorklet, threshold, stop when the
      room is full). Not yet tried on the device.
- [ ] Idea: "Play in the browser" as other hub firmwares have (WASM build of the host code: the real UI on a canvas,
      sp_core / sp_seq in an AudioWorklet, the computer keyboard as the keys); then `play=` in tools/fm1_sync.py.
- [ ] Roadmap E: the loop length (BARS 1-32 / AUTO) shown while REC / ARMED, a knob changes it there.
- [ ] Mute / solo saved? (now a performance state, gone at power-off, as the DJ filter.)
- [x] 1.9.2: SMOOTH (GLO > OUTPUT, saved, ZS_VER 6): the jump at a cut / end / TRUNC start bridged in ~1 ms (the
      cuts were up to 11x a sound's own steps; the attacks unchanged); the editor fades trimmed ends (1 ms in, 2 ms out).
- [x] 1.9.1: SEL latches the faders 5-8 (lit) / 1-4, and from a page it goes to the faders (it was held).
- [x] 1.9: the faders and LEVEL in an audio taper (squared; they were linear: 50 was only -6 dB); older saves
      and the kit's pads converted to sound the same (ZS_VER 5, gen_kit.py taper()).
- [x] 1.8: BACK 12 s by default (6 / 12 / 30 / 60 s / OFF).
- [x] 1.7: GLO > SETUP > BACK: pages go back after 6 / 15 / 30 / 60 s or never (30 s by default, saved: ZS_VER 4 with 8 bytes of UI settings); the version in the GLO header.
- [x] 1.6: the engine against the original: the dynamic filter opens fully as CUT at the hit (it was half open) and
      closes two octaves with the decay; the fader after the filter (it changed the tone); the envelope ramped
      inside a block; a soft knee on the master (sp_out); own samples stored at ×2 / ×2.7 (TUNE -12 set by wave_set).
      Not yet heard on the device. Left as is: the 33/45 ratio (exact would be 20/27, 17 cents; both sides agree so
      the pitch comes out right), pure ZOH also when tuned up past 44.1 kHz.
- [x] 1.5: a start with a kick sampled into the LCD and the name typed on drums (ba-dum-tss; a key skips it); the editor shows the 32 pads (choose a sample, drop a file on a pad; link PADS).
- [x] 1.4: the sample editor (web/editor.html over sp_link.c; sp_samples.c): 96 KB free (2.5 s), + sloopDX's bank room
      after a forced backup of it (6.3 s). Not yet tried on the device.
- [ ] Matomo for zp12.designburgapps.com (not set up; the impressum says none).
