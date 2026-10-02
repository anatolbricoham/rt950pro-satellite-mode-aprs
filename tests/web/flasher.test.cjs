// Node test of site/flasher/rt950-flasher.js against an emulated bootloader.
// node tests/web/flasher.test.cjs [firmware.BTF]
const fs = require("fs");
const path = require("path");
const assert = require("assert");
const F = require("../../site/flasher/rt950-flasher.js");

class FakeRadio {
  constructor({inBootloader = false, model = "RT-950", dropProbes = 0} = {}) {
    this.mode = inBootloader ? "boot" : "fw";
    this.model = model; this.dropProbes = dropProbes;
    this.rx = []; this.image = []; this.finished = false; this.count = null;
    this.queue = new F.ByteQueue();
  }
  answer(cmd, res) {
    const body = Uint8Array.from([cmd, 0, res, 0, 0]);
    const crc = F.crc16(body);
    setTimeout(() => this.queue.push(Uint8Array.from([0xaa, ...body, crc >> 8, crc & 0xff, 0x55])), 1);
  }
  async write(bytes) {
    for (const b of bytes) this.rx.push(b);
    if (this.mode === "fw") {
      const s = Buffer.from(this.rx).toString("latin1");
      if (s.endsWith("PROGRAMBT9000U")) { this.rx = []; setTimeout(() => this.queue.push([6]), 1); }
      else if (s.endsWith("UPDATE")) { this.rx = []; this.mode = "boot"; setTimeout(() => this.queue.push([6]), 1); }
      return;
    }
    while (this.rx.length) {
      const i = this.rx.indexOf(0xaa);
      if (i < 0) { this.rx = []; return; }
      if (i) this.rx.splice(0, i);
      if (this.rx.length < 6) return;
      const len = (this.rx[4] << 8) | this.rx[5];
      if (this.rx.length < 9 + len) return;
      const f = this.rx.splice(0, 9 + len);
      const body = Uint8Array.from(f.slice(1, 6 + len));
      const crc = (f[6 + len] << 8) | f[7 + len];
      const cmd = f[1], arg = (f[2] << 8) | f[3], data = Uint8Array.from(f.slice(6, 6 + len));
      if (crc !== F.crc16(body)) { this.answer(cmd, 0xe2); continue; }
      if (cmd === 0x42) { if (this.dropProbes-- > 0) continue; this.answer(cmd, 0xe5); }
      else if (cmd === 0x0a) this.answer(cmd, Buffer.from(data).toString() === "BOOTLOADER_V3" ? 6 : 0xe5);
      else if (cmd === 0x02) this.answer(cmd, Buffer.from(data.slice(0, this.model.length)).toString() === this.model ? 6 : 0xe6);
      else if (cmd === 0x04) { this.count = ((data[0] << 8) | data[1]) + 1; this.answer(cmd, 6); }
      else if (cmd === 0x03) {
        if (len !== 1024 || arg * 1024 !== this.image.length) this.answer(cmd, 0xe1);
        else { for (const b of data) this.image.push(b); this.answer(cmd, 6); }
      } else if (cmd === 0x45) { this.finished = true; this.answer(cmd, 6); }
      else this.answer(cmd, 0xe5);
    }
  }
}

(async () => {
  const fwPath = process.argv[2] || path.join(__dirname, "../../build/rt950-custom.BTF");
  const bytes = new Uint8Array(fs.readFileSync(fwPath));
  for (const opts of [{inBootloader: false, dropProbes: 5}, {inBootloader: true}]) {
    const radio = new FakeRadio(opts);
    const fl = new F.Rt950Flasher(radio);
    let last = 0;
    const info = await fl.flash(bytes, {enterBootloader: !opts.inBootloader, progress: (d) => { last = d; }});
    assert.ok(radio.finished, "end command received");
    assert.strictEqual(radio.count, info.blocks);
    assert.strictEqual(last, info.blocks);
    assert.deepStrictEqual(Buffer.from(radio.image.slice(0, bytes.length)), Buffer.from(bytes));
    console.log(`  ${opts.inBootloader ? "in bootloader" : "from firmware"}: ${info.blocks} blocks OK`);
  }
  const bad = new FakeRadio({inBootloader: true, model: "UV-K5"});
  await assert.rejects(new F.Rt950Flasher(bad).flash(bytes, {enterBootloader: false}), /model mismatch/);
  assert.throws(() => F.firmwareInfo(new Uint8Array(100)), /too small/);
  // packet layout matches the Python implementation
  assert.deepStrictEqual(Array.from(F.packet(0x42)), [0xaa, 0x42, 0, 0, 0, 0, ...(c => [c >> 8, c & 255])(F.crc16([0x42, 0, 0, 0, 0])), 0x55]);
  console.log("  wrong model refused, short file refused, packet layout OK");
  console.log("RESULT: PASS");
})().catch(e => { console.error(e); process.exit(1); });
