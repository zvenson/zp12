#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""zp12's site (docs/, served as zp12.designburgapps.com):
  index.html            web/landing.html (/*VER*/ the version)
  install/              the installer (tools/make_installer.py: sloopDX's tested update path) and the package
  cheatsheet.html, zp12-cheat-sheet.pdf, impressum.html, favicon.svg, img/, zp12-beat.mp4
Usage: make_site.py <sloopdx repo> build/zp12-X.Y.fwsc X.Y [video.mp4]"""
import shutil, sys
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
    for a, b in (('href="favicon.svg"', 'href="../favicon.svg"'), ('href="cheatsheet.html"', 'href="../cheatsheet.html"'),
                 ('href="zp12-cheat-sheet.pdf"', 'href="../zp12-cheat-sheet.pdf"'),
                 ('href="https://dx7.designburgapps.com/impressum.html"', 'href="../impressum.html"'),
                 ('<div class="links"><a href="https://dx7.designburgapps.com/">sloopDX</a>', '<div class="links"><a href="../">zp12</a><a href="https://dx7.designburgapps.com/">sloopDX</a>')):
        h = h.replace(a, b)
    (inst / "index.html").write_text(h, encoding="utf-8")
    for f in ("cheatsheet.html", "zp12-cheat-sheet.pdf", "favicon.svg", "zp12-beat.mp4"):
        if (inst / f).exists():
            shutil.move(str(inst / f), str(docs / f)) if not (docs / f).exists() or f == "favicon.svg" else (inst / f).unlink()
    for f in ("cheatsheet.html", "zp12-cheat-sheet.pdf", "impressum.html"):
        shutil.copy(web / f, docs / f)
    if (docs / "install" / "favicon.svg").exists():
        (docs / "install" / "favicon.svg").unlink()
    shutil.copytree(web / "img", docs / "img", dirs_exist_ok=True)
    if video:
        shutil.copy(video, docs / "zp12-beat.mp4")
    (docs / "index.html").write_text((web / "landing.html").read_text(encoding="utf-8").replace("/*VER*/", version), encoding="utf-8")
    print(f"site: {docs} (start page, install/ {version}, cheat sheet, impressum, img/, video)")


if __name__ == "__main__":
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    main(*sys.argv[1:5])
