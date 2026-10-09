#!/usr/bin/env python3
"""Cloudflare in front of the two static sites (zp12.designburgapps.com, dx7.designburgapps.com): the Pi's home
line is the bottleneck, so Cloudflare keeps the files at its edge.

  tools/cf_cache.py rule     the cache rule: the hosts eligible for cache, 2 h at the edge for pages, 30 days for
                             files (pictures, videos, packages: a deploy purges what changed; the browser asks
                             the edge again after 10 min)
  tools/cf_cache.py purge    after a deploy: the files that changed since the last purge out of the edge (their
                             hashes in ~/.cache/designburg/cf-purged.json; every edge refills from the home line)
  tools/cf_cache.py purge-url URL...   just these (fm1_sync.py: a changed catalogue)
  tools/cf_cache.py check    what the edge says for a few of their files (HIT, MISS, ...)

The API token (Zone: Cache Rules Edit, Cache Purge, Zone Read; designburgapps.com only) is read from
$CF_TOKEN_FILE or ~/.config/designburg/cf-token and never printed. Without Zone Read: the zone's id (not a secret)
in ~/.config/designburg/cf-zone."""
import hashlib, json, os, sys, urllib.error, urllib.request
from pathlib import Path

ZONE = "designburgapps.com"
HOSTS = ["zp12.designburgapps.com", "dx7.designburgapps.com", "fm1.designburgapps.com", "designburgapps.com"]   # (our static sites; the apps and the WordPress sites on the zone keep Cloudflare's default: files yes, pages no)
RULE_REF = "zp12-dx7-static"
EDGE_TTL, BROWSER_TTL = 7200, 600
FILE_TTL = 30 * 86400                                 # (not a page: a picture, a video, a package; purged when it changes)
PAGES = ("html", "json", "txt", "xml", "webmanifest")
SEEN = Path.home() / ".cache/designburg/cf-purged.json"
API = "https://api.cloudflare.com/client/v4"
HERE = Path(__file__).resolve().parents[1]
DOCS = {"zp12.designburgapps.com": HERE / "docs", "fm1.designburgapps.com": HERE / "fm1",           # (dx7: sloopDX's docs/, beside this repo: "repo" on the Pi)
        "dx7.designburgapps.com": next((p for p in (HERE.parent / "repo" / "docs", HERE.parent / "sloopdx" / "docs") if p.is_dir()), None),
        "designburgapps.com": next((p for p in (HERE.parent / "repo" / "deploy" / "designburgapps",       # (sloopDX's deploy/: the entry page)
                                                HERE.parent / "sloopdx" / "deploy" / "designburgapps") if p.is_dir()), None)}


def token():
    p = Path(os.environ.get("CF_TOKEN_FILE", Path.home() / ".config/designburg/cf-token"))
    if not p.exists():
        sys.exit(f"cf_cache: no token in {p}")
    return p.read_text().strip()


def call(method, path, body=None, missing_ok=False):
    req = urllib.request.Request(API + path, method=method, data=json.dumps(body).encode() if body is not None else None,
                                 headers={"Authorization": "Bearer " + token(), "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return json.load(r)
    except urllib.error.HTTPError as e:
        if missing_ok and e.code == 404:
            return {"success": False, "result": {}}
        out = json.load(e)
        sys.exit(f"cf_cache: {method} {path}: {e.code} {[x.get('message') for x in out.get('errors', [])]}")


def zone_id():
    zf = Path(os.environ.get("CF_ZONE_FILE", Path.home() / ".config/designburg/cf-zone"))   # (a token without Zone Read)
    if zf.exists() and zf.read_text().strip():
        return zf.read_text().strip()
    r = call("GET", f"/zones?name={ZONE}")
    if not r["result"]:
        sys.exit(f"cf_cache: zone {ZONE} not visible to the token")
    return r["result"][0]["id"]


def rule():
    z = zone_id()
    expr = "(http.host in {" + " ".join(f'"{h}"' for h in HOSTS) + "})"
    mine = {"ref": RULE_REF, "description": "zp12 + dx7: static, cached at the edge (tools/cf_cache.py)",
            "expression": expr, "action": "set_cache_settings",
            "action_parameters": {"cache": True, "edge_ttl": {"mode": "override_origin", "default": EDGE_TTL},
                                  "browser_ttl": {"mode": "override_origin", "default": BROWSER_TTL}}}
    files = {"ref": RULE_REF + "-files", "description": "zp12 + dx7 + fm1: pictures, videos, packages, 30 days at the edge",
             "expression": expr[:-1] + " and http.request.uri.path.extension ne \"\" and not http.request.uri.path.extension in {"
                           + " ".join(f'"{e}"' for e in PAGES) + "})",
             "action": "set_cache_settings",
             "action_parameters": {"cache": True, "edge_ttl": {"mode": "override_origin", "default": FILE_TTL},
                                   "browser_ttl": {"mode": "override_origin", "default": BROWSER_TTL}}}
    r = call("GET", f"/zones/{z}/rulesets/phases/http_request_cache_settings/entrypoint", missing_ok=True)   # (none yet: 404)
    rules = [x for x in r.get("result", {}).get("rules", []) if x.get("ref") not in (RULE_REF, files["ref"])] if r.get("success") else []
    keep = [{k: x[k] for k in ("ref", "description", "expression", "action", "action_parameters", "enabled") if k in x} for x in rules]
    call("PUT", f"/zones/{z}/rulesets/phases/http_request_cache_settings/entrypoint", {"rules": keep + [mine, files]})   # (the later wins)
    print(f"cf_cache: rule {RULE_REF} set for {', '.join(HOSTS)} (edge {EDGE_TTL} s, browser {BROWSER_TTL} s); {len(keep)} other rule(s) kept")


def urls():
    """every file of the sites (its URL, directory index too) with its hash, and dx7's pages without a checkout"""
    out = {}
    for host, root in DOCS.items():
        if root is None:
            continue
        for p in sorted(root.rglob("*")):
            if p.is_file() and not p.name.startswith("."):
                rel = p.relative_to(root).as_posix()
                h = hashlib.sha1(p.read_bytes()).hexdigest()
                out[f"https://{host}/{rel}"] = h
                if p.name == "index.html":
                    out[f"https://{host}/{rel[:-len('index.html')]}"] = h
    if DOCS["dx7.designburgapps.com"] is None:            # (no sloopDX checkout here: its pages that change)
        out |= {f"https://dx7.designburgapps.com/{p}": None for p in ("", "index.html", "webapp/installer/", "webapp/editor/",
                                                                      "cheatsheet.html", "impressum.html")}
    return out


def purge():
    """only what changed: a purge empties every edge, and each one fetches it again from the Pi"""
    z, now = zone_id(), urls()
    try:
        seen = json.loads(SEEN.read_text())
    except (OSError, ValueError):
        seen = {}
    u = [k for k, h in now.items() if h is None or seen.get(k) != h]
    for i in range(0, len(u), 30):                    # (30 URLs a call on the free plan)
        call("POST", f"/zones/{z}/purge_cache", {"files": u[i:i + 30]})
    SEEN.parent.mkdir(parents=True, exist_ok=True)
    SEEN.write_text(json.dumps({k: h for k, h in now.items() if h}))
    print(f"cf_cache: {len(u)} of {len(now)} URLs purged (changed)")


def purge_url():
    call("POST", f"/zones/{zone_id()}/purge_cache", {"files": sys.argv[2:32]})
    print(f"cf_cache: {len(sys.argv[2:32])} URL(s) purged")


def check():
    for u in ("https://zp12.designburgapps.com/", "https://zp12.designburgapps.com/zp12-beat.mp4",
              "https://zp12.designburgapps.com/editor/", "https://dx7.designburgapps.com/"):
        for _ in range(2):                            # (the first may be the MISS that fills the edge)
            with urllib.request.urlopen(urllib.request.Request(u, method="HEAD"), timeout=30) as r:
                st = r.headers.get("cf-cache-status")
        print(f"{st:10} {u}")


if __name__ == "__main__":
    {"rule": rule, "purge": purge, "purge-url": purge_url, "check": check}.get(sys.argv[1] if len(sys.argv) > 1 else "", lambda: sys.exit(__doc__))()
