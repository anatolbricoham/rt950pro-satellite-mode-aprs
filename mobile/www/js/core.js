/*
 * core.js - RT-950 / RT-950 Pro codeplug and programming protocol (JavaScript)
 *
 * Port of pc/rt950_toolkit (codeplug.py, protocol.py, country.py, csvio.py)
 * for the web and Android programmer. Field tables come from data/meta.json,
 * exported from the Python code (tools/export_web_meta.py), and the .rt950
 * file format is the same, so files move freely between the desktop and the
 * phone. Cross-checked against the Python implementation by
 * tests/web/core.test.cjs.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
(function (root) {
"use strict";

let META = null;
const CH = 990, ZONES = 10, PER_ZONE = 99;
const ZONE_ADDR = 0xC000, ZONE_STRIDE = 16;
const DTMF_CHARS = "0123456789ABCD*#";
const HANDSHAKE = "PROGRAMBT9000U";
const FACTORY_CHALLENGE = hex("53454E4411100F0603011302130E060C0D0C1204110D0B0E00");
const KEYS = ["BHT ", "CO 7", "A ES", " EIY", "M PQ", "XN Y", "RVB ", " HQP", "W RC", "MS N",
              " SAT", "K DH", "ZO R", "C SL", "6RB ", " JCG", "PN V", "J PK", "EK L", "I LZ"];

function hex(s) { return Uint8Array.from(s.match(/../g).map(h => parseInt(h, 16))); }
function ascii(s) { return Uint8Array.from([...s].map(c => c.charCodeAt(0) & 0xff)); }
function setMeta(m) { META = m; }
function regions() { return META.regions; }

/* ------------------------------------------------------------- encodings */

function decodeTone(b0, b1) {
  if ((b0 === 0 && b1 === 0) || (b0 === 0xff && b1 === 0xff)) return "OFF";
  const dcs = META.dcs;
  if (b1 === 0) {
    if (b0 >= 1 && b0 <= dcs.length) return "D" + dcs[b0 - 1] + "N";
    if (b0 > dcs.length && b0 <= 2 * dcs.length) return "D" + dcs[b0 - 1 - dcs.length] + "I";
    return "OFF";
  }
  return ((b0 | (b1 << 8)) / 10).toFixed(1);
}

function encodeTone(t) {
  t = String(t || "OFF").trim().toUpperCase();
  if (!t || t === "OFF" || t === "NONE") return [0, 0];
  if (t[0] === "D" && t.length === 5) {
    const i = META.dcs.indexOf(t.slice(1, 4));
    if (i < 0) throw new Error("bad DCS code " + t);
    return [1 + i + (t[4] === "I" ? META.dcs.length : 0), 0];
  }
  const v = Math.round(parseFloat(t) * 10);
  if (!(v > 0 && v <= 0xffff)) throw new Error("bad CTCSS tone " + t);
  return [v & 0xff, v >> 8];
}

function bcdToHz(b) {
  if (b.every(x => x === 0xff) || b.every(x => x === 0)) return null;
  let v = 0;
  for (let i = 3; i >= 0; i--) {
    const hi = b[i] >> 4, lo = b[i] & 15;
    if (hi > 9 || lo > 9) return null;
    v = v * 100 + hi * 10 + lo;
  }
  return v * 10;
}

function hzToBcd(hz) {
  if (hz == null) return [0xff, 0xff, 0xff, 0xff];
  let v = Math.round(hz / 10);
  if (v < 0 || v > 99999999) throw new Error("frequency out of range");
  const out = [];
  for (let i = 0; i < 4; i++) { const two = v % 100; v = Math.floor(v / 100); out.push(((two / 10 | 0) << 4) | (two % 10)); }
  return out;
}

function digitsToHz(b, intDigits = 3) {
  if (b.some(x => x > 9)) return null;
  const s = Array.from(b).join("");
  return Math.round(parseFloat(s.slice(0, intDigits) + "." + s.slice(intDigits)) * 1e6);
}

function hzToDigits(hz, n, intDigits = 3) {
  const frac = n - intDigits;
  const s = String(Math.round(hz / Math.pow(10, 6 - frac))).padStart(n, "0");
  if (s.length > n) throw new Error("frequency out of range");
  return [...s].map(Number);
}

let GBK = null;
function textGet(b) {
  const out = [];
  for (const x of b) { if (x === 0xff || x === 0) break; out.push(x); }
  try { GBK = GBK || new TextDecoder("gbk"); return GBK.decode(Uint8Array.from(out)).trimEnd(); }
  catch (e) { return String.fromCharCode(...out).trimEnd(); }
}

function textPut(s, n, pad = 0xff) {
  const raw = [];
  for (const c of String(s)) { const k = c.charCodeAt(0); raw.push(k >= 0x20 && k < 0x7f ? k : 0x3f); }
  const out = raw.slice(0, n);
  while (out.length < n) out.push(pad);
  return out;
}

/* --------------------------------------------------------------- codeplug */

class Channel {
  constructor(index, o = {}) {
    Object.assign(this, {index, rx: null, tx: null, rxTone: "OFF", txTone: "OFF", power: "High",
      bandwidth: "Wide", scramble: "OFF", busyLock: "OFF", scanAdd: "ON", encryption: "OFF",
      txEnable: "ON", rxAm: "FM", signalCode: 1, pttId: "OFF", name: ""}, o);
  }
  get empty() { return this.rx == null; }
  get zone() { return Math.floor(this.index / PER_ZONE); }
  get number() { return this.index + 1; }
}

function decodeChannel(idx, r) {
  const c = new Channel(idx);
  c.rx = bcdToHz(r.slice(0, 4));
  if (c.rx == null) return c;
  c.tx = bcdToHz(r.slice(4, 8));
  c.rxTone = decodeTone(r[8], r[9]);
  c.txTone = decodeTone(r[10], r[11]);
  c.signalCode = r[12] < 16 ? r[12] + 1 : 1;
  c.pttId = META.ptt_id[r[13]] || "OFF";
  const p = r[14] & 15; c.power = META.power[p] || "High";
  c.scramble = META.scramble[r[14] >> 4] || "OFF";
  const f = r[15];
  c.rxAm = f & 1 ? "AM" : "FM";
  c.txEnable = f & 2 ? "ON" : "OFF";
  c.scanAdd = f & 4 ? "ON" : "OFF";
  c.busyLock = f & 8 ? "ON" : "OFF";
  c.encryption = META.encryption[(f >> 4) & 3];
  c.bandwidth = META.bandwidth[(f >> 6) & 1];
  c.name = textGet(r.slice(20, 32));
  return c;
}

function encodeChannel(c, old) {
  if (c.empty) return new Array(32).fill(0xff);
  const fresh = !old || old[0] === 0xff;
  const r = fresh ? [...new Array(16).fill(0), ...new Array(16).fill(0xff)] : Array.from(old);
  r.splice(0, 4, ...hzToBcd(c.rx));
  r.splice(4, 4, ...hzToBcd(c.tx != null ? c.tx : c.rx));
  r.splice(8, 2, ...encodeTone(c.rxTone));
  r.splice(10, 2, ...encodeTone(c.txTone));
  r[12] = Math.max(0, Math.min(15, (c.signalCode | 0) - 1));
  r[13] = Math.max(0, META.ptt_id.indexOf(c.pttId));
  const sc = Math.max(0, META.scramble.indexOf(c.scramble));
  r[14] = Math.max(0, META.power.indexOf(c.power)) | (sc << 4);
  let f = fresh ? 0 : r[15] & 0x80;
  f |= c.rxAm === "AM" ? 1 : 0;
  f |= (c.txEnable === "ON" ? 1 : 0) << 1;
  f |= (c.scanAdd === "ON" ? 1 : 0) << 2;
  f |= (c.busyLock === "ON" ? 1 : 0) << 3;
  f |= Math.max(0, META.encryption.indexOf(c.encryption)) << 4;
  f |= (c.bandwidth === "Narrow" ? 1 : 0) << 6;
  r[15] = f;
  if (fresh) r.splice(16, 4, 0xff, 0xff, 0xff, 0xff);
  r.splice(20, 12, ...textPut(c.name, 12));
  return r;
}

function b64(u8) {
  if (typeof Buffer !== "undefined") return Buffer.from(u8).toString("base64");
  let s = ""; for (const b of u8) s += String.fromCharCode(b); return btoa(s);
}
function unb64(s) {
  if (typeof Buffer !== "undefined") return new Uint8Array(Buffer.from(s, "base64"));
  return Uint8Array.from(atob(s), c => c.charCodeAt(0));
}

class Codeplug {
  constructor() {
    this.mem = {};
    for (const r of regions()) this.mem[r.name] = new Uint8Array(r.size).fill(0xff);
    this.valid = new Set(["channels"]);
    this.touched = new Set();
    this.meta = {model: "RT-950 Pro", source: "new"};
  }
  markValid() { this.valid = new Set(regions().map(r => r.name)); }
  loc(addr) {
    for (const r of regions()) if (addr >= r.addr && addr < r.addr + r.size) return [r.name, addr - r.addr];
    throw new Error("address 0x" + addr.toString(16) + " outside codeplug");
  }
  peek(a) { const [n, o] = this.loc(a); return this.mem[n][o]; }
  poke(a, v) { const [n, o] = this.loc(a); this.touched.add(a); this.mem[n][o] = v; }
  read(a, n) { const out = []; for (let i = 0; i < n; i++) out.push(this.peek(a + i)); return out; }
  write(a, d) { d.forEach((b, i) => this.poke(a + i, b)); }
  channel(i) { return decodeChannel(i, Array.from(this.mem.channels.slice(i * 32, i * 32 + 32))); }
  channels() { const o = []; for (let i = 0; i < CH; i++) o.push(this.channel(i)); return o; }
  setChannel(c) { this.write(c.index * 32, encodeChannel(c, Array.from(this.mem.channels.slice(c.index * 32, c.index * 32 + 32)))); }
  clearChannel(i) { this.write(i * 32, new Array(32).fill(0xff)); }
  zoneName(z) { return textGet(this.read(ZONE_ADDR + z * ZONE_STRIDE, ZONE_STRIDE)); }
  setZoneName(z, s) { this.write(ZONE_ADDR + z * ZONE_STRIDE, textPut(s, ZONE_STRIDE)); }
  vfoFreq(v) { return digitsToHz(this.read(0x8000 + 0x20 * v, 8)); }
  setVfoFreq(v, hz) { this.write(0x8000 + 0x20 * v, hzToDigits(hz, 8)); }
  fieldGet(f) {
    const b = this.peek(f.addr);
    if (!f.positions.length) return b;
    let v = 0; f.positions.forEach((p, i) => { v |= ((b >> p) & 1) << i; }); return v;
  }
  fieldSet(f, value) {
    if (!f.positions.length) { this.poke(f.addr, value & 0xff); return; }
    let b = this.peek(f.addr);
    f.positions.forEach((p, i) => { b = (value >> i) & 1 ? b | (1 << p) : b & ~(1 << p) & 0xff; });
    this.poke(f.addr, b);
  }
  textValue(t) { return textGet(this.read(t.addr, t.length)); }
  setTextValue(t, s) { this.write(t.addr, textPut(t.upper ? String(s).toUpperCase() : s, t.length)); }
  aprsCall() {
    const call = this.textValue({addr: 0x1000F + 1, length: 6});
    const ssid = this.peek(0x1000F + 7);
    return [call, ssid < 16 ? ssid : 0];
  }
  mergedRegion(name, base) {
    const r = regions().find(x => x.name === name), out = Uint8Array.from(base);
    for (const a of this.touched) { const o = a - r.addr; if (o >= 0 && o < out.length) out[o] = this.mem[name][o]; }
    return out;
  }
  toJSON() {
    const partial = regions().filter(r => !this.valid.has(r.name));
    const touched = [...this.touched].filter(t => partial.some(r => t >= r.addr && t < r.addr + r.size)).sort((a, b) => a - b);
    const out = {format: "rt950-toolkit-codeplug", version: 1,
                 meta: Object.assign({}, this.meta, {valid_regions: [...this.valid].sort(), touched}), regions: {}};
    for (const r of regions()) out.regions[r.name] = {addr: r.addr, data: b64(this.mem[r.name])};
    return out;
  }
  static fromJSON(d) {
    if (d.format !== "rt950-toolkit-codeplug") throw new Error("not an RT-950 Toolkit codeplug file");
    const cp = new Codeplug();
    cp.meta = Object.assign({}, d.meta || {});
    const vr = cp.meta.valid_regions; delete cp.meta.valid_regions;
    cp.touched = new Set(cp.meta.touched || []); delete cp.meta.touched;
    cp.valid = new Set(vr != null ? vr : regions().map(r => r.name));
    for (const r of regions()) if (d.regions[r.name]) cp.mem[r.name].set(unb64(d.regions[r.name].data).slice(0, r.size));
    return cp;
  }
}

/* --------------------------------------------------------------- protocol */

function keyForChallenge(p) {
  const sel = p[4];
  const idx = ((sel & 0x20) ? (sel - 0x20) * 2 + 1 : (sel - 0x10) * 2) + 1;
  return ascii(KEYS[p[4 + idx]]);
}

function xorCrypt(data, key) {
  const out = new Uint8Array(data.length);
  for (let i = 0; i < data.length; i++) {
    const b = data[i], k = key[i % 4];
    out[i] = (k === 0x20 || b === 0 || b === 0xff || b === k || b === (k ^ 0xff)) ? b : b ^ k;
  }
  return out;
}

class ByteQueue {
  constructor() { this.buf = []; this.waiters = []; }
  push(chunk) { for (const b of chunk) this.buf.push(b); this.waiters.splice(0).forEach(w => w()); }
  clear() { this.buf.length = 0; }
  async take(n, timeout) {
    const t0 = Date.now();
    while (this.buf.length < n) {
      const left = timeout - (Date.now() - t0);
      if (left <= 0) throw new Error(`radio timeout (${this.buf.length}/${n} bytes)`);
      await new Promise(r => { this.waiters.push(r); setTimeout(r, Math.min(left, 25)); });
    }
    return Uint8Array.from(this.buf.splice(0, n));
  }
}

/** OEM programming link over any transport {queue: ByteQueue, write(u8), prepare?()}.
 *  Works over the USB cable (Web Serial) and Bluetooth LE (same framing). */
class OemLink {
  constructor(t, log = () => {}) { this.t = t; this.q = t.queue; this.log = log; this.key = null; this.model = ""; }
  async handshake() {
    if (this.t.prepare) await this.t.prepare();          // BLE: init token on 0xFF31
    this.q.clear();
    await this.t.write(ascii(HANDSHAKE));
    let b = await this.q.take(1, 5000), tries = 0;
    while (b[0] !== 0x06 && tries++ < 32) b = await this.q.take(1, 2000);
    if (b[0] !== 0x06) throw new Error("no ACK to PROGRAMBT9000U");
    await this.t.write(ascii("F"));
    await this.q.take(16, 5000);
    await this.t.write(ascii("M"));
    let first = await this.q.take(1, 5000);
    const raw = first[0] === 0x06 ? await this.q.take(12, 5000)
      : Uint8Array.from([...first, ...(await this.q.take(11, 5000))]);
    this.model = String.fromCharCode(...raw).replace(/[\0\xff\x01-\x1f]+/g, "").trim();
    if (!/RT-?950/.test(this.model)) throw new Error(`unexpected model "${this.model}" (expected RT-950)`);
    await new Promise(r => setTimeout(r, 100));
    this.q.clear();
    this.key = keyForChallenge(FACTORY_CHALLENGE);
    await this.t.write(FACTORY_CHALLENGE);
    if ((await this.q.take(1, 5000))[0] !== 0x06) throw new Error("encryption handshake refused");
    return this.model;
  }
  async readBlock(addr, n, cmd = 0x52) {
    await this.t.write(Uint8Array.from([cmd, (addr >> 8) & 0xff, addr & 0xff, n]));
    const r = await this.q.take(4 + n, 8000);
    if (r[0] !== cmd || ((r[1] << 8) | r[2]) !== (addr & 0xffff)) throw new Error("bad read reply at 0x" + addr.toString(16));
    return xorCrypt(r.slice(4), this.key);
  }
  async writeBlock(addr, data, cmd = 0x57) {
    const pkt = new Uint8Array(4 + data.length);
    pkt.set([cmd, (addr >> 8) & 0xff, addr & 0xff, data.length]); pkt.set(xorCrypt(data, this.key), 4);
    await this.t.write(pkt);
    const a = await this.q.take(1, cmd === 0x58 ? 30000 : 8000);
    if (a[0] !== 0x06) throw new Error("write refused at 0x" + addr.toString(16));
  }
  async end(wrote) { try { await this.t.write(Uint8Array.from([wrote ? 0x45 : 0x06])); } catch (e) {} }
}

async function readCodeplug(link, progress = () => {}) {
  const cp = new Codeplug(), total = regions().reduce((s, r) => s + r.size, 0);
  let done = 0;
  for (const r of regions()) {
    const [rc, , ta] = r.transfer, buf = new Uint8Array(r.size);
    for (let off = 0; off < r.size; off += 128) {
      const n = Math.min(128, r.size - off);
      buf.set(await link.readBlock(ta + off, n, rc), off);
      done += n; progress(done, total, r.name);
    }
    cp.mem[r.name] = buf;
  }
  cp.meta = {model: link.model, source: "radio", read_at: new Date().toISOString().slice(0, 19).replace("T", " ")};
  cp.markValid(); cp.touched.clear();
  return cp;
}

function regionData(cp, name, base) {
  if (cp.valid.has(name)) return cp.mem[name];
  const r = regions().find(x => x.name === name);
  if (![...cp.touched].some(t => t >= r.addr && t < r.addr + r.size)) return null;
  if (!base || !base.valid.has(name)) throw new Error(`region ${name} is incomplete: read the radio first`);
  return cp.mergedRegion(name, base.mem[name]);
}

/** Write `cp`; `base` is a fresh read (the backup). With `onlyChanged` only
 *  regions that differ from `base` are sent (much faster over Bluetooth). */
async function writeCodeplug(link, cp, {regions: names = null, base = null, verify = true, onlyChanged = true,
                                         progress = () => {}} = {}) {
  const plan = [];
  for (const r of regions()) {
    if (names && !names.includes(r.name)) continue;
    const d = regionData(cp, r.name, base);
    if (!d) continue;
    if (onlyChanged && base && base.valid.has(r.name) && d.every((b, i) => b === base.mem[r.name][i])) continue;
    plan.push([r, d]);
  }
  const total = plan.reduce((s, [r]) => s + r.size, 0) * (verify ? 2 : 1);
  let done = 0;
  for (const [r, data] of plan) {
    const [rc, wc, ta] = r.transfer;
    for (let off = 0; off < r.size; off += 128) {
      const chunk = data.slice(off, off + 128);
      const old = base && base.valid.has(r.name) ? base.mem[r.name].slice(off, off + 128) : null;
      if (onlyChanged && old && chunk.every((b, i) => b === old[i])) { done += chunk.length * (verify ? 2 : 1); continue; }
      await link.writeBlock(ta + off, chunk, wc);
      done += chunk.length; progress(done, total, r.name);
      if (verify) {
        const back = await link.readBlock(ta + off, chunk.length, rc);
        if (!back.every((b, i) => b === chunk[i])) throw new Error(`verify failed in ${r.name} at 0x${(r.addr + off).toString(16)}`);
        done += chunk.length; progress(done, total, "verify " + r.name);
      }
    }
  }
  return plan.map(([r]) => r.name);
}

/* ---------------------------------------------------------------- country */

function locatorToLatLon(loc) {
  loc = loc.trim();
  if (loc.length < 4) throw new Error("locator needs at least 4 characters");
  const L = loc.slice(0, 2).toUpperCase() + loc.slice(2, 4) + loc.slice(4, 6).toLowerCase();
  let lon = (L.charCodeAt(0) - 65) * 20 - 180 + (+L[2]) * 2, lat = (L.charCodeAt(1) - 65) * 10 - 90 + (+L[3]);
  if (L.length >= 6) return [lat + (L.charCodeAt(5) - 97) * 2.5 / 60 + 1.25 / 60, lon + (L.charCodeAt(4) - 97) * 5 / 60 + 2.5 / 60];
  return [lat + 0.5, lon + 1];
}

function km(a, b) {
  const r = Math.PI / 180, [la1, lo1, la2, lo2] = [a[0] * r, a[1] * r, b[0] * r, b[1] * r];
  const h = Math.sin((la2 - la1) / 2) ** 2 + Math.cos(la1) * Math.cos(la2) * Math.sin((lo2 - lo1) / 2) ** 2;
  return 6371 * 2 * Math.asin(Math.sqrt(h));
}

const KINDS = ["repeater", "airport", "simplex", "pmr", "cb"];
const ORDER = {repeater: 0, simplex: 1, airport: 2, pmr: 3, cb: 4};
const DEFAULT_ZONES = new Set(["", "ZONEONE", "ZONETWO", "ZONETHREE", "ZONEFOUR", "ZONEFIVE", "ZONESIX",
                               "ZONESEVEN", "ZONEEIGHT", "ZONENINE", "ZONETEN"]);

function score(c, ref) {
  if (c.lat == null || !ref) return (c.kind === "repeater" || c.kind === "airport") ? 5000 : 0;
  let d = km(ref, [c.lat, c.lon]);
  if (c.kind === "airport") d -= ({large_airport: 80, medium_airport: 30})[c.airport_type] || 0;
  return d;
}

function selectChannels(chans, room, ref) {
  const fixed = chans.filter(c => c.kind !== "repeater" && c.kind !== "airport");
  const geo = chans.filter(c => c.kind === "repeater" || c.kind === "airport").sort((a, b) => score(a, ref) - score(b, ref));
  const kept = fixed.slice(0, room).concat(geo.slice(0, Math.max(0, room - fixed.length)));
  const dropped = chans.filter(c => !kept.includes(c));
  const key = c => c.kind === "repeater" ? [0, c.band === "2m" ? 0 : 1, ref ? score(c, ref) : 0, c.name]
    : c.kind === "airport" ? [2, ref ? score(c, ref) : 0, c.airport || "", c.name] : [ORDER[c.kind], 0, 0, ""];
  kept.sort((a, b) => { const x = key(a), y = key(b); for (let i = 0; i < 4; i++) { if (x[i] < y[i]) return -1; if (x[i] > y[i]) return 1; } return 0; });
  return [kept, dropped];
}

function applyCountry(cp, data, {mode = "overwrite", locator = null, kinds = KINDS, renameZones = null} = {}) {
  const ref = locator ? locatorToLatLon(locator) : null;
  if (renameZones == null) renameZones = mode === "overwrite";
  const report = {country: data.country, mode, zones: []};
  if (mode === "overwrite") for (let i = 0; i < ZONES * PER_ZONE; i++) if (!cp.channel(i).empty) cp.clearChannel(i);
  data.zones.slice(0, ZONES).forEach((zone, z) => {
    const zr = {zone: z, name: zone.name, added: 0, duplicates: 0, dropped: []};
    report.zones.push(zr);
    const chans = zone.channels.filter(c => kinds.includes(c.kind));
    const base = z * PER_ZONE, existing = new Set(), free = [];
    for (let i = base; i < base + PER_ZONE; i++) {
      const c = cp.channel(i);
      if (c.empty) free.push(i); else existing.add(`${c.rx}/${c.tx}/${c.txTone}`);
    }
    const fresh = chans.filter(c => !existing.has(`${c.rx}/${c.tx}/${c.tone_tx || "OFF"}`));
    zr.duplicates = chans.length - fresh.length;
    let zref = ref;
    if (!zref) {
      const pts = fresh.filter(c => c.lat != null);
      zref = pts.length ? [pts.reduce((s, c) => s + c.lat, 0) / pts.length, pts.reduce((s, c) => s + c.lon, 0) / pts.length] : null;
    }
    const [kept, dropped] = selectChannels(fresh, free.length, zref);
    zr.dropped = dropped.map(c => c.name);
    kept.forEach((c, k) => {
      cp.setChannel(new Channel(free[k], {rx: c.rx, tx: c.tx, rxTone: "OFF", txTone: c.tone_tx || "OFF",
        power: (c.kind === "repeater" || c.kind === "simplex") ? "High" : "Low", bandwidth: c.bw || "Wide",
        scanAdd: "ON", txEnable: c.tx_enable === false ? "OFF" : "ON", rxAm: c.mode === "AM" ? "AM" : "FM",
        name: c.name.slice(0, 12)}));
      zr.added++;
    });
    const cur = cp.zoneName(z).replace(/ /g, "").toUpperCase();
    if (renameZones || DEFAULT_ZONES.has(cur)) cp.setZoneName(z, zone.name.slice(0, 16));
  });
  return report;
}

/* -------------------------------------------------------------------- CSV */

const mhz = hz => hz == null ? "" : (hz / 1e6).toFixed(5);

function exportChirp(cp) {
  const rows = [["Location", "Name", "Frequency", "Duplex", "Offset", "Tone", "rToneFreq", "cToneFreq", "DtcsCode",
                 "DtcsPolarity", "Mode", "TStep", "Skip", "Comment"].join(",")];
  for (const c of cp.channels()) {
    if (c.empty) continue;
    let duplex = "", offset = "0.000000";
    const d = (c.tx || c.rx) - c.rx;
    if (c.txEnable === "OFF") duplex = "off";
    else if (d && Math.abs(d) < 10e6) { duplex = d > 0 ? "+" : "-"; offset = (Math.abs(d) / 1e6).toFixed(6); }
    else if (d) { duplex = "split"; offset = (c.tx / 1e6).toFixed(6); }
    let tone = "", rt = "88.5", ct = "88.5", dcs = "023", pol = "NN";
    if (c.txTone[0] === "D") { tone = "DTCS"; dcs = c.txTone.slice(1, 4); pol = (c.txTone[4] === "I" ? "R" : "N") + (c.rxTone[4] === "I" ? "R" : "N"); }
    else if (c.txTone !== "OFF") { tone = c.rxTone === c.txTone ? "TSQL" : "Tone"; rt = c.txTone; ct = c.rxTone !== "OFF" ? c.rxTone : c.txTone; }
    const mode = c.rxAm === "AM" ? "AM" : (c.bandwidth === "Narrow" ? "NFM" : "FM");
    rows.push([c.number, `"${c.name.replace(/"/g, "")}"`, mhz(c.rx), duplex, offset, tone, rt, ct, dcs, pol, mode, "5.00",
               c.scanAdd === "ON" ? "" : "S", ""].join(","));
  }
  return rows.join("\r\n") + "\r\n";
}

const api = {setMeta, regions, Channel, Codeplug, decodeTone, encodeTone, bcdToHz, hzToBcd, decodeChannel,
             encodeChannel, keyForChallenge, xorCrypt, ByteQueue, OemLink, readCodeplug, writeCodeplug,
             regionData, applyCountry, selectChannels, locatorToLatLon, exportChirp, mhz, KINDS,
             CH, ZONES, PER_ZONE, FACTORY_CHALLENGE, DTMF_CHARS};
if (typeof module !== "undefined") module.exports = api;
root.RT950 = Object.assign(root.RT950 || {}, api);
})(typeof window !== "undefined" ? window : globalThis);
