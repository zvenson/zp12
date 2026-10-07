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
check(info.proto === 1 && info.version === "zp12 1.3", `HELLO: protocol 1, "${info.version}"`);
const store0 = await dump(0xC4000, 0x8000), banks0 = await dump(0xA0000, 0x3000), smp0 = await dump(0xEA000, 0x1800);
const presets0 = await dump(0xDC000, 0x1000);

// 1. backup of everything
drop = 1;
const file = await Z.backup(link, ["zp12", "banks"]);
const b = Z.readBackup(file);
check(b.version === "zp12 1.3" && b.parts.has("zp12") && b.parts.has("banks"), "backup: the version and both parts in the file");
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

dev.stdin.end();
console.log(bad ? "LINK TEST FAILED" : "link test passed");
process.exit(bad ? 1 : 0);
