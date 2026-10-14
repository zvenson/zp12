#!/usr/bin/env python3
"""The FM-1 firmware switcher page (thefirmwarehub.com): web/fm1.html with sloopDX's tested update path
(web/fm1pkg.js, web/fm1ota.js) inlined, the Matomo count (sloopDX's tools/matomo_snippet.py), into fm1/index.html.
The catalogue and the packages: tools/fm1_sync.py.
  tools/make_fm1.py [sloopdx repo]"""
import subprocess, sys
from datetime import date
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]


def devices(text, dev):
    """the devices menu (web/devices.css, web/devices.html) into a page: /*DEVICES*/ in its style, <!--DEVICES--> in its nav;
    the page's own device marked"""
    menu = (HERE / "web" / "devices.html").read_text(encoding="utf-8").replace(f'data-dev="{dev}"', f'data-dev="{dev}" aria-current="page"')
    return (text.replace("/*DEVICES*/\n", (HERE / "web" / "devices.css").read_text(encoding="utf-8"))
                .replace("<!--DEVICES-->\n", menu))


def main(sloopdx=str(HERE.parent / "sloopdx")):
    web = Path(sloopdx) / "web"
    sys.path.insert(0, str(web))
    import make_site                                    # sloopDX's: strip_module
    lib = make_site.strip_module((web / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        make_site.strip_module((web / "fm1ota.js").read_text(encoding="utf-8"))
    out = HERE / "fm1" / "index.html"
    out.write_text(devices((HERE / "web" / "fm1.html").read_text(encoding="utf-8").replace("/*LIB*/", lib), "fm1"), encoding="utf-8")
    (HERE / "fm1" / "robots.txt").write_text("User-agent: *\nAllow: /\nDisallow: /fw/\nDisallow: /src/\n"
                                            "Sitemap: https://thefirmwarehub.com/sitemap.xml\n")
    (HERE / "fm1" / "sitemap.xml").write_text('<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n'
                                             f'<url><loc>https://thefirmwarehub.com/</loc><lastmod>{date.today()}</lastmod>'
                                             '<changefreq>daily</changefreq><priority>1.0</priority></url>\n'
                                             f'<url><loc>https://thefirmwarehub.com/kb/</loc><lastmod>{date.today()}</lastmod>'
                                             '<changefreq>weekly</changefreq><priority>0.7</priority></url>\n'
                                             f'<url><loc>https://thefirmwarehub.com/vibe/</loc><lastmod>{date.today()}</lastmod>'
                                             '<changefreq>weekly</changefreq><priority>0.7</priority></url>\n'
                                             f'<url><loc>https://thefirmwarehub.com/smk37/</loc><lastmod>{date.today()}</lastmod>'
                                             '<changefreq>weekly</changefreq><priority>0.7</priority></url>\n</urlset>\n')
    kb = HERE / "fm1" / "kb"                            # the knowledge base: no menu link yet (the user's wish, 2026-10-13)
    kb.mkdir(exist_ok=True)
    (kb / "index.html").write_text(devices((HERE / "web" / "fm1kb.html").read_text(encoding="utf-8"), "fm1"), encoding="utf-8")
    smk = HERE / "fm1" / "smk37"                        # the SMK-37 Pro's hub: its firmwares (one so far), installed from their own pages
    (smk / "img").mkdir(parents=True, exist_ok=True)
    (smk / "index.html").write_text(devices((HERE / "web" / "smk37.html").read_text(encoding="utf-8"), "smk37"), encoding="utf-8")
    for p in (HERE / "firmwarehub" / "img").glob("smk-*.png"):
        (smk / "img" / p.name).write_bytes(p.read_bytes())
    hub = HERE / "firmwarehub"                          # thefirmwarehub.com: the start page, a card per device
    hub.mkdir(exist_ok=True)
    (hub / "index.html").write_text((HERE / "web" / "firmwarehub.html").read_text(encoding="utf-8"), encoding="utf-8")
    (hub / "robots.txt").write_text("User-agent: *\nAllow: /\nSitemap: https://thefirmwarehub.com/sitemap.xml\n")
    (hub / "sitemap.xml").write_text('<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n'
                                     f'<url><loc>https://thefirmwarehub.com/</loc><lastmod>{date.today()}</lastmod>'
                                     '<changefreq>weekly</changefreq><priority>1.0</priority></url>\n</urlset>\n')
    snip = Path(sloopdx) / "tools" / "matomo_snippet.py"
    if snip.exists():
        for page in (out, kb / "index.html", smk / "index.html"):
            subprocess.run([sys.executable, str(snip), str(page)], check=True, capture_output=True)
    (HERE / "fm1" / "rescue").mkdir(exist_ok=True)       # the rescue script, served next to the page (#rescue)
    (HERE / "fm1" / "rescue" / "fm1_rescue.py").write_text((HERE / "tools" / "fm1_rescue.py").read_text(encoding="utf-8"), encoding="utf-8")
    print(f"make_fm1: {out}")


if __name__ == "__main__":
    main(*sys.argv[1:2])
