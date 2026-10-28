// SPDX-License-Identifier: GPL-3.0-or-later
// fm1-txtr — the engine (WebAssembly) running in an AudioWorklet.
//
// Messages from the page: {t:'set',id,raw} {t:'play',on} {t:'freeze',on} {t:'snap',s}
//   {t:'copy',from,to} {t:'note',n,v} {t:'panic'} {t:'save'} {t:'load',bytes} {t:'init'}
//   {t:'values'} {t:'midi',bytes}
// Messages to the page: {t:'ready',n} {t:'status',st,ov,eff} {t:'values',a,b}
//   {t:'saved',bytes,name} {t:'loaded',ok} {t:'midiout',bytes}

const STATUS_LEN = 16 + 10 + 2 * 32;
const OV_BINS = 480;

class TxtrProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const mod = new WebAssembly.Module(options.processorOptions.wasm);
    const imports = {};
    for (const im of WebAssembly.Module.imports(mod)) (imports[im.module] ??= {})[im.name] = () => 0;
    this.x = new WebAssembly.Instance(mod, imports).exports;
    this.x._initialize();
    this.x.txw_init(sampleRate, (Math.random() * 0xffffffff) >>> 0);
    this.n = this.x.txw_nparams();
    this.tick = 0;
    this.port.onmessage = (e) => this.onMessage(e.data);
    this.port.postMessage({ t: 'ready', n: this.n, fs: sampleRate });
  }

  mem() { return this.x.memory.buffer; }

  sendValues() {
    const a = new Int16Array(this.n), b = new Int16Array(this.n);
    for (let i = 0; i < this.n; i++) { a[i] = this.x.txw_get_snap(i, 0); b[i] = this.x.txw_get_snap(i, 1); }
    this.port.postMessage({ t: 'values', a, b });
  }

  onMessage(m) {
    const x = this.x;
    switch (m.t) {
      case 'set': x.txw_set(m.id, m.raw); break;
      case 'play': x.txw_play(m.on ? 1 : 0); break;
      case 'freeze': x.txw_freeze(m.on ? 1 : 0); break;
      case 'snap': x.txw_edit_snap(m.s); break;
      case 'copy': x.txw_copy_snap(m.from, m.to); this.sendValues(); break;
      case 'note': if (m.v) x.txw_note_on(m.n, m.v); else x.txw_note_off(m.n); break;
      case 'panic': x.txw_panic(); break;
      case 'init': x.txw_patch_init(); this.sendValues(); break;
      case 'values': this.sendValues(); break;
      case 'save': {
        const len = x.txw_patch_save();
        const bytes = new Uint8Array(this.mem(), x.txw_patch_buf(), len).slice();
        const nb = new Uint8Array(this.mem(), x.txw_patch_name(), 16);
        let name = '';
        for (const c of nb) { if (!c) break; name += String.fromCharCode(c); }
        this.port.postMessage({ t: 'saved', bytes, name });
        break;
      }
      case 'load': {
        const cap = x.txw_patch_cap();
        const src = new Uint8Array(m.bytes);
        const len = Math.min(src.length, cap);
        new Uint8Array(this.mem(), x.txw_patch_buf(), len).set(src.subarray(0, len));
        const ok = x.txw_patch_load(len) === 0;
        this.port.postMessage({ t: 'loaded', ok });
        if (ok) this.sendValues();
        break;
      }
      case 'midi': {
        const b = new Uint8Array(m.bytes);
        new Uint8Array(this.mem(), x.txw_midi_buf(), b.length).set(b);
        x.txw_midi_in(b.length);
        break;
      }
    }
  }

  process(inputs, outputs) {
    const out = outputs[0];
    const x = this.x;
    const frames = out[0].length;
    x.txw_render(frames);
    out[0].set(new Float32Array(this.mem(), x.txw_left(), frames));
    if (out[1]) out[1].set(new Float32Array(this.mem(), x.txw_right(), frames));

    const mo = x.txw_midi_out();
    if (mo > 0) this.port.postMessage({ t: 'midiout', bytes: new Uint8Array(this.mem(), x.txw_midi_buf(), mo).slice() });

    if (++this.tick >= 12) {                      // ~29 status frames a second at 44.1 kHz
      this.tick = 0;
      const st = new Float32Array(this.mem(), x.txw_status(), STATUS_LEN).slice();
      const ov = new Uint8Array(this.mem(), x.txw_overview(OV_BINS), OV_BINS).slice();
      const eff = new Float32Array(this.mem(), x.txw_eff(), this.n).slice();
      this.port.postMessage({ t: 'status', st, ov, eff });
    }
    return true;
  }
}

registerProcessor('txtr', TxtrProcessor);
