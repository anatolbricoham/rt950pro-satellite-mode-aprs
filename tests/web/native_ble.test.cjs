// The Android (Capacitor) Bluetooth transport against a mocked BluetoothLe
// plugin wired to the fake radio: hex value encoding, init token, handshake,
// full read.  node tests/web/native_ble.test.cjs
const fs = require("fs"), path = require("path"), assert = require("assert");
const W = path.join(__dirname, "../../mobile/www");
const u16 = n => `0000${n.toString(16)}-0000-1000-8000-00805f9b34fb`;
let radio, listener = null, tokens = [], keys = [];
const plugin = {
  async initialize(o) { assert.strictEqual(o.androidNeverForLocation, true); },
  async requestEnable() {}, async requestConnectionPriority() {},
  async requestDevice(o) { assert.ok(o.optionalServices.includes(u16(0xffe0))); return {deviceId: "AA:BB", name: "RT-950 PRO"}; },
  async connect() {},
  async getServices() { return {services: [{uuid: u16(0xffe0), characteristics: [{uuid: u16(0xffe1)}]},
                                           {uuid: u16(0xff30), characteristics: [{uuid: u16(0xff31)}]}]}; },
  async addListener(key, cb) { keys.push(key); listener = cb; return {remove() {}}; },
  async startNotifications() {}, async stopNotifications() {}, async disconnect() {},
  async write(a) { if (a.characteristic === u16(0xff31)) tokens.push(a.value); else this.writeWithoutResponse(a); },
  async writeWithoutResponse(a) {
    assert.match(a.value, /^([0-9a-f]{2})+$/); assert.ok(a.value.length <= 40);
    radio.feed(Buffer.from(a.value, "hex"));
  },
};
globalThis.Capacitor = {isNativePlatform: () => true, registerPlugin: () => plugin};
globalThis.window = globalThis;
require(path.join(W, "js/core.js")); require(path.join(W, "js/transport.js"));
const R = globalThis.RT950;
R.setMeta(JSON.parse(fs.readFileSync(path.join(W, "data/meta.json"))));
const FakeRadio = require("./fake_radio.js");
(async () => {
  assert.ok(R.NativeBleTransport.available());
  const t = new R.NativeBleTransport();
  await t.open();
  assert.deepStrictEqual(keys, [`notification|AA:BB|${u16(0xffe0)}|${u16(0xffe1)}`]);
  radio = new FakeRadio(R);
  radio.mem.fill(0x11, 0, 32);
  radio.out = d => setTimeout(() => listener({value: Buffer.from(d).toString("hex").replace(/(..)/g, "$1 ")}), 1);
  const link = new R.OemLink(t);
  t.prepare = (orig => async () => { const t0 = Date.now(); await orig.call(t); assert.ok(Date.now() - t0 >= 1100); })(t.prepare);
  assert.strictEqual(await link.handshake(), "RT-950 Pro");
  assert.strictEqual(tokens.length, 1); assert.ok(tokens[0].startsWith("3f3f3f3f02") && tokens[0].length === 40);
  const cp = await R.readCodeplug(link);
  assert.ok(cp.mem.channels.slice(0, 32).every(b => b === 0x11));
  await t.close();
  console.log("  native BLE: init token, hex framing, handshake and full read OK");
  console.log("RESULT: PASS");
})().catch(e => { console.error(e); process.exit(1); });
