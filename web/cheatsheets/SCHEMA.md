# Cheat sheet data (web/cheatsheets/<catalogue id>.json), rendered by tools/make_fm1.py

One JSON per firmware, English, short, verified in the firmware's source at the hub's current release.

```json
{
  "id": "sloop",                      // the hub catalogue id
  "name": "SLOOP",
  "version": "2.7.1",                 // the release this sheet was written for (the hub's current one)
  "ref": "v2.7.1",                    // the git tag / commit read
  "manual": "https://…",              // the author's own manual / README (linked as "the full manual")
  "intro": "One sentence: what it is and the one thing to know first.",
  "sections": [
    { "title": "Getting around",
      "rows": [ { "do": ["HOME"], "what": "Back to the main screen." },
                { "do": ["SHIFT? no: use real labels", "…"], "what": "…" } ] }
  ],
  "keys": [                           // optional: what the 27 note keys do on the main screen (zones)
    { "keys": "white" | "black" | [0, 1, 2], "label": "Steps 1-16", "color": "#42f5f5" }
  ],
  "tips": ["Short, useful, verified."],
  "checked": ["What you verified in which source file (for us, not shown)."],
  "unsure": ["Anything you could not verify (for us, not shown)."]
}
```

Control names: use exactly the FM-1's printed labels.
- Buttons (14): FX, SEL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC, OCT-, OCT+
- Encoders (7): SELECT, ALGORITHM, PRESETS, KNOB 1, KNOB 2, KNOB 3, KNOB 4
- Note keys (27): F3 .. G5; "white key 1-16" (left to right), "black key 1-11"; index 0 = F3 .. 26 = G5
- Combinations: list every control in "do" in the order you press them, e.g. ["hold ENV", "KNOB 1"],
  ["hold OCT-", "power on"]. Verbs allowed in a "do" item: "hold X", "tap X", "turn X", "X twice", "power on".

Sections: 4-7 of: Getting around · Playing · Sound · Sequencer / patterns · Effects · Saving & loading ·
MIDI & sync · Settings · Recovery. Rows: 3-10 per section. Only what the firmware really does.

## One format for all (make_fm1 enforces it, whatever the JSON says)
- Sections always in this order: Getting around, Playing, Sound, Sequencer / patterns, Effects, Saving & loading,
  MIDI & sync, Settings, Recovery (other titles go last). Use exactly these titles.
- Key zones get the hub's colours in their order (cyan, pink, beige, blue, orange, lilac, green, yellow); a zone whose
  label says "same" is grey. The "color" in the JSON is ignored.
- Every sheet: the lila signet (fm1/img/signet-lila.svg) + "thefirmwarehub.com" top right (no mail address on the sheet), the firmware's colour from its card (LOOK in web/fm1.html)
  as the band, name, frames and the Install button; template web/cs-sheet.html (not web/cheatsheet.html: zp12's own sheet).
