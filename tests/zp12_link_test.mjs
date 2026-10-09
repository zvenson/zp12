// SPDX-License-Identifier: GPL-3.0-only
// The backup tool end to end without an FM-1: web/zp12link.js against the firmware's link code
// (tests/zp12_link_host.c: sp_link.c on a flash in RAM).
//   cc -O2 -Wall -Wno-unused-function -Ifirmware/src -o build/host/zp12_link_host tests/zp12_link_host.c
//   node tests/zp12_link_test.mjs
import { spawn } from "node:child_process";
import { createRequire } from "node:module";
import readline from "node:readline";
const require = createRequire(import.meta.url);
const Z = require("../web/zp12link.js");

const dev = spawn("build/host/zp12_link_host", [], { stdio: ["pipe", "pipe", "inherit"] });
const lines = readline.createInterface({ input: dev.stdout });
let hook = null, reboots = 0, drop = 0;
const hex = (b) => b.map((x) => x.toString(16).padStart(2, "0")).join("");
const link = new Z.Link((bytes) => {
  if (drop > 0) { drop--; return; }                       // (a message lost on the way: asked again)
  dev.stdin.write(hex(bytes) + "\n");
}, { timeout: 300 });
lines.on("line", (l) => {
  if (l === "REBOOT") { reboots++; return; }
  if (hook) { const h = hook; hook = null; h(l); return; }
  link.receive(l.match(/../g).map((x) => parseInt(x, 16)));
});
const raw = (cmd) => new Promise((ok) => { hook = ok; dev.stdin.write(cmd + "\n"); });
const dump = async (a, n) => Uint8Array.from((await raw(`DUMP ${a.toString(16)} ${n.toString(16)}`)).match(/../g).map((x) => parseInt(x, 16)));

let bad = 0;
const check = (ok, what) => { console.log(`link: ${what.padEnd(72)} ${ok ? "ok" : "FAIL"}`); bad += !ok; };
const same = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);

const info = await link.hello();
check(info.proto === 3 && info.version === "zp12 1.5" && info.kit === 27, `HELLO: protocol 3, "${info.version}", ${info.kit} kit waves`);
const store0 = await dump(0xC4000, 0x8000), banks0 = await dump(0xA0000, 0x3000), smp0 = await dump(0xEA000, 0x1800);
const presets0 = await dump(0xDC000, 0x1000);

// 1. backup of everything
drop = 1;
const file = await Z.backup(link, ["zp12", "banks"]);
const b = Z.readBackup(file);
check(b.version === "zp12 1.5" && b.parts.has("zp12") && b.parts.has("banks"), "backup: the version and both parts in the file");
check(b.sectors.size === 8 + 3 + 2, `backup: only the sectors that are not empty (${b.sectors.size})`);
check(file.length < 60000, `backup: ${file.length} bytes`);

// 2. the device changes: the store and a bank written over, a new sample, then restore of both parts
await link.erase(0xC4000);
await link.write(0xC4000, Array(512).fill(0x11));
await link.erase(0xA1000);
await link.erase(0xF0000);
await link.write(0xF0000, Array(512).fill(0x22));
let e = null;
try { await link.erase(0xDC000); } catch (x) { e = x; }
check(e && /error 2/.test(e.message), "ERASE outside zp12's rooms and sloopDX's banks: refused");
e = null;
try { await link.write(0x90000, [1, 2, 3]); } catch (x) { e = x; }
check(e && /error 2/.test(e.message), "WRITE into the firmware: refused");

await Z.restore(link, file, ["zp12", "banks"]);
check(same(await dump(0xC4000, 0x8000), store0), "restore: zp12's store as it was");
check(same(await dump(0xA0000, 0x3000), banks0), "restore: sloopDX's banks as they were");
check(same(await dump(0xEA000, 0x1800), smp0), "restore: the sample as it was");
check((await dump(0xF0000, 0x1000)).every((x) => x === 0xff), "restore: what was not in the backup erased");
check(same(await dump(0xDC000, 0x1000), presets0), "restore: sloopDX's presets never touched");
check(reboots === 1, "restore: the FM-1 restarts at the end");

// 3. only zp12 restored: the banks stay as they are now
await link.erase(0xA0000);
await link.erase(0xC5000);
const before = (await raw("STATS")).split(" ").map(Number);
await Z.restore(link, file, ["zp12"]);
const after = (await raw("STATS")).split(" ").map(Number);
check(same(await dump(0xC4000, 0x8000), store0), "restore zp12 only: the store back");
check((await dump(0xA0000, 0x1000)).every((x) => x === 0xff), "restore zp12 only: the banks left alone");
check(after[1] - before[1] === 1, `restore: only the sector that differed erased (${after[1] - before[1]})`);

// 4. a damaged file is refused before anything is written
const broken = file.slice(); broken[100] ^= 1;
e = null;
try { await Z.restore(link, broken, ["zp12"]); } catch (x) { e = x; }
check(e && /damaged/.test(e.message), "restore: a damaged file refused (CRC)");

// 5. own samples: the resampler as the C one, upload, what the firmware reads, delete, room
{
  const x = Float64Array.from({ length: 3000 }, (_, i) => 0.8 * Math.sin(i * 0.05) * Math.exp(-i / 1500) + (i % 7 === 0 ? 0.1 : 0));
  const c = (await raw(`RS 44100 26040 ${[...x].map((v) => v.toPrecision(17)).join(" ")}`)).trim().split(" ").map(Number);
  const js = [...Z.resample12(x, 44100, 26040)];
  check(c.length === js.length && c.every((v, i) => v === js[i]), `resample12: the browser's ${js.length} samples = the C one's, bit for bit`);
  const slowC = (await raw(`RS ${Math.round(44100 * 45 / 33)} 26040 ${[...x].map((v) => v.toPrecision(17)).join(" ")}`)).trim().split(" ").map(Number);
  const slowJ = [...Z.resample12(x, Math.round(44100 * 45 / 33), 26040)];
  check(slowC.length === slowJ.length && slowC.every((v, i) => v === slowJ[i]) && Math.abs(slowJ.length / js.length - 33 / 45) < 0.01,
        `at 45: ${slowJ.length} samples (33/45 of ${js.length}), bit for bit as the C one`);
  const enc = Z.encode(Float32Array.from(x), 44100, { slow: true });
  check(enc.length === slowJ.length && Math.max(...enc.map(Math.abs)) >= 1980, "encode: normalised, at 45 as asked");
  const y = Z.resample12(x, 44100, 26040), packed = Z.pack12(y);
  check([...Z.unpack12(packed, y.length)].every((v, i) => v === y[i]), "pack12 / unpack12: 12 bit packed and back");

  let d = await Z.readDir(link);
  check(d.copy === -1 && d.slots.every((s) => !s), "directory: none at first");
  const r0 = Z.room(d, false);
  check(r0.free === 24 * 4096 && r0.total === 24 * 4096, `room: ${r0.free / 1024} KB free without sloopDX's banks`);
  const s0 = await Z.upload(link, d, { name: "my kick", y, rate: 26040 });
  let w = (await raw("WAVES")).trim().split(" ");
  check(s0 === 0 && w[0] === "0" && +w[2] === y.length && w[3] === "26040" && w[4] === "MY" , `upload: the firmware plays slot 0, ${w[2]} samples (name "${w[4]} ${w[5]}")`);
  const atFlash = await dump(+w[1], packed.length);
  check(same(atFlash, packed), "upload: the bytes in flash are the packed sample");
  const s1 = await Z.upload(link, d, { name: "SNARE2", y, rate: 27500, slow: true });
  const wl = await raw("WAVES");
  check(s1 === 1 && / 27500 SNARE2 1 /.test(wl + " "), "a second one: 27.5 kHz, stored for 45->33");
  d = await Z.readDir(link);
  check(d.gen === 2 && d.slots[0].name === "MY KICK" && d.slots[1].flags === 1, "directory read back: two, generation 2, names upper case");
  await link.assign(5, info.kit + s1);
  check((await raw("PADS")).split(" ")[5] === String(27 + 1), "ASSIGN: pad A6 plays the second one");
  const pd = await link.pads();
  check(pd.waves.length === 32 && pd.waves[5] === 28 && pd.waves[0] === 0 && pd.kit.length === 27 && pd.kit[26] === "PIANO",
        `PADS: the 32 pads (A6 = ${pd.waves[5]}) and the kit's ${pd.kit.length} names`);
  e = null;
  try { await link.assign(6, info.kit + 7); } catch (x) { e = x; }
  check(e, "ASSIGN to an empty slot: refused");
  await Z.remove(link, d, s0);
  w = (await raw("WAVES")).trim().split(" ");
  check(w[0] === "1" && w.length === 6, "delete: only the second one left");
  // a long one: more than the free rooms hold without the banks, fits with them
  const big = new Int16Array(70000);
  e = null;
  try { await Z.upload(link, d, { name: "LONG", y: big }); } catch (x) { e = x; }
  check(e && /not enough room/.test(e.message), "too long for the free rooms: refused before anything is written");
  const sl = await Z.upload(link, d, { name: "LONG", y: big }, true);
  check(d.slots[sl].off >= 0xa0000 && d.slots[sl].off < 0xc4000 && d.flags === 1, "with sloopDX's bank room: it goes there, the directory says so");
  w = (await raw("WAVES")).trim().split(" ");
  check(w.includes("LONG"), "the firmware takes a sample in the bank room");
  const so = await Z.upload(link, d, { name: "OCT", y: Z.encode(Float32Array.from(x), 44100, { oct: true, slow: true }), oct: true, slow: true });
  check(/ OCT 3 /.test((await raw("WAVES")) + " ") && d.slots[so].n === Math.floor(3000 * 26040 / Math.round(44100 * 45 / 33 * 2)),
        "stored at x2.7 (45->33 and double speed): a third of the samples, flags 3");
  check(Math.abs(Z.playSecs(d.slots[so].n, 26040, 3) - 3000 / 44100) < 0.002 && Z.zoh(Z.unpack12(Z.pack12(y), y.length), 26040, true, true).length > y.length * 2,
        "played back: as long as the original, the FM-1 stepping half as fast");
  const back = await Z.fetchSample(link, d.slots[1]);
  check(back.length === y.length && back.every((v, i) => v === y[i]), "fetchSample: read back as it was uploaded");
  const z = Z.zoh(y, 26040, false);
  check(Math.abs(z.length - Math.round(y.length * 44100 / 26040)) <= 2, `zoh: ${z.length} frames at 44.1 kHz as the FM-1 steps through them`);
  // a backup keeps them: zp12's part holds the directory and the free rooms
  const f2 = await Z.backup(link, ["zp12", "banks"]);
  await Z.remove(link, d, 1);
  await Z.restore(link, f2, ["zp12", "banks"]);
  await link.reload();
  w = (await raw("WAVES")).trim().split(" ");
  check(w.includes("SNARE2") && w.includes("LONG"), "backup + restore: the samples come back");
}

// 6. chop to pads: 16 chops as one; a stop half way leaves the FM-1 as it was
{
  let d = await Z.readDir(link);
  const pads0 = (await raw("PADS")).trim(), waves0 = (await raw("WAVES")).trim(), gen0 = d.gen;
  const x = Float32Array.from({ length: 16 * 4000 }, (_, i) => Math.sin(i * 0.02) * Math.exp(-(i % 4000) / 800));
  const list = Array.from({ length: 16 }, (_, j) => ({ name: "BRK" + String(j + 1).padStart(2, "0"),
    y: Z.encode(x, 44100, { start: j * 4000, end: (j + 1) * 4000 }), rate: 26040, pad: 8 + j }));
  let steps = 0, e = null;
  try { await Z.uploadMany(link, d, list, false, (k) => { steps = k; }, () => steps > 40); } catch (x) { e = x; }
  check(e && /stopped/.test(e.message) && (await raw("PADS")).trim() === pads0 && (await raw("WAVES")).trim() === waves0 &&
        (await Z.readDir(link)).gen === gen0, "chops: stopped half way: the samples, the directory and the pads as they were");
  d = await Z.readDir(link);
  const slots = await Z.uploadMany(link, d, list);
  const w = (await raw("WAVES")).trim(), p = (await raw("PADS")).trim().split(" ").map(Number);
  check(slots.length === 16 && list.every((s) => w.includes(s.name)) && (await Z.readDir(link)).gen === gen0 + 1,
        "chops: 16 samples on the FM-1, the directory written once");
  check(list.every((s, j) => p[8 + j] === info.kit + slots[j]), "chops: B1..D8 each play their chop (channels 1-8 twice)");
  check(p.slice(0, 8).join() === pads0.split(" ").slice(0, 8).join(), "chops: the other pads untouched");
  const back = await Z.fetchSample(link, d.slots[slots[7]]);
  check(back.length === list[7].y.length && back.every((v, i) => v === list[7].y[i]), "chops: the 8th read back as sent");
  e = null;
  try { await Z.uploadMany(link, d, list); } catch (x) { e = x; }
  check(e && /places free|room/.test(e.message), `chops: 16 more do not fit: refused before writing (${e && e.message})`);
}

dev.stdin.end();
console.log(bad ? "LINK TEST FAILED" : "link test passed");
process.exit(bad ? 1 : 0);
