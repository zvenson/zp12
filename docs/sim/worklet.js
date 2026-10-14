// SPDX-License-Identifier: GPL-3.0-only
// zp12 in an AudioWorklet: zp12.wasm is the firmware (firmware/src, unchanged) on a simulated FM-1 (sim/sim.c).
// Each 128-frame render quantum runs the device's clock: the audio ISR's 128-frame halves come out of sim_render,
// the main loop runs once per millisecond of it. ~30 times a second the screen (when it changed), the LEDs and the
// bank go to the page; about once a second the store (the flash the firmware saves to), when it was written.
// After OberVave's web/sim/worklet.js, Makkuro37's (M. Geisler) and winampFM's (hericdk), GPL-3.0.
class Zp12 extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const o = options.processorOptions || {};
    this.ex = null;
    this.fbHash = -1;
    this.writes = 0;
    this.lastPost = 0;
    this.lastStore = 0;
    try {
      const mod = new WebAssembly.Module(o.wasm);
      this.ex = new WebAssembly.Instance(mod, {}).exports;
      const ex = this.ex, size = ex.sim_store_size();
      if (o.store && o.store.byteLength === size)            // the saved loops, sounds, songs, before boot
        new Uint8Array(ex.memory.buffer, ex.sim_store(), size).set(new Uint8Array(o.store));
      if (o.master !== undefined) ex.sim_master(o.master);
      if (ex.sim_init(44100) !== 0) throw new Error('sim_init failed');
      this.writes = ex.sim_store_writes();
      this.port.postMessage({ t: 'ready' });
    } catch (err) {
      this.ex = null;
      this.port.postMessage({ t: 'error', message: String((err && err.message) || err) });
    }
    this.port.onmessage = (e) => this.onMessage(e.data);
  }
  onMessage(m) {
    const ex = this.ex;
    if (!ex) return;
    if (m.t === 'key') ex.sim_key(m.k, m.down ? 1 : 0);
    else if (m.t === 'btn') ex.sim_button(m.b, m.down ? 1 : 0);
    else if (m.t === 'enc') ex.sim_enc(m.e, m.steps | 0);
    else if (m.t === 'midi') ex.sim_midi(m.d[0] | 0, m.d[1] | 0, m.d[2] | 0);
    else if (m.t === 'master') ex.sim_master(m.v | 0);
  }
  publish() {
    const ex = this.ex, tr = [];
    const msg = { t: 'frame', leds: ex.sim_leds_buttons(), keys: ex.sim_leds_keys(), glow: ex.sim_glow_keys(), bank: ex.sim_bank(), halted: ex.sim_halted() };
    const h = ex.sim_fb_hash();
    if (h !== this.fbHash) {
      this.fbHash = h;
      msg.fb = new Uint16Array(ex.memory.buffer, ex.sim_screen(), 240 * 240).slice();
      tr.push(msg.fb.buffer);
    }
    this.port.postMessage(msg, tr);
  }
  saveStore() {
    const ex = this.ex, w = ex.sim_store_writes();
    if (w === this.writes) return;
    this.writes = w;
    const data = new Uint8Array(ex.memory.buffer, ex.sim_store(), ex.sim_store_size()).slice();
    this.port.postMessage({ t: 'store', data: data.buffer }, [data.buffer]);
  }
  process(inputs, outputs) {
    const ex = this.ex;
    if (!ex) return true;
    const out = outputs[0], n = out[0].length;
    ex.sim_render(n);
    out[0].set(new Float32Array(ex.memory.buffer, ex.sim_out_l(), n));
    if (out[1]) out[1].set(new Float32Array(ex.memory.buffer, ex.sim_out_r(), n));
    if (currentTime - this.lastPost >= 1 / 30) {
      this.lastPost = currentTime;
      this.publish();
    }
    if (currentTime - this.lastStore >= 1) {
      this.lastStore = currentTime;
      this.saveStore();
    }
    return true;
  }
}
registerProcessor('zp12', Zp12);
