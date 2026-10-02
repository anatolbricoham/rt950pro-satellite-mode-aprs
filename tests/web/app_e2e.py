"""app_e2e.py - End-to-end test of the web / Android programmer UI in
Chromium (Playwright) with a mocked Web Bluetooth radio.

The mock exposes service 0xFFE0 / characteristic 0xFFE1 (write + notify) and
0xFF31 (init token) and forwards everything to tests/web/fake_radio.js, the
same emulator the Node tests use. Screenshots go to the folder given as
argument (default build_host/web)."""
import functools
import http.server
import os
import sys
import threading

from playwright.sync_api import sync_playwright

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
WWW = os.path.join(ROOT, "mobile", "www")
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build_host", "web")

MOCK = open(os.path.join(ROOT, "tests", "web", "fake_radio.js")).read() + r"""
window.__radioReady = (async () => {})();
(() => {
  let radio = null, notify = null;
  const mk = (uuid, write) => ({
    uuid,
    async startNotifications() {}, addEventListener(t, cb) { notify = cb; },
    async writeValueWithoutResponse(v) { write(new Uint8Array(v)); },
    async writeValueWithResponse(v) { write(new Uint8Array(v)); },
  });
  window.__tokens = [];
  const serial = mk("ffe1", b => {
    if (!radio) {
      radio = new FakeRadio(window.RT950);
      window.__radio = radio;
      for (let i = 0; i < 0x7C00; i++) radio.mem[i] = 0xff;
      // one channel in the radio: 145.500 S20 at slot 0
      const ch = new window.RT950.Channel(0, {rx: 145500000, tx: 145500000, name: "S20 MINE"});
      radio.mem.set(window.RT950.encodeChannel(ch, null), 0);
      radio.mem.fill(0x5a, 0x9000, 0x9080);
      radio.queue = {push: d => { const v = new DataView(Uint8Array.from(d).buffer); notify({target: {value: v}}); }};
      radio.out = d => setTimeout(() => radio.queue.push(d), 1);
    }
    radio.feed(b);
  });
  const init = mk("ff31", b => window.__tokens.push(Array.from(b)));
  const svcSerial = {uuid: "ffe0", async getCharacteristic(u) { if (u === 0xffe1) return serial; throw new Error("x"); }};
  const svcInit = {uuid: "ff30", async getCharacteristic(u) { if (u === 0xff31) return init; throw new Error("x"); }};
  Object.defineProperty(navigator, "bluetooth", {value: {
    async requestDevice(opts) { window.__reqOpts = opts; return {name: "RT-950 PRO", gatt: {
      async connect() { return {async getPrimaryService(u) { return svcSerial; },
                                async getPrimaryServices() { return [svcSerial, svcInit]; }}; },
      disconnect() {}}}; },
  }});
})();
"""


def serve():
    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *a):
            pass
    h = functools.partial(Quiet, directory=WWW)
    s = http.server.ThreadingHTTPServer(("127.0.0.1", 0), h)
    threading.Thread(target=s.serve_forever, daemon=True).start()
    return s


def main():
    os.makedirs(OUT, exist_ok=True)
    srv = serve()
    url = "http://127.0.0.1:%d/index.html" % srv.server_address[1]
    errors = []
    with sync_playwright() as p:
        b = p.chromium.launch()
        pg = b.new_page(viewport={"width": 412, "height": 860}, device_scale_factor=2, locale="es-ES")
        pg.on("pageerror", lambda e: errors.append(str(e)))
        pg.on("dialog", lambda d: d.accept())
        pg.add_init_script(MOCK)
        pg.goto(url)
        pg.wait_for_selector("#discDlg[open]")
        pg.screenshot(path=os.path.join(OUT, "app-disclaimer.png"))
        pg.click("#discYes")
        pg.click("#btnBle")
        pg.wait_for_function("document.getElementById('statusText').textContent.includes('RT-950')")
        pg.click("#btnRead")
        pg.wait_for_function("document.getElementById('statusText').textContent.startsWith('Hecho')", timeout=60000)
        assert pg.evaluate("window.__tokens.length") == 1 and pg.evaluate("window.__tokens[0].slice(0,5)") == [63, 63, 63, 63, 2]
        pg.screenshot(path=os.path.join(OUT, "app-radio.png"))
        pg.click(".tabs button[data-tab=channels]")
        assert "S20 MINE" in pg.inner_text("#chList")
        # country preset, append mode
        pg.click(".tabs button[data-tab=country]")
        pg.check("input[name=mode][value=append]")
        pg.fill("#ctyLoc", "IN80DK")
        pg.click("#btnCtyApply")
        out = pg.inner_text("#ctyOut")
        assert "ES (append)" in out, out
        pg.screenshot(path=os.path.join(OUT, "app-country.png"), full_page=True)
        pg.click(".tabs button[data-tab=channels]")
        pg.select_option("#zoneSel", "3")
        pg.screenshot(path=os.path.join(OUT, "app-channels.png"))
        # write back
        pg.click(".tabs button[data-tab=radio]")
        pg.click("#btnWrite")
        pg.wait_for_function("document.getElementById('log').textContent.includes('written:')", timeout=120000)
        res = pg.evaluate("""() => { const r = window.__radio;
            return {zone: String.fromCharCode(...r.mem.slice(0xC000, 0xC003)),
                    settings: r.mem.slice(0x9000, 0x9080).every(b => b === 0x5a),
                    mine: window.RT950.decodeChannel(0, Array.from(r.mem.slice(0, 32))).name,
                    used: Array.from({length: 990}, (_, i) => r.mem[i * 32] !== 0xff).filter(Boolean).length,
                    aprsWrites: r.log.filter(x => x[0] === 'X').length}; }""")
        assert res["zone"] == "EA1" and res["settings"] and res["mine"] == "S20 MINE" and res["aprsWrites"] == 0, res
        assert res["used"] > 400, res
        pg.click(".tabs button[data-tab=settings]")
        pg.screenshot(path=os.path.join(OUT, "app-settings.png"))
        pg.click(".tabs button[data-tab=help]")
        pg.screenshot(path=os.path.join(OUT, "app-help.png"))
        b.close()
    srv.shutdown()
    if errors:
        raise SystemExit("page errors: %s" % errors)
    print("  write result:", res)
    print("RESULT: PASS")


if __name__ == "__main__":
    main()
