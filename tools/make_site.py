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


# the start page's menu, on the installer, the editor and the backup page too (its own scoped style: the installer
# is sloopDX's dark page); cur: the page it is on
NAV_CSS = """<style id="zp-nav">
  .zpnav { position: sticky; top: 0; z-index: 50; background: rgba(206, 207, 201, 0.95); backdrop-filter: blur(8px);
           border-bottom: 1px solid #b9bbb3; font: 16px/1.6 "Inter", -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Arial, sans-serif; }
  .zpnav .zw { max-width: 1320px; margin: 0 auto; padding: 0 32px; display: flex; align-items: center; height: 60px; gap: 22px; }
  .zpnav .zm { font: italic 900 30px/1 "Helvetica Neue", Arial, sans-serif; letter-spacing: -1px; color: #1e3060; text-decoration: none; white-space: nowrap; }
  .zpnav .zm small { font: italic 400 15px/1 "Helvetica Neue", Arial, sans-serif; letter-spacing: 0; margin-left: 8px; }
  .zpnav .zr { flex: 1; height: 6px; border-top: 2px solid #c82e32; border-bottom: 2px solid #1e3060; }
  .zpnav .zl { display: flex; gap: 20px; font-size: 14.5px; align-items: center; }
  .zpnav .zl a { color: #1e3060; text-decoration: none; }
  .zpnav .zl a:hover { text-decoration: underline; }
  .zpnav .zl a.cur { font-weight: 700; box-shadow: inset 0 -2px 0 #c82e32; }
  .zpnav .zl a.gh { background: #263e70; color: #fff; padding: 6px 14px; border-radius: 999px; font-weight: 650; }
  @media (max-width: 980px) { .zpnav .zl a:not(.gh):not(.in) { display: none; } .zpnav .zr { display: none; } .zpnav .zw { justify-content: space-between; } }
  @media (max-width: 620px) { .zpnav .zw { padding: 0 18px; } }
</style>"""


def nav(cur, extra=""):
    links = [("Video", "../#video"), ("Workflow", "../#workflow"), ("Sound", "../#sound"), ("Cheat sheet", "../#cheatsheet"),
             ("Samples", "../editor/"), ("Backup", "../backup/")]
    a = "".join(f'<a href="{h}"{" class=\"cur\"" if n == cur else ""}>{n}</a>' for n, h in links)
    inst = f'<a class="in{" cur" if cur == "Install" else ""}" href="../install/"><b>Install</b></a>'
    return (f'<nav class="zpnav"><div class="zw">\n  <a class="zm" href="../">zp12<small>sampling drums</small></a>\n  <span class="zr"></span>\n'
            f'  <div class="zl">{a}\n    {inst}<a class="gh" href="https://github.com/zvenson/zp12">GitHub</a></div>{extra}\n</div></nav>')


def put_nav(html, cur, old=r"<nav\b.*?</nav>", extra=""):
    import re
    html, n = re.subn(old, lambda m: nav(cur, extra), html, count=1, flags=re.S)
    if not n:
        raise SystemExit(f"make_site: no <nav> for {cur}")
    return html.replace("</head>", NAV_CSS + "\n</head>", 1)


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
    # SLOOP keeps its own samples where zp12 saves (0xC4000..0xDBFFF): say so before the install
    h = h.replace("Back to sloopDX any time with its installer.</p>",
                  "Back to sloopDX any time with its installer.</p>\n    <p class=\"small\"><b>Coming from SLOOP with your own samples?</b> "
                  "zp12 saves where SLOOP keeps them: back them up first. zp12 writes only to the flash areas meant for data, "
                  "never to the app, the Bluetooth data or the MAC; sloopDX's projects, banks and kit stay where they are.</p>", 1)
    h = put_nav(h, "Install", extra='\n  <button class="lang" id="lang" hidden></button>')   # (the installer's script wants #lang)
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
    (docs / "backup" / "index.html").write_text(put_nav((web / "backup.html").read_text(encoding="utf-8"), "Backup"), encoding="utf-8")
    shutil.copy(web / "zp12link.js", docs / "backup" / "zp12link.js")
    (docs / "editor").mkdir(exist_ok=True)
    (docs / "editor" / "index.html").write_text(put_nav((web / "editor.html").read_text(encoding="utf-8"), "Samples"), encoding="utf-8")
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
