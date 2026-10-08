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
import hashlib, json, re, subprocess, sys, urllib.request
from datetime import datetime, timezone
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
OUT = HERE / "fm1"
BLK, KEEP, BLOCKS = 0x30, 0x2F, 20                # (as fm1pkg.js productOf)

# id, name, author, repo, the identity the running firmware reports (regex), what it is; source: "github" (the
# latest release's .fwsc) or a path in this machine's checkouts (our own, deployed from there)
FIRMWARES = [
    dict(id="zp12", name="zp12", author="zvenson", repo="zvenson/zp12", ident=r"FM-1_97\d",
         src="zp12:docs/install/firmware", site="https://zp12.designburgapps.com",
         what="A 12-bit sampling drum machine in the spirit of the 80s: 26 kHz, pitched with nothing smoothed, "
              "loops and songs, your own samples from the browser."),
    dict(id="felucca", name="Felucca", author="Leo Kuroshita (Hügelton Instruments)", repo="hugelton/Felucca",
         ident=r"FM-1_91\d", src="github",
         what="The custom firmware the others build on: the FM-1's own synth engines, a sequencer, effects and a "
              "better panel, with an editor in the browser."),
    dict(id="sloop", name="SLOOP", author="isod89", repo="isod89/sloop-fm1", ident=r"FM-1_900", src="github",
         what="A live groovebox: tracks, layers, a step sequencer, song mode, punch-in effects and samples, played "
              "on the FM-1 in real time."),
    dict(id="sloopdx", name="sloopDX", author="zvenson", repo="zvenson/dxsloop", ident=r"FM-1_9[0-6]\d",
         src="sloopdx:docs/firmware", site="https://dx7.designburgapps.com",
         what="SLOOP's live workflow with a real DX7 inside: six operators, 32 algorithms, your own .syx banks, "
              "FM drums you program."),
    dict(id="x0x", name="X0X", author="Charles Vestal", repo="charlesvestal/fm1-x0x", ident=r"FM-1_900\d{4}",
         src="github",
         what="A groovebox: 909 and 808 drums, two 303s with TB-3PO, a breakbeat generator and song mode."),
    dict(id="fomni", name="FoMni", author="Charles Vestal", repo="charlesvestal/fm1-fomni", ident=r"FM-1_800\d{4}",
         src="github",
         what="A chord harp inspired by the Omnichord: strum the white keys, pick chords on the black ones."),
]


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def get(url, accept="application/vnd.github+json"):
    req = urllib.request.Request(url, headers={"Accept": accept, "User-Agent": "fm1-switcher (designburgapps.com)"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def latest_github(repo):
    rels = json.loads(get(f"https://api.github.com/repos/{repo}/releases?per_page=10"))   # (betas too: "latest" skips them)
    rel = next(r for r in rels if not r["draft"] and any(a["name"].endswith(".fwsc") for a in r["assets"]))
    asset = next(a for a in rel["assets"] if a["name"].endswith(".fwsc"))
    return (get(asset["browser_download_url"], "application/octet-stream"), rel["tag_name"].lstrip("v"),
            rel["published_at"][:10], f"https://github.com/{repo}/releases/tag/{rel['tag_name']}",
            f"https://github.com/{repo}/tree/{rel['tag_name']}")


def latest_local(spec, sloopdx):
    where, sub = spec.split(":", 1)
    root = (Path(sloopdx) if where == "sloopdx" else HERE) / sub
    vkey = lambda p: [int(n) for n in re.findall(r"\d+", p.name)]   # (by version: a checkout's mtimes say nothing)
    pkgs = sorted(root.glob("*.fwsc"), key=vkey)
    if not pkgs:
        raise SystemExit(f"fm1_sync: no package in {root}")
    p = pkgs[-1]
    version = re.sub(r"^[a-z0-9]+-|\.fwsc$", "", p.name)
    date = datetime.fromtimestamp(p.stat().st_mtime, timezone.utc).strftime("%Y-%m-%d")
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
            raw, version, date, rel_url, src_url = latest_github(f["repo"]) if f["src"] == "github" else latest_local(f["src"], sloopdx)
            ident = product_of(raw)
            if not re.fullmatch(f["ident"], ident) or b"FELUCCA-LOADER-1" not in raw:
                raise ValueError(f"{version}: identity {ident!r} or the update loader not as expected")
        except Exception as e:                        # (GitHub down, a broken release: the last good one stays)
            print(f"fm1_sync: {f['name']}: {e}", file=sys.stderr)
            if f["id"] in old and (out / old[f["id"]]["pkg"]).exists():
                cat.append(old[f["id"]] | {k: f[k] for k in ("name", "author", "what")})
                keep.add(Path(old[f["id"]]["pkg"]).name)
            continue
        name = f"{f['id']}-{version}.fwsc"
        if not (out / "fw" / name).exists() or (out / "fw" / name).read_bytes() != raw:
            (out / "fw" / name).write_bytes(raw)
        keep.add(name)
        cat.append({k: f[k] for k in ("id", "name", "author", "repo", "ident", "what")} | {
            "site": f.get("site") or f"https://github.com/{f['repo']}", "version": version, "date": date,
            "pkg": "fw/" + name, "product": ident, "size": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
            "release": rel_url or f.get("site"), "source": src_url or f"https://github.com/{f['repo']}",
            "tag": rel_url.rsplit("/", 1)[-1] if rel_url else None,
            "mirror": f"src/{f['id']}-{version}.tar.gz" if rel_url else None,
            "beta": bool(re.search(r"beta|alpha|rc", version, re.I) or version.startswith("0."))})
        print(f"fm1_sync: {f['name']:8} {version:14} {date}  {ident:14} {len(raw)} B")
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
