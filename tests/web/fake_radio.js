// fake_radio.js - RT-950 programming port emulator (browser + Node), shared by
// the web tests. Behaves like pc/tests/oem_emulator.py.
(function (root) {
class FakeRadio {
  constructor(R) {
    this.R = R; this.queue = new R.ByteQueue(); this.mem = new Uint8Array(0x10000).fill(0xff);
    this.aprs = new Uint8Array(0x100).fill(0xff); this.rx = []; this.state = "idle"; this.key = null; this.log = [];
    this.out = b => setTimeout(() => this.queue.push(b), 0);
  }
  feed(bytes) {
    const R = this.R;
    for (const b of bytes) this.rx.push(b);
    for (;;) {
      if (this.state === "idle") {
        const s = String.fromCharCode(...this.rx); const i = s.indexOf("PROGRAMBT9000U");
        if (i < 0) return; this.rx.splice(0, i + 14); this.state = "cmd"; this.out([6]); continue;
      }
      if (!this.rx.length) return;
      const c = this.rx[0];
      if (c === 0x46) { this.rx.shift(); this.out(new Uint8Array(16)); }
      else if (c === 0x4d) { this.rx.shift(); this.out([6, ...Array.from("RT-950 Pro\0\0", ch => ch.charCodeAt(0))]); }
      else if (c === 0x53) { if (this.rx.length < 25) return; const p = Uint8Array.from(this.rx.splice(0, 25)); this.key = R.keyForChallenge(p); this.out([6]); }
      else if (c === 0x52 || c === 0x54) {
        if (this.rx.length < 4) return; const h = this.rx.splice(0, 4), a = (h[1] << 8) | h[2], n = h[3];
        const src = c === 0x52 ? this.mem : this.aprs; this.log.push([String.fromCharCode(c), a]);
        this.out([...h, ...R.xorCrypt(src.slice(a, a + n), this.key)]);
      } else if (c === 0x57 || c === 0x58) {
        if (this.rx.length < 4 || this.rx.length < 4 + this.rx[3]) return;
        const h = this.rx.splice(0, 4), a = (h[1] << 8) | h[2], n = h[3];
        const d = R.xorCrypt(Uint8Array.from(this.rx.splice(0, n)), this.key);
        (c === 0x57 ? this.mem : this.aprs).set(d, a); this.log.push([String.fromCharCode(c), a]); this.out([6]);
      } else { this.rx.shift(); this.state = "idle"; }
    }
  }
}
if (typeof module !== "undefined") module.exports = FakeRadio;
root.FakeRadio = FakeRadio;
})(typeof window !== "undefined" ? window : globalThis);
