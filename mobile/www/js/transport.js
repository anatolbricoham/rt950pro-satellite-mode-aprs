/*
 * transport.js - Links to the radio for the web / Android programmer
 *
 *  BleTransport      Web Bluetooth (Chrome on Android, Windows, Linux, macOS)
 *  NativeBleTransport  Capacitor BluetoothLe plugin (the Android APK)
 *  SerialTransport   Web Serial: USB programming cable (desktop Chrome/Edge)
 *
 * Bluetooth: the radio's module exposes a serial characteristic 0xFFE1
 * (write + notify, service 0xFFE0) that carries exactly the same framing as
 * the cable. A 20-byte init token ("????" 0x02 + 15 random bytes) must be
 * written to characteristic 0xFF31 first. Protocol facts from the MIT
 * licensed rt950-ble project (bartasx) and checked against the cable
 * protocol implemented in core.js.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
(function (root) {
"use strict";
const R = root.RT950;
const SERVICE = 0xffe0, SERIAL = 0xffe1, INIT = 0xff31;
const CANDIDATE_SERVICES = [0xffe0, 0xff30, 0xff00, 0xfff0, 0x18f0];
const NAMES = ["RT-950", "RT950", "Radtel", "walkie", "Walkie", "RT-"];
const CHUNK = 20;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const uuid16 = n => `0000${n.toString(16).padStart(4, "0")}-0000-1000-8000-00805f9b34fb`;

function initToken() {
  const t = new Uint8Array(20);
  t.set([0x3f, 0x3f, 0x3f, 0x3f, 0x02]);
  crypto.getRandomValues(t.subarray(5));
  return t;
}

class BleTransport {
  static available() { return !!(typeof navigator !== "undefined" && navigator.bluetooth); }
  constructor() { this.queue = new R.ByteQueue(); this.kind = "ble"; }
  async open() {
    this.device = await navigator.bluetooth.requestDevice({
      filters: [{services: [SERVICE]}, ...NAMES.map(n => ({namePrefix: n}))],
      optionalServices: CANDIDATE_SERVICES,
    });
    this.name = this.device.name || "RT-950";
    this.server = await this.device.gatt.connect();
    const svc = await this.server.getPrimaryService(SERVICE);
    this.serial = await svc.getCharacteristic(SERIAL);
    this.init = null;
    for (const s of await this.server.getPrimaryServices()) {
      try { this.init = await s.getCharacteristic(INIT); break; } catch (e) { /* not in this service */ }
    }
    await this.serial.startNotifications();
    this.serial.addEventListener("characteristicvaluechanged",
      e => this.queue.push(new Uint8Array(e.target.value.buffer.slice(e.target.value.byteOffset,
        e.target.value.byteOffset + e.target.value.byteLength))));
  }
  async prepare() {
    if (this.init) {
      await this.init.writeValueWithResponse(initToken());
      await sleep(1200);
    }
    this.queue.clear();
  }
  async write(bytes) {
    for (let i = 0; i < bytes.length; i += CHUNK) {
      const c = bytes.slice(i, i + CHUNK);
      try { await this.serial.writeValueWithoutResponse(c); }
      catch (e) { await this.serial.writeValueWithResponse(c); }
      if (i + CHUNK < bytes.length) await sleep(10);
    }
  }
  async close() { try { this.device.gatt.disconnect(); } catch (e) {} }
}

/* Capacitor community BluetoothLe plugin, used directly (no bundler):
   on Android/iOS values travel as hex strings ("0a1b...") between JS and the
   native side, as the plugin's own BleClient does. */
const toHex = u8 => Array.from(u8, b => b.toString(16).padStart(2, "0")).join("");
const fromHex = s => {
  if (typeof s !== "string") return new Uint8Array(s.buffer || s);
  const h = s.replace(/[^0-9a-fA-F]/g, ""), out = new Uint8Array(h.length >> 1);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(h.substr(i * 2, 2), 16);
  return out;
};

class NativeBleTransport {
  static plugin() {
    const C = root.Capacitor;
    if (!C || !C.isNativePlatform || !C.isNativePlatform()) return null;
    return C.registerPlugin ? C.registerPlugin("BluetoothLe") : (C.Plugins || {}).BluetoothLe;
  }
  static available() { return !!NativeBleTransport.plugin(); }
  constructor() { this.queue = new R.ByteQueue(); this.kind = "ble"; this.ble = NativeBleTransport.plugin(); }
  async open() {
    const b = this.ble;
    await b.initialize({androidNeverForLocation: true});
    try { await b.requestEnable(); } catch (e) { /* already on or not supported */ }
    const dev = await b.requestDevice({optionalServices: CANDIDATE_SERVICES.map(uuid16)});
    this.id = dev.deviceId; this.name = dev.name || "RT-950";
    await b.connect({deviceId: this.id, timeout: 15000});
    try { await b.requestConnectionPriority({deviceId: this.id, connectionPriority: 1}); } catch (e) {}
    const {services} = await b.getServices({deviceId: this.id});
    this.svc = uuid16(SERVICE); this.chr = uuid16(SERIAL); this.initSvc = null;
    for (const s of services || []) {
      if ((s.characteristics || []).some(c => c.uuid.toLowerCase() === uuid16(INIT))) this.initSvc = s.uuid;
    }
    this.listener = await b.addListener(`notification|${this.id}|${this.svc}|${this.chr}`,
      ev => this.queue.push(fromHex(ev.value)));
    await b.startNotifications({deviceId: this.id, service: this.svc, characteristic: this.chr});
  }
  async prepare() {
    if (this.initSvc) {
      await this.ble.write({deviceId: this.id, service: this.initSvc, characteristic: uuid16(INIT), value: toHex(initToken())});
      await sleep(1200);
    }
    this.queue.clear();
  }
  async write(bytes) {
    for (let i = 0; i < bytes.length; i += CHUNK) {
      const value = toHex(bytes.slice(i, i + CHUNK));
      const args = {deviceId: this.id, service: this.svc, characteristic: this.chr, value};
      try { await this.ble.writeWithoutResponse(args); } catch (e) { await this.ble.write(args); }
      if (i + CHUNK < bytes.length) await sleep(10);
    }
  }
  async close() {
    try { await this.ble.stopNotifications({deviceId: this.id, service: this.svc, characteristic: this.chr}); } catch (e) {}
    try { this.listener?.remove(); } catch (e) {}
    try { await this.ble.disconnect({deviceId: this.id}); } catch (e) {}
  }
}

class SerialTransport {
  static available() { return !!(typeof navigator !== "undefined" && navigator.serial); }
  constructor() { this.queue = new R.ByteQueue(); this.kind = "serial"; this.reading = false; }
  async open() {
    this.port = await navigator.serial.requestPort();
    await this.port.open({baudRate: 115200, dataBits: 8, stopBits: 1, parity: "none", bufferSize: 4096});
    this.name = "USB";
    this.writer = this.port.writable.getWriter();
    this.reading = true;
    this.loop = (async () => {
      while (this.reading && this.port.readable) {
        this.reader = this.port.readable.getReader();
        try { for (;;) { const {value, done} = await this.reader.read(); if (done) break; if (value) this.queue.push(value); } }
        catch (e) {} finally { this.reader.releaseLock(); }
      }
    })();
  }
  async write(bytes) { await this.writer.write(bytes); }
  async close() {
    this.reading = false;
    try { await this.reader?.cancel(); } catch (e) {}
    try { this.writer.releaseLock(); } catch (e) {}
    try { await this.loop; } catch (e) {}
    try { await this.port.close(); } catch (e) {}
  }
}

root.RT950 = Object.assign(root.RT950 || {}, {BleTransport, NativeBleTransport, SerialTransport, initToken, toHex, fromHex});
})(typeof window !== "undefined" ? window : globalThis);
