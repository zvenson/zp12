#!/usr/bin/env python3
"""The zp12 installer page: sloopDX's installer (web/index_pkg.html, fm1pkg.js, fm1ota.js: the same,
tested update path) with zp12's package and texts. Usage:
  make_installer.py <sloopdx repo> build/zp12-X.Y.fwsc <version> <out dir>
writes <out>/index.html and <out>/firmware/zp12-<version>.fwsc (for now the page sits at dx7's /zp12/)."""
import hashlib, json, re, shutil, sys
from pathlib import Path

BLK, KEEP, BLOCKS = 512, 16, 20   # (as sloopDX's make_site.py; checked against its product_of below)


def main(sloopdx, pkg, version, out, own_site=False):
    web = Path(sloopdx) / "web"
    sys.path.insert(0, str(web))
    import make_site                                    # sloopDX's own: product_of, strip_module
    raw = Path(pkg).read_bytes()
    product = make_site.product_of(raw)
    if not re.fullmatch(r"FM-1_97\d", product) or b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: identity {product!r} or loader marker missing")
    html = (web / "index_pkg.html").read_text(encoding="utf-8")
    lib = make_site.strip_module((web / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        make_site.strip_module((web / "fm1ota.js").read_text(encoding="utf-8"))
    name = f"zp12-{version}.fwsc"
    meta = json.dumps({"version": version, "product": product, "pkg": "firmware/" + name,
                       "sha256": hashlib.sha256(raw).hexdigest()})
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta)
    swaps = [
        (r"<title>.*?</title>", "<title>zp12: a 12-bit sampling drum machine for the M-VAVE FM-1</title>"),
        (r'<meta name="description" content=".*?">', '<meta name="description" content="zp12: 12-bit sampling drums for the M-VAVE FM-1, installed from Chrome or Edge. Early test build.">\n<meta name="robots" content="noindex">'),
        (r'<meta property="og:title" content=".*?">', '<meta property="og:title" content="zp12">'),
        ("<!--LOGO-->", "<b>zp<span style=\"color:var(--led)\">12</span></b>"),
        (r'<div class="links">.*?</div>', '<div class="links"><a href="../">sloopDX</a><a href="../webapp/installer/">Back to sloopDX</a>'
         '<a class="gh" href="https://github.com/zvenson/zp12">GitHub</a></div>'),
        ('<p class="eyebrow">Firmware for the M-VAVE FM-1</p>', '<p class="eyebrow">Early test build for the M-VAVE FM-1</p>'),
        ("<h1>Install <b>sloopDX</b></h1>", "<h1>Install <b>zp12</b></h1>"),
        (r'<p class="lead">.*?</p>', '<p class="lead">12-bit sampling drums in the spirit of the 80s: 32 sounds at 26.04 kHz, pitched without interpolation, eight channels with their filters. This build plays the factory kit on the keys; the sequencer and your own samples come next. Back to sloopDX any time with its installer.</p>'),
        (r'<div class="dxanim".*?</div>', ""),
        (r'<aside class="side">.*?</aside>', '''<aside class="side">
      <section class="card">
        <h2>Play it</h2>
        <p class="small"><b>White keys 1&ndash;8</b>: bank A, <b>9&ndash;16</b>: bank B &nbsp; <b>OCT+</b>: banks C + D, <b>OCT&minus;</b>: A + B &nbsp; <b>ARP</b>: MULTI PITCH (the sound on every key)</p>
        <p class="small"><b>KNOB 1&ndash;4</b>: the last pad's TUNE &middot; FINE &middot; DECAY &middot; LEVEL &nbsp; <b>EDIT</b>: TRUNC (start, end, reverse, 45/33), OUT (channel, pan, cutoff, resonance) &nbsp; <b>GLO</b>: the mix of channels 1&ndash;4, 5&ndash;8 &nbsp; <b>SELECT</b>: tempo &nbsp; <b>ALGORITHM</b>: the sound</p>
        <h2>Back to sloopDX</h2>
        <p class="small">Open the <a href="../webapp/installer/">sloopDX installer</a> and press Install. If the FM-1 does not answer: hold OCT&minus; while switching it on (USB rescue), then install.</p>
      </section>
    </aside>'''),
        (r"<footer><div class=\"wrap\">.*?<span data-t=\"license\">", '<footer><div class="wrap">\n  zp12: based on SLOOP (isod89) and Felucca, on the FM-1 platform as sloopDX carries it (GPL-3.0). Factory sounds: CC0 (VSCO-2 CE, VCSL, Sonic Pi). Inspired by the 12-bit samplers of the 80s; their names are trademarks of their owners, no affiliation. Not affiliated with M-VAVE.<br>\n  <span data-t="license">'),
        ("https://github.com/zvenson/dxsloop", "https://github.com/zvenson/zp12"),
        ("github.com/zvenson/dxsloop", "github.com/zvenson/zp12"),
        ("../../impressum.html", "../impressum.html"),
        ("../../", "../"),
    ]
    if own_site:                                        # zp12.designburgapps.com: sloopDX's pages by their address
        swaps += [("../webapp/installer/", "https://dx7.designburgapps.com/webapp/installer/"),
                  ('href="../"', 'href="https://dx7.designburgapps.com/"'),
                  ("../impressum.html", "https://dx7.designburgapps.com/impressum.html"),
                  ('href="../editor/"', 'href="https://dx7.designburgapps.com/webapp/editor/"'),
                  ('<link rel="icon" href="../favicon.svg" type="image/svg+xml">', '<link rel="icon" href="favicon.svg" type="image/svg+xml">'),
                  ('<link rel="icon" href="../favicon-32.png" sizes="32x32">\n', ""),
                  ('<link rel="apple-touch-icon" href="../apple-touch-icon.png">', '<link rel="apple-touch-icon" href="favicon.svg">')]
    for old, new in swaps:
        if old.startswith(("<", "../", "https", "github", "href")) and not any(c in old for c in "*?\\("):
            if old not in html:
                raise SystemExit(f"installer: {old[:40]!r} not found; update make_installer.py")
            html = html.replace(old, new)
        else:
            html, n = re.subn(old, lambda m: new, html, count=1, flags=re.S)
            if not n:
                raise SystemExit(f"installer: {old[:40]!r} not found; update make_installer.py")
    html = re.sub(r"\bsloopDX \$\{meta.version\}", "zp12 ${meta.version}", html)
    out = Path(out)
    (out / "firmware").mkdir(parents=True, exist_ok=True)
    for old in (out / "firmware").glob("zp12-*.fwsc"):
        old.unlink()
    shutil.copy(pkg, out / "firmware" / name)
    (out / "index.html").write_text(html, encoding="utf-8")
    if own_site:                                        # the icon: a navy pad with a red LED, "12"
        (out / "favicon.svg").write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><rect width="64" height="64" rx="10" fill="#263e70"/>'
                                         '<circle cx="50" cy="14" r="6" fill="#ff2820"/><text x="32" y="50" font-family="Arial,Helvetica,sans-serif" '
                                         'font-weight="700" font-size="30" text-anchor="middle" fill="#cecfc9">12</text></svg>\n')
    print(f"installer: {out / 'index.html'} ({len(html)} B), firmware/{name} ({len(raw)} B, {product})")


if __name__ == "__main__":
    main(*sys.argv[1:5], own_site="--own-site" in sys.argv)
