// SPDX-License-Identifier: GPL-3.0-only
// The browser simulator without a browser: loads docs/sim/zp12.wasm in node, boots it (the splash and its drums),
// plays pads, runs the sequencer (PLAY, REC: an overdub records hits), turns the tempo, plays a pad over MIDI and
// checks: it sounds, no NaN, no clipping, the screen changes, transport and recording work, and it renders far
// faster than real time; then SAVE and a new instance on the saved flash; the page's worklet.js in a stand-in
// scope. Last, the same script through the native build (sim/native.c, the same sources on the host): the same
// samples and the same screens.
//   sh sim/build.sh && node tests/sim_test.mjs [docs/sim/zp12.wasm]
import fs from 'fs';
import path from 'path';
import { execFileSync } from 'child_process';
import { fileURLToPath } from 'url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const WASM = process.argv[2] || path.join(ROOT, 'docs/sim/zp12.wasm');
let fail = 0;
const check = (ok, what) => { console.log((ok ? '  ok    ' : '  FAIL  ') + what); if (!ok) fail = 1; };

const bytes = fs.readFileSync(WASM);
const ex = (await WebAssembly.instantiate(bytes, {})).instance.exports;
console.log(`sim: ${WASM} (${bytes.length} B)`);
check(WebAssembly.Module.imports(new WebAssembly.Module(bytes)).length === 0, 'no imports (standalone)');
check(ex.sim_init(44100) === 0, 'sim_init(44100): boots');

// the script, in sim/native.c's words (T ms: keys MASK / btn ID / enc ID N / midi ST D1 D2 / shot / end).
// Keys: bit 0 = F3 = white key 1 = pad A1 (kick), bit 2 = A2, bit 4 = A3. Buttons: 12 PLAY, 13 REC. Encoder 0: SELECT (tempo)
const SCRIPT = `
1500 shot
3300 shot
3500 keys 0x1
3700 keys 0
4000 btn 12
4100 btn 13
4300 keys 0x4
4400 keys 0
4600 keys 0x10
4700 keys 0
5500 btn 13
5600 shot
6000 btn 12
6100 enc 0 4
6300 midi 0x90 36 100
7000 shot
7500 end
`;
const ev = SCRIPT.trim().split('\n').map((l) => l.trim().split(/\s+/)).map(([t, w, ...a]) => ({ t: +t, w, a: a.map(Number) }));
const END = ev.find((e) => e.w === 'end').t;
const MAXF = Math.ceil(END * 44.1) + 512;
const L = new Float32Array(MAXF), R = new Float32Array(MAXF), shots = {}, at = {};
let nf = 0, ei = 0;
const fb = () => new Uint16Array(ex.memory.buffer, ex.sim_screen(), 240 * 240).slice();
const hashes = new Set();
while (ex.sim_now() < END) {
  const now = ex.sim_now();
  while (ei < ev.length && ev[ei].t <= now + 1) {       // (as sim/native.c: before the millisecond's pass)
    const e = ev[ei++];
    if (e.w === 'keys') for (let k = 0; k < 27; k++) ex.sim_key(k, (e.a[0] >> k) & 1);
    else if (e.w === 'btn') { ex.sim_button(e.a[0], 1); ex.sim_button(e.a[0], 0); }
    else if (e.w === 'enc') ex.sim_enc(e.a[0], e.a[1]);
    else if (e.w === 'midi') ex.sim_midi(e.a[0], e.a[1], e.a[2]);
    else if (e.w === 'shot') shots[e.t] = fb();
  }
  ex.sim_render(1);
  L[nf] = new Float32Array(ex.memory.buffer, ex.sim_out_l(), 1)[0];
  R[nf] = new Float32Array(ex.memory.buffer, ex.sim_out_r(), 1)[0];
  nf++;
  const m = ex.sim_now();
  if (m !== now) {
    if (m % 100 === 0) hashes.add(ex.sim_fb_hash());
    if (m === 3400 || m === 4500 || m === 5800 || m === 6500) at[m] = { play: ex.sim_playing(), rec: ex.sim_recording(), leds: ex.sim_leds_buttons(), keys: ex.sim_leds_keys() };
  }
}

const rmsOf = (a, b) => { let e = 0; for (let i = a * 44.1 | 0; i < b * 44.1; i++) e += L[i] * L[i] + R[i] * R[i]; return Math.sqrt(e / (2 * (b - a) * 44.1)); };
let peak = 0, nan = 0;
for (let i = 0; i < nf; i++) for (const v of [L[i], R[i]]) { if (!Number.isFinite(v)) nan++; else peak = Math.max(peak, Math.abs(v)); }
const rSplash = rmsOf(500, 3000), rPad = rmsOf(3500, 3800), rQuiet = rmsOf(3150, 3480), rMidi = rmsOf(6300, 6600);
console.log(`  ${nf} frames, peak ${peak.toFixed(3)}; rms: splash ${rSplash.toFixed(4)}, quiet ${rQuiet.toFixed(4)}, pad A1 ${rPad.toFixed(4)}, MIDI 36 ${rMidi.toFixed(4)}`);
check(nan === 0, 'no NaN / Inf');
check(rSplash > 0.003, 'the start plays its drums');
check(rPad > 0.01 && rPad > 5 * rQuiet, 'white key 1 plays pad A1 (the kick)');
check(rMidi > 0.01, 'MIDI note 36 plays pad A1');
check(peak < 32767 / 32768, 'no clipping (peak below full scale)');
check(hashes.size >= 10, `the screen changes (${hashes.size} different screens at 100 ms steps)`);
check(at[3400].play === 0 && at[4500].play === 1 && at[6500].play === 0, 'PLAY starts and stops the sequencer');
check(at[4500].rec !== 0 && at[5800].rec === 0, 'REC while playing records, REC again stops it');
check(((at[4500].leds >> 12) & 1) === 1 && ((at[4500].leds >> 13) & 1) === 1, `PLAY and REC lit while recording (LEDs 0x${at[4500].leds.toString(16)})`);
check(ex.sim_loop_events() >= 2, `the overdub kept the hits (${ex.sim_loop_events()} in loop 1)`);
check(ex.sim_bpm10() === 920, `SELECT +4: tempo 92.0 (${ex.sim_bpm10() / 10})`);
check(ex.sim_halted() === 0, 'running (no reboot / update request)');

// speed: 10 s with the loop playing and pads, in 128-frame quanta as the worklet asks
{
  const WHITE = [0, 2, 4, 6, 7, 9, 11, 12];
  ex.sim_button(12, 1); ex.sim_button(12, 0);
  const t0 = performance.now();
  for (let i = 0; i < 44100 * 10 / 128; i++) {
    if (i % 40 === 0) ex.sim_key(WHITE[(i / 40) % 8], 1);       // (white keys: pads A1-A8; a black key would switch loops)
    if (i % 40 === 20) ex.sim_key(WHITE[(i / 40 | 0) % 8], 0);
    ex.sim_render(128);
  }
  const ms = performance.now() - t0;
  console.log(`  speed: 10 s of audio in ${ms.toFixed(0)} ms (${(10000 / ms).toFixed(0)}x real time; 1 s in ${(ms / 10).toFixed(1)} ms)`);
  check(ms < 2000, 'renders at least 5x faster than real time');
  check(ex.sim_peak() < 32767, 'no clipping with the loop and pads');
  ex.sim_button(12, 1); ex.sim_button(12, 0);
}

// storage: SAVE writes the flash (sp_store.c, 0xC4000..); a new instance on those bytes boots with the loop
{
  const size = ex.sim_store_size(), w0 = ex.sim_store_writes(), hits = ex.sim_loop_events();
  for (let i = 0; i < 100; i++) ex.sim_render(128);
  ex.sim_button(9, 1); for (let i = 0; i < 20; i++) ex.sim_render(128); ex.sim_button(9, 0);
  for (let i = 0; i < 44100 * 2 / 128; i++) ex.sim_render(128);
  check(size === 0x16000 && ex.sim_store_writes() > w0, `SAVE writes the flash (${ex.sim_store_writes() - w0} erases / pages)`);
  const ex2 = (await WebAssembly.instantiate(bytes, {})).instance.exports;
  new Uint8Array(ex2.memory.buffer, ex2.sim_store(), size).set(new Uint8Array(ex.memory.buffer, ex.sim_store(), size));
  ex2.sim_init(44100);
  for (let i = 0; i < 44100 * 4 / 128; i++) ex2.sim_render(128);
  check(hits >= 2 && ex2.sim_loop_events() === hits && ex2.sim_bpm10() === 920, `a new instance with the saved flash has the loop (${ex2.sim_loop_events()} hits) and the tempo`);
}

// the page's AudioWorklet (worklet.js) in a stand-in for the browser's worklet scope: boots, renders 128-frame
// quanta, sends the screen and LEDs, takes a key, keeps the store it is given
{
  const vm = await import('vm');
  const posted = [];
  let Proc = null;
  const ctx = {
    AudioWorkletProcessor: class { constructor() { this.port = { postMessage: (m) => posted.push(m), onmessage: null }; } },
    registerProcessor: (name, c) => { if (name === 'zp12') Proc = c; },
    currentTime: 0, WebAssembly, Float32Array, Uint16Array, Uint8Array, Error, String,
  };
  vm.runInNewContext(fs.readFileSync(path.join(path.dirname(WASM), 'worklet.js'), 'utf8'), ctx);
  const store = new Uint8Array(ex.memory.buffer, ex.sim_store(), ex.sim_store_size()).slice();
  const w = new Proc({ processorOptions: { wasm: bytes, store: store.buffer, master: 900 } });
  const out = [[new Float32Array(128), new Float32Array(128)]];
  let e2 = 0, frames = 0;
  for (let q = 0; q < 44100 * 5 / 128; q++) {
    ctx.currentTime = q * 128 / 44100;
    if (q === Math.round(4 * 44100 / 128)) w.port.onmessage({ data: { t: 'key', k: 0, down: 1 } });
    w.process([], out);
    if (q > 4 * 44100 / 128) for (const v of out[0][0]) e2 += v * v;
  }
  for (const m of posted) if (m.t === 'frame' && m.fb) frames++;
  check(posted[0] && posted[0].t === 'ready', 'worklet.js: boots (ready)');
  check(frames >= 10, `worklet.js: sends the screen when it changes (${frames} frames in 5 s)`);
  check(e2 > 0.01, 'worklet.js: a key sounds through process()');
  check(w.ex.sim_loop_events() === ex.sim_loop_events(), 'worklet.js: boots with the store it was given');
}

// the native build, same script: the same samples and the same screens
const host = path.join(ROOT, 'build/host/zp12-sim');
let haveCC = true;
try {
  fs.mkdirSync(path.join(ROOT, 'build/host'), { recursive: true });
  const ver = (fs.readFileSync(path.join(ROOT, 'README.md'), 'utf8').match(/\*\*Status: ([0-9][0-9.]*)\*\*/) || [0, '0.0'])[1];
  // (-fno-stack-protector: sp_ui.c ui_splash writes "12-bit sampling drums  " + the version past its char b[24])
  execFileSync('cc', ['-O2', '-w', '-fno-stack-protector', '-DSIM_NATIVE', `-DZP12_VER="${ver}"`, '-Isim/hal', '-Ifirmware/src',
    '-Ifirmware/gen', '-Ibuild/gen', '-o', host, 'sim/native.c'], { cwd: ROOT });
} catch (e) { haveCC = false; console.log('  (no cc: the comparison with the native build is skipped)'); }
if (haveCC) {
  fs.writeFileSync(host + '.txt', SCRIPT);
  execFileSync(host, [host, host + '.txt'], { cwd: ROOT });
  const wav = fs.readFileSync(host + '.wav'), hs = new Int16Array(wav.buffer, wav.byteOffset + 44, (wav.length - 44) >> 1);
  const hn = hs.length >> 1;
  let diff = 0, maxd = 0;
  for (let i = 0; i < hn && i < nf; i++) {
    const dl = Math.abs(Math.round(L[i] * 32768) - hs[2 * i]), dr = Math.abs(Math.round(R[i] * 32768) - hs[2 * i + 1]);
    if ((dl || dr) && !diff) console.log(`  first difference at frame ${i} (${(i / 44.1).toFixed(1)} ms): wasm ${Math.round(L[i] * 32768)} native ${hs[2 * i]}`);
    if (dl || dr) diff++;
    maxd = Math.max(maxd, dl, dr);
  }
  check(hn === nf && diff === 0, `sound = the native build's, sample for sample (${hn} frames, ${diff} differ, max ${maxd})`);
  for (const t of Object.keys(shots)) {
    const ppm = fs.readFileSync(`${host}-${t}.ppm`), off = ppm.length - 240 * 240 * 3, s = shots[t];
    let bad = 0;
    for (let k = 0; k < 57600; k++) {
      const c = s[k], r = ((c >> 11) * 255 / 31) | 0, g = ((c >> 5 & 63) * 255 / 63) | 0, b = ((c & 31) * 255 / 31) | 0;
      if (ppm[off + 3 * k] !== r || ppm[off + 3 * k + 1] !== g || ppm[off + 3 * k + 2] !== b) bad++;
    }
    check(bad === 0, `screen at ${t} ms = the native build's (${bad} pixels differ)`);
  }
}
console.log(fail ? 'SIM TEST FAILED' : 'SIM TEST PASSED');
process.exit(fail);
