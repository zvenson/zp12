// SPDX-License-Identifier: GPL-3.0-only
// Chop to pads (web/zp12link.js): the markers against test sounds with known hits, fit to room.
//   node tests/zp12_chop_test.mjs
import { createRequire } from "node:module";
const require = createRequire(import.meta.url);
const Z = require("../web/zp12link.js");

let bad = 0;
const check = (ok, what) => { console.log(`chop: ${what.padEnd(76)} ${ok ? "ok" : "FAIL"}`); bad += !ok; };
let seed = 1;
const rnd = () => ((seed = (seed * 1103515245 + 12345) >>> 0) / 2 ** 32) * 2 - 1;

// a break: 2 bars at 90 BPM, a hit on every 8th (kick, hat, snare, hat ...), quiet noise between
function brk(rate, bpm = 90, bars = 2) {
  const n = Math.round(bars * 4 * 60 / bpm * rate), x = new Float32Array(n), on = [];
  for (let i = 0; i < n; i++) x[i] = rnd() * 0.002;
  const step = 60 / bpm / 2 * rate;
  for (let k = 0; k < bars * 8; k++) {
    const at = Math.round(k * step), lvl = [0.9, 0.3, 0.7, 0.3][k % 4], tone = k % 4 === 0 ? 60 : 0;
    on.push(at);
    for (let i = 0; i < 0.15 * rate && at + i < n; i++) {
      const e = Math.exp(-i / (0.03 * rate));
      x[at + i] += lvl * e * (tone ? Math.sin(2 * Math.PI * tone * i / rate) * 0.6 + rnd() * 0.4 : rnd());
    }
  }
  return { x, on, n };
}

for (const rate of [44100, 48000, 22050]) {
  const { x, on, n } = brk(rate), nov = Z.chopNovelty(x, rate);
  const hits = Z.chopHits(x, nov, 5), ms = (s) => s / rate * 1000;
  const near = on.map((t) => hits.find((h) => ms(t - h) >= -1 && ms(t - h) <= 4));
  check(hits.length === on.length && near.every((h) => h != null),
        `${rate} Hz: Find hits: ${hits.length} of ${on.length}, each 0-4 ms before its hit`);
  const soft = Z.chopHits(x, nov, 1);
  check(soft.length > 0 && soft.length < hits.length, `${rate} Hz: sensitivity 1 finds only the loud ones (${soft.length})`);
  const tapAt = on[5] + Math.round(0.03 * rate), snapped = Z.chopSnap(x, nov, tapAt);
  check(ms(on[5] - snapped) >= -1 && ms(on[5] - snapped) <= 4, `${rate} Hz: a tap 30 ms late snaps to the hit (${ms(snapped - on[5]).toFixed(1)} ms)`);
  check(Z.chopBpm(0, n, rate, 2) === 90 && Z.chopBpm(0, n, rate, 1) === 90 * 1 / 2 * 2, `${rate} Hz: BPM from its length: ${Z.chopBpm(0, n, rate, 2)}`);
  const g = Z.chopGrid(0, n, 90, 0.5, rate);
  check(g.length === 16 && g.every((p, i) => Math.abs(p - on[i]) <= 1), `${rate} Hz: the grid at 90, 1/8: on the 16 hits`);
}

const e8 = Z.chopEqual(1000, 9000, 8);
check(e8.length === 8 && e8[0] === 1000 && e8[1] === 2000 && e8[7] === 8000, "Equal: 8 parts of 1000..9000");
const L = Z.chopList([100, 500, 900], 1500, [null, { len: 50 }, { off: true }]);
check(L[0].end === 500 && L[1].end === 550 && L[1].full === 900 && L[2].off && L[2].end === 1500,
      "chopList: up to the next marker, a length of its own, the last to the end, left out");
check(Z.chopList([100, 500], 1500, [{ len: 9999 }])[0].end === 500, "chopList: never longer than up to the next marker");

// encLen = what encode makes
const { x: bx } = brk(44100);
for (const o of [{}, { slow: true }, { oct: true }, { slow: true, oct: true, rate: 27500 }]) {
  const y = Z.encode(bx, 44100, { ...o, start: 3000, end: 25000 });
  check(y.length === Z.encLen(22000, 44100, o), `encLen ${JSON.stringify(o)}: ${y.length} as encode`);
}

// room: 96 KB free (24 sectors); 16 chops of 0.5 s at 1x need 16 x 6 sectors: too much; Fit to room
const dir = { gen: 0, flags: 0, copy: -1, slots: Array(24).fill(null) };
const chops = Array.from({ length: 16 }, (_, i) => ({ start: i * 30000, end: i * 30000 + (i < 4 ? 22050 : 4000) }));
const o = { rate: 26040 };
const lens = (Lmax) => chops.map((c) => Z.encLen(Math.min(c.end - c.start, Lmax), 44100, o));
check(Z.plan(dir, lens(Infinity), false) === null, "plan: 4 long + 12 short chops do not fit 96 KB");
const fit = Z.chopFit(dir, chops, 44100, o, false);
check(isFinite(fit) && fit < 22050 && fit > 4000 && !!Z.plan(dir, lens(fit), false) && !Z.plan(dir, lens(fit + 2000), false),
      `Fit to room: the long ones cut at ${(fit / 44100).toFixed(3)} s, then all fit (a bit more not)`);
check(Z.chopFit(dir, chops.slice(4), 44100, o, false) === Infinity, "Fit to room: short ones fit as they are");
check(isFinite(Z.chopFit(dir, chops, 44100, o, false)) && Z.chopFit(dir, chops, 44100, { rate: 26040, oct: true }, false) === Infinity,
      "stored at x2 the same chops fit as they are");
const full = { ...dir, slots: dir.slots.map((_, i) => (i < 20 ? { off: 0xea000 + i * 4096, n: 100 } : null)) };
check(Z.plan(full, [100, 100, 100, 100], false) && !Z.plan(full, [100, 100, 100, 100, 100], false), "plan: 4 places free, a 5th chop does not fit");
check(Z.chanOf(0) === 1 && Z.chanOf(9) === 2 && Z.chanOf(31) === 8 && /filter/.test(Z.filterOf(2)) && /9 kHz/.test(Z.filterOf(4)) && Z.filterOf(7) === "open",
      "channels: a pad's position (A1 = 1, B2 = 2, D8 = 8), the filter there");

console.log(bad ? "CHOP TEST FAILED" : "chop test passed");
process.exit(bad ? 1 : 0);
