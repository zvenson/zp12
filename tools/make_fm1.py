#!/usr/bin/env python3
"""The FM-1 firmware switcher page (thefirmwarehub.com): web/fm1.html with sloopDX's tested update path
(web/fm1pkg.js, web/fm1ota.js) inlined, the Matomo count (sloopDX's tools/matomo_snippet.py), into fm1/index.html.
The catalogue and the packages: tools/fm1_sync.py.
  tools/make_fm1.py [sloopdx repo]"""
import html, json, subprocess, sys
from datetime import date
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]


def smk_stars(repos):
    """the repos' GitHub stars, fetched once a day (web/smk37-stars.json keeps them: a build without the network or over
    the API's limit takes the last counts)"""
    import urllib.request
    from datetime import date as _d
    f = HERE / "web" / "smk37-stars.json"
    was = json.loads(f.read_text(encoding="utf-8")) if f.exists() else {}
    if was.get("day") == str(_d.today()) and all(r in was.get("stars", {}) for r in repos):
        return was["stars"]
    stars = dict(was.get("stars", {}))
    for r in repos:
        try:
            req = urllib.request.Request(f"https://api.github.com/repos/{r}", headers={"User-Agent": "fm1-switcher (thefirmwarehub.com)"})
            stars[r] = json.loads(urllib.request.urlopen(req, timeout=20).read()).get("stargazers_count")
        except Exception:
            pass
    f.write_text(json.dumps({"day": str(_d.today()), "stars": stars}, indent=1) + "\n", encoding="utf-8")
    return stars


def smk_cards():
    """the SMK-37 page's cards from web/smk37.json: (the firmwares made for it, the FM-1 ports), static for the search engines"""
    d = json.loads((HERE / "web" / "smk37.json").read_text(encoding="utf-8"))
    sys.path.insert(0, str(HERE / "tools"))
    import fm1_sync                                     # (a port's kinds: its FM-1 entry's, as the FM-1 page shows them)
    kinds = {f["id"]: [f["kind"]] if isinstance(f["kind"], str) else f["kind"] for f in fm1_sync.FIRMWARES}
    label = {"synth": "synth", "groove": "groovebox", "drums": "drums · sampler", "chords": "chords", "dev": "for developers", "other": "other"}
    e = lambda x: html.escape(str(x), quote=True)
    out = lambda u, t, c="": f'<a{c} href="{e(u)}" target="_blank" rel="noopener">{t}</a>'
    home = lambda f: f["orig_repo"] if "orig_repo" in f else f["source"].split("github.com/")[1].split("/tree/")[0].split("/blob/")[0]
    stars = smk_stars(sorted({home(f) for f in d["own"] + d["ports"]}))
    def card(f, port):
        kind = kinds[f["fm1"]] if port else f["kind"]
        tags = ('<span class="tag beta">beta</span>' if f.get("beta") else "") + ('<span class="tag port">FM-1 port</span>' if port else "") + \
            "".join(f'<span class="tag kind">{label[k]}</span>' for k in kind) + f'<span class="tag model">{e(f["models"])}</span>'
        by = (f'{e(f["orig"])} by {out("https://github.com/" + f["orig_repo"], e(f["by"]))}, SMK-37 build by '
              f'{out("https://github.com/genkigenki", "genkigenki")}' if port else f'by {e(f["by"])}')
        links = out(f["install"], f.get("install_label", "Install on its installer ↗"), ' class="go"')
        if port:
            links += f'<a href="/#fw-{e(f["fm1"])}">On the FM-1 hub</a>'
        links += "".join(f'<a href="{e(u)}">{e(t)}</a>' if u.startswith("/") else out(u, f"{e(t)} ↗") for t, u in f.get("links", []))
        return (f'<article class="fw" id="fw-{e(f["id"])}" data-kind="{" ".join(kind)}"><div class="shot{" wide" if f.get("wide") else ""}"><img src="{e(f["img"])}" '
                f'alt="{e(f["name"])} on the SMK-37" loading="lazy"></div>\n  <h3>{e(f["name"])}<small>{e(f["version"])}</small>'
                f'<a class="star" href="https://github.com/{e(home(f))}" target="_blank" rel="noopener" title="Star it on GitHub">'
                f'★ Star{"" if stars.get(home(f)) is None else " · " + str(stars[home(f)])}</a></h3>'
                f'<div class="by">{by}</div><div class="tags">{tags}</div>\n  <p class="what">{e(f["what"])}</p>'
                + (f'<div class="note">{e(f["note"])}</div>' if f.get("note") else "")
                + f'<div class="links">{links}</div><div class="meta">GPL-3.0 · {out(f["source"], "source")}</div></article>\n')
    return "".join(card(f, False) for f in d["own"]), "".join(card(f, True) for f in d["ports"]), str(len(d["own"]) + len(d["ports"]))


def credits():
    """the footer's credits, from the catalogues: ours (zvenson's), the FM-1's others by author, the SMK-37's"""
    sys.path.insert(0, str(HERE / "tools"))
    import fm1_sync
    d = json.loads((HERE / "web" / "smk37.json").read_text(encoding="utf-8"))
    names = lambda l: l[0] if len(l) == 1 else ", ".join(l[:-1]) + " and " + l[-1]
    ours = [f["name"] for f in fm1_sync.FIRMWARES if f["author"] == "zvenson"]
    by = {}
    for f in fm1_sync.FIRMWARES:
        if f["author"] != "zvenson":
            by.setdefault(f["author"], []).append(f["name"])
    smk = {}
    for f in d["own"]:
        if f["by"] != "zvenson":
            smk.setdefault(f["by"], []).append(f["name"])
    smk.setdefault("genkigenki", []).append("the SMK-37 builds of the FM-1 firmwares")
    e = lambda x: html.escape(x, quote=False)
    return (e(names(ours)), "; ".join(f"{e(names(n))} by {e(a)}" for a, n in by.items()),
            "; ".join(f"{e(names(n))} by {e(a)}" for a, n in smk.items()))


def devices(text, dev):
    """the devices menu (web/devices.css, web/devices.html) into a page: /*DEVICES*/ in its style, <!--DEVICES--> in its nav;
    the page's own device marked"""
    menu = (HERE / "web" / "devices.html").read_text(encoding="utf-8").replace(f'data-dev="{dev}"', f'data-dev="{dev}" aria-current="page"')
    own, ports, n = smk_cards()
    ours, fm1, smk = credits()
    foot = ((HERE / "web" / "footer.html").read_text(encoding="utf-8").replace("<!--F-OURS-->", ours)
            .replace("<!--F-FM1-->", fm1).replace("<!--F-SMK-->", smk))
    text = text.replace("/*FOOTER*/\n", (HERE / "web" / "footer.css").read_text(encoding="utf-8")).replace("<!--FOOTER-->\n", foot)
    return (text.replace("/*DEVICES*/\n", (HERE / "web" / "devices.css").read_text(encoding="utf-8"))
                .replace("<!--DEVICES-->\n", menu).replace("<!--SMK-OWN-->", own).replace("<!--SMK-PORTS-->", ports)
                .replace("<!--SMK-N-->", n))


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
    imp = HERE / "fm1" / "impressum"                    # the legal notice and privacy policy, the hub's own
    imp.mkdir(exist_ok=True)
    (imp / "index.html").write_text(devices((HERE / "web" / "hub-impressum.html").read_text(encoding="utf-8"), ""), encoding="utf-8")
    for p in (HERE / "firmwarehub" / "img").glob("*.png"):    # (smk-*: Felucca SMK's screens, makkuro37: its home screen)
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
        for page in (out, kb / "index.html", smk / "index.html", imp / "index.html"):
            subprocess.run([sys.executable, str(snip), str(page)], check=True, capture_output=True)
    (HERE / "fm1" / "rescue").mkdir(exist_ok=True)       # the rescue script, served next to the page (#rescue)
    (HERE / "fm1" / "rescue" / "fm1_rescue.py").write_text((HERE / "tools" / "fm1_rescue.py").read_text(encoding="utf-8"), encoding="utf-8")
    print(f"make_fm1: {out}")


if __name__ == "__main__":
    main(*sys.argv[1:2])
