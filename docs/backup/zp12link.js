// SPDX-License-Identifier: GPL-3.0-only  (C) 2026 Sven Trogus
// zp12's USB link for the web tools (firmware/src/sp_link.c): raw flash commands over SysEx, and on top of them
// the backup (a file of the flash zp12 uses, and optionally sloopDX's DX7 banks) and its restore.
// Used by web/backup.html in the browser and by tests/zp12_link_test.mjs in node.
"use strict";
(function (root) {
  const CMD = { HELLO: 1, READ: 2, ERASE: 3, WRITE: 4, HOLD: 5, REBOOT: 6 };
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
      const r = await this.request(CMD.HELLO);
      return { proto: r.data[0], version: String.fromCharCode(...r.data.slice(1)) };
    }
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

  const api = { CMD, PARTS, SECT, Link, pack7, unpack7, frame, parse, crc32, backup, readBackup, restore };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else root.ZP12Link = api;
})(typeof self !== "undefined" ? self : this);
