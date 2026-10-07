# zp12sloop: concept (one page)

A sampling drum machine for the M-VAVE FM-1 that sounds and works like an 80s 12-bit sampler (inspired
by the E-mu SP-12 / SP-1200; those are trademarks of their owners, no affiliation). Based on SLOOP 2.3
(isod89, GPL-3.0, on Felucca): its sample playback from flash, the editor upload, the step sequencer,
song mode, tap tempo and metronome stay; every synth engine, the FM and the factory instrument sets go.
**Only the sampler.**

## Sound (first, audible in host WAVs before anything else)

- **Storage:** 12-bit linear, packed (2 samples in 3 bytes), at **26.04 kHz** (SP-1200) or **27.5 kHz**
  (SP-12), chosen per sound when it is loaded. No ADPCM: the grit must be the 12 bits, not a codec.
- **Pitch by playback rate, no interpolation** (zero-order hold): a sound tuned up skips samples, tuned
  down repeats them; the aliasing is the sound. TUNE in semitones (−24…+12) and FINE (cents).
  **45→33 mode:** a sound loaded "at 45" is stored sped up by 45/33 (less memory, fewer samples) and
  played at 33/45: the classic trick, longer sounds and more crunch.
- **Output channels and filters** as on the original's 8 outputs: every sound plays on a channel 1–8,
  one sound at a time per channel (a new hit cuts the last: the mono choke).
  - Channels 1–2: a **4-pole resonant low-pass** (SSM2044-style, on an envelope that follows DECAY).
  - Channels 3–6: a fixed low-pass each (the original's fixed output filters).
  - Channels 7–8: unfiltered.
- Per sound: **TUNE, FINE, DECAY, LEVEL, PAN, START / END** (truncate), **CHANNEL**, **REVERSE**.
- A DAC stage: 12-bit output quantise per channel, then the mix. SLOOP's DUST stays as an extra.

## Pads and keys

- **32 sounds = 4 banks (A–D) × 8 pads.** The 16 white keys are two banks at once: keys 1–8 bank A,
  9–16 bank B; **OCT+** shows C + D, **OCT−** back to A + B (the bank letters glow on the screen).
- Black keys: the pad's functions while held (see pages), nothing sounds on them in pad mode.
- **MULTI PITCH** (ARP button toggles it): the selected sound over all 27 keys chromatically, recordable
  (the original spreads one sound over the 8 pads; the FM-1 gets a whole keyboard).
- **MULTI LEVEL** (later): one sound, 8 velocities on the pads.

## Pages (four knobs each, tap a button to step through its pages)

| Button | Pages · KNOB 1 · 2 · 3 · 4 |
| --- | --- |
| **EDIT** (the last pad hit) | SOUND: TUNE · FINE · DECAY · LEVEL — TRUNC: START · END · REVERSE · 45→33 — OUT: CHANNEL · PAN · FILTER · RESO (ch 1–2) |
| **SEQ** | SEGMENT: number · LENGTH (bars) · QUANT (1/8 · 1/8T · 1/16 · 1/16T · 1/32 · 1/32T) · SWING (50 · 54 · 58 · 63 · 67 · 71 %) |
| **SAVE** | SONG: the chain of segments with repeats (SLOOP's song mode) — PROJECT: slots, load, save |
| **GLO** | MIX 1–4 · MIX 5–8 (the eight channel levels) — TEMPO: BPM · CLICK · TAP — SYSTEM |
| **REC** | real-time recording with the metronome; held: **erase** (the pad held while it plays is wiped) |

Destructive actions (clear segment, delete sound, new project) need the **AGAIN** turn, as in sloopDX.
Step edit: SEQ held shows 16 steps of the segment on the white keys, as in SLOOP.

## Sequencer

- A **segment** is 1–4 bars on a 1/32 grid (128 steps); every step holds which of the 32 sounds hit and
  how hard (4 levels). 16 segments per project; the **song** chains them (SLOOP's arranger: up to 16
  entries × repeats). Quantize applies when recording; swing in the SP steps on the 1/16 or 1/8 grid.
- Recording: one bar count-in with the click, overdub, erase while playing (REC held + pad).

## Memory (measured on SLOOP 2.3)

| | |
| --- | --- |
| App area 581 KB | SLOOP 2.3 uses 550 KB, 316 KB of it factory samples. Without engines and sets the code is ~200 KB; the rest holds a small **CC0 factory kit** (SLOOP's sets, own recordings) |
| **User sample memory** | the flash outside the app: **356 KB** (0xA0000–0xDC000, 0x93000–0x97000, 0xE5000–0xFC000) = **~237 000 samples = 9.1 s at 26.04 kHz, 12-bit** (the SP-1200 had 10 s); with 45→33 up to ~12 s. Said honestly on the site. |
| RAM | 72.7 of 96 KB in SLOOP 2.3, less without the engines; segments live in flash, the playing one in RAM |

## Sampling

- **USB audio on the FM-1 is output only** (device → computer, UAC1 IN), so there is no live sampling
  from the computer. Sounds come from the **own web editor**: drop a WAV / AIFF / MP3, trim, normalise,
  choose 26.04 / 27.5 kHz and 45→33; the browser resamples to 12 bit (the same code as the device's
  host tests, checked bit-exact), shows the waveform and plays the result as the FM-1 will.
- Kits and single sounds as files (.zp12kit / .wav) to back up and share. No E-mu or third-party
  libraries; only CC0 and own material.

## Look

Dark panel, red / amber LED tones, seven-segment digits for tempo, segment and values; the wordmark
**zp12** with its own boot splash (generated like sloopDX's `tools/gen_logo.py`). Not sloopDX's colours,
no E-mu styling.

## Order of work

1. This concept. 2. Sound core: 12-bit ZOH playback, channels and filters, host WAVs + golden renders.
3. Sampling path (editor, flash layout, bit-exact). 4. Pads, banks, pages, sequencer changes.
5. Site zp12.designburgapps.com (installer, editor, cheat sheet from the code, Impressum, Matomo),
CI, releases in docs/firmware, M-UPGRADE way back, issue to isod89. Credits SLOOP / Felucca stay.
