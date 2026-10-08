#!/usr/bin/env python3
"""The FM-1 firmware switcher page (fm1.designburgapps.com): web/fm1.html with sloopDX's tested update path
(web/fm1pkg.js, web/fm1ota.js) inlined, the Matomo count (sloopDX's tools/matomo_snippet.py), into fm1/index.html.
The catalogue and the packages: tools/fm1_sync.py.
  tools/make_fm1.py [sloopdx repo]"""
import subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]


def main(sloopdx=str(HERE.parent / "sloopdx")):
    web = Path(sloopdx) / "web"
    sys.path.insert(0, str(web))
    import make_site                                    # sloopDX's: strip_module
    lib = make_site.strip_module((web / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        make_site.strip_module((web / "fm1ota.js").read_text(encoding="utf-8"))
    out = HERE / "fm1" / "index.html"
    out.write_text((HERE / "web" / "fm1.html").read_text(encoding="utf-8").replace("/*LIB*/", lib), encoding="utf-8")
    snip = Path(sloopdx) / "tools" / "matomo_snippet.py"
    if snip.exists():
        subprocess.run([sys.executable, str(snip), str(out)], check=True, capture_output=True)
    print(f"make_fm1: {out}")


if __name__ == "__main__":
    main(*sys.argv[1:2])
