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
import hashlib, json, re, subprocess, sys, urllib.error, urllib.request
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
OUT = HERE / "fm1"
BLK, KEEP, BLOCKS = 0x30, 0x2F, 20                # (as fm1pkg.js productOf)

# id, editor / play (its web editor, its version in the browser), kind (the page's filter, one or a list: synth,
# groove, drums, chords), name, author, repo, the identity the running firmware reports (regex), what it is; source: "github" (the
# latest release's .fwsc) or a path in this machine's checkouts (our own, deployed from there)
FIRMWARES = [  # (the page orders them: zp12, sloopDX, then the others by their GitHub stars)
    # (Salt, ChoralRoot, Jangada, Melodee, Hortator, GHOULBOX, Bubba, Floyd, DX7 Banks, WaveLoop report numbers the others use too: their check is any FM-1
    # identity, the update loader is checked as for all; the page names the running one only when it is unambiguous)
    dict(id="zp12", editor="https://zp12.designburgapps.com/editor/",
         kind="drums", name="zp12", author="zvenson", repo="zvenson/zp12", ident=r"FM-1_97\d",
         src="zp12:docs/install/firmware", site="https://zp12.designburgapps.com",
         what="A standalone 12-bit sampler inspired by the E-mu SP-1200: sample in the browser (mic, line-in or a "
              "file), unplug, and the FM-1 plays on its own: nothing smoothed, loops and songs."),
    dict(id="hortator", play="https://deadactive.github.io/hortator/",
         kind="drums", name="Hortator", author="DEADACTIVE", repo="deadactive/hortator", ident=r"FM-1_\d{3,8}", src="github",
         site="https://deadactive.github.io/hortator/",
         what="A drum machine: eight tracks of drums, a step sequencer, Grids, a pumping compressor, LFOs, resonators and "
              "live effects. Play it in the browser first."),
    dict(id="felucca", editor="https://hugelton.github.io/Felucca/webapp/editor/", play="https://hugelton.github.io/Felucca/webapp/try/",
         kind=["synth", "groove"], name="Felucca", author="Leo Kuroshita (Hügelton Instruments)", repo="hugelton/Felucca",
         ident=r"FM-1_91\d", src="github",
         what="The custom firmware the others build on: the FM-1's own synth engines, a sequencer, effects and a "
              "better panel, with an editor in the browser."),
    dict(id="salt", editor="https://chancethemaker.github.io/Felucca/webapp/editor/",
         kind=["synth", "groove"], name="Felucca [Salt]", author="Chance Roth (ChanceTheMaker)", repo="ChanceTheMaker/Felucca",
         ident=r"FM-1_\d{3,8}", src="github",
         what="Felucca with a hardware-inspired Studio in the browser (play it there without an FM-1): many engines, "
              "presets, skins. A beta."),
    dict(id="jangada", editor="https://zednaked.github.io/jangada/webapp/editor/", play="https://zednaked.github.io/jangada/webapp/studio/",
         kind=["synth", "groove"], name="Jangada", author="zednaked", repo="zednaked/jangada", ident=r"FM-1_\d{3,8}", src="github",
         what="Dark, industrial, Brazilian: drones that breathe, ten engines (6-op FM, a superwave analog with a ladder "
              "filter), four tracks, a mod matrix, live effects."),
    dict(id="ghoulbox", editor="https://jasonpersinger.me/ghoulbox-fm1-dungeon-synth/webapp/editor/",
         kind=["synth", "groove"], name="GHOULBOX", author="Jason Persinger", repo="jasonpersinger/ghoulbox-fm1-dungeon-synth",
         ident=r"FM-1_\d{3,8}", src="github", site="https://jasonpersinger.me/ghoulbox-fm1-dungeon-synth/",
         what="Dungeon synth: a cathedral hall reverb, tape and crush, a hurdy-gurdy engine, choirs, organs and "
              "recorded psaltery and harp, an old-RPG screen with wall torches."),
    dict(id="melodee", editor="https://keremimo.github.io/melodee/webapp/editor/",
         kind=["synth", "groove"], name="Melodee", author="keremimo", repo="keremimo/melodee", ident=r"FM-1_\d{3,8}", src="github",
         what="Total playing pleasure, far beyond Felucca: recording without quantize, microtonal scales, a Casio CZ-1, "
              "a bit-by-bit Dexed, scale modes made for an MPC Sample as its companion."),
    dict(id="sloop", editor="https://isod89.github.io/sloop-fm1/webapp/editor/",
         kind="groove", name="SLOOP", author="isod89", repo="isod89/sloop-fm1", ident=r"FM-1_900", src="github",
         what="A live groovebox: tracks, layers, a step sequencer, song mode, punch-in effects and samples, played "
              "on the FM-1 in real time."),
    dict(id="bubba", editor="https://erbubar23.github.io/sloop-fm1/webapp/editor/",
         kind=["groove", "synth", "drums"], name="Bubba", author="Erbubar23", repo="Erbubar23/sloop-fm1", ident=r"FM-1_\d{3,8}",
         src="github", site="https://erbubar23.github.io/sloop-fm1/",
         what="An eight-track groovebox you play live: seven synths and a drum machine, 38 kits, a sampler that slices "
              "your loops on the device and a looper on every track. No factory patterns: everything you hear, you play."),
    dict(id="floyd", editor="https://isod89.github.io/sloop-fm1/webapp/editor/",
         kind="synth", name="SLOOP Floyd FM", author="René Bohne", repo="renebohne/sloop-fm1", ident=r"FM-1_\d{3,8}",
         src="github", pkg=r"sloop-[^/\"]+\.fwsc",              # (its release carries a felucca-*.fwsc too: the sloop one)
         what="SLOOP with a visual four-operator FM engine after Floyd Steinberg's 4-OP idea: an ADSR per operator, "
              "seven pages, a live oscilloscope that draws the envelopes as you play."),
    dict(id="sloopbanks", editor="https://isod89.github.io/sloop-fm1/webapp/editor/",
         kind=["groove", "synth"], name="SLOOP DX7 Banks", author="majnikool", repo="majnikool/sloop-fm1", ident=r"FM-1_\d{3,8}",
         src="github",
         what="SLOOP 2.4 with up to twelve DX7 voice banks, kept and named in the editor, presets by kind. Test builds: "
              "back up first."),
    dict(id="sloopdx", editor="https://dx7.designburgapps.com/webapp/editor/",
         kind=["groove", "synth"], name="sloopDX", author="zvenson", repo="zvenson/dxsloop", ident=r"FM-1_93\d",
         src="sloopdx:docs/firmware", site="https://dx7.designburgapps.com",
         what="SLOOP's live workflow with Dexed's DX7 engine inside: six operators, 32 algorithms, your own .syx banks, "
              "every voice edited on the FM-1, FM drums you program, 128 steps a track."),
    dict(id="x0x", play="https://charlesvestal.github.io/fm1-x0x/emu/",
         kind="groove", name="X0X", author="Charles Vestal", repo="charlesvestal/fm1-x0x", ident=r"FM-1_900\d{4}",
         src="github",
         what="A groovebox: 909 and 808 drums, two 303s with TB-3PO, a breakbeat generator and song mode."),
    dict(id="fomni", play="https://charlesvestal.github.io/fm1-fomni/emu/",
         kind="chords", name="FoMni", author="Charles Vestal", repo="charlesvestal/fm1-fomni", ident=r"FM-1_800\d{4}",
         src="github",
         what="A chord harp inspired by the Omnichord: strum the white keys, pick chords on the black ones."),
    dict(id="choralroot", play="https://quixotic7.github.io/ChoralRootFM1/emu/",
         kind="chords", name="ChoralRoot", author="Quixotic7", repo="Quixotic7/ChoralRootFM1", ident=r"FM-1_\d{3,8}",
         src="github",
         what="A Telepathic Orchid-style chord instrument: one hand plays roots, the other shapes chords; voicings, "
              "performance modes, bass and a looper."),
    dict(id="waveloop", editor="https://eli7vh.github.io/Felucca/webapp/editor/",
         kind=["synth", "groove"], name="WaveLoop FM-1", author="ELI7VH", repo="ELI7VH/Felucca", ident=r"FM-1_\d{3,8}",
         src="github", site="https://eli7vh.github.io/Felucca/mod/",
         what="Felucca played from an Arturia MiniLab 3: track faders, a DJ filter, momentary effect pads; 32 patches a "
              "bank, 12 evolving songs, revoiced 808 and CR78 kits."),
    dict(id="fimba", play="https://jadamsowers.github.io/fm1-fimba/",
         kind=["synth", "chords"], name="FiMba-1", author="jadamsowers", repo="jadamsowers/fm1-fimba",
         ident=r"FM-1_800\d{4}", src="github", site="https://jadamsowers.github.io/fm1-fimba/",
         what="A physically modelled kalimba: tines laid out like the real one, thumb-roll chords, mbira patterns, "
              "a sound hole to cover, grains, tape and a plate reverb. Play it in the browser first."),
]


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def get(url, accept="application/vnd.github+json"):
    req = urllib.request.Request(url, headers={"Accept": accept, "User-Agent": "fm1-switcher (designburgapps.com)"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def latest_feed(repo, pkg=r"[^/\"]+\.fwsc"):
    """as latest_github from the releases feed and the assets' page: no API, so no 60 an hour (PC and Pi share one)"""
    feed = get(f"https://github.com/{repo}/releases.atom", "application/atom+xml").decode()
    entries = sorted(re.findall(r"<entry>(.*?)</entry>", feed, re.S),   # (newest first: the feed goes by the tags)
                     key=lambda e: re.search(r"<updated>([^<]+)</updated>", e).group(1), reverse=True)
    for entry in entries[:10]:
        tag = re.search(r'href="https://github\.com/[^"]+/releases/tag/([^"]+)"', entry).group(1)
        page = get(f"https://github.com/{repo}/releases/expanded_assets/{tag}", "text/html").decode()
        asset = re.search(r'href="(/[^"]+/releases/download/[^/"]+/' + pkg + ')"', page)
        if asset:
            return (get("https://github.com" + asset.group(1), "application/octet-stream"), re.sub(r"^\D*", "", tag),
                    re.search(r"<updated>([^<]+)</updated>", entry).group(1)[:19] + "Z",
                    f"https://github.com/{repo}/releases/tag/{tag}", f"https://github.com/{repo}/tree/{tag}")
    raise ValueError("no release with a .fwsc")


def latest_github(repo, pkg=r"[^/\"]+\.fwsc"):
    """the newest release with a package (pkg: which one when a release has several)"""
    try:
        rels = json.loads(get(f"https://api.github.com/repos/{repo}/releases?per_page=10"))   # (betas too: "latest" skips them)
    except urllib.error.HTTPError as e:
        if e.code not in (403, 429):
            raise
        return latest_feed(repo, pkg)                      # (the API's limit reached)
    rels.sort(key=lambda r: r.get("published_at") or "", reverse=True)   # (the API goes by the tags, not by the release)
    rel = next(r for r in rels if not r["draft"] and any(re.fullmatch(pkg, a["name"]) for a in r["assets"]))
    asset = next(a for a in rel["assets"] if re.fullmatch(pkg, a["name"]))
    return (get(asset["browser_download_url"], "application/octet-stream"), re.sub(r"^\D*", "", rel["tag_name"]),   # (v1.2, drum-v0.14.0: from the first digit)
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


def latest_local(spec, sloopdx):
    where, sub = spec.split(":", 1)
    root = (Path(sloopdx) if where == "sloopdx" else HERE) / sub
    vkey = lambda p: [int(n) for n in re.findall(r"\d+", p.name)]   # (by version: a checkout's mtimes say nothing)
    pkgs = sorted(root.glob("*.fwsc"), key=vkey)
    if not pkgs:
        raise SystemExit(f"fm1_sync: no package in {root}")
    p = pkgs[-1]
    version = re.sub(r"^[a-z0-9]+-|\.fwsc$", "", p.name)
    date = datetime.fromtimestamp(p.stat().st_mtime, timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    return p.read_bytes(), version, date, None, None


def main(sloopdx=str(HERE.parent / "sloopdx"), out=OUT, purge=False):
    (out / "fw").mkdir(parents=True, exist_ok=True)
    try:
        old = {f["id"]: f for f in json.loads((out / "catalog.json").read_text())["firmwares"]}
    except (OSError, ValueError, KeyError):
        old = {}
    keep, cat = set(), []
    for f in FIRMWARES:
        try:
            raw, version, date, rel_url, src_url = latest_github(f["repo"], f.get("pkg", r"[^/\"]+\.fwsc")) if f["src"] == "github" else latest_local(f["src"], sloopdx)
            ident = product_of(raw)
            if not re.fullmatch(f["ident"], ident) or b"FELUCCA-LOADER-1" not in raw:
                raise ValueError(f"{version}: identity {ident!r} or the update loader not as expected")
        except Exception as e:                        # (GitHub down, a broken release: the last good one stays)
            print(f"fm1_sync: {f['name']}: {e}", file=sys.stderr)
            if f["id"] in old and (out / old[f["id"]]["pkg"]).exists():
                cat.append(old[f["id"]] | {k: f[k] for k in ("kind", "name", "author", "what")} | {k: f.get(k) for k in ("editor", "play")}
                           | stars_daily(f, old[f["id"]]))   # (the stars on their own: a new count even without the release)
                keep.add(Path(old[f["id"]]["pkg"]).name)
            continue
        name = f"{f['id']}-{version}.fwsc"
        if not (out / "fw" / name).exists() or (out / "fw" / name).read_bytes() != raw:
            (out / "fw" / name).write_bytes(raw)
        keep.add(name)
        cat.append({k: f[k] for k in ("id", "kind", "name", "author", "repo", "ident", "what")} | {
            "editor": f.get("editor"), "play": f.get("play"), **stars_daily(f, old.get(f["id"], {})),
            "site": f.get("site") or f"https://github.com/{f['repo']}", "version": version, "date": date[:10],
            "published": date,                        # (the time too: the page's "Recently updated" order)
            "pkg": "fw/" + name, "product": ident, "size": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
            "release": rel_url or f.get("site"), "source": src_url or f"https://github.com/{f['repo']}",
            "tag": rel_url.rsplit("/", 1)[-1] if rel_url else None,
            "mirror": f"src/{f['id']}-{version}.tar.gz" if rel_url else None,
            "beta": bool(re.search(r"beta|alpha|rc|test", version, re.I) or version.startswith("0."))})
        print(f"fm1_sync: {f['name']:8} {version:14} {date[:10]}  {ident:14} {len(raw)} B")
    if list(old.values()) == cat:                     # (the order counts too: it is the page's)
        print("fm1_sync: unchanged")
        return
    for p in (out / "fw").glob("*.fwsc"):
        if p.name not in keep:
            p.unlink()
    tmp = out / "catalog.json.tmp"                    # (whole or not at all: the page may be reading it)
    tmp.write_text(json.dumps({"updated": datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC"),
                               "firmwares": cat}, indent=1, ensure_ascii=False) + "\n")
    tmp.replace(out / "catalog.json")
    print("fm1_sync: catalogue written")
    if purge:
        day = datetime.now(timezone.utc).strftime("%Y-%m-%d")    # (the page asks for catalog.json?<UTC day>)
        subprocess.run([sys.executable, str(HERE / "tools" / "cf_cache.py"), "purge-url",
                        "https://fm1.designburgapps.com/catalog.json", f"https://fm1.designburgapps.com/catalog.json?{day}"], check=False)


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
