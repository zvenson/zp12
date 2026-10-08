// SPDX-License-Identifier: GPL-3.0-only  (C) 2026 Sven Trogus
// zp12's USB link for the web tools (firmware/src/sp_link.c): raw flash commands over SysEx, and on top of them
// the backup (a file of the flash zp12 uses, and optionally sloopDX's DX7 banks) and its restore.
// Used by web/backup.html in the browser and by tests/zp12_link_test.mjs in node.
"use strict";
(function (root) {
  const CMD = { HELLO: 1, READ: 2, ERASE: 3, WRITE: 4, HOLD: 5, REBOOT: 6, RELOAD: 7, ASSIGN: 8, PADS: 9 };
  const SECT = 4096, CHUNK = 512;
  // the parts of a backup: zp12's store and sample rooms; sloopDX's 8 DX7 user banks, the bank in use, MY KIT
  const PARTS = {
    zp12: [[0xC4000, 0xDC000], [0xE5000, 0xE9000], [0xEA000, 0xFC000]],
    banks: [[0xA0000, 0xC4000]],
  };
  const MAGIC = "ZP12BAK1";

  // the updater's 7-bit packing (firmware/src/ota.c ota_pack7 / ota_unpack7): a bit stream, LSB first
  function pack7(bytes) {
    const out = [];
    let acc = 0, nb = 0;
    for (const b of bytes) {
      acc |= b << nb; nb += 8;
      while (nb >= 7) { out.push(acc & 0x7f); acc >>>= 7; nb -= 7; }
    }
    if (nb) out.push(acc & 0x7f);
    return out;
  }
  function unpack7(w) {
    const out = [];
    let acc = 0, nb = 0;
    for (const b of w) {
      acc |= (b & 0x7f) << nb; nb += 7;
      if (nb >= 8) { out.push(acc & 0xff); acc >>>= 8; nb -= 8; }
    }
    return out;
  }
  // a request; len: the length field (the data's, or for READ the bytes wanted)
  function frame(cmd, addr, data, len = data.length) {
    const m = [0x7d, 0x5a, 0x50, cmd, addr & 255, (addr >> 8) & 255, (addr >> 16) & 255, len & 255, len >> 8, ...data];
    let s = 0;
    for (let i = 3; i < m.length; i++) s += m[i];
    m.push(~s & 255);
    return [0xf0, ...pack7(m), 0xf7];
  }
  // a reply's bytes (between F0 and F7) -> {cmd, rc, addr, data} or null
  function parse(w) {
    const m = unpack7(w);
    if (m.length < 11 || m[0] !== 0x7d || m[1] !== 0x5a || m[2] !== 0x50) return null;
    const n = m[8] | m[9] << 8;
    if (m.length < 11 + n) return null;
    let s = 0;
    for (let i = 3; i < 10 + n; i++) s += m[i];
    if ((~s & 255) !== m[10 + n]) return null;
    return { cmd: m[3], rc: m[4], addr: m[5] | m[6] << 8 | m[7] << 16, data: m.slice(10, 10 + n) };
  }

  // one request at a time, its reply matched by cmd and addr; a lost or damaged one is asked again
  class Link {
    constructor(send, opts = {}) {
      this.send = send;
      this.timeout = opts.timeout || 1500;
      this.tries = opts.tries || 4;
      this.wait = null;
      this.buf = null;
    }
    // feed raw MIDI bytes (whole SysEx messages or pieces)
    receive(bytes) {
      for (const b of bytes) {
        if (b === 0xf0) { this.buf = []; continue; }
        if (b === 0xf7) {
          if (this.buf && this.wait) {
            const r = parse(this.buf);
            if (r && r.cmd === this.wait.cmd && (r.addr === this.wait.addr || r.cmd === CMD.HELLO)) {
              const w = this.wait; this.wait = null; clearTimeout(w.timer); w.ok(r);
            }
          }
          this.buf = null;
          continue;
        }
        if (this.buf && b < 0x80) this.buf.push(b);
      }
    }
    async request(cmd, addr = 0, data = [], len = data.length) {
      const body = frame(cmd, addr, data, len);
      for (let t = 0; t < this.tries; t++) {
        try {
          const r = await new Promise((ok, fail) => {
            this.wait = { cmd, addr, ok, timer: setTimeout(() => { this.wait = null; fail(new Error("timeout")); }, this.timeout) };
            this.send(body);
          });
          return r;
        } catch (e) { if (t + 1 === this.tries) throw new Error(`no answer from the FM-1 (command ${cmd})`); }
      }
    }
    async hello() {
      const r = await this.request(CMD.HELLO), z = r.data.indexOf(0, 1);
      const h = { proto: r.data[0], version: String.fromCharCode(...r.data.slice(1, z < 0 ? undefined : z)), kit: z < 0 ? 0 : r.data[z + 1] };
      this.info = h;
      return h;
    }
    async reload() { const r = await this.request(CMD.RELOAD); return r.data[0]; }
    // the 32 pads' waves (255: none) and the kit's names (the own ones are in the directory)
    async pads() {
      const r = await this.request(CMD.PADS), d = r.data, names = [];
      let s = "";
      for (let i = 32; i < d.length; i++) { if (d[i]) s += String.fromCharCode(d[i]); else { names.push(s); s = ""; } }
      return { waves: d.slice(0, 32), kit: names };
    }
    async assign(pad, wave) { const r = await this.request(CMD.ASSIGN, (pad & 31) | (wave & 63) << 8); if (r.rc) throw new Error(`that sample is not on the FM-1 (${r.rc})`); }
    async read(addr, n) {
      const r = await this.request(CMD.READ, addr, [], n);
      if (r.rc || r.data.length !== n) throw new Error(`read ${hex(addr)}: error ${r.rc}`);
      return r.data;
    }
    async erase(addr) { const r = await this.request(CMD.ERASE, addr); if (r.rc) throw new Error(`erase ${hex(addr)}: error ${r.rc}`); }
    async write(addr, data) { const r = await this.request(CMD.WRITE, addr, data); if (r.rc) throw new Error(`write ${hex(addr)}: error ${r.rc}`); }
    async hold() { await this.request(CMD.HOLD); }
    async reboot() { try { await this.request(CMD.REBOOT); } catch (e) { /* it may be gone before it answers */ } }
  }
  function hex(a) { return "0x" + a.toString(16).toUpperCase(); }

  async function readSector(link, a) {
    const s = new Uint8Array(SECT);
    for (let o = 0; o < SECT; o += CHUNK) s.set(await link.read(a + o, CHUNK), o);
    return s;
  }
  const blank = (s) => s.every((b) => b === 0xff);
  function sectorsOf(parts) {
    const r = [];
    for (const p of parts) for (const [lo, hi] of PARTS[p]) for (let a = lo; a < hi; a += SECT) r.push([a, p]);
    return r;
  }

  function crc32(u8) {
    let c = ~0;
    for (let i = 0; i < u8.length; i++) { c ^= u8[i]; for (let k = 0; k < 8; k++) c = (c >>> 1) ^ (0xedb88320 & -(c & 1)); }
    return ~c >>> 0;
  }

  // the backup file: "ZP12BAK1", version text (16), the ranges (lo, hi, part), the sectors that are not empty, a CRC
  async function backup(link, parts, progress = () => {}) {
    const info = await link.hello();
    await link.hold();
    const list = sectorsOf(parts), keep = [];
    for (let i = 0; i < list.length; i++) {
      const s = await readSector(link, list[i][0]);
      if (!blank(s)) keep.push([list[i][0], s]);
      progress(i + 1, list.length);
    }
    const ranges = parts.flatMap((p) => PARTS[p].map(([lo, hi]) => [lo, hi, p === "banks" ? 1 : 0]));
    const size = 8 + 16 + 4 + ranges.length * 12 + 4 + keep.length * (4 + SECT) + 4;
    const f = new Uint8Array(size), v = new DataView(f.buffer);
    let o = 0;
    for (const c of MAGIC) f[o++] = c.charCodeAt(0);
    for (let k = 0; k < 16; k++) f[o++] = k < info.version.length ? info.version.charCodeAt(k) : 0;
    v.setUint32(o, ranges.length, true); o += 4;
    for (const [lo, hi, p] of ranges) { v.setUint32(o, lo, true); v.setUint32(o + 4, hi, true); v.setUint32(o + 8, p, true); o += 12; }
    v.setUint32(o, keep.length, true); o += 4;
    for (const [a, s] of keep) { v.setUint32(o, a, true); f.set(s, o + 4); o += 4 + SECT; }
    v.setUint32(o, crc32(f.subarray(0, o)), true);
    return f;
  }

  // what a backup file holds: {version, parts: Set, ranges, sectors: Map(addr -> data)}; throws when damaged
  function readBackup(f) {
    const v = new DataView(f.buffer, f.byteOffset, f.byteLength);
    if (f.length < 36 || String.fromCharCode(...f.subarray(0, 8)) !== MAGIC) throw new Error("not a zp12 backup");
    if (crc32(f.subarray(0, f.length - 4)) !== v.getUint32(f.length - 4, true)) throw new Error("the backup file is damaged (CRC)");
    let o = 8;
    const version = String.fromCharCode(...f.subarray(o, o + 16)).replace(/\0.*$/, ""); o += 16;
    const nr = v.getUint32(o, true); o += 4;
    const ranges = [], parts = new Set();
    for (let i = 0; i < nr; i++) {
      const r = [v.getUint32(o, true), v.getUint32(o + 4, true), v.getUint32(o + 8, true) ? "banks" : "zp12"]; o += 12;
      ranges.push(r); parts.add(r[2]);
    }
    const ns = v.getUint32(o, true); o += 4;
    const sectors = new Map();
    for (let i = 0; i < ns; i++) { sectors.set(v.getUint32(o, true), f.slice(o + 4, o + 4 + SECT)); o += 4 + SECT; }
    return { version, parts, ranges, sectors };
  }

  // write the parts chosen back: every sector of their ranges as in the file (empty ones erased), the ones that
  // are already so left alone; then the FM-1 restarts with them
  async function restore(link, f, parts, progress = () => {}) {
    const b = readBackup(f);
    await link.hello();
    await link.hold();
    const list = [];
    for (const [lo, hi, p] of b.ranges) if (parts.includes(p)) for (let a = lo; a < hi; a += SECT) list.push(a);
    for (let i = 0; i < list.length; i++) {
      const a = list[i], want = b.sectors.get(a), have = await readSector(link, a);
      if (want ? !have.every((x, k) => x === want[k]) : !blank(have)) {
        await link.erase(a);
        if (want) for (let o = 0; o < SECT; o += CHUNK) if (!want.subarray(o, o + CHUNK).every((x) => x === 0xff)) await link.write(a + o, [...want.subarray(o, o + CHUNK)]);
      }
      progress(i + 1, list.length);
    }
    await link.reboot();
    return b;
  }

  // ---- own samples (firmware/src/sp_samples.c): a directory of 24 slots at 0xD8000 / 0xD9000 (the newer good
  // copy counts), the samples packed 12 bit in whole sectors of the free rooms (or sloopDX's bank room, if wanted)
  const DIR = [0xd8000, 0xd9000], NSLOT = 24, DIRLEN = 16 + NSLOT * 20, MAGIC_DIR = 0x4d53505a;
  const ROOMS = [[0xda000, 0xdc000], [0xe5000, 0xe9000], [0xea000, 0xfc000]], BANKROOM = [0xa0000, 0xc4000];
  const RATES = [26040, 27500];
  const bytesOf = (n) => Math.floor((n + 1) / 2) * 3;
  const emptyDir = () => ({ gen: 0, flags: 0, copy: -1, slots: Array.from({ length: NSLOT }, () => null) });

  function parseDir(u8) {
    const v = new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
    if (u8.length < DIRLEN || v.getUint32(0, true) !== MAGIC_DIR || crc32(u8.subarray(16, DIRLEN)) !== v.getUint32(12, true)) return null;
    const d = { gen: v.getUint32(4, true), flags: v.getUint32(8, true), slots: [] };
    for (let i = 0; i < NSLOT; i++) {
      const o = 16 + i * 20, off = v.getUint32(o, true);
      const name = String.fromCharCode(...u8.subarray(o + 12, o + 20)).replace(/\0.*$/, "");
      d.slots.push(off ? { off, n: v.getUint32(o + 4, true), rate: v.getUint16(o + 8, true), flags: u8[o + 10], name } : null);
    }
    return d;
  }
  function buildDir(d) {
    const u8 = new Uint8Array(DIRLEN), v = new DataView(u8.buffer);
    v.setUint32(0, MAGIC_DIR, true); v.setUint32(4, d.gen >>> 0, true); v.setUint32(8, d.flags >>> 0, true);
    d.slots.forEach((s, i) => {
      if (!s) return;
      const o = 16 + i * 20;
      v.setUint32(o, s.off, true); v.setUint32(o + 4, s.n, true); v.setUint16(o + 8, s.rate, true); u8[o + 10] = s.flags & 255;
      for (let k = 0; k < 8 && k < s.name.length; k++) u8[o + 12 + k] = s.name.charCodeAt(k);
    });
    v.setUint32(12, crc32(u8.subarray(16, DIRLEN)), true);
    return u8;
  }
  async function readDir(link) {
    let best = emptyDir();
    for (let c = 0; c < 2; c++) {
      const d = parseDir(Uint8Array.from(await link.read(DIR[c], DIRLEN)));
      if (d && (best.copy < 0 || d.gen > best.gen)) { d.copy = c; best = d; }
    }
    return best;
  }
  // the directory into the older copy (the newer one stays good until this one is), then the FM-1 reads it again
  async function writeDir(link, d) {
    const c = d.copy === 0 ? 1 : 0;
    d.gen = (d.gen + 1) >>> 0;
    d.flags = d.slots.some((s) => s && s.off >= BANKROOM[0] && s.off < BANKROOM[1]) ? 1 : 0;
    await link.erase(DIR[c]);
    await link.write(DIR[c], [...buildDir(d)]);
    d.copy = c;
    return link.reload();
  }
  function used(d) {
    const u = new Set();
    for (const s of d.slots) if (s) for (let a = s.off; a < s.off + bytesOf(s.n); a += SECT) u.add(a - (a % SECT));
    return u;
  }
  function rooms(banks) { return banks ? [...ROOMS, BANKROOM] : ROOMS; }
  // the free runs of whole sectors: [{at, sectors}]
  function freeRuns(d, banks) {
    const u = used(d), r = [];
    for (const [lo, hi] of rooms(banks)) {
      let at = -1;
      for (let a = lo; a <= hi; a += SECT) {
        const free = a < hi && !u.has(a);
        if (free && at < 0) at = a;
        if (!free && at >= 0) { r.push({ at, sectors: (a - at) / SECT }); at = -1; }
      }
    }
    return r;
  }
  function room(d, banks) {                       // what is left: bytes in all, the longest sample in samples
    const r = freeRuns(d, banks), big = Math.max(0, ...r.map((x) => x.sectors));
    return { free: r.reduce((s, x) => s + x.sectors, 0) * SECT, longest: Math.floor(big * SECT / 3) * 2,
             total: rooms(banks).reduce((s, [lo, hi]) => s + hi - lo, 0) };
  }
  function alloc(d, bytes, banks) {               // the smallest free run it fits in
    const need = Math.ceil(bytes / SECT), fit = freeRuns(d, banks).filter((x) => x.sectors >= need).sort((a, b) => a.sectors - b.sectors);
    return fit.length ? fit[0].at : -1;
  }

  // a sampler's input: a Hann-windowed sinc low-pass at 0.45 of the lower rate, then 12 bit (as
  // tests/sp_core_test.c resample12, the host's and the kit's): x in -1..1 -> -2048..2047
  function resample12(x, from, to) {
    const H = 24, fc = 0.45 * Math.min(to, from) / from, ratio = from / to, k = Math.floor(x.length * to / from);
    const y = new Int16Array(k), span = Math.ceil(H * ratio);
    for (let i = 0; i < k; i++) {
      const t = i * ratio, c = Math.floor(t);
      let s = 0, wsum = 0;
      for (let j = c - span + 1; j <= c + span; j++) {
        const dd = t - j, a = 2 * fc * dd, sinc = a === 0 ? 1 : Math.sin(Math.PI * a) / (Math.PI * a);
        const g = 2 * fc * sinc * (0.5 + 0.5 * Math.cos(Math.PI * dd / (span + 1)));
        if (j >= 0 && j < x.length) s += x[j] * g;
        wsum += g;
      }
      s = s / wsum * 2047;
      s = s > 2047 ? 2047 : s < -2048 ? -2048 : s;
      y[i] = Math.sign(s) * Math.round(Math.abs(s));   // (C's lround: halves away from zero)
    }
    return y;
  }
  // a sound from the computer as the FM-1 will hold it: mono -1..1 at srcRate, the part start..end (samples),
  // normalised; stored fast and played slow, the old trick: slow = at 45, played at 33 (a third more time),
  // oct = at double speed, played an octave down on the pad (twice the time, half the bandwidth, the grit)
  const speedOf = (o) => (o.slow ? 45 / 33 : 1) * (o.oct ? 2 : 1);
  function encode(x, srcRate, o = {}) {
    let p = x.subarray(o.start || 0, o.end || x.length);
    const from = Math.round(srcRate * speedOf(o)), to = o.rate || RATES[0];
    if (o.fade !== false && ((o.start || 0) > 0 || (o.end || x.length) < x.length)) {   // trimmed inside the sound: no click
      p = Float32Array.from(p);
      const fi = (o.start || 0) > 0 ? Math.min(p.length >> 2, Math.round(srcRate * 0.001)) : 0;   // 1 ms in
      const fo = (o.end || x.length) < x.length ? Math.min(p.length >> 2, Math.round(srcRate * 0.002)) : 0;   // 2 ms out
      for (let i = 0; i < fi; i++) p[i] *= i / fi;
      for (let i = 0; i < fo; i++) p[p.length - 1 - i] *= i / fo;
    }
    const y = resample12(p, from, to);
    if (o.normalize === false) return y;
    let pk = 0;                                  // normalised as it comes out (the low-pass takes peaks off): once more
    for (const v of y) pk = Math.max(pk, Math.abs(v));
    return pk > 0 && pk < 1985 ? resample12(p.map((v) => v * 1985 / pk), from, to) : y;
  }
  function pack12(y) {                            // 2 samples in 3 bytes (firmware sp_pack / sp_sample)
    const out = new Uint8Array(bytesOf(y.length));
    for (let i = 0, o = 0; i < y.length; i += 2, o += 3) {
      const a = y[i] & 0xfff, b = i + 1 < y.length ? y[i + 1] & 0xfff : 0;
      out[o] = a & 255; out[o + 1] = (a >> 8) | ((b << 4) & 0xf0); out[o + 2] = b >> 4;
    }
    return out;
  }
  function unpack12(u8, n) {
    const y = new Int16Array(n);
    for (let i = 0; i < n; i++) {
      const p = (i >> 1) * 3, v = i & 1 ? (u8[p + 1] >> 4) | (u8[p + 2] << 4) : u8[p] | ((u8[p + 1] & 15) << 8);
      y[i] = (v ^ 0x800) - 0x800;
    }
    return y;
  }
  // what the FM-1 plays: the samples held, no interpolation, at its rate (x 33/45 at 33, half an octave down), as
  // 44.1 kHz floats
  function zoh(y, rate, slow, oct) {
    const step = Math.floor(Math.floor((slow ? Math.floor(rate * 33 / 45) : rate) * 65536 / 44100) * (oct ? 0.5 : 1));
    const out = new Float32Array(Math.floor(y.length * 65536 / step));
    let pos = 0, frac = 0;
    for (let i = 0; i < out.length && pos < y.length; i++) {
      out[i] = y[pos] / 2048;
      frac += step;
      while (frac >= 65536) { frac -= 65536; pos++; }
    }
    return out;
  }

  // a sample onto the FM-1 into a free slot: the sectors erased and written (read back by the FM-1), then the
  // directory; returns the slot. banks: sloopDX's bank room may be used (after a backup of it)
  async function upload(link, d, s, banks = false, progress = () => {}) {
    const slot = d.slots.findIndex((x) => !x);
    if (slot < 0) throw new Error(`all ${NSLOT} places are taken: delete one first`);
    const bytes = pack12(s.y), at = alloc(d, bytes.length, banks);
    if (at < 0) throw new Error(`not enough room: ${(bytes.length / 1024).toFixed(0)} KB wanted, ${(room(d, banks).longest * 1.5 / 1024).toFixed(0)} KB in one piece free`);
    const sectors = Math.ceil(bytes.length / SECT), steps = sectors * (1 + SECT / CHUNK);
    let done = 0;
    await link.hold();
    for (let k = 0; k < sectors; k++) {
      const a = at + k * SECT;
      await link.erase(a); progress(++done, steps);
      for (let o = 0; o < SECT; o += CHUNK) {
        const part = bytes.subarray(k * SECT + o, k * SECT + o + CHUNK);
        if (part.length && !part.every((b) => b === 0xff)) await link.write(a + o, [...part]);
        progress(++done, steps);
      }
    }
    d.slots[slot] = { off: at, n: s.y.length, rate: s.rate || RATES[0], flags: (s.slow ? 1 : 0) | (s.oct ? 2 : 0), name: cleanName(s.name) };
    await writeDir(link, d);
    return slot;
  }
  async function remove(link, d, slot) { d.slots[slot] = null; await writeDir(link, d); }
  async function rename(link, d, slot, name) { d.slots[slot].name = cleanName(name); await writeDir(link, d); }
  async function fetchSample(link, s) {           // its 12-bit values back from the FM-1 (to listen to, to save)
    const n = bytesOf(s.n), u8 = new Uint8Array(n);
    for (let o = 0; o < n; o += CHUNK) u8.set(await link.read(s.off + o, Math.min(CHUNK, n - o)), o);
    return unpack12(u8, s.n);
  }
  function cleanName(n) { return String(n || "").toUpperCase().replace(/[^ -~]/g, "").trim().slice(0, 8) || "SAMPLE"; }
  function wav(y, rate) {                          // 16-bit mono WAV of 12-bit values
    const b = new ArrayBuffer(44 + y.length * 2), v = new DataView(b), w = (o, s) => [...s].forEach((c, i) => v.setUint8(o + i, c.charCodeAt(0)));
    w(0, "RIFF"); v.setUint32(4, 36 + y.length * 2, true); w(8, "WAVEfmt "); v.setUint32(16, 16, true); v.setUint16(20, 1, true);
    v.setUint16(22, 1, true); v.setUint32(24, rate, true); v.setUint32(28, rate * 2, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true);
    w(36, "data"); v.setUint32(40, y.length * 2, true);
    y.forEach((x, i) => v.setInt16(44 + i * 2, x * 16, true));
    return new Uint8Array(b);
  }

  const playSecs = (n, rate, flags) => n / ((flags & 1 ? rate * 33 / 45 : rate) * (flags & 2 ? 0.5 : 1));   // as the FM-1 plays it
  const api = { CMD, PARTS, SECT, speedOf, playSecs, Link, pack7, unpack7, frame, parse, crc32, backup, readBackup, restore,
                NSLOT, ROOMS, BANKROOM, RATES, readDir, writeDir, parseDir, buildDir, room, alloc, resample12, encode, pack12,
                unpack12, zoh, upload, remove, rename, fetchSample, cleanName, wav, bytesOf };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else root.ZP12Link = api;
})(typeof self !== "undefined" ? self : this);
