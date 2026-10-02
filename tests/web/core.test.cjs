// Cross-check mobile/www/js/core.js against the Python toolkit (files in $XCHECK,
// written by pc/tests/test_web_core.py) and run protocol sessions on a fake radio.
const fs = require("fs"), path = require("path"), assert = require("assert");
const W = path.join(__dirname, "../../mobile/www");
const R = require(path.join(W, "js/core.js"));
R.setMeta(JSON.parse(fs.readFileSync(path.join(W, "data/meta.json"))));
const X = process.env.XCHECK;
const load = f => R.Codeplug.fromJSON(JSON.parse(fs.readFileSync(path.join(X, f))));
const country = c => JSON.parse(fs.readFileSync(path.join(W, `data/country-${c.toLowerCase()}.json`)));

// 1. decoding identical to Python
const cp = load("sample.rt950");
const py = JSON.parse(fs.readFileSync(path.join(X, "sample_channels.json")));
const js = cp.channels().filter(c => !c.empty).map(c => [c.number, c.name, c.rx, c.tx, c.rxTone, c.txTone, c.power,
  c.bandwidth, c.scramble, c.busyLock, c.scanAdd, c.encryption, c.txEnable, c.rxAm, c.signalCode, c.pttId]);
assert.deepStrictEqual(js, py); console.log("  channel decoding matches Python (" + js.length + " channels)");
// 2. re-encoding a decoded channel gives the same bytes
for (const c of cp.channels().filter(c => !c.empty)) {
  const old = Array.from(cp.mem.channels.slice(c.index * 32, c.index * 32 + 32));
  assert.deepStrictEqual(R.encodeChannel(c, old), old);
}
console.log("  channel re-encoding is byte-identical");
// 3. country presets identical to Python
for (const [code, loc] of [["ES", "IM98IB"], ["GB", "IO91WM"], ["GB", null]]) {
  const ref = load(`${code}_${loc}.rt950`);
  const mine = new R.Codeplug();
  R.applyCountry(mine, country(code), {mode: "overwrite", locator: loc});
  assert.deepStrictEqual(Buffer.from(mine.mem.channels), Buffer.from(ref.mem.channels), `${code} channels`);
  assert.deepStrictEqual(Buffer.from(mine.mem.zones), Buffer.from(ref.mem.zones), `${code} zones`);
}
const ap = load("sample.rt950"); ap.markValid();
R.applyCountry(ap, country("ES"), {mode: "append", locator: "IN80DK"});
assert.deepStrictEqual(Buffer.from(ap.mem.channels), Buffer.from(load("append_ES.rt950").mem.channels));
console.log("  country presets (ES, GB, append) byte-identical to Python");
// 4. .rt950 round trip keeps valid regions and touched bytes
const p = new R.Codeplug(); p.setZoneName(0, "EA4");
const q = R.Codeplug.fromJSON(JSON.parse(JSON.stringify(p.toJSON())));
assert.ok(q.valid.has("channels") && !q.valid.has("zones") && q.touched.has(0xC000));
console.log("  .rt950 round trip OK");

// 5. protocol on a fake radio (same behaviour as pc/tests/oem_emulator.py)
class FakeRadio {
  constructor() {
    this.queue = new R.ByteQueue(); this.mem = new Uint8Array(0x10000).fill(0xff); this.aprs = new Uint8Array(0x100).fill(0xff);
    this.rx = []; this.state = "idle"; this.key = null; this.log = []; this.prepared = 0;
  }
  async prepare() { this.prepared++; }
  send(b) { setTimeout(() => this.queue.push(b), 0); }
  async write(bytes) {
    for (const b of bytes) this.rx.push(b);
    for (;;) {
      if (this.state === "idle") {
        const s = Buffer.from(this.rx).toString("latin1"); const i = s.indexOf("PROGRAMBT9000U");
        if (i < 0) return; this.rx.splice(0, i + 14); this.state = "cmd"; this.send([6]); continue;
      }
      if (!this.rx.length) return;
      const c = this.rx[0];
      if (c === 0x46) { this.rx.shift(); this.send(new Uint8Array(16)); }
      else if (c === 0x4d) { this.rx.shift(); this.send(Buffer.from("RT-950 Pro\0\0")); }
      else if (c === 0x53) { if (this.rx.length < 25) return; const p = Uint8Array.from(this.rx.splice(0, 25)); this.key = R.keyForChallenge(p); this.send([6]); }
      else if (c === 0x52 || c === 0x54) {
        if (this.rx.length < 4) return; const h = this.rx.splice(0, 4), a = (h[1] << 8) | h[2], n = h[3];
        const src = c === 0x52 ? this.mem : this.aprs; this.log.push([String.fromCharCode(c), a]);
        this.send([...h, ...R.xorCrypt(src.slice(a, a + n), this.key)]);
      } else if (c === 0x57 || c === 0x58) {
        if (this.rx.length < 4 || this.rx.length < 4 + this.rx[3]) return;
        const h = this.rx.splice(0, 4), a = (h[1] << 8) | h[2], n = h[3];
        const d = R.xorCrypt(Uint8Array.from(this.rx.splice(0, n)), this.key);
        (c === 0x57 ? this.mem : this.aprs).set(d, a); this.log.push([String.fromCharCode(c), a]); this.send([6]);
      } else { this.rx.shift(); this.state = "idle"; }
    }
  }
}
(async () => {
  const radio = new FakeRadio();
  const src = load("sample.rt950");
  for (const r of R.regions()) (r.name === "aprs" ? radio.aprs : radio.mem).set(src.mem[r.name], r.name === "aprs" ? 0 : r.addr);
  radio.mem.fill(0x5a, 0x9000, 0x9080);
  const link = new R.OemLink(radio);
  assert.strictEqual(await link.handshake(), "RT-950 Pro");
  assert.strictEqual(radio.prepared, 1);
  const read = await R.readCodeplug(link);
  for (const r of R.regions()) assert.deepStrictEqual(Buffer.from(read.mem[r.name]), Buffer.from(r.name === "settings" ? new Uint8Array(0x80).fill(0x5a) : src.mem[r.name]));
  assert.ok(radio.log.some(([c, a]) => c === "T" && a === 0));
  console.log("  read: identical codeplug, APRS page through T at 0");
  // country preset made from scratch, then written on top of the radio
  const preset = new R.Codeplug();
  R.applyCountry(preset, country("ES"), {mode: "overwrite", locator: "IM98IB"});
  await assert.rejects(R.writeCodeplug(link, preset, {}), /read the radio first/);
  radio.log.length = 0;
  const written = await R.writeCodeplug(link, preset, {base: read});
  assert.deepStrictEqual(written, ["channels", "zones"]);
  assert.ok(radio.mem.slice(0x9000, 0x9080).every(b => b === 0x5a), "settings untouched");
  assert.ok(!radio.log.some(([c]) => c === "X"), "APRS page untouched");
  assert.deepStrictEqual(Buffer.from(radio.mem.slice(0, 0x7C00)), Buffer.from(preset.mem.channels));
  assert.strictEqual(String.fromCharCode(...radio.mem.slice(0xC000, 0xC003)), "EA1");
  console.log("  write (differential + verify): only channels and zone names, settings untouched");
  // differential: nothing changed -> nothing written
  radio.log.length = 0;
  const again = await R.readCodeplug(link);
  radio.log.length = 0;
  await R.writeCodeplug(link, again, {base: again});
  assert.ok(!radio.log.some(([c]) => c === "W" || c === "X"));
  console.log("  unchanged codeplug: no blocks written");
  await link.end(true);
  console.log("RESULT: PASS");
})().catch(e => { console.error(e); process.exit(1); });
