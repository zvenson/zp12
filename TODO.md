# zp12 TODO

Released: 0.9 (FM-1_979). Open, from the user's tests:

- [ ] Parameter changes recordable ("play with tune and stuff"): proposal: while REC, store TUNE / DECAY / LEVEL with
      each hit (a per-hit lock, as on the SP); needs room in `sq_ev_t` or a side table, a store version bump.
- [ ] Factory hats quieter in the kit (`tools/gen_kit.py`, then bump `ZS_KIT_ID`).
- [ ] Web editor for own samples (FM-1 USB audio is output only: upload over MIDI sysex like sloopDX's bank upload;
      user sample flash ~356 KB = ~9.1 s at 26.04 kHz).
- [ ] Matomo for zp12.designburgapps.com (not set up; the impressum says none).
