#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""zp12's factory kit: the 16 CC0 drum hits of assets/samples-cc0/KIT (SLOOP's built-in set, CC0 1.0),
cut to a length that fits, faded, resampled to 12 bit at 26.04 kHz and packed (2 samples in 3 bytes),
as build/gen/zp12_kit.h: the waves, and the 32 pads' default sounds (bank A and B the kit, C a dirtier
copy lower at 45->33 and a piano (VCSL, CC0), D chords and sounds from SLOOP's hip-hop pack: two E-piano chords, a horn stab, vibes,
a bass, scratches).

  tools/gen_kit.py build/gen/zp12_kit.h

The resampler is the one of tests/sp_core_test.c and the web editor: a Hann-windowed sinc low-pass at 0.45 of
the lower rate (a sampler's input filter), then 12 bit rounded."""
import math
import sys
import wave
from pathlib import Path

import numpy as np

KIT = Path(__file__).resolve().parents[1] / "assets" / "samples-cc0" / "KIT"
RATE = 26040
# name, file, seconds kept, channel (0..7), decay, level, pan, cut, reso
SOUNDS = [
    ("KICK", "10_kick_BDrumNew_hit_v5.wav", 0.55, 0, 110, 127, 0, 127, 0),
    ("SNARE", "11_snare_Snare2_HitSN_v7.wav", 0.38, 1, 100, 118, 0, 127, 0),
    ("RIM", "12_rim_sidestick.wav", 0.16, 5, 90, 100, -10, 127, 0),
    ("CLAP", "13_clap_Clap_rr1.wav", 0.40, 3, 95, 110, 6, 127, 0),
    ("HAT", "14_hihat_HitC_v3.wav", 0.16, 2, 60, 90, 18, 127, 0),
    ("OHAT", "15_ohat_HitO.wav", 0.55, 2, 100, 84, 18, 127, 0),       # (channel 3 with HAT: it chokes it)
    ("TOM L", "16_tomlo_TomL_HitS_v4.wav", 0.42, 4, 100, 112, -16, 127, 0),
    ("TOM H", "17_tomhi_TomH_HitS_v4.wav", 0.38, 4, 100, 108, 16, 127, 0),
    ("CRASH", "18_crash_susCymb2_hit_f1.wav", 0.75, 6, 110, 80, -20, 127, 0),
    ("RIDE", "19_ride_susCymb2_stick_mf1.wav", 0.55, 6, 110, 76, 20, 127, 0),
    ("COWBL", "20_cowbell_Cowbell1_Normal_v3.wav", 0.26, 7, 100, 90, 10, 127, 0),
    ("TAMB", "00_Tamb1_Hit_v2_rr1_Mid.wav", 0.26, 7, 100, 84, -12, 127, 0),
    ("SHAKR", "01_Mid_ShakerDouble_Down_rr1.wav", 0.26, 7, 100, 80, 14, 127, 0),
    ("CONGA", "02_Conga_HitN_v2_rr1_Sum.wav", 0.32, 5, 100, 100, -14, 127, 0),
    ("CLAVE", "03_Claves1_Hit_v2_rr1_Mid.wav", 0.16, 5, 100, 90, 12, 127, 0),
    ("WOOD", "04_wood_click_mp.wav", 0.16, 5, 100, 90, -6, 127, 0),
]


def read(path):
    with wave.open(str(path)) as w:
        n, ch, sw, r = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
        b = w.readframes(n)
    if sw == 3:                                         # 24 bit: little-endian triplets, sign from the top byte
        u = np.frombuffer(b, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        raw = (u[:, 0] | u[:, 1] << 8 | u[:, 2] << 16)
        raw = np.where(raw & 0x800000, raw - 0x1000000, raw).astype(np.float64) / 8388608.0
    else:
        raw = np.frombuffer(b, dtype={1: np.uint8, 2: np.int16, 4: np.int32}[sw]).astype(np.float64)
        raw = (raw - 128) / 128.0 if sw == 1 else raw / (32768.0 if sw == 2 else 2147483648.0)
    return raw.reshape(-1, ch).mean(1), r


def resample12(x, src, dst):
    """as tests/sp_core_test.c resample12 (vectorised; the editor's must equal the C one, this kit need not)"""
    H = 24
    fc = 0.45 * min(src, dst) / src
    ratio = src / dst
    k = len(x) * dst // src
    span = math.ceil(H * ratio)
    t = np.arange(k) * ratio
    c = np.floor(t).astype(np.int64)
    out = np.zeros(k)
    wsum = np.zeros(k)
    for j in range(-span + 1, span + 1):
        idx = c + j
        d = t - idx
        a = 2.0 * fc * d
        sinc = np.where(a == 0, 1.0, np.sin(np.pi * a) / np.where(a == 0, 1, np.pi * a))
        g = 2.0 * fc * sinc * (0.5 + 0.5 * np.cos(np.pi * d / (span + 1)))
        ok = (idx >= 0) & (idx < len(x))
        out += np.where(ok, x[np.clip(idx, 0, len(x) - 1)], 0.0) * g
        wsum += g
    s = np.clip(np.rint(out / wsum * 2047.0), -2048, 2047).astype(np.int64)
    return s


def pack(s):
    s = [int(v) & 0xFFF for v in s]
    if len(s) & 1:
        s.append(0)
    out = bytearray()
    for i in range(0, len(s), 2):
        a, b = s[i], s[i + 1]
        out += bytes((a & 255, (a >> 8) | ((b << 4) & 0xF0), b >> 4))
    return out


HIP = Path(__file__).resolve().parents[1] / "assets" / "samples-cc0" / "HIPHOP"
# bank D: chords and sounds built from SLOOP's hip-hop pack (assets/samples-cc0/HIPHOP/CREDITS.txt): each note
# resampled from the nearest recorded one (as a sampler would pitch it), a few ms apart (a hand on a keyboard),
# summed, a gentle low-pass and a soft clip (the record it could have come from)
# name, [(file, root midi, note midi, delay ms)], seconds, channel, decay, level, pan, sends (cho, dly, rev)
MELODIC = [
    ("EP Dm9", [("01_Ds3.wav", 51, 50, 0), ("01_Ds3.wav", 51, 53, 6), ("02_Cs4.wav", 61, 57, 11), ("02_Cs4.wav", 61, 60, 15),
                ("02_Cs4.wav", 61, 64, 19)], 1.00, 7, 120, 88, 0, (20, 30, 40)),
    ("EP Gm9", [("01_Ds3.wav", 51, 55, 0), ("02_Cs4.wav", 61, 58, 6), ("02_Cs4.wav", 61, 62, 11), ("02_Cs4.wav", 61, 65, 15),
                ("02_Cs4.wav", 61, 69, 19)], 0.95, 7, 120, 86, 0, (20, 30, 40)),
    ("HORNS", [("01_A3.wav", 57, 57, 0), ("03_Ds4.wav", 63, 62, 3), ("04_G4.wav", 67, 65, 6)], 0.70, 6, 100, 88, 10, (0, 20, 30)),
    ("VIBES", [("01_C4.wav", 60, 60, 0)], 0.70, 6, 110, 100, -10, (30, 20, 40)),
    ("BASS", [("04_A2.wav", 45, 45, 0)], 0.80, 0, 110, 92, 0, (0, 0, 0)),
    ("SCRCH", [("00_vinyl_scratch.wav", 60, 60, 0)], 0.27, 5, 127, 100, 0, (0, 0, 10)),
    ("SPIN", [("01_vinyl_backspin.wav", 60, 60, 0)], 0.50, 5, 127, 90, 0, (0, 0, 20)),
    ("SNAP", [("03_perc_snap.wav", 60, 60, 0)], 0.30, 3, 100, 100, 12, (0, 10, 40)),
]


PIANO = Path(__file__).resolve().parents[1] / "assets" / "samples-cc0" / "PIANO"
# C6-C8: VCSL's Steinway B (CC0), stored a 45->33 step high and played at 33 (the old trick: a third more time
# in the same memory, grainier): two funky stabs, a ii-V (Cm9, F13), and one note for MULTI PITCH
# name, [(file, root midi, note midi, delay ms)], seconds stored, channel, decay, level, pan, sends
PIANOS = [
    ("PNO Cm9", [("00_C3_m48.wav", 48, 48, 0), ("01_Gs3_m56.wav", 56, 58, 5), ("02_E4_m64.wav", 64, 63, 9),
                 ("02_E4_m64.wav", 64, 67, 12), ("03_C5_m72.wav", 72, 74, 15)], 0.55, 7, 105, 92, -8, (15, 25, 35)),
    ("PNO F13", [("00_C3_m48.wav", 48, 41, 0), ("00_C3_m48.wav", 48, 51, 5), ("01_Gs3_m56.wav", 56, 57, 9),
                 ("02_E4_m64.wav", 64, 62, 12), ("02_E4_m64.wav", 64, 67, 15)], 0.55, 7, 105, 92, 8, (15, 25, 35)),
    ("PIANO", [("01_Gs3_m56.wav", 56, 60, 0)], 0.45, 4, 110, 100, 0, (10, 0, 30)),
]
SLOW = 45 / 33


def chord(notes, secs, src=HIP, up=1.0, top=6500):
    out = None
    for f, root, note, ms in notes:
        x, r = read(src / f)
        y = resample12(x, int(round(r * up * 2 ** ((note - root) / 12))), RATE).astype(np.float64) / 2047.0
        y = np.concatenate([np.zeros(int(ms * RATE / 1000)), y])
        out = y if out is None else (np.pad(out, (0, max(0, len(y) - len(out)))) + np.pad(y, (0, max(0, len(out) - len(y)))))
    out = out[:int(secs * RATE)]
    lp, z = 1 - math.exp(-2 * math.pi * top / RATE), 0.0
    for i in range(len(out)):                          # the record's top end
        z += (out[i] - z) * lp
        out[i] = z
    out = np.tanh(out / np.abs(out).max() * 1.4) / np.tanh(1.4)
    fade = max(1, len(out) // 5)
    out[-fade:] *= np.linspace(1, 0, fade)
    return np.clip(np.rint(out * 0.95 * 2047.0), -2048, 2047).astype(np.int64)


def main(dst):
    blobs, waves = bytearray(), []
    for name, f, secs, *_ in SOUNDS:
        x, r = read(KIT / f)
        x = x[:int(secs * r)]
        fade = max(1, len(x) // 6)
        x[-fade:] *= np.linspace(1, 0, fade)
        peak = np.abs(x).max()
        if peak > 0:
            x = x * (0.95 / peak)                          # every hit at the same peak: the levels set the mix
        s = resample12(x, r, RATE)
        waves.append((len(blobs), len(s), name))
        blobs += pack(s)
        while len(blobs) % 4:
            blobs.append(0)
    for name, notes, secs, *_ in MELODIC:
        s = chord(notes, secs)
        waves.append((len(blobs), len(s), name))
        blobs += pack(s)
        while len(blobs) % 4:
            blobs.append(0)
    for name, notes, secs, *_ in PIANOS:
        s = chord(notes, secs, PIANO, SLOW, 9000)
        waves.append((len(blobs), len(s), name))
        blobs += pack(s)
        while len(blobs) % 4:
            blobs.append(0)
    rows = ",\n".join(",".join(str(b) for b in blobs[i:i + 32]) for i in range(0, len(blobs), 32))
    lines = [f"/* generated by tools/gen_kit.py: the CC0 kit (assets/samples-cc0/KIT), 12 bit at {RATE} Hz */",
             f"#define KIT_NWAVE {len(waves)}",
             f"#define KIT_BYTES {len(blobs)}",
             "static const uint8_t KIT_DATA[] __attribute__((aligned(4))) = {", rows, "};",
             "static const struct { uint32_t off, n; const char *name; } KIT_WAVE[KIT_NWAVE] = {"]
    lines += [f'    {{{o}, {n}, "{nm}"}},' for o, n, nm in waves]
    lines += ["};", "/* the 32 pads: wave, tune, fine, decay, level, pan, chan, flags, start, end, cut, reso */",
              "static const sp_sound_t KIT_PADS[32] = {"]
    pads = []
    taper = lambda l: min(127, round(math.sqrt(127 * l)))   # LEVEL is squared in the engine (an audio taper): the same loudness
    SENDS = {"SNARE": (0, 0, 45), "CLAP": (0, 35, 30), "RIM": (0, 0, 25), "OHAT": (0, 0, 15), "TOM L": (0, 0, 20),
             "TOM H": (0, 0, 20), "CONGA": (0, 0, 25), "CRASH": (0, 0, 20)}   # a little space on some (factory taste)
    for i, (name, f, secs, ch, dec, lvl, pan, cut, res) in enumerate(SOUNDS):
        pads.append((i, 0, 0, dec, taper(lvl), pan, ch, 0, 0, 1000, cut, res, 0, *SENDS.get(name, (0, 0, 0))))
    for i in range(5):                                 # C1-C5: bank A an octave down at 45->33, the filter on ch 1-2
        name, f, secs, ch, dec, lvl, pan, cut, res = SOUNDS[i]
        pads.append((i, -7, 0, min(127, dec + 10), taper(lvl), pan, ch, 2, 0, 1000, 90 if ch < 2 else 127, 40 if ch < 2 else 0, 0, 0, 0, 0))
    for i, (name, notes, secs, ch, dec, lvl, pan, snd) in enumerate(PIANOS):   # C6-C8: the piano, at 33
        pads.append((len(SOUNDS) + len(MELODIC) + i, 0, 0, dec, taper(lvl), pan, ch, 2, 0, 1000, 127, 0, 0, *snd))
    for i, (name, notes, secs, ch, dec, lvl, pan, snd) in enumerate(MELODIC):   # D: the chords and sounds
        pads.append((len(SOUNDS) + i, 0, 0, dec, taper(lvl), pan, ch, 0, 0, 1000, 127, 0, 0, *snd))
    lines += ["    {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d}," % p if len(p) == 12 else
              "    {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, {%d, %d, %d}}," % p for p in pads]
    lines += ["};", ""]
    Path(dst).parent.mkdir(parents=True, exist_ok=True)
    Path(dst).write_text("\n".join(lines))
    secs = sum(n for _, n, _ in waves) / RATE
    print(f"kit: {len(waves)} waves, {secs:.2f} s, {len(blobs)} B")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "build/gen/zp12_kit.h")
