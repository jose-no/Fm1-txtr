// SPDX-License-Identifier: GPL-3.0-or-later
// fm1-txtr — the control page. Builds every control from web/gen/params.js (generated from
// params/params.py), runs the engine in an AudioWorklet and draws the live view.
//
// Today it drives the browser build of the engine. The same page will drive the FM-1 over
// USB (Web MIDI + SysEx) once the firmware speaks the protocol in docs/PROTOCOL.md.

import { TX } from './gen/params.js';

const P = TX.params;
const ID = Object.fromEntries(P.map((p, i) => [p.id, i]));
const MOD_SOURCES = ['LFO 1', 'LFO 2', 'LFO 3', 'S&H', 'Walk', 'Follower', 'Note', 'Velocity', 'Drift'];
const BIPOLAR_SOURCES = 5;                      // the first five swing both ways

const vals = [new Int16Array(P.length), new Int16Array(P.length)];
P.forEach((p, i) => { vals[0][i] = vals[1][i] = p.rdef; });
let edit = 0;                                   // snapshot being edited
let node = null, ctx = null;
let eff = null, status = null, overview = null;
const ctls = new Array(P.length);

// ------------------------------------------------------------ values and formatting

const norm = (p, raw) => (p.rmax === p.rmin ? 0 : (raw - p.rmin) / (p.rmax - p.rmin));
const rawOf = (p, n) => Math.round(p.rmin + Math.min(1, Math.max(0, n)) * (p.rmax - p.rmin));
const labelsOf = (p) => (p.labels === '@targets' ? TX.targetLabels : p.labels);

function phys(p, n) {
  if (p.curve === 'lin') return p.lo + n * (p.hi - p.lo);
  if (p.curve === 'exp') return p.lo * Math.pow(p.hi / p.lo, n);
  return rawOf(p, n);
}

function fmt(p, raw) {
  if (p.curve === 'enum') return labelsOf(p)[raw] ?? String(raw);
  if (p.id.endsWith('_ratio')) return raw === 0 ? '0.5' : String(raw);
  if (p.id === 'note_custom') return maskNames(raw);
  const v = phys(p, norm(p, raw));
  switch (p.unit) {
    case 'Hz': return v >= 1000 ? `${(v / 1000).toFixed(v >= 10000 ? 0 : 1)} kHz` : `${v.toFixed(v < 10 ? 2 : v < 100 ? 1 : 0)} Hz`;
    case 'ms': return v >= 1000 ? `${(v / 1000).toFixed(2)} s` : `${v.toFixed(v < 10 ? 1 : 0)} ms`;
    case 's': return `${v.toFixed(v < 10 ? 2 : 1)} s`;
    case '%': return `${Math.round(v)}%`;
    case '/s': return `${v.toFixed(v < 10 ? 1 : 0)} per s`;
    case '': return String(Math.round(v));
    default: return `${Math.round(v)} ${p.unit}`;
  }
}

const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
function maskNames(m) { return NOTE_NAMES.filter((_, i) => m & (1 << i)).join(' '); }

function baseNorm(i) {
  const p = P[i];
  const a = norm(p, vals[0][i]);
  if (!p.morph || vals[0][i] === vals[1][i]) return a;
  const m = norm(P[ID.morph], vals[0][ID.morph]);
  const b = norm(p, vals[1][i]);
  return p.curve === 'enum' ? (m < 0.5 ? a : b) : a + (b - a) * m;
}

// ------------------------------------------------------------ talking to the engine

function post(msg) { if (node) node.port.postMessage(msg); }

function setRaw(i, raw) {
  const p = P[i];
  raw = Math.min(p.rmax, Math.max(p.rmin, Math.round(raw)));
  if (!p.morph) { vals[0][i] = vals[1][i] = raw; } else { vals[edit][i] = raw; }
  post({ t: 'set', id: i, raw });
  refresh(i);
  if (i === ID.morph) P.forEach((_, k) => refreshMod(k));
}

function pushAll() {
  for (const s of [0, 1]) {
    post({ t: 'snap', s });
    P.forEach((_, i) => post({ t: 'set', id: i, raw: vals[s][i] }));
  }
  post({ t: 'snap', s: edit });
}

// ------------------------------------------------------------ controls

function makeCtl(i, { showName = true } = {}) {
  const p = P[i];
  const el = document.createElement('div');
  el.className = 'ctl';
  const name = document.createElement('div');
  name.className = 'name';
  name.textContent = p.name;
  const val = document.createElement('div');
  val.className = 'val';
  if (showName) el.append(name);
  el.append(val);
  const c = { el, val, p };

  if (p.id === 'note_custom') {
    const row = document.createElement('div');
    row.className = 'mask';
    row.setAttribute('role', 'group');
    row.setAttribute('aria-label', 'Notes in the custom scale');
    c.toggles = NOTE_NAMES.map((nm, k) => {
      const b = document.createElement('button');
      b.textContent = nm;
      b.onclick = () => setRaw(i, (vals[edit][i] ^ (1 << k)) | 1);
      row.append(b);
      return b;
    });
    el.append(row);
    el.style.gridColumn = '1 / -1';
  } else if (p.curve === 'enum') {
    const sel = document.createElement('select');
    sel.setAttribute('aria-label', p.name);
    labelsOf(p).forEach((l, k) => sel.add(new Option(l, k)));
    sel.onchange = () => setRaw(i, +sel.value);
    el.append(sel);
    c.sel = sel;
    val.hidden = true;
  } else {
    const bar = document.createElement('div');
    bar.className = 'bar' + (p.curve !== 'exp' && p.lo < 0 && p.hi > 0 ? ' bip' : '');
    bar.tabIndex = 0;
    bar.setAttribute('role', 'slider');
    bar.setAttribute('aria-label', p.name);
    const fill = document.createElement('div'); fill.className = 'fill';
    const mod = document.createElement('div'); mod.className = 'mod';
    bar.append(fill, mod);
    el.append(bar);
    Object.assign(c, { bar, fill, mod });
    attachDrag(i, bar);
  }
  ctls[i] = c;
  refresh(i);
  return el;
}

function attachDrag(i, bar) {
  const p = P[i];
  let start = null;
  bar.addEventListener('pointerdown', (e) => {
    bar.setPointerCapture(e.pointerId);
    start = { x: e.clientX, n: norm(p, vals[edit][i]), moved: false };
  });
  bar.addEventListener('pointermove', (e) => {
    if (!start) return;
    const dx = e.clientX - start.x;
    if (Math.abs(dx) > 2) start.moved = true;
    if (!start.moved) return;
    const w = bar.clientWidth * (e.shiftKey ? 6 : 1);    // shift = fine
    setRaw(i, rawOf(p, start.n + dx / w));
  });
  bar.addEventListener('pointerup', (e) => {
    if (start && !start.moved) {
      const r = bar.getBoundingClientRect();
      setRaw(i, rawOf(p, (e.clientX - r.left) / r.width));
    }
    start = null;
  });
  bar.addEventListener('dblclick', () => setRaw(i, p.rdef));
  bar.addEventListener('keydown', (e) => {
    const step = p.curve === 'int' ? 1 : e.shiftKey ? 1 : 10;
    if (e.key === 'ArrowRight' || e.key === 'ArrowUp') { setRaw(i, vals[edit][i] + step); e.preventDefault(); }
    if (e.key === 'ArrowLeft' || e.key === 'ArrowDown') { setRaw(i, vals[edit][i] - step); e.preventDefault(); }
  });
}

function refresh(i) {
  const c = ctls[i];
  if (!c) return;
  const p = P[i], raw = vals[edit][i];
  c.val.textContent = fmt(p, raw);
  c.el.classList.toggle('b-diff', p.morph && vals[0][i] !== vals[1][i]);
  if (c.sel) c.sel.value = raw;
  if (c.toggles) c.toggles.forEach((b, k) => b.setAttribute('aria-pressed', String(!!(raw & (1 << k)))));
  if (c.bar) {
    const n = norm(p, raw);
    if (c.bar.classList.contains('bip')) {
      const lo = Math.min(n, 0.5), hi = Math.max(n, 0.5);
      c.fill.style.left = `${lo * 100}%`; c.fill.style.width = `${(hi - lo) * 100}%`;
    } else {
      c.fill.style.left = '0'; c.fill.style.width = `${n * 100}%`;
    }
    c.bar.setAttribute('aria-valuetext', c.val.textContent);
  }
  if (i === ID.note_root) refreshRoots();
}

function refreshMod(i) {
  const c = ctls[i];
  if (!c || !c.bar || !eff) return;
  const e = eff[i], b = baseNorm(i);
  const on = Math.abs(e - b) > 0.003;
  c.bar.classList.toggle('modded', on);
  if (on) c.mod.style.left = `calc(${e * 100}% - 1px)`;
}

// ------------------------------------------------------------ pages

const OP_SUBS = ['Operator', 'Op rates', 'Op levels', 'Op extras'];
// the FM-1 screen fits 10 characters; the browser can spell the rows out
const OP_ROW_NAMES = { ratio: 'Ratio', fine: 'Fine', level: 'Level', detune: 'Detune', r1: 'Rate 1', r2: 'Rate 2',
  r3: 'Rate 3', r4: 'Rate 4', l1: 'Level 1', l2: 'Level 2', l3: 'Level 3', l4: 'Level 4', vel: 'Vel sens',
  krs: 'Key rate', ams: 'Amp mod', on: 'On' };

function buildPages() {
  const root = document.getElementById('pages');
  for (const page of TX.pages) {
    const sec = document.createElement('section');
    sec.className = 'page' + (page === 'SOUND' ? ' wide' : page === 'MOD' ? ' double' : '');
    const h = document.createElement('h2');
    h.textContent = page.charAt(0) + page.slice(1).toLowerCase();
    sec.append(h);
    const subs = TX.subs.filter((s) => s.page === page).map((s) => s.name);
    let opsDone = false;
    for (const sub of subs) {
      if (OP_SUBS.includes(sub)) { if (!opsDone) { sec.append(buildOps()); opsDone = true; } continue; }
      if (sub === 'Matrix') { sec.append(buildMatrix()); continue; }
      sec.append(buildSub(page, sub));
    }
    root.append(sec);
  }
}

function buildSub(page, sub) {
  const d = document.createElement('div');
  d.className = 'sub';
  const h = document.createElement('h3');
  h.textContent = sub;
  const k = document.createElement('div');
  k.className = 'knobs';
  P.forEach((p, i) => { if (p.page === page && p.sub === sub) k.append(makeCtl(i)); });
  d.append(h, k);
  return d;
}

function buildOps() {
  const d = document.createElement('div');
  d.className = 'sub ops-sub';
  const h = document.createElement('h3');
  h.textContent = 'Operators';
  const t = document.createElement('table');
  t.className = 'ops';
  const head = t.insertRow();
  head.insertCell().outerHTML = '<th></th>';
  for (let n = 1; n <= 6; n++) head.insertCell().outerHTML = `<th scope="col">Op ${n}</th>`;
  for (const sub of OP_SUBS) {
    for (let knob = 0; knob < 4; knob++) {
      const row = t.insertRow();
      const first = P.find((p) => p.sub === sub && p.knob === knob && p.op === 1);
      const th = document.createElement('th');
      th.scope = 'row';
      th.textContent = OP_ROW_NAMES[first.id.replace(/^op1_/, '')] ?? first.name.replace(/^Op1 /, '');
      row.append(th);
      for (let n = 1; n <= 6; n++) {
        const i = P.findIndex((p) => p.sub === sub && p.knob === knob && p.op === n);
        row.insertCell().append(makeCtl(i, { showName: false }));
      }
    }
  }
  d.append(h, t);
  return d;
}

function buildMatrix() {
  const d = document.createElement('div');
  d.className = 'sub';
  const h = document.createElement('h3');
  h.textContent = 'Matrix';
  d.append(h);
  for (let n = 1; n <= 8; n++) {
    const k = document.createElement('div');
    k.className = 'knobs';
    k.style.marginBottom = '6px';
    for (const f of ['src', 'dst', 'amt', 'via']) k.append(makeCtl(ID[`mx${n}_${f}`]));
    d.append(k);
  }
  return d;
}

// ------------------------------------------------------------ live view

const roots = document.getElementById('roots');
NOTE_NAMES.forEach((nm, k) => {
  const b = document.createElement('button');
  b.textContent = nm;
  b.title = `Root ${nm}`;
  b.onclick = () => setRaw(ID.note_root, k);
  roots.append(b);
});
function refreshRoots() {
  [...roots.children].forEach((b, k) => b.setAttribute('aria-pressed', String(vals[edit][ID.note_root] === k)));
}

const mods = document.getElementById('mods');
const modBars = MOD_SOURCES.map((nm) => {
  const s = document.createElement('span');
  const bar = document.createElement('i');
  const fill = document.createElement('b');
  bar.append(fill);
  s.append(nm, bar);
  mods.append(s);
  return fill;
});

const cloud = document.getElementById('cloud');
const ring = document.getElementById('ring');
const css = (v) => getComputedStyle(document.documentElement).getPropertyValue(v).trim();

function drawCloud() {
  const dpr = window.devicePixelRatio || 1;
  const W = cloud.clientWidth, H = cloud.clientHeight;
  if (cloud.width !== W * dpr) { cloud.width = W * dpr; cloud.height = H * dpr; }
  const g = cloud.getContext('2d');
  g.setTransform(dpr, 0, 0, dpr, 0, 0);
  g.clearRect(0, 0, W, H);
  const mid = H / 2;
  if (overview) {
    g.fillStyle = '#3b4b5b';
    const n = overview.length, bw = W / n;
    for (let b = 0; b < n; b++) {
      const a = (overview[b] / 255) * (H * 0.46);
      g.fillRect(b * bw, mid - a, Math.max(1, bw - 0.4), a * 2 || 1);
    }
  }
  if (!status) {
    g.fillStyle = '#aab6c2';
    g.font = `15px ${css('--font')}`;
    g.fillText('Start audio to hear it. The buffer and its grains appear here.', 16, mid + 5);
    return;
  }
  const frozen = status[1] > 0;
  const hx = status[6] * W;
  g.fillStyle = css('--hot');
  g.fillRect(hx - 1, 0, 2, H);
  const ng = status[7];
  for (let k = 0; k < Math.min(ng, 32); k++) {
    const x = status[26 + k] * W;
    const amp = status[58 + k];
    const y = mid + (((k * 37) % 17) - 8) / 8 * H * 0.32;
    g.beginPath();
    g.fillStyle = `rgba(47, 107, 255, ${0.35 + Math.min(0.65, amp)})`;
    g.arc(x, y, 2.5 + Math.min(1, amp) * 7, 0, Math.PI * 2);
    g.fill();
  }
  if (frozen) {
    g.fillStyle = css('--hot');
    g.font = `600 14px ${css('--font')}`;
    g.fillText('Frozen', 12, 22);
  }
}

function drawRing() {
  const g = ring.getContext('2d');
  const W = ring.width, c = W / 2, r = W / 2 - 14;
  g.clearRect(0, 0, W, W);
  if (!status) return;
  const steps = status[3] || 16;
  const pat = (status[4] | 0) | ((status[5] | 0) << 16);
  const cur = status[2];
  for (let s = 0; s < steps; s++) {
    const a = -Math.PI / 2 + (s / steps) * Math.PI * 2;
    const x = c + Math.cos(a) * r, y = c + Math.sin(a) * r;
    const hit = (pat >>> s) & 1;
    g.beginPath();
    g.arc(x, y, hit ? 6 : 3, 0, Math.PI * 2);
    g.fillStyle = s === cur && status[0] ? css('--signal') : hit ? '#eef1f3' : '#4a5a69';
    g.fill();
  }
  g.fillStyle = '#aab6c2';
  g.font = `13px ${css('--font')}`;
  g.textAlign = 'center';
  const hits = P[ID.seq_density] ? vals[edit][ID.seq_density] : 0;
  g.fillText(`${Math.min(hits, steps)} of ${steps}`, c, c + 4);
}

function frame() {
  drawCloud();
  drawRing();
  if (status) {
    document.getElementById('r-bpm').textContent = `${Math.round(status[12])} bpm`;
    document.getElementById('r-grains').textContent = String(status[7]);
    document.getElementById('r-voices').textContent = String(status[8]);
    document.getElementById('r-level').value = status[11];
    MOD_SOURCES.forEach((_, k) => {
      const v = status[17 + k];
      const f = modBars[k];
      if (k < BIPOLAR_SOURCES) {
        const lo = Math.min(v, 0), hi = Math.max(v, 0);
        f.style.left = `${(lo + 1) * 50}%`; f.style.width = `${(hi - lo) * 50}%`;
      } else {
        f.style.left = '0'; f.style.width = `${Math.min(1, Math.max(0, v)) * 100}%`;
      }
    });
    const playing = status[0] > 0, frozen = status[1] > 0;
    playBtn.setAttribute('aria-pressed', String(playing));
    playBtn.textContent = playing ? 'Stop' : 'Play';
    freezeBtn.setAttribute('aria-pressed', String(frozen));
  }
  requestAnimationFrame(frame);
}

// ------------------------------------------------------------ header controls

const startBtn = document.getElementById('start');
const playBtn = document.getElementById('play');
const freezeBtn = document.getElementById('freeze');
const panicBtn = document.getElementById('panic');

startBtn.onclick = async () => {
  startBtn.disabled = true;
  startBtn.textContent = 'Starting…';
  try {
    ctx = new AudioContext({ latencyHint: 'playback' });
    const [wasm] = await Promise.all([
      fetch('txtr.wasm').then((r) => { if (!r.ok) throw new Error('txtr.wasm not found: run make web'); return r.arrayBuffer(); }),
      ctx.audioWorklet.addModule('worklet.js'),
    ]);
    node = new AudioWorkletNode(ctx, 'txtr', { numberOfOutputs: 1, outputChannelCount: [2], processorOptions: { wasm } });
    node.port.onmessage = (e) => onEngine(e.data);
    node.connect(ctx.destination);
    await ctx.resume();
  } catch (err) {
    startBtn.disabled = false;
    startBtn.textContent = 'Start audio';
    document.getElementById('engine-note').textContent = `Couldn't start the engine: ${err.message}`;
  }
};

function onEngine(m) {
  switch (m.t) {
    case 'ready':
      pushAll();
      startBtn.hidden = true;
      for (const b of [playBtn, freezeBtn, panicBtn]) b.disabled = false;
      break;
    case 'status':
      status = m.st; overview = m.ov; eff = m.eff;
      P.forEach((_, i) => refreshMod(i));
      break;
    case 'values':
      vals[0].set(m.a); vals[1].set(m.b);
      P.forEach((_, i) => refresh(i));
      break;
    case 'saved': {
      const a = document.createElement('a');
      const t = new Date();
      const stamp = `${t.getFullYear()}${String(t.getMonth() + 1).padStart(2, '0')}${String(t.getDate()).padStart(2, '0')}-${String(t.getHours()).padStart(2, '0')}${String(t.getMinutes()).padStart(2, '0')}`;
      a.href = URL.createObjectURL(new Blob([m.bytes], { type: 'application/octet-stream' }));
      a.download = `txtr-${stamp}.txtr`;
      a.click();
      URL.revokeObjectURL(a.href);
      break;
    }
    case 'loaded':
      document.getElementById('engine-note').textContent = m.ok ? 'Patch loaded.' : "That file isn't an fm1-txtr patch.";
      break;
    case 'midiout':
      if (midiOuts.length) for (let k = 0; k < m.bytes.length; k += 3) for (const o of midiOuts) o.send(m.bytes.subarray(k, k + 3));
      break;
  }
}

playBtn.onclick = () => post({ t: 'play', on: !(status && status[0] > 0) });
freezeBtn.onclick = () => {
  const on = !(status && status[1] > 0);
  vals[0][ID.buf_freeze] = vals[1][ID.buf_freeze] = on ? 1 : 0;
  refresh(ID.buf_freeze);
  post({ t: 'freeze', on });
};
panicBtn.onclick = () => post({ t: 'panic' });

document.querySelectorAll('[data-snap]').forEach((b) => {
  b.onclick = () => {
    edit = +b.dataset.snap;
    document.querySelectorAll('[data-snap]').forEach((x) => x.setAttribute('aria-pressed', String(x === b)));
    post({ t: 'snap', s: edit });
    P.forEach((_, i) => refresh(i));
  };
});
document.getElementById('copy-ab').onclick = () => { vals[1].set(vals[0]); post({ t: 'copy', from: 0, to: 1 }); P.forEach((_, i) => refresh(i)); };
document.getElementById('copy-ba').onclick = () => { vals[0].set(vals[1]); post({ t: 'copy', from: 1, to: 0 }); P.forEach((_, i) => refresh(i)); };

document.getElementById('save').onclick = () => {
  if (!node) { document.getElementById('engine-note').textContent = 'Start audio first, then save.'; return; }
  post({ t: 'save' });
};
document.getElementById('load').onchange = async (e) => {
  const f = e.target.files[0];
  if (!f) return;
  if (!node) { document.getElementById('engine-note').textContent = 'Start audio first, then load the patch.'; return; }
  post({ t: 'load', bytes: await f.arrayBuffer() });
  e.target.value = '';
};
document.getElementById('init').onclick = () => {
  P.forEach((p, i) => { vals[0][i] = vals[1][i] = p.rdef; refresh(i); });
  post({ t: 'init' });
};

// ------------------------------------------------------------ MIDI

let midiOuts = [];
document.getElementById('midi').onclick = async (ev) => {
  const btn = ev.currentTarget;
  if (!navigator.requestMIDIAccess) { btn.textContent = 'No Web MIDI here'; btn.disabled = true; return; }
  try {
    const access = await navigator.requestMIDIAccess();
    const wire = () => {
      midiOuts = [...access.outputs.values()];
      for (const inp of access.inputs.values()) {
        inp.onmidimessage = (m) => {
          const s = m.data[0];
          if ((s & 0xe0) === 0x80 || s === 0xf8 || s === 0xfa || s === 0xfb || s === 0xfc) post({ t: 'midi', bytes: m.data });
        };
      }
      btn.textContent = `MIDI: ${access.inputs.size} in, ${access.outputs.size} out`;
      btn.setAttribute('aria-pressed', 'true');
    };
    access.onstatechange = wire;
    wire();
  } catch {
    btn.textContent = 'MIDI blocked';
  }
};

buildPages();
refreshRoots();
requestAnimationFrame(frame);
