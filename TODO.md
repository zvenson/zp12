# zp12 TODO

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
- [x] 1.6: the engine against the original: the dynamic filter opens fully as CUT at the hit (it was half open) and
      closes two octaves with the decay; the fader after the filter (it changed the tone); the envelope ramped
      inside a block; a soft knee on the master (sp_out); own samples stored at ×2 / ×2.7 (TUNE -12 set by wave_set).
      Not yet heard on the device. Left as is: the 33/45 ratio (exact would be 20/27, 17 cents; both sides agree so
      the pitch comes out right), pure ZOH also when tuned up past 44.1 kHz.
- [x] 1.5: a start with a kick sampled into the LCD and the name typed on drums (ba-dum-tss; a key skips it); the editor shows the 32 pads (choose a sample, drop a file on a pad; link PADS).
- [x] 1.4: the sample editor (web/editor.html over sp_link.c; sp_samples.c): 96 KB free (2.5 s), + sloopDX's bank room
      after a forced backup of it (6.3 s). Not yet tried on the device.
- [ ] Matomo for zp12.designburgapps.com (not set up; the impressum says none).
