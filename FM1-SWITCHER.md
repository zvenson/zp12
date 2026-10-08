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

## Messages to the authors (post as a GitHub Discussion, or an issue if Discussions are off)

**Felucca** — github.com/hugelton/Felucca · **SLOOP** — github.com/isod89/sloop-fm1 ·
**X0X / FoMni** — github.com/charlesvestal/fm1-x0x (one message for both)

> **Title:** Make Firmware, Not War: your firmware on the FM-1 Firmware Hub
>
> Hi! I made a small page where FM-1 owners can try every open-source firmware with one click and go back just as easily,
> no need to pick a side: https://fm1.designburgapps.com
>
> [Felucca / SLOOP / X0X and FoMni] is on it: your latest release, mirrored unchanged (same SHA-256 as on GitHub),
> with your name, a short description, and links to your repo, the release and the source (a copy of the tagged
> source is kept next to it, as GPL-3.0 asks). It installs through Felucca's own update path, the same one your
> releases use. The page checks the package before writing and offers the USB rescue if anything goes wrong.
>
> A script picks up your new releases, so the page follows you. If you would rather not be listed, or want the
> description worded differently, a screenshot, a link to your docs, or anything else, just say so here or mail
> sven@designburg.net and I will change it at once.
>
> Thanks for [Felucca: the foundation all of this stands on / SLOOP: the workflow sloopDX and zp12 are built on /
> X0X and FoMni: two great ideas for the FM-1]!
>
> zvenson (sloopDX, zp12)
