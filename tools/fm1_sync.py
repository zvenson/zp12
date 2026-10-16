#!/usr/bin/env python3
"""The FM-1 firmware switcher's catalogue (fm1.designburgapps.com, served from this repo's fm1/): the newest .fwsc
of every firmware, mirrored unchanged next to the page (GitHub's downloads cannot be fetched from a browser: no
CORS), checked (the FM-1 package identity, the Felucca update loader), with its SHA-256, version and date.

  tools/fm1_sync.py [--out DIR] [--purge] [sloopdx repo]
                                        -> DIR/fw/<id>-<version>.fwsc, DIR/catalog.json (DIR: fm1/, here for a look;
                                           on the Pi every 15 min into fm1live, outside git, served as /catalog.json
                                           and /fw/: the page always offers the newest release). A firmware whose new
                                           release fails the check keeps its last good package. --purge: a changed
                                           catalogue out of Cloudflare's edge (tools/cf_cache.py)
  tools/fm1_sync.py [--out DIR] --sources SRC
                                        -> SRC/<id>-<version>.tar.gz: the source of every package in the catalogue,
                                           the project's archive of that tag (GPL-3.0 §6: the source next to the
                                           object code, kept even if a repository goes); on the Pi, outside git,
                                           served as fm1.designburgapps.com/src/

GPL-3.0 projects only (each package's source is at the project's repository and tag, linked on the page).
A firmware is added to FIRMWARES below; its author asked first."""
import hashlib, json, os, re, subprocess, sys, urllib.error, urllib.parse, urllib.request
from email.utils import parsedate_to_datetime
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
OUT = HERE / "fm1"
BLK, KEEP, BLOCKS = 0x30, 0x2F, 20                # (as fm1pkg.js productOf)

# id, editor / play (its web editor, its version in the browser), kind (the page's filter, one or a list: synth,
# groove, drums, chords, dev, other), added (the day it came on the hub, not its release), name, author, repo, the identity the running firmware reports (regex), what it is; source: "github" (the
# latest release's .fwsc), "pages:<its web installer>" (no releases: the package its installer names), "repo:<dir>" (no releases: the newest
# package committed in that directory of the repository, its tag v<version>) or a path in this machine's checkouts (our own, deployed from there)
# installs on the cards: Matomo's "Install OK" events of site 4 (the page sends "<name> <version>"), all versions
# together; the token (view access is enough) in ~/.config/designburg/matomo-token, none there: no counts
# (and only with FM1_INSTALLS=1: /srv/fm1/staging/sync.sh sets it, the live sync not yet)
MATOMO, MATOMO_SITE, MATOMO_TOKEN = "https://matomo.designburgapps.com/", 4, Path.home() / ".config/designburg/matomo-token"

# vintage=True: the firmware is, at its core, one classic instrument rebuilt (not only some of its sounds); the page
# names no maker in a tag, the description says what it is after
FIRMWARES = [  # (the page orders them: zp12, sloopDX, then the others by their GitHub stars)
    # (Salt, ChoralRoot, Jangada, Melodee, Hortator, GHOULBOX, Bubba, Floyd, DX7 Banks, WaveLoop report numbers the others use too: their check is any FM-1
    # identity, the update loader is checked as for all; the page names the running one only when it is unambiguous)
    dict(id="zp12", vintage=True, added="2026-10-08", play="https://thefirmwarehub.com/play/zp12/", editor="https://zp12.designburgapps.com/editor/",
         kind="drums", name="zp12", author="zvenson", repo="zvenson/zp12", ident=r"FM-1_97\d",
         src="zp12:docs/install/firmware", site="https://zp12.designburgapps.com",
         what="A standalone 12-bit sampler inspired by the E-mu SP-1200: sample in the browser (mic, line-in or a "
              "file), unplug, and the FM-1 plays on its own: nothing smoothed, loops and songs."),
    dict(id="hortator", added="2026-10-08", play="https://deadactive.github.io/hortator/",
         kind="drums", name="Hortator", author="DEADACTIVE", repo="deadactive/hortator", ident=r"FM-1_\d{3,8}", src="github",
         site="https://deadactive.github.io/hortator/",
         what="A drum machine: eight tracks of drums, a step sequencer, Grids, a pumping compressor, LFOs, resonators and "
              "live effects. Play it in the browser first."),
    dict(id="felucca", added="2026-10-08", editor="https://hugelton.github.io/Felucca/webapp/editor/", play="https://hugelton.github.io/Felucca/webapp/try/",
         kind=["groove", "dev"], name="Felucca", author="Leo Kuroshita (Hügelton Instruments)", repo="hugelton/Felucca",
         ident=r"FM-1_91\d", src="github",
         what="The custom firmware the others build on: the FM-1's own synth engines, a sequencer, effects and a "
              "better panel, with an editor in the browser."),
    dict(id="salt", added="2026-10-08", editor="https://chancethemaker.github.io/Felucca/webapp/editor/",
         kind=["synth", "groove"], name="Felucca [Salt]", author="Chance Roth (ChanceTheMaker)", repo="ChanceTheMaker/Felucca",
         ident=r"FM-1_\d{3,8}", src="github",
         what="Felucca with a hardware-inspired Studio in the browser (play it there without an FM-1): many engines, "
              "presets, skins. A beta."),
    dict(id="jangada", added="2026-10-08", editor="https://zednaked.github.io/jangada/webapp/editor/", play="https://zednaked.github.io/jangada/webapp/studio/",
         kind=["synth", "groove"], name="Jangada", author="zednaked", repo="zednaked/jangada", ident=r"FM-1_\d{3,8}", src="github",
         what="Dark, industrial, Brazilian: drones that breathe, ten engines (6-op FM, a superwave analog with a ladder "
              "filter), four tracks, a mod matrix, live effects."),
    dict(id="ghoulbox", added="2026-10-09", editor="https://jasonpersinger.me/ghoulbox-fm1-dungeon-synth/webapp/editor/",
         kind=["synth", "groove"], name="GHOULBOX", author="Jason Persinger", repo="jasonpersinger/ghoulbox-fm1-dungeon-synth",
         ident=r"FM-1_\d{3,8}", src="github", site="https://jasonpersinger.me/ghoulbox-fm1-dungeon-synth/",
         what="Dungeon synth: a cathedral hall reverb, tape and crush, a hurdy-gurdy engine, choirs, organs and "
              "recorded psaltery and harp, an old-RPG screen with wall torches."),
    dict(id="melodee", added="2026-10-08", editor="https://keremimo.github.io/melodee/webapp/editor/",
         kind=["synth", "groove"], name="Melodee", author="keremimo", repo="keremimo/melodee", ident=r"FM-1_\d{3,8}", src="github",
         what="Total playing pleasure, far beyond Felucca: recording without quantize, microtonal scales, a Casio CZ-1, "
              "a bit-by-bit Dexed, scale modes made for an MPC Sample as its companion."),
    dict(id="sloop", added="2026-10-08", editor="https://isod89.github.io/sloop-fm1/webapp/editor/",
         kind="groove", name="SLOOP", author="isod89", repo="isod89/sloop-fm1", ident=r"FM-1_900", src="github",
         what="A live groovebox: tracks, layers, a step sequencer, song mode, punch-in effects and samples, played "
              "on the FM-1 in real time."),
    dict(id="bubba", added="2026-10-13", play="https://erbubar23.github.io/bubba-machine/en/",
         kind=["groove", "drums", "synth"], name="Bubba Machine", author="Erbubar23", repo="Erbubar23/bubba-machine",
         ident=r"FM-1_003", src="github", pkg=r"bubba-machine-[^/\"]+\.fwsc", loader="mvave",   # (its releases carry bubba-diag too)
         site="https://erbubar23.github.io/bubba-machine/en/",
         what="A six-track groovebox written from scratch: TR drums from Plaits' 808 and 909 models, a sampler with a "
              "16-slice slicer, a bass and three free tracks (analog, 6-op FM, 303 acid), scenes and fills, all at 44.1 kHz."),
    dict(id="floyd", added="2026-10-09", editor="https://isod89.github.io/sloop-fm1/webapp/editor/",
         kind="synth", name="SLOOP Floyd FM", author="René Bohne", repo="renebohne/sloop-fm1", ident=r"FM-1_\d{3,8}",
         src="github", pkg=r"sloop-[^/\"]+\.fwsc",              # (its release carries a felucca-*.fwsc too: the sloop one)
         what="SLOOP with a visual four-operator FM engine after Floyd Steinberg's 4-OP idea: an ADSR per operator, "
              "seven pages, a live oscilloscope that draws the envelopes as you play."),
    dict(id="sloopbanks", vintage=True, added="2026-10-09", editor="https://isod89.github.io/sloop-fm1/webapp/editor/",
         kind=["groove", "synth"], name="SLOOP DX7 Banks", author="majnikool", repo="majnikool/sloop-fm1", ident=r"FM-1_\d{3,8}",
         src="github",
         what="SLOOP 2.4 with up to twelve DX7 voice banks, kept and named in the editor, presets by kind. Test builds: "
              "back up first."),
    dict(id="sloopdx", vintage=True, added="2026-10-08", editor="https://dx7.designburgapps.com/webapp/editor/",
         kind=["groove", "synth"], name="sloopDX", author="zvenson", repo="zvenson/dxsloop", ident=r"FM-1_93\d",
         src="sloopdx:docs/firmware", site="https://dx7.designburgapps.com",
         what="SLOOP's live workflow with Dexed's DX7 engine and 128 steps a track: six operators, 32 algorithms, "
              "your own .syx banks, every voice edited on the FM-1, FM drums you program."),
    dict(id="x0x", vintage=True, added="2026-10-08", play="https://charlesvestal.github.io/fm1-x0x/emu/",
         kind="groove", name="X0X", author="Charles Vestal", repo="charlesvestal/fm1-x0x", ident=r"FM-1_90\d{5}",
         src="github",
         what="A groovebox: 909 and 808 drums, two 303s with TB-3PO, a breakbeat generator and song mode."),
    dict(id="fomni", vintage=True, added="2026-10-08", play="https://charlesvestal.github.io/fm1-fomni/emu/",
         kind="chords", name="FoMni", author="Charles Vestal", repo="charlesvestal/fm1-fomni", ident=r"FM-1_800\d{4}",
         src="github",
         what="A chord harp inspired by the Omnichord: strum the white keys, pick chords on the black ones."),
    dict(id="choralroot", added="2026-10-08", play="https://quixotic7.github.io/ChoralRootFM1/emu/",
         kind="chords", name="ChoralRoot", author="Quixotic7", repo="Quixotic7/ChoralRootFM1", ident=r"FM-1_\d{3,8}",
         src="github",
         what="A Telepathic Orchid-style chord instrument: one hand plays roots, the other shapes chords; voicings, "
              "performance modes, bass and a looper."),
    dict(id="waveloop", added="2026-10-09", editor="https://eli7vh.github.io/Felucca/webapp/editor/",
         kind=["synth", "groove"], name="WaveLoop FM-1", author="ELI7VH", repo="ELI7VH/Felucca", ident=r"FM-1_\d{3,8}",
         src="github", site="https://eli7vh.github.io/Felucca/mod/",
         what="Felucca played from an Arturia MiniLab 3: track faders, a DJ filter, momentary effect pads; 32 patches a "
              "bank, 12 evolving songs, revoiced 808 and CR78 kits."),
    dict(id="obervave", vintage=True, play="https://thefirmwarehub.com/play/obervave/", added="2026-10-13",
         kind="synth", name="OberVave", author="zvenson", repo="zvenson/obervave", ident=r"FM-1_95\d",
         src="repo:docs/firmware",
         what="An 8-voice analog-style poly in the spirit of the classic American polys of 1979-84: two oscillators a "
              "voice, sync, cross-mod, a 12 / 24 dB filter with a SEM-style morph, an arpeggiator with hold, chorus, "
              "ping-pong delay and a plate reverb. A two-track step sequencer played as sloopDX: live recording on top, "
              "chords, long notes, a click and a count-in."),
    dict(id="dinghy", added="2026-10-09",
         kind="dev", name="Dinghy", author="zvenson", repo="zvenson/dinghy", ident=r"FM-1_99\d", src="github",
         what="Your base for your own FM-1 firmware: everything under it done and documented (updates, USB rescue, "
              "USB-MIDI, USB audio, keys, screen, settings), a 4-voice sine on top. Fork it, replace the sine."),
    dict(id="fimba", added="2026-10-08", play="https://jadamsowers.github.io/fm1-fimba/",
         kind=["synth", "chords"], name="FiMba-1", author="jadamsowers", repo="jadamsowers/fm1-fimba",
         ident=r"FM-1_800\d{4}", src="github", site="https://jadamsowers.github.io/fm1-fimba/",
         what="A physically modelled kalimba: tines laid out like the real one, thumb-roll chords, mbira patterns, "
              "a sound hole to cover, grains, tape and a plate reverb. Play it in the browser first."),
    dict(id="jiant", added="2026-10-10", editor="https://juanjiant-bit.github.io/JIANT/webapp/editor/", play="https://juanjiant-bit.github.io/JIANT/",
         kind=["synth", "groove"], name="JIANT FM", author="juanjiant-bit", repo="juanjiant-bit/JIANT", ident=r"FM-1_\d{3,8}",
         src="pages:https://juanjiant-bit.github.io/JIANT/webapp/installer/", site="https://juanjiant-bit.github.io/JIANT/",
         what="A bio-synthetic groovebox: six tracks drawn as living beings in thermal colours, eight engines (FM6 with "
              "Dexed, analog, phase, voice…), mutating macros, song mode from SLOOP. Not yet tested much on a real FM-1."),
    dict(id="chord", added="2026-10-12", kind="chords", name="fm1-chord", author="math0ne", repo="math0ne/fm1-chord", ident=r"FM-1_\d{3,8}",
         src="github",
         what="A chord machine after the HiChord, with extras: chords in a key under the white keys, slash chords and "
              "inversions, sounds, envelopes, effects and a looper."),
    dict(id="fumi", vintage=True, added="2026-10-12", kind=["synth", "chords"], name="FuMi-1", author="cartesive", repo="cartesive/fumi-1",
         ident=r"FM-1_70\d{5}", src="github",
         what="A shigin conductor after the Suiko ST-50: its two rows of keys, the key set in honsū, equal or its own "
              "just tuning, a koto and twelve more sounds on a 6-op FM engine, ring time, vibrato, trill and a looper."),
    dict(id="maku", added="2026-10-12", play="https://okumurari2.github.io/maku/",
         kind=["groove", "synth"], name="MAKU", author="okumurari", repo="okumurari2/maku", ident=r"FM-1_\d{3,8}",
         src="github", site="https://okumurari2.github.io/maku/",
         what="An ambience machine for DJ interludes: switch it on and a random world in key plays, four parts, buttons "
              "that are verbs, and the worlds you like breed new ones. Clock follow. Play it in the browser first."),
    dict(id="rainbow", added="2026-10-12", play="https://joshbartlettband-web.github.io/felucca-rainbow/webapp/try/",
         kind="other", name="Rainbow Mode", author="joshbartlettband-web", repo="joshbartlettband-web/felucca-rainbow",
         ident=r"FM-1_\d{3,8}", src="pages:https://joshbartlettband-web.github.io/felucca-rainbow/webapp/installer/",
         site="https://joshbartlettband-web.github.io/felucca-rainbow/",
         what="A music toy for little children: 63 cartoon friends, every key in tune, chords, strumming, songs to follow "
              "and an ear game, notes on a staff, a volume cap for small ears."),
    dict(id="amb", added="2026-10-13", play="https://charlesvestal.github.io/fm1-amb/emu/",
         kind="synth", name="AMB-1", author="Charles Vestal", repo="charlesvestal/fm1-amb", ident=r"FM-1_\d{3,8}",
         src="github", site="https://charlesvestal.github.io/fm1-amb/",
         what="A generative ambient machine after Music for Airports: it plays itself from the start, breathing voices, "
              "bells on tape loops of unequal length, a gliding bass. You do not perform it, you tend it."),
    dict(id="ctl", added="2026-10-13", play="https://charlesvestal.github.io/fm1-ctl/emu/",
         kind=["chords", "other"], name="CTL-1", author="Charles Vestal", repo="charlesvestal/fm1-ctl", ident=r"FM-1_6000\d{3}",
         src="github", site="https://charlesvestal.github.io/fm1-ctl/",
         what="A MIDI controller: notes in any scale with its chords on the black keys, four banks of knobs, an "
              "arpeggiator and Euclidean drums, with General MIDI sounds of its own when nothing is connected."),
    dict(id="winampfm", added="2026-10-14", play="https://hericdk.github.io/winampFM/sim/",
         kind=["groove", "synth"], name="winampFM Synth", author="Hericdk", repo="hericdk/winampFM", ident=r"FM-1_900",
         src="repo:release", site="https://hericdk.github.io/winampFM/",   # (no releases: the package committed in release/, its tag v<version>)
         what="Winamp 2.x on the FM-1, built on SLOOP: your sounds as a playlist, four tracks you record as loops, "
              "effects on sliders and a visualizer made of lines. The real firmware plays in a 3D simulator first."),
    dict(id="mycelium", added="2026-10-14", kind="groove", name="MYCELIUM", author="Mandalars", repo="megagott/mycelium-fm1",
         ident=r"FM-1_900", src="github", site="https://www.mandalars.org/mandalars/fm1/",
         what="Forest, Psytrance, Goa and Hitech grooves, built on Felucca and SLOOP: original drums and icons and four "
              "factory songs. A test release: not yet fully tested on the device."),
    dict(id="polly", vintage=True, added="2026-10-13", play="https://charlesvestal.github.io/fm1-polly/emu/",
         kind=["groove", "chords"], name="Polly", author="Charles Vestal", repo="charlesvestal/fm1-polly", ident=r"FM-1_3\d{6}",
         src="github", site="https://charlesvestal.github.io/fm1-polly/",
         what="A pocket arranger after the QY70: pick a style, play three keys for a chord and a band of eight follows, "
              "songs from sections, General MIDI sounds from Sonivox EAS, and a General MIDI module over MIDI in."),
    dict(id="etch", added="2026-10-13", kind="synth", name="ETCH", author="SikkenWorks", repo="SikkenWorks/ETCH",
         ident=r"FM-1_\d{3,8}", src="github", ver="pkg", site="https://etch.sikkenworks.com",   # (one fixed tag: the version from the package's name)
         what="Draw the sound: a waveform drawn and changed with the knobs, saved, morphed into another one, and a "
              "third waveform modulating the result."),
    dict(id="fm106", vintage=True, added="2026-10-13", kind="synth", name="FM-106", author="PatttF", repo="PatttF/fm-106",
         ident=r"FM-1_\d{3,8}", src="github", ver="tag",            # (tags Beta2, release: the tag as it is)
         what="The Juno-106 emulation of KR-106 ported to the FM-1: six voices on both cores, the BBD stereo chorus, "
              "128 factory presets and a 1980s Roland look."),
    dict(id="tribe", added="2026-10-13", play="https://apiel.github.io/fm1-tribe/#emulator",
         kind=["groove", "drums"], name="TRIBE", author="apiel", repo="apiel/fm1-tribe", ident=r"FM-1_\d{3,8}",
         src="pages:https://apiel.github.io/fm1-tribe/install/", site="https://apiel.github.io/fm1-tribe/",
         what="A ten-track groovebox where any engine plays on any track: kicks, 909 and 808 voices, the 303, sixteen "
              "clips a track, scatter effects and four master knobs you assign. Built on X0X."),
    dict(id="duell", added="2026-10-13", play="https://oscillr.github.io/DUELL/emu/",
         kind="synth", name="DUELL", author="OSCILL/R", repo="oscillr/DUELL", ident=r"FM-1_\d{3,8}", src="github",
         what="A multimorphing bitimbral synthesizer: two complete parts, three ways to play them together, and a "
              "morph that glides between two versions of every sound."),
    dict(id="skiff", added="2026-10-13", editor="https://illuminus5.github.io/skiff/",
         kind=["groove", "synth"], name="Skiff", author="illuminus5", repo="illuminus5/skiff", ident=r"FM-1_71\d",
         src="github", pkg=r"skiff-[^/\"]+\.fwsc", site="https://illuminus5.github.io/skiff/",
         what="Six tracks of synths and drums with a groovebox's workflow: fourteen engines, a 64-step sequencer with "
              "chords, ratchets, trig conditions and locks, songs, PERFORM with scenes, six themes and a browser editor; "
              "FM6 sounds built from scratch on the unit. Built on Felucca 1.1.5; its sounds and projects load."),
    dict(id="fart", added="2026-10-13", kind="other", name="Fart Machine One", author="adamcharleswilsonmusic-code",
         repo="adamcharleswilsonmusic-code/Fart-Machine-One", ident=r"FM-1_701", src="github",
         what="A questionable musical instrument: eleven artisanal farts, reverb, delay and a sixteen-step sequencer."),
    dict(id="orbit", added="2026-10-13", kind=["groove", "synth"], name="ORBIT", author="dspaudio", repo="dspaudio/orbit",
         ident=r"FM-1_\d{3,8}", src="github",
         what="A four-track groovebox: three synths and a drum machine in a colour-coded Synth, Drum, Event Tape and "
              "Mixer workflow, loops turned into songs, user macros, screens after the OP-1. Based on SLOOP."),
    dict(id="felucca8", added="2026-10-13", kind=["synth", "groove"], name="Felucca 8 Patterns", author="echoe",
         repo="echoe/Felucca_8_patterns", ident=r"FM-1_\d{3,8}", src="github",
         what="Felucca 1.5.1 with eight sequences a track to switch between and chain one after another; the user "
              "samples are left out to make room."),
    dict(id="pigeon", added="2026-10-15", play="https://theshrimpblimp.com/pigeon/webapp/emu/",
         kind=["drums", "groove"], name="PIGEON", author="The Shrimp Blimp", repo="TheShrimpBlimp/pigeon", ident=r"FM-1_900",
         src="pages:https://theshrimpblimp.com/pigeon/webapp/installer/", site="https://theshrimpblimp.com/pigeon/",
         what="An old-school hip-hop and house sampler after the SP-404 and the PO-33: two song samples and a 16-pad kit, "
              "up to 32 chops a song, a 64-step sequencer, patterns and a song arrangement. Built on Felucca."),
    dict(id="cs1", vintage=True, added="2026-10-15", play="https://twochannels.github.io/CS-1/emu/",
         kind="synth", name="CS-1", author="twochannels", repo="twochannels/CS-1", ident=r"FM-1_950",
         src="pages:https://twochannels.github.io/CS-1/install.html", site="https://twochannels.github.io/CS-1/",
         what="A Yamaha CS-80 in spirit: eight voices of two channels, high- and low-pass filters in series, ring "
              "modulator, the touch response and 22 tones from the owner's manual. A sound module played over MIDI."),
    dict(id="hyp1", added="2026-10-15", play="https://rustypawz.github.io/fm1-HYP1/emu/",
         kind="groove", name="HYP-1", author="RedtailWorks", repo="RustyPawz/fm1-HYP1", ident=r"FM-1_701\d{4}",
         src="pages:https://rustypawz.github.io/fm1-HYP1/install/", site="https://rustypawz.github.io/fm1-HYP1/",
         what="Hypnotic techno and acid: a breathing pad, a deep drone, a generative 303 line and grain over drums you "
              "sequence. Flip patterns on the bar, break it down, build it up. Forked from AMB-1."),
    dict(id="tonitor", added="2026-10-15", kind=["synth", "other"], name="Tonitor", author="Nuno Zimas",
         repo="nzimas/tonitor", ident=r"FM-1_700", src="pages:https://nzimas.github.io/tonitor/firmware/tonitor.fwsc",
         site="https://nzimas.github.io/tonitor/",
         what="Six tracks for live ambient and experimental music: three synth engines, patches generated by algorithm, "
              "free-length loops with up to ten overdubs each, a granulator, filters and a lush reverb."),
    dict(id="noizdemons", added="2026-10-15", kind="synth", name="NOIZ DEMONS", author="SPIKE the Percussionist",
         repo="SPIKE-the-Percussionist/noiz_demons", ident=r"FM-1_900", src="github",
         what="Felucca with randomization as a ritual: every new sound summons one of 30 demons of the Ars Goetia, its "
              "sigil takes the screen and its rank decides how wild the sound gets."),
    dict(id="coralflower", added="2026-10-15", play="https://ficusficusov.github.io/CoralFlower/play/",
         kind="chords", name="CoralFlower", author="FicusFicusov", repo="FicusFicusov/CoralFlower", ident=r"FM-1_930",
         src="github", site="https://ficusficusov.github.io/CoralFlower/",
         what="Chords after the Orchid, with a UX made for the FM-1: one key turns every note into a chord with a bass, "
              "27 scales, an arpeggiator, a looper, a plate reverb, and a pixel flower that grows as you play."),
    dict(id="vector37", added="2026-10-16", play="https://genkigenki.github.io/smk37/vector37-sim/",
         kind="synth", name="Vector37", author="genkigenki", repo="genkigenki/Vector37", ident=r"FM-1_\d{3,8}",
         src="smk:vector37",                            # (no releases: the FM-1 package on genkigenki's SMK-37 installer)
         what="An orbiting overtone synth: four corners of two overtone generators each, a sun on a path and a moon "
              "around it decide how much of each you hear; the knobs shape the selected planet. Made for the SMK-37, "
              "here its FM-1 build. After the Beetlecrab Vector."),
]


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


GITHUB_TOKEN = Path.home() / ".config/designburg/github-token"   # (read-only, public repos: 5000 API calls an hour, not 60)


def get(url, accept="application/vnd.github+json"):
    h = {"Accept": accept, "User-Agent": "fm1-switcher (designburgapps.com)"}
    if url.startswith("https://api.github.com/") and GITHUB_TOKEN.exists():
        h["Authorization"] = "Bearer " + GITHUB_TOKEN.read_text().strip()
    req = urllib.request.Request(url, headers=h)
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def version_of(tag, asset, ver=None):
    """the version shown: the tag's number; ver="tag" the tag as it is (Beta2), ver="pkg" the package's name (a fixed tag)"""
    if ver == "pkg":
        return re.search(r"\d+(?:\.\d+)+", asset).group(0)
    return tag if ver == "tag" else re.sub(r"^\D*", "", tag)


def latest_feed(repo, pkg=r"[^/\"]+\.fwsc", ver=None):
    """as latest_github from the releases feed and the assets' page: no API, so no 60 an hour (PC and Pi share one)"""
    feed = get(f"https://github.com/{repo}/releases.atom", "application/atom+xml").decode()
    entries = sorted(re.findall(r"<entry>(.*?)</entry>", feed, re.S),   # (newest first: the feed goes by the tags)
                     key=lambda e: re.search(r"<updated>([^<]+)</updated>", e).group(1), reverse=True)
    for entry in entries[:10]:
        tag = re.search(r'href="https://github\.com/[^"]+/releases/tag/([^"]+)"', entry).group(1)
        page = get(f"https://github.com/{repo}/releases/expanded_assets/{tag}", "text/html").decode()
        asset = re.search(r'href="(/[^"]+/releases/download/[^/"]+/' + pkg + ')"', page)
        if asset:
            return (get("https://github.com" + asset.group(1), "application/octet-stream"), version_of(tag, asset.group(1).rsplit("/", 1)[-1], ver),
                    re.search(r"<updated>([^<]+)</updated>", entry).group(1)[:19] + "Z",
                    f"https://github.com/{repo}/releases/tag/{tag}", f"https://github.com/{repo}/tree/{tag}")
    raise ValueError("no release with a .fwsc")


def latest_github(repo, pkg=r"[^/\"]+\.fwsc", ver=None):
    """the newest release with a package (pkg: which one when a release has several)"""
    try:
        rels = json.loads(get(f"https://api.github.com/repos/{repo}/releases?per_page=10"))   # (betas too: "latest" skips them)
    except urllib.error.HTTPError as e:
        if e.code not in (403, 429):
            raise
        return latest_feed(repo, pkg, ver)                 # (the API's limit reached)
    rels.sort(key=lambda r: r.get("published_at") or "", reverse=True)   # (the API goes by the tags, not by the release)
    rel = next(r for r in rels if not r["draft"] and any(re.fullmatch(pkg, a["name"]) for a in r["assets"]))
    asset = next(a for a in rel["assets"] if re.fullmatch(pkg, a["name"]))
    return (get(asset["browser_download_url"], "application/octet-stream"), version_of(rel["tag_name"], asset["name"], ver),   # (v1.2, drum-v0.14.0: from the first digit)
            rel["published_at"], f"https://github.com/{repo}/releases/tag/{rel['tag_name']}",
            f"https://github.com/{repo}/tree/{rel['tag_name']}")


def stars(repo):
    """the repository's GitHub stars (shown on its card, a link to star it)"""
    return json.loads(get(f"https://api.github.com/repos/{repo}")).get("stargazers_count")


def stars_daily(f, was):
    """the stars once a day (a deploy runs the sync too: the API's 60 an hour go to the releases), else the last count"""
    today = datetime.now(timezone.utc).strftime("%Y-%m-%d")
    if was.get("starsDay") == today:
        return {"stars": was.get("stars"), "starsDay": today}
    try:
        return {"stars": stars(f["repo"]), "starsDay": today}
    except Exception:                                 # (GitHub busy: the last count, tried again next run)
        return {"stars": was.get("stars"), "starsDay": was.get("starsDay")}


SMK_INSTALLER = "https://genkigenki.github.io/smk37/"   # (genkigenki's installer: its const FW names each port's version)


def smk_version(tag):
    """a tag or an installer's version as the SMK-37 page shows it: smk37-v1.3.1-beta, v0.9.24-beta -> 1.3.1 beta, 0.9.24 beta"""
    v = re.sub(r"^.*?(?=\d+\.\d)", "", tag, count=1)             # (from the first number with a dot: smk37-v1.3.1 -> 1.3.1)
    return re.sub(r"[-_ ]?(beta|alpha|rc)\b", r" \1", v, flags=re.I)


def smk37(old):
    """the SMK-37 page's versions and stars, live (the page is static, from git; it takes these from catalog.json): the ports'
    and Makkuro37's from genkigenki's installer, the others' from their newest tag (web/smk37.json "tags": a pattern).
    A firmware on the installer but not on the page: a problem, for the admin's task page"""
    d = json.loads((HERE / "web" / "smk37.json").read_text(encoding="utf-8"))
    vers, problems, inst = dict(old.get("versions", {})), [], None
    try:
        page = get(SMK_INSTALLER, "text/html").decode()
        inst = {f["id"]: f for f in json.loads(re.search(r"const FW = (\[.*?\]);\n", page, re.S).group(1))}
    except Exception as e:
        problems.append({"id": "smk37", "name": "SMK-37 installer", "repo": "genkigenki/smk37", "error": f"its version list: {e}"[:300]})
    ours = {f["id"] for f in d["own"] + d["ports"]}
    for f in d["own"] + d["ports"]:
        try:
            if f.get("tags"):                             # (the newest tag that matches, by its numbers)
                tags = [t["name"] for t in json.loads(get(f"https://api.github.com/repos/{f['tags_repo']}/tags?per_page=100"))
                        if re.fullmatch(f["tags"], t["name"])]
                num = lambda t: [int(n) for n in re.findall(r"\d+", smk_version(t))]
                v = smk_version(max(tags, key=num))
            elif inst is not None and f["id"] in inst:
                v = smk_version(inst[f["id"]]["version"])
            else:
                continue
            vers[f["id"]] = {"version": v, "beta": bool(re.search(r"beta|alpha|rc", v, re.I))}
        except Exception as e:                        # (the last known version stays)
            problems.append({"id": f["id"], "name": f["name"] + " (SMK-37)", "repo": f.get("tags_repo", ""), "error": str(e)[:300]})
    for i, f in (inst or {}).items():
        if i not in ours:
            problems.append({"id": i, "name": f["name"] + " (SMK-37)", "repo": "genkigenki/smk37",
                             "error": f"on genkigenki's installer ({f['version']}), not on the hub: a card for web/smk37.json"})
    today = datetime.now(timezone.utc).strftime("%Y-%m-%d")
    st, day = dict(old.get("stars", {})), old.get("starsDay")
    if day != today:                                  # (the stars once a day, as the FM-1's)
        home = lambda f: f["orig_repo"] if "orig_repo" in f else f["source"].split("github.com/")[1].split("/tree/")[0].split("/blob/")[0]
        for r in sorted({home(f) for f in d["own"] + d["ports"]}):
            try:
                st[r] = stars(r)
            except Exception:
                pass
        day = today
    return {"versions": vers, "stars": st, "starsDay": day}, problems


def latest_pages(spec, repo):
    """the package a GitHub Pages installer names (a project without releases, built from main): in its meta, else the
    first .fwsc the page links; or the spec is the package itself. The version: the meta's, else the file name's
    (pigeon-0.1-beta.fwsc), else the package's date (cs1.fwsc: a name without one)"""
    url = spec.split(":", 1)[1]
    meta = {}
    if url.endswith(".fwsc"):
        pkg = url
    else:
        page = get(url, "text/html").decode()
        m = re.search(r"const meta = (?=\{)", page)
        if m:                                         # (read as JSON to its end: it may nest, e.g. a list of versions)
            meta = json.JSONDecoder().raw_decode(page, m.end())[0]
        else:
            m = re.search(r"[\"']([^\"'\s]+\.fwsc)[\"']", page)
            if not m:
                raise ValueError(f"no package on {url}")
            meta = {"pkg": m.group(1)}
        pkg = urllib.parse.urljoin(url, meta["pkg"])
    req = urllib.request.Request(urllib.parse.quote(pkg, safe=":/?=&%~+"), headers={"User-Agent": "fm1-switcher (designburgapps.com)"})
    with urllib.request.urlopen(req, timeout=60) as r:
        raw = r.read()
        date = parsedate_to_datetime(r.headers["Last-Modified"]).strftime("%Y-%m-%dT%H:%M:%SZ")
    named = re.fullmatch(r"[A-Za-z0-9]+[-_]v?(\d[\w.+-]*)\.fwsc", pkg.rsplit("/", 1)[-1])
    version = (meta.get("version") or (named.group(1) if named else date[:10].replace("-", "."))).lstrip("v")   # (2026.10.14: the page shows it whole)
    commit = version.rsplit("-", 1)[-1]                # (v0.6.2-be3719e: the build's commit; 1.1.5-rb: none, then main)
    return raw, version, date, None, f"https://github.com/{repo}/tree/{commit if re.fullmatch(r'[0-9a-f]{7,40}', commit) else 'main'}"


def latest_repo(spec, repo):
    """the newest package committed in the repository (repo:<dir>; a project without releases yet): one API call for
    the listing, one for the commit's date; the source at the tag v<version>"""
    sub = spec.split(":", 1)[1]
    files = json.loads(get(f"https://api.github.com/repos/{repo}/contents/{sub}"))
    vkey = lambda f: [int(n) for n in re.findall(r"\d+", f["name"])]
    pkgs = sorted((f for f in files if f["name"].endswith(".fwsc")), key=vkey)
    if not pkgs:
        raise ValueError(f"no package in {sub}")
    pk = pkgs[-1]
    version = re.sub(r"^[a-z0-9]+-|\.fwsc$", "", pk["name"])
    commits = json.loads(get(f"https://api.github.com/repos/{repo}/commits?path={urllib.parse.quote(sub + '/' + pk['name'])}&per_page=1"))
    date = commits[0]["commit"]["committer"]["date"] if commits else datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    return get(pk["download_url"], "application/octet-stream"), version, date, None, f"https://github.com/{repo}/tree/v{version}"


def latest_smk(spec, repo):
    """the FM-1 package of a firmware made for the SMK-37 (smk:<id>): its "fm1" variant on genkigenki's installer, whose
    const FW names the version and the package; the date: the package's Last-Modified"""
    fid = spec.split(":", 1)[1]
    page = get(SMK_INSTALLER, "text/html").decode()
    f = {x["id"]: x for x in json.loads(re.search(r"const FW = (\[.*?\]);\n", page, re.S).group(1))}[fid]
    v = f["v"]["fm1"]
    with urllib.request.urlopen(urllib.request.Request(urllib.parse.urljoin(SMK_INSTALLER, v["pkg"]),
                                                       headers={"User-Agent": "fm1-switcher (designburgapps.com)"}), timeout=60) as r:
        raw = r.read()
        date = parsedate_to_datetime(r.headers["Last-Modified"]).strftime("%Y-%m-%dT%H:%M:%SZ")
    if hashlib.sha256(raw).hexdigest() != v["sha256"]:
        raise ValueError(f"{f['version']}: the package is not the one the installer names (sha256)")
    return raw, f["version"], date, None, f"https://github.com/{repo}"


def latest_local(spec, sloopdx):
    where, sub = spec.split(":", 1)
    root = (Path(sloopdx) if where == "sloopdx" else HERE) / sub
    vkey = lambda p: [int(n) for n in re.findall(r"\d+", p.name)]   # (by version: a checkout's mtimes say nothing)
    pkgs = sorted(root.glob("*.fwsc"), key=vkey)
    if not pkgs:
        raise SystemExit(f"fm1_sync: no package in {root}")
    p = pkgs[-1]
    version = re.sub(r"^[a-z0-9]+-|\.fwsc$", "", p.name)
    # (the release date: the commit that brought the package; a fresh clone's mtimes are the day of the clone)
    log = subprocess.run(["git", "-C", str(root), "log", "-1", "--format=%cI", "--", p.name], capture_output=True, text=True).stdout.strip()
    date = (datetime.fromisoformat(log).astimezone(timezone.utc) if log else
            datetime.fromtimestamp(p.stat().st_mtime, timezone.utc)).strftime("%Y-%m-%dT%H:%M:%SZ")
    return p.read_bytes(), version, date, None, None


def installs(old):
    """our id -> its successful installs so far (Matomo: visitors who opt out are not in it); unreachable: the last run's"""
    if os.environ.get("FM1_INSTALLS") != "1":          # (only where the sync is told to: the test site for now)
        return {}
    last = {i: f["installs"] for i, f in old.items() if f.get("installs")}
    if not MATOMO_TOKEN.exists():
        return last
    try:
        body = urllib.parse.urlencode({"module": "API", "method": "Events.getName", "idSite": MATOMO_SITE, "period": "range",
                                       "date": "2026-10-01,today", "secondaryDimension": "eventAction", "flat": 1,
                                       "filter_limit": -1, "format": "JSON", "token_auth": MATOMO_TOKEN.read_text().strip()}).encode()
        req = urllib.request.Request(MATOMO + "index.php", data=body, headers={"User-Agent": "fm1-switcher (thefirmwarehub.com)"})
        rows = json.loads(urllib.request.urlopen(req, timeout=30).read())
        if isinstance(rows, dict):
            raise ValueError(rows.get("message"))
    except Exception as e:
        print(f"fm1_sync: installs from Matomo: {e}", file=sys.stderr)
        return last
    names = sorted(((f["name"] + " ", f["id"]) for f in FIRMWARES), key=lambda x: -len(x[0]))   # (the longest first: SLOOP DX7 Banks before SLOOP)
    n = {}
    for r in rows:
        name, _, action = r.get("label", "").rpartition(" - ")
        if action != "Install OK":
            continue
        i = next((i for p, i in names if (name + " ").startswith(p)), None)
        if i:
            n[i] = n.get(i, 0) + int(r.get("nb_events", 0))
    return n


def main(sloopdx=str(HERE.parent / "sloopdx"), out=OUT, purge=False):
    (out / "fw").mkdir(parents=True, exist_ok=True)
    try:
        old = {f["id"]: f for f in json.loads((out / "catalog.json").read_text())["firmwares"]}
    except (OSError, ValueError, KeyError):
        old = {}
    keep, cat, problems = set(), [], []
    for f in FIRMWARES:
        try:
            raw, version, date, rel_url, src_url = latest_github(f["repo"], f.get("pkg", r"[^/\"]+\.fwsc"), f.get("ver")) if f["src"] == "github" \
                else latest_pages(f["src"], f["repo"]) if f["src"].startswith("pages:") \
                else latest_repo(f["src"], f["repo"]) if f["src"].startswith("repo:") \
                else latest_smk(f["src"], f["repo"]) if f["src"].startswith("smk:") else latest_local(f["src"], sloopdx)
            ident = product_of(raw)
            # (Felucca's update loader; loader="mvave": M-VAVE's own, the official V15 with its app replaced, installed
            # and resumed as the official one is: page.install() passes the identity to resume())
            if not re.fullmatch(f["ident"], ident) or (b"FELUCCA-LOADER-1" not in raw) != (f.get("loader") == "mvave"):
                raise ValueError(f"{version}: identity {ident!r} or the update loader not as expected")
        except Exception as e:                        # (GitHub down, a broken release: the last good one stays)
            print(f"fm1_sync: {f['name']}: {e}", file=sys.stderr)
            problems.append({"id": f["id"], "name": f["name"], "repo": f["repo"], "error": str(e)[:300]})
            if isinstance(e, urllib.error.HTTPError) and e.code == 404:
                continue                              # (the repo deleted or private: off the page, back when it is again)
            if f["id"] in old and (out / old[f["id"]]["pkg"]).exists():
                cat.append(old[f["id"]] | {k: f[k] for k in ("kind", "name", "author", "what")} | {k: f.get(k) for k in ("editor", "play", "added", "loader")}
                           | {"vintage": bool(f.get("vintage"))} | stars_daily(f, old[f["id"]]))   # (the stars on their own: a new count even without the release)
                keep.add(Path(old[f["id"]]["pkg"]).name)
            continue
        # the package's name: never two contents under one name (/fw/ is cached for good, at Cloudflare's edge too): the same
        # bytes keep their name; a new build under the same version (Rainbow Mode: 1.1.5-rb again and again) a name of its own
        digest, o = hashlib.sha256(raw).hexdigest(), old.get(f["id"], {})
        if o.get("sha256") == digest and (out / o.get("pkg", "-")).is_file():
            name = Path(o["pkg"]).name
        elif o.get("version") == version:
            name = f"{f['id']}-{version}+{digest[:8]}.fwsc"
        else:
            name = f"{f['id']}-{version}.fwsc"
        if not (out / "fw" / name).exists() or (out / "fw" / name).read_bytes() != raw:
            (out / "fw" / name).write_bytes(raw)
        keep.add(name)
        cat.append({k: f[k] for k in ("id", "kind", "name", "author", "repo", "ident", "what")} | {
            "editor": f.get("editor"), "play": f.get("play"), "added": f.get("added"), "loader": f.get("loader"),
            "vintage": bool(f.get("vintage")), **stars_daily(f, old.get(f["id"], {})),   # (vintage: one classic instrument rebuilt)
            "site": f.get("site") or f"https://github.com/{f['repo']}", "version": version, "date": date[:10],
            "published": date,                        # (the time too: the page's "Recently updated" order)
            "pkg": "fw/" + name, "product": ident, "size": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
            "release": rel_url or f.get("site"), "source": src_url or f"https://github.com/{f['repo']}",
            "tag": rel_url.rsplit("/", 1)[-1] if rel_url else None,
            "mirror": f"src/{f['id']}-{version}.tar.gz" if rel_url else None,
            "beta": bool(re.search(r"beta|alpha|rc|test", version, re.I) or version.startswith("0."))})
        print(f"fm1_sync: {f['name']:8} {version:14} {date[:10]}  {ident:14} {len(raw)} B")
    # (what went wrong this run: DIR/problems.json, not served (nginx serves only catalog.json and fw/); the VIBE admin's
    # task page shows it, mounted read-only into vibe-php)
    try:
        old_smk = json.loads((out / "catalog.json").read_text()).get("smk37", {})
    except (OSError, ValueError):
        old_smk = {}
    smk, smk_problems = smk37(old_smk)
    problems += smk_problems
    (out / "problems.json").write_text(json.dumps({"at": datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC"),
                                                   "problems": problems}, indent=1, ensure_ascii=False) + "\n")
    inst = installs(old)
    for c in cat:
        if inst.get(c["id"]):
            c["installs"] = inst[c["id"]]
        else:
            c.pop("installs", None)
    if list(old.values()) == cat and old_smk == smk:  # (the order counts too: it is the page's)
        print("fm1_sync: unchanged")
        return
    for p in (out / "fw").glob("*.fwsc"):
        if p.name not in keep:
            p.unlink()
    tmp = out / "catalog.json.tmp"                    # (whole or not at all: the page may be reading it)
    tmp.write_text(json.dumps({"updated": datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC"),
                               "firmwares": cat, "smk37": smk}, indent=1, ensure_ascii=False) + "\n")
    tmp.replace(out / "catalog.json")
    print("fm1_sync: catalogue written")
    if purge:
        day = datetime.now(timezone.utc).strftime("%Y-%m-%d")    # (the page asks for catalog.json?<UTC day>)
        subprocess.run([sys.executable, str(HERE / "tools" / "cf_cache.py"), "purge-url",   # (cf_cache keeps its own zone's)
                        *(f"https://{h}/catalog.json{q}" for h in ("thefirmwarehub.com", "fm1.designburgapps.com") for q in ("", f"?{day}"))],
                       check=False)


def sources(dest, out=OUT):
    """the source archive of each catalogued package (the GitHub ones: their tag), the old ones removed"""
    dest = Path(dest)
    dest.mkdir(parents=True, exist_ok=True)
    cat = json.loads((out / "catalog.json").read_text())["firmwares"]
    keep = set()
    for f in cat:
        if not f.get("mirror"):
            continue
        name = Path(f["mirror"]).name
        keep.add(name)
        if not (dest / name).exists():
            (dest / name).write_bytes(get(f"https://github.com/{f['repo']}/archive/refs/tags/{f['tag']}.tar.gz", "application/octet-stream"))
            print(f"fm1_sync: source {name} ({(dest / name).stat().st_size // 1024} KB)")
    for p in dest.glob("*.tar.gz"):
        if p.name not in keep:
            p.unlink()


if __name__ == "__main__":
    args, out, purge = sys.argv[1:], OUT, False
    if "--out" in args:
        i = args.index("--out"); out = Path(args[i + 1]).resolve(); del args[i:i + 2]
    if "--purge" in args:
        args.remove("--purge"); purge = True
    if args[:1] == ["--sources"]:
        sources(args[1], out)
    else:
        main(*args[:1], out=out, purge=purge)
