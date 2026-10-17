#!/usr/bin/env python3
"""The FM-1 firmware switcher page (thefirmwarehub.com): web/fm1.html with sloopDX's tested update path
(web/fm1pkg.js, web/fm1ota.js) inlined, the Matomo count (sloopDX's tools/matomo_snippet.py), into fm1/index.html.
The catalogue and the packages: tools/fm1_sync.py.
  tools/make_fm1.py [sloopdx repo]"""
import hashlib, html, json, re, subprocess, sys
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
    vintage = {f["id"] for f in fm1_sync.FIRMWARES if f.get("vintage")}   # (a classic instrument rebuilt: as on the FM-1 page)
    label = {"synth": "synth", "groove": "groovebox", "drums": "drums · sampler", "chords": "chords", "dev": "for developers", "other": "other"}
    e = lambda x: html.escape(str(x), quote=True)
    out = lambda u, t, c="": f'<a{c} href="{e(u)}" target="_blank" rel="noopener">{t}</a>'
    home = lambda f: f["orig_repo"] if "orig_repo" in f else f["source"].split("github.com/")[1].split("/tree/")[0].split("/blob/")[0]
    stars = smk_stars(sorted({home(f) for f in d["own"] + d["ports"]}))
    # each card in its firmware's colour, the FM-1 page's (its LOOK): a port its FM-1 original's, Felucca SMK Felucca's
    page = (HERE / "web" / "fm1.html").read_text(encoding="utf-8")
    look = dict(re.findall(r'(\w+): "(#[0-9a-f]{6})"', page[page.index("const LOOK"):page.index("};", page.index("const LOOK"))]))
    colour = lambda f: look.get(f.get("fm1") or {"felucca-smk": "felucca"}.get(f["id"], f["id"]))
    def card(f, port):
        kind = kinds[f["fm1"]] if port else f["kind"]
        old = f.get("fm1", f["id"]) in vintage
        tags = ('<span class="tag beta">beta</span>' if f.get("beta") else "") + ('<span class="tag port">FM-1 port</span>' if port else "") + \
            "".join(f'<span class="tag kind">{label[k]}</span>' for k in kind) + \
            ('<span class="tag vintage" title="Built after a classic instrument (the description says which)">vintage</span>' if old else "") + \
            f'<span class="tag model">{e(f["models"])}</span>'
        by = (f'{e(f["orig"])} by {out("https://github.com/" + f["orig_repo"], e(f["by"]))}, SMK-37 build by '
              f'{out("https://github.com/genkigenki", "genkigenki")}' if port else f'by {e(f["by"])}')
        links = out(f["install"], f.get("install_label", "Install on its installer ↗"), ' class="go"')
        if port:
            links += f'<a href="/#fw-{e(f["fm1"])}">On the FM-1 hub</a>'
        links += "".join(f'<a href="{e(u)}">{e(t)}</a>' if u.startswith("/") else out(u, f"{e(t)} ↗") for t, u in f.get("links", []))
        n = stars.get(home(f))                          # (data-stars, data-name: the page's Sort; .head: the list view's name column)
        c = colour(f)
        return (f'<article class="fw" id="fw-{e(f["id"])}"{f' style="--c: {c}"' if c else ""} data-kind="{" ".join(kind + ["vintage"] * old)}" data-stars="{-1 if n is None else n}" data-repo="{e(home(f))}" '
                f'data-hub="smk37:{e(f["id"])}" data-name="{e(f["name"])}"><div class="shot{" wide" if f.get("wide") else ""}"><img src="{e(f["img"])}" '
                f'alt="{e(f["name"])} on the SMK-37" loading="lazy"></div>\n  <div class="head"><h3>{e(f["name"])}<small>{e(f["version"])}</small>'
                f'</h3>'
                f'<div class="by">{by}</div><div class="tags">{tags}</div></div>\n  <p class="what">{e(f["what"])}</p>'
                + (f'<div class="note">{e(f["note"])}</div>' if f.get("note") else "")
                + f'<div class="links">{links}</div><div class="meta">GPL-3.0 · {out(f["source"], "source")}</div></article>\n')
    return "".join(card(f, False) for f in d["own"]), "".join(card(f, True) for f in d["ports"]), str(len(d["own"]) + len(d["ports"]))


def tools_cards():
    """the Tools page's sections from web/tools.json: a group per section, a card per tool, static for the search engines;
    M-VAVE's own (not open source) in a box apart. Returns (the jump links, the sections)"""
    d = json.loads((HERE / "web" / "tools.json").read_text(encoding="utf-8"))
    e = lambda x: html.escape(str(x), quote=True)
    def a(u, t, c=""):                                  # (our own pages in the same tab, the others' in a new one)
        return f'<a{c} href="{e(u)}">{t}</a>' if u.startswith("/") else f'<a{c} href="{e(u)}" target="_blank" rel="noopener">{t} ↗</a>'
    def card(t):
        by = " and ".join(a(u, e(n)).replace(" ↗", "") for n, u in t["by"])
        tags = "".join(f'<span class="tag {"smk" if d_ == "SMK-37" else "fm1"}">{e(d_)}</span>' for d_ in t["devices"])
        links = [(n, u) for n, u in t["links"]] + ([("Source", t["source"])] if t["source"] else [])
        if not t["links"] and t["source"]:
            links = [("Source on GitHub" if "github.com" in t["source"] else "Source", t["source"])]
        ls = "".join(a(u, e(n), ' class="go"' if i == 0 else "") for i, (n, u) in enumerate(links))
        return (f'<article class="tool" id="t-{e(t["id"])}"><h3>{e(t["name"])}</h3>\n  <div class="by">by {by}</div>'
                f'<div class="tags">{tags}</div>\n  <p class="what">{e(t["what"])}</p>'
                + (f'<p class="note">{e(t["note"])}</p>' if t.get("note") else "")
                + f'<div class="links">{ls}</div><div class="lic">Licence: {e(t["licence"])}</div></article>\n')
    jump = "".join(f'<a href="#{e(g["id"])}">{e(g["title"])}</a>' for g in d["groups"]) + '<a href="#official">Official</a>'
    out = "".join(f'<section class="grp" id="{e(g["id"])}"><h2>{e(g["title"])}</h2><p class="sub">{e(g["sub"])}</p>\n<div class="grid">\n'
                  + "".join(card(t) for t in g["tools"]) + '</div></section>\n' for g in d["groups"])
    out += ('<section class="official" id="official"><h2>Official, not open source</h2>'
            '<p class="sub">From M-VAVE itself, for comparison: not one of the open projects above.</p>\n'
            + card(d["official"]) + '</section>\n')
    return jump, out


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


def devices(text, dev, ref="hub"):
    """the devices menu (web/devices.css, web/devices.html) into a page: /*DEVICES*/ in its style, <!--DEVICES--> in its nav;
    the page's own device marked"""
    menu = (HERE / "web" / "devices.html").read_text(encoding="utf-8").replace(f'data-dev="{dev}"', f'data-dev="{dev}" aria-current="page"')
    if ref == "kb":                                     # (the Knowledge Base link beside Devices: marked on the KB itself)
        menu = menu.replace('<a class="kbl"', '<a class="kbl" aria-current="page"')
    own, ports, n = smk_cards()
    ours, fm1, smk = credits()
    foot = ((HERE / "web" / "footer.html").read_text(encoding="utf-8").replace("<!--F-OURS-->", ours)
            .replace("<!--F-FM1-->", fm1).replace("<!--F-SMK-->", smk))
    text = (text.replace("/*VIBEBAR*/\n", (HERE / "web" / "vibe-banner.css").read_text(encoding="utf-8"))   # (the VIBE line on top, ?ref= per page)
                .replace("<!--VIBEBAR-->\n", (HERE / "web" / "vibe-banner.html").read_text(encoding="utf-8").replace("<!--REF-->", ref)))
    text = text.replace("/*FOOTER*/\n", (HERE / "web" / "footer.css").read_text(encoding="utf-8")).replace("<!--FOOTER-->\n", foot)
    # (the favourites, ratings and the account: one code, one style for every device page, web/hubaccount.*)
    text = (text.replace("/*ACCOUNTCSS*/\n", (HERE / "web" / "hubaccount.css").read_text(encoding="utf-8"))
                .replace("/*ACCOUNT*/\n", (HERE / "web" / "hubaccount.js").read_text(encoding="utf-8")))
    # (the hub as an app: the manifest and icons in the head, the bar and the worker's registration at the end, web/hubapp.*;
    #  not VIBE's nav, a page of its own)
    if "</head>" in text:
        app = (HERE / "web" / "hubapp-head.html").read_text(encoding="utf-8") + \
            "<style>\n" + (HERE / "web" / "hubapp.css").read_text(encoding="utf-8") + "</style>\n"
        text = text.replace("</head>", app + "</head>", 1)
        text = text.replace("</body>", "<script>\n" + (HERE / "web" / "hubapp.js").read_text(encoding="utf-8") + "</script>\n</body>", 1)
    # (Cloudflare's email obfuscation off for our pages: the addresses stay plain text and links, also without JavaScript;
    #  the legal notice needs its address readable)
    text = text.replace("<body>\n", "<body>\n<!--email_off-->\n", 1).replace("</body>", "<!--/email_off-->\n</body>", 1)
    def fp(t):                                          # (/img/x.png and /smk37/img/x.png: ?v=<its fingerprint>)
        def one(mo):
            f = HERE / "fm1" / mo.group(1).lstrip("/")
            return mo.group(0) if not f.exists() else f'{mo.group(1)}?v={hashlib.sha256(f.read_bytes()).hexdigest()[:8]}"'
        return re.sub(r'(/(?:smk37/)?img/[\w.-]+\.png)"', one, t)
    text = text.replace('<nav><div class="wrap"><a class="mark"', '<nav class="top"><div class="wrap"><a class="mark"', 1)   # (the nav of the page: devices.css)
    return fp(text.replace("/*DEVICES*/\n", (HERE / "web" / "devices.css").read_text(encoding="utf-8"))
                .replace("<!--DEVICES-->\n", menu).replace("<!--SMK-OWN-->", own).replace("<!--SMK-PORTS-->", ports)
                .replace("<!--SMK-N-->", n))


VIBE_NAV = """<style>
/*DEVICES*/
  /* (VIBE's page: the hub's nav on its own, its base too; VIBE's links, fonts and .wrap kept out) */
  nav.top { z-index: 40; border-bottom: 1px solid var(--line); color: var(--fg);
            font: 16px/1.65 "Inter", -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Arial, sans-serif; }
  nav.top *, nav.top *::before, nav.top *::after { box-sizing: border-box; }
  nav.top a { text-decoration: none; }                  /* (each link has its colour in devices.css) */
  nav.top .wrap { margin: 0 auto; padding: 0 clamp(16px, 4vw, 72px); width: 100%; }
  nav.top > .wrap { display: flex; align-items: center; justify-content: space-between; min-height: 60px; column-gap: 16px; }
  @media (max-width: 760px) { nav.top .wrap { padding: 0 16px; } nav.top .home { display: none; } }   /* (as on the hub's pages) */
  nav.top .mark { font-size: 21px; font-weight: 300; color: var(--fg); letter-spacing: -.2px; }
  nav.top .mark b { font-weight: 800; color: var(--cyan); }
  nav.top .home { font-size: 14px; color: var(--dim); }
  @media (max-width: 400px) { nav.top .mark { font-size: 18px; } } @media (max-width: 350px) { nav.top .mark { font-size: 16px; } }
  .lang { top: 76px; } @media (max-width: 900px) { .lang { top: 112px; } }   /* (EN / DE under the nav) */
</style>
<nav><div class="wrap"><a class="mark" href="https://thefirmwarehub.com/">FM-1 <b>Firmware Hub</b></a>
<!--DEVICES-->
  <a class="home" href="https://designburg.net/">designburg.net</a></div></nav>
"""


def main(sloopdx=str(HERE.parent / "sloopdx")):
    web = Path(sloopdx) / "web"
    sys.path.insert(0, str(web))
    import make_site                                    # sloopDX's: strip_module
    lib = make_site.strip_module((web / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        make_site.strip_module((web / "fm1ota.js").read_text(encoding="utf-8"))
    out = HERE / "fm1" / "index.html"
    imgv = {p.stem: hashlib.sha256(p.read_bytes()).hexdigest()[:8] for p in sorted((HERE / "fm1" / "img").glob("*.png"))}
    out.write_text(devices((HERE / "web" / "fm1.html").read_text(encoding="utf-8").replace("/*LIB*/", lib)
                           .replace("/*IMGV*/{}", json.dumps(imgv, separators=(",", ":"))), "fm1"), encoding="utf-8")
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
                                             '<changefreq>weekly</changefreq><priority>0.7</priority></url>\n'
                                             f'<url><loc>https://thefirmwarehub.com/tools/</loc><lastmod>{date.today()}</lastmod>'
                                             '<changefreq>weekly</changefreq><priority>0.6</priority></url>\n</urlset>\n')
    kb = HERE / "fm1" / "kb"                            # the knowledge base: no menu link yet (the user's wish, 2026-10-13)
    kb.mkdir(exist_ok=True)
    (kb / "index.html").write_text(devices((HERE / "web" / "fm1kb.html").read_text(encoding="utf-8"), "fm1", "kb"), encoding="utf-8")
    tools = HERE / "fm1" / "tools"                      # the tools around the firmwares (web/tools.json), only linked
    tools.mkdir(exist_ok=True)
    jump, cards = tools_cards()
    (tools / "index.html").write_text(devices((HERE / "web" / "tools.html").read_text(encoding="utf-8")
                                              .replace("<!--TOOLS-JUMP-->", jump).replace("<!--TOOLS-->\n", cards), "tools", "tools"), encoding="utf-8")
    smk = HERE / "fm1" / "smk37"                        # the SMK-37 Pro's hub: its firmwares (one so far), installed from their own pages
    (smk / "img").mkdir(parents=True, exist_ok=True)
    (smk / "index.html").write_text(devices((HERE / "web" / "smk37.html").read_text(encoding="utf-8"), "smk37", "smk37"), encoding="utf-8")
    # the browser simulators, as their repos build them: OberVave's (web/sim/build.sh), zp12's (this repo: sim/build.sh)
    for name, sim in (("obervave", HERE.parent / "obervave" / "docs" / "sim"), ("zp12", HERE / "docs" / "sim")):
        if sim.exists():
            play = HERE / "fm1" / "play" / name
            play.mkdir(parents=True, exist_ok=True)
            for p in sim.iterdir():
                (play / p.name).write_bytes(p.read_bytes())
    imp = HERE / "fm1" / "impressum"                    # the legal notice and privacy policy, the hub's own
    imp.mkdir(exist_ok=True)
    (imp / "index.html").write_text(devices((HERE / "web" / "hub-impressum.html").read_text(encoding="utf-8"), "", "impressum"), encoding="utf-8")
    for p in (HERE / "firmwarehub" / "img").glob("*.png"):    # (smk-*: Felucca SMK's screens, makkuro37: its home screen)
        (smk / "img" / p.name).write_bytes(p.read_bytes())
    (HERE / "hubnav").mkdir(exist_ok=True)              # VIBE's page: the nav (its layout.php includes hubnav.html; copied there on deploy)
    (HERE / "hubnav" / "vibe.html").write_text(devices(VIBE_NAV, "vibe", "vibe"), encoding="utf-8")
    hub = HERE / "firmwarehub"                          # thefirmwarehub.com: the start page, a card per device
    hub.mkdir(exist_ok=True)
    (hub / "index.html").write_text((HERE / "web" / "firmwarehub.html").read_text(encoding="utf-8"), encoding="utf-8")
    (hub / "robots.txt").write_text("User-agent: *\nAllow: /\nSitemap: https://thefirmwarehub.com/sitemap.xml\n")
    (hub / "sitemap.xml").write_text('<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n'
                                     f'<url><loc>https://thefirmwarehub.com/</loc><lastmod>{date.today()}</lastmod>'
                                     '<changefreq>weekly</changefreq><priority>1.0</priority></url>\n</urlset>\n')
    snip = Path(sloopdx) / "tools" / "matomo_snippet.py"
    if snip.exists():
        plays = sorted((HERE / "fm1" / "play").glob("*/index.html"))     # (the simulators: counted as the hub's pages)
        for page in (out, kb / "index.html", smk / "index.html", tools / "index.html", imp / "index.html", *plays):
            subprocess.run([sys.executable, str(snip), str(page)], check=True, capture_output=True)
    (HERE / "fm1" / "rescue").mkdir(exist_ok=True)       # the rescue script, served next to the page (#rescue)
    (HERE / "fm1" / "rescue" / "fm1_rescue.py").write_text((HERE / "tools" / "fm1_rescue.py").read_text(encoding="utf-8"), encoding="utf-8")
    print(f"make_fm1: {out}")


if __name__ == "__main__":
    main(*sys.argv[1:2])
