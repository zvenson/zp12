#!/usr/bin/env python3
"""Cloudflare in front of the two static sites (zp12.designburgapps.com, dx7.designburgapps.com): the Pi's home
line is the bottleneck, so Cloudflare keeps the files at its edge.

  tools/cf_cache.py rule     the cache rule: the two hosts eligible for cache, 2 h at the edge (HTML too; the
                             browser asks the edge again after 10 min)
  tools/cf_cache.py purge    after a deploy: the two sites' files out of the edge (by URL: only these sites)
  tools/cf_cache.py check    what the edge says for a few of their files (HIT, MISS, ...)

The API token (Zone: Cache Rules Edit, Cache Purge, Zone Read; designburgapps.com only) is read from
$CF_TOKEN_FILE or ~/.config/designburg/cf-token and never printed. Without Zone Read: the zone's id (not a secret)
in ~/.config/designburg/cf-zone."""
import json, os, sys, urllib.request
from pathlib import Path

ZONE = "designburgapps.com"
HOSTS = ["zp12.designburgapps.com", "dx7.designburgapps.com"]
RULE_REF = "zp12-dx7-static"
EDGE_TTL, BROWSER_TTL = 7200, 600
API = "https://api.cloudflare.com/client/v4"
DOCS = {"zp12.designburgapps.com": Path(__file__).resolve().parents[1] / "docs"}


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
    r = call("GET", f"/zones/{z}/rulesets/phases/http_request_cache_settings/entrypoint", missing_ok=True)   # (none yet: 404)
    rules = [x for x in r.get("result", {}).get("rules", []) if x.get("ref") != RULE_REF] if r.get("success") else []
    keep = [{k: x[k] for k in ("ref", "description", "expression", "action", "action_parameters", "enabled") if k in x} for x in rules]
    call("PUT", f"/zones/{z}/rulesets/phases/http_request_cache_settings/entrypoint", {"rules": keep + [mine]})
    print(f"cf_cache: rule {RULE_REF} set for {', '.join(HOSTS)} (edge {EDGE_TTL} s, browser {BROWSER_TTL} s); {len(keep)} other rule(s) kept")


def urls():
    """every file of zp12's docs/ (its URL, directory index too), and dx7's pages that change on a deploy"""
    out = []
    for host, root in DOCS.items():
        for p in sorted(root.rglob("*")):
            if p.is_file() and not p.name.startswith("."):
                rel = p.relative_to(root).as_posix()
                out.append(f"https://{host}/{rel}")
                if p.name == "index.html":
                    out.append(f"https://{host}/{rel[:-len('index.html')]}")
    out += [f"https://dx7.designburgapps.com/{p}" for p in ("", "index.html", "webapp/installer/", "webapp/editor/",
                                                            "cheatsheet.html", "impressum.html")]
    return out


def purge():
    z, u = zone_id(), urls()
    for i in range(0, len(u), 30):                    # (30 URLs a call on the free plan)
        call("POST", f"/zones/{z}/purge_cache", {"files": u[i:i + 30]})
    print(f"cf_cache: {len(u)} URLs purged")


def check():
    for u in ("https://zp12.designburgapps.com/", "https://zp12.designburgapps.com/zp12-beat.mp4",
              "https://zp12.designburgapps.com/editor/", "https://dx7.designburgapps.com/"):
        for _ in range(2):                            # (the first may be the MISS that fills the edge)
            with urllib.request.urlopen(urllib.request.Request(u, method="HEAD"), timeout=30) as r:
                st = r.headers.get("cf-cache-status")
        print(f"{st:10} {u}")


if __name__ == "__main__":
    {"rule": rule, "purge": purge, "check": check}.get(sys.argv[1] if len(sys.argv) > 1 else "", lambda: sys.exit(__doc__))()
