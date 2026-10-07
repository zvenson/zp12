# zp12 TODO

Released: 0.9 (FM-1_979). Open, from the user's tests:

- [x] Parameter changes recordable: TUNE / FINE / DECAY / CUT turned while recording lock onto the pad's hits
      (`sq_lk`, ZS_VER 3). Open: a way to drop a pad's locks again (now: record over them, or erase the hits).
- [x] PRESETS = the last pad's sample; REC held 2 s = clear the loop, EDIT + OCT- = undo / redo; SAVE held + a
      black key = save the loop into that loop (as sloopDX). Not yet tried on the device.
- [ ] Factory hats quieter in the kit (`tools/gen_kit.py`, then bump `ZS_KIT_ID`).
- [ ] Web editor for own samples (FM-1 USB audio is output only: upload over MIDI sysex like sloopDX's bank upload;
      user sample flash ~356 KB = ~9.1 s at 26.04 kHz).
- [ ] Matomo for zp12.designburgapps.com (not set up; the impressum says none).
