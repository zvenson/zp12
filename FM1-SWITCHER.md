# The FM-1 firmware switcher (fm1.designburgapps.com)

A page that installs any of the FM-1's custom firmwares with one click: `fm1/` in this repo (built by
`tools/make_fm1.py` from `web/fm1.html`), its catalogue and packages by `tools/fm1_sync.py`. On the Pi, nginx serves
`zp12repo/fm1` as fm1.designburgapps.com and `~/docker/sloopdx-site/fm1src` (the source copies, outside git) as `/src/`.

## Updating the catalogue

    python3 tools/fm1_sync.py && python3 tools/make_fm1.py     # here: the newest packages, the page
    git add fm1 && git commit -m "Switcher: ..." && git push
    ssh pi-remote '~/docker/sloopdx-site/update.sh'             # pulls, then purges Cloudflare
    ssh pi-remote 'cd ~/docker/sloopdx-site/zp12repo && python3 tools/fm1_sync.py --sources ../fm1src'

## Licence

All listed firmwares are GPL-3.0. Conveying their unchanged packages is allowed (§4, §6): the licence is linked, the
source is at each project's repository and tag and a copy of it is kept next to the package (`/src/`, so it stays
available even if a repository goes). The page does not claim the authors endorse it.

## Messages to the authors (post as a GitHub Discussion, or an issue if Discussions are off)

**Felucca** — github.com/hugelton/Felucca · **SLOOP** — github.com/isod89/sloop-fm1 ·
**X0X / FoMni** — github.com/charlesvestal/fm1-x0x (one message for both)

> **Title:** Your firmware on an FM-1 firmware switcher
>
> Hi! I made a small page where FM-1 owners can try every custom firmware with one click and go back just as easily:
> https://fm1.designburgapps.com
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
> Sven (sloopDX, zp12)
