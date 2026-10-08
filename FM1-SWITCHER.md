# The FM-1 Firmware Hub, "Make Firmware, Not War" (fm1.designburgapps.com)

A page that installs any of the FM-1's custom firmwares with one click: `fm1/` in this repo (built by
`tools/make_fm1.py` from `web/fm1.html`), its catalogue and packages by `tools/fm1_sync.py`. On the Pi, nginx serves
`zp12repo/fm1` as fm1.designburgapps.com and `~/docker/sloopdx-site/fm1src` (the source copies, outside git) as `/src/`.

## The catalogue: always the newest release

Every night at 4:00 the Pi's cron runs `tools/fm1_sync.py` into `~/docker/sloopdx-site/fm1live` (outside git; nginx
serves it as `/catalog.json` and `/fw/`), and the source copies into `fm1src`. GitHub does not let a browser download
release files (no CORS header), so the packages are passed through the Pi, checked first (FM-1 identity, the update
loader); a release that fails keeps the last good package. A changed catalogue is purged from Cloudflare. sloopDX and
zp12 come from the Pi's checkouts (`repo/docs/firmware`, `zp12repo/docs/install/firmware`): update.sh syncs after
its pull, so they are listed as soon as they are deployed.

    python3 tools/fm1_sync.py && python3 tools/make_fm1.py     # here: the page, with a local catalogue to look at
    git add web/fm1.html fm1 && git commit -m "Switcher: ..." && git push
    ssh pi-remote '~/docker/sloopdx-site/update.sh'             # pulls, syncs, purges Cloudflare

## Licence

All listed firmwares are GPL-3.0. Conveying their unchanged packages is allowed (§4, §6): the licence is linked, the
source is at each project's repository and tag and a copy of it is kept next to the package (`/src/`, so it stays
available even if a repository goes). The page does not claim the authors endorse it.

## Messages to the authors

Where: **Felucca** — a Discussion, github.com/hugelton/Felucca/discussions (or the mail on github.com/hugelton) ·
**SLOOP** — an issue, github.com/isod89/sloop-fm1/issues/new (no Discussions, no other contact) ·
**X0X / FoMni** — one issue in github.com/charlesvestal/fm1-x0x/issues/new, naming FoMni too (or charles.pizza).

> **Title:** Make Firmware, Not War: [Felucca / SLOOP / X0X and FoMni] on the FM-1 Firmware Hub
>
> Hi! I made a page where FM-1 owners can try every open-source firmware with one click and go back just as easily,
> no need to pick a side: https://fm1.designburgapps.com
>
> [Felucca / SLOOP / X0X and FoMni] is on it: your newest release, mirrored unchanged (same SHA-256 as your GitHub
> release), with your name, a short description, a screenshot from your repository, and links to your repo, the
> release and the source (a copy of the tagged source is kept next to it, as GPL-3.0 asks). The page checks your
> repository every night, so a new release is on it the next morning. It installs through Felucca's own update
> path, the same one your releases use, checks the package before writing, and points to the USB rescue.
>
> If you would rather not be listed, or want the description, the picture or anything else changed, just say so
> here or mail sven@designburg.net and I will change it at once. I tested the switch on my FM-1
> (zp12 -> Felucca -> zp12) and it works.
>
> Thanks for [Felucca: the foundation all of this stands on / SLOOP: the workflow sloopDX and zp12 are built on /
> X0X and FoMni: two great ideas for the FM-1]!
>
> zvenson (sloopDX, zp12)
