#!/usr/bin/env python3
"""The hub's app icons (fm1/app/: the web app's manifest, web/hubapp.js): "FW HUB" on the page's dark, its four colours
under it, and the QR code to the live hub (segno). Square and full bleed (Android and iOS round them themselves); the maskable one with its contents in the inner 80 %.
  tools/make_appicons.py"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parents[1]
FONTS = Path("/run/host/fonts/rsms-inter-fonts")
BG, FG, CYAN = "#151213", "#efe8e1", "#42f5f5"
URL = "https://thefirmwarehub.com/"
STRIP = ["#42f5f5", "#afd9f4", "#f4c0cb", "#e6d1b9"]


def icon(size, inner):
    """size px, the contents inside inner (0..1) of it"""
    s = 4                                                  # (drawn at 4x, scaled down: smooth edges)
    W = size * s
    im = Image.new("RGB", (W, W), BG)
    d = ImageDraw.Draw(im)
    box = W * inner
    big = ImageFont.truetype(str(FONTS / "InterDisplay-Black.ttf"), int(box * .46))
    small = ImageFont.truetype(str(FONTS / "InterDisplay-Bold.ttf"), int(box * .17))
    cx = W / 2
    top = (W - box) / 2
    d.text((cx, top + box * .47), "FW", font=big, fill=CYAN, anchor="ms")
    d.text((cx, top + box * .71), "HUB", font=small, fill=FG, anchor="ms")
    sw, sh = box * .62, box * .05                          # (the page's strip: cyan, blue, pink, beige)
    x0, y0 = cx - sw / 2, top + box * .81
    for i, c in enumerate(STRIP):
        d.rectangle([x0 + sw / 4 * i, y0, x0 + sw / 4 * (i + 1), y0 + sh], fill=c)
    return im.resize((size, size), Image.LANCZOS)


def main():
    out = HERE / "fm1" / "app"
    out.mkdir(parents=True, exist_ok=True)
    icon(192, .92).save(out / "icon-192.png", optimize=True)
    icon(512, .92).save(out / "icon-512.png", optimize=True)
    icon(512, .72).save(out / "icon-maskable-512.png", optimize=True)
    icon(180, .86).save(out / "apple-touch-icon.png", optimize=True)
    try:                                                   # (the code a computer shows to scan with the phone: the live hub's)
        import segno
        segno.make(f"{URL}?app=1", error="m").save(str(out / "qr.svg"), scale=4, border=2, dark=BG, light=FG, xmldecl=False)
    except ImportError:
        print("make_appicons: no segno (pip install segno), qr.svg kept")
    print(f"make_appicons: {out}")


if __name__ == "__main__":
    main()
