#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""zp12's site (docs/, served as zp12.designburgapps.com):
  index.html            web/landing.html (/*VER*/ the version)
  install/              the installer (tools/make_installer.py: sloopDX's tested update path) and the package
  backup/               web/backup.html + web/zp12link.js (the backup tool)
  editor/               web/editor.html (the sample editor; it loads ../backup/zp12link.js)
  zp12-cheat-sheet.pdf, impressum.html, favicon.svg, img/, zp12-beat.mp4; the cheat sheet itself is a section of
  the start page (web/cheatsheet.html's sheet and its scoped style, <!--CHEATSHEET-->), cheatsheet.html only sends
  old links there
Usage: make_site.py <sloopdx repo> build/zp12-X.Y.fwsc X.Y [video.mp4]"""
import shutil, subprocess, sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SRC / "tools"))
import make_installer  # noqa: E402


def main(sloopdx, pkg, version, video=None):
    docs, web = SRC / "docs", SRC / "web"
    for old in ("index.html", "firmware", "zp12-beat.mp4"):          # (the installer used to be the start page)
        p = docs / old
        if p.is_dir():
            shutil.rmtree(p)
        elif p.exists():
            p.unlink()
    inst = docs / "install"
    make_installer.main(sloopdx, pkg, version, str(inst), own_site=True)
    h = (inst / "index.html").read_text(encoding="utf-8")
    for a, b in (('href="favicon.svg"', 'href="../favicon.svg"'), ('href="cheatsheet.html"', 'href="../#cheatsheet"'),
                 ('href="zp12-cheat-sheet.pdf"', 'href="../zp12-cheat-sheet.pdf"'),
                 ('href="https://dx7.designburgapps.com/impressum.html"', 'href="../impressum.html"'),
                 ('<a class="logo" href="https://dx7.designburgapps.com/" aria-label="sloopDX">', '<a class="logo" href="../" aria-label="zp12">'),
                 ('<link rel="canonical" href="https://dx7.designburgapps.com/webapp/installer/">',
                  '<link rel="canonical" href="https://zp12.designburgapps.com/install/">'),
                 ('<div class="links"><a href="https://dx7.designburgapps.com/">sloopDX</a>', '<div class="links"><a href="../">zp12</a><a href="https://dx7.designburgapps.com/">sloopDX</a>')):
        h = h.replace(a, b)
    (inst / "index.html").write_text(h, encoding="utf-8")
    for f in ("cheatsheet.html", "zp12-cheat-sheet.pdf", "favicon.svg", "zp12-beat.mp4"):
        if (inst / f).exists():
            shutil.move(str(inst / f), str(docs / f)) if not (docs / f).exists() or f == "favicon.svg" else (inst / f).unlink()
    for f in ("zp12-cheat-sheet.pdf", "impressum.html"):
        shutil.copy(web / f, docs / f)
    (docs / "cheatsheet.html").write_text('<!doctype html><meta charset="utf-8"><title>zp12 cheat sheet</title>'
                                          '<meta http-equiv="refresh" content="0; url=./#cheatsheet">'
                                          '<link rel="canonical" href="https://zp12.designburgapps.com/#cheatsheet">'
                                          '<a href="./#cheatsheet">The cheat sheet</a>\n', encoding="utf-8")
    (docs / "backup").mkdir(exist_ok=True)
    shutil.copy(web / "backup.html", docs / "backup" / "index.html")
    shutil.copy(web / "zp12link.js", docs / "backup" / "zp12link.js")
    (docs / "editor").mkdir(exist_ok=True)
    shutil.copy(web / "editor.html", docs / "editor" / "index.html")
    if (docs / "install" / "favicon.svg").exists():
        (docs / "install" / "favicon.svg").unlink()
    shutil.copytree(web / "img", docs / "img", dirs_exist_ok=True)
    if video:
        shutil.copy(video, docs / "zp12-beat.mp4")
    import hashlib
    vh = hashlib.sha256((docs / "zp12-beat.mp4").read_bytes()).hexdigest()[:8] if (docs / "zp12-beat.mp4").exists() else version
    page = (web / "landing.html").read_text(encoding="utf-8").replace("zp12-beat.mp4?v=/*VER*/", "zp12-beat.mp4?v=" + vh)
    cs = (web / "cheatsheet.html").read_text(encoding="utf-8")
    style = cs[cs.index('<style id="cs-style">'):cs.index("</style>", cs.index('<style id="cs-style">')) + 8]
    sheet = cs[cs.index("<!--cs-->"):cs.index("<!--/cs-->") + 10]
    page = page.replace("</head>", style + "\n</head>", 1).replace("<!--CHEATSHEET-->", sheet, 1)
    (docs / "index.html").write_text(page.replace("/*VER*/", version), encoding="utf-8")   # (the video by its hash: no stale cache)
    snip = Path(sloopdx) / "tools" / "matomo_snippet.py"                                  # (the Matomo count, zp12's site ID)
    if snip.exists():
        subprocess.run([sys.executable, str(snip), *map(str, docs.rglob("*.html"))], check=True, capture_output=True)
    print(f"site: {docs} (start page, install/ {version}, cheat sheet, impressum, img/, video)")


if __name__ == "__main__":
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    main(*sys.argv[1:5])
