/*
 * rt950-flasher.js - RT-950 / RT-950 Pro firmware update over Web Serial
 *
 * Same protocol as pc/rt950_toolkit/flasher.py:
 *   firmware:   "PROGRAMBT9000U" -> 06, "UPDATE" -> 06  (MCU resets into the bootloader)
 *   bootloader: AA cmd argH argL lenH lenL data.. crcH crcL 55  (CRC-16/CCITT over cmd..data)
 *               answer AA cmd 00 result 00 00 crcH crcL 55, result 06 = OK
 *   42 probe -> 0A "BOOTLOADER_V3" -> 02 model (32 B @0x3E0) -> 04 blocks-1
 *   -> 03 data (1024 B, arg = sequence) -> 45 end
 *
 * The core (Rt950Flasher) only needs a transport with write(Uint8Array) and
 * a byte queue fed by the reader, so it runs both in the browser and in the
 * Node test (tests/web/flasher.test.mjs).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

const BAUD = 115200;
const BLOCK = 1024;
const MODEL_OFFSET = 0x3e0, MODEL_SIZE = 32, KEY_OFFSET = 0x400;
const RESULTS = {0x06: "OK", 0xe1: "wrong data length", 0xe2: "data verification error",
                 0xe3: "flash write error", 0xe5: "command not applicable", 0xe6: "model mismatch"};
const enc = new TextEncoder();
const sleep = ms => new Promise(r => setTimeout(r, ms));

function crc16(data) {
  let crc = 0;
  for (const b of data) {
    crc ^= b << 8;
    for (let i = 0; i < 8; i++) crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff;
  }
  return crc;
}

function packet(cmd, arg = 0, data = new Uint8Array(0)) {
  const body = new Uint8Array(5 + data.length);
  body.set([cmd, (arg >> 8) & 0xff, arg & 0xff, (data.length >> 8) & 0xff, data.length & 0xff]);
  body.set(data, 5);
  const crc = crc16(body);
  const out = new Uint8Array(body.length + 4);
  out[0] = 0xaa; out.set(body, 1);
  out[out.length - 3] = crc >> 8; out[out.length - 2] = crc & 0xff; out[out.length - 1] = 0x55;
  return out;
}

function firmwareInfo(bytes) {
  if (bytes.length < KEY_OFFSET + 16) throw new Error("not a firmware file (too small)");
  const model = new TextDecoder("ascii").decode(bytes.slice(MODEL_OFFSET, MODEL_OFFSET + 12)).replace(/\0+$/, "").trim();
  const blocks = Math.ceil(bytes.length / BLOCK);
  const encrypted = bytes.slice(KEY_OFFSET, KEY_OFFSET + 16).some(b => b !== 0);
  if (!/RT-?950/.test(model)) throw new Error(`firmware is for "${model}", not for an RT-950`);
  return {model, blocks, size: bytes.length, encrypted};
}

class ByteQueue {
  constructor() { this.buf = []; this.waiters = []; }
  push(chunk) { for (const b of chunk) this.buf.push(b); this.waiters.splice(0).forEach(w => w()); }
  clear() { this.buf.length = 0; }
  async take(n, timeout) {
    const t0 = Date.now();
    while (this.buf.length < n) {
      const left = timeout - (Date.now() - t0);
      if (left <= 0) break;
      await new Promise(r => { this.waiters.push(r); setTimeout(r, Math.min(left, 20)); });
    }
    return Uint8Array.from(this.buf.splice(0, Math.min(n, this.buf.length)));
  }
  async frame(timeout) {          // AA .. 55, at least 9 bytes
    const t0 = Date.now(), out = [];
    while (Date.now() - t0 < timeout) {
      const b = await this.take(1, timeout - (Date.now() - t0));
      if (!b.length) break;
      if (!out.length && b[0] !== 0xaa) continue;
      out.push(b[0]);
      if (b[0] === 0x55 && out.length >= 9) return Uint8Array.from(out);
    }
    return Uint8Array.from(out);
  }
}

class Rt950Flasher {
  constructor(transport, log = () => {}) { this.t = transport; this.q = transport.queue; this.log = log; }

  async command(cmd, arg = 0, data = new Uint8Array(0), timeout = 5000) {
    await this.t.write(packet(cmd, arg, data));
    const r = await this.q.frame(timeout);
    if (r.length < 9) throw new Error(`no answer from the bootloader to command 0x${cmd.toString(16)}`);
    return r[3];
  }

  async ok(cmd, arg, data, timeout, what) {
    const res = await this.command(cmd, arg, data, timeout);
    if (res !== 0x06) throw new Error(`${what} refused: ${RESULTS[res] || "0x" + res.toString(16)}`);
  }

  async enterBootloader() {
    this.q.clear();
    await this.t.write(enc.encode("PROGRAMBT9000U"));
    let r = await this.q.take(1, 3000);
    if (r[0] !== 0x06) throw new Error("the radio did not answer (is it on and is the cable connected?)");
    await this.t.write(enc.encode("UPDATE"));
    r = await this.q.take(1, 3000);
    if (r[0] !== 0x06) throw new Error("the radio refused UPDATE");
    this.log("radio restarting into the bootloader");
  }

  async probe(flood) {
    const pkt = packet(0x42), t0 = Date.now(), limit = flood ? 15000 : 1500;
    await sleep(30);
    this.q.clear();
    while (Date.now() - t0 < limit) {
      await this.t.write(pkt);
      const r = await this.q.frame(flood ? 25 : 1000);
      if (r.length >= 9) {
        await sleep(200);
        while ((await this.q.take(64, 300)).length) { /* drain */ }
        this.q.clear();
        return;
      }
    }
    throw new Error("no answer from the bootloader: switch the radio off, hold the two lower side keys while " +
                    "switching it on, and retry with 'radio already in bootloader mode'");
  }

  async flash(bytes, {enterBootloader = true, progress = () => {}} = {}) {
    const info = firmwareInfo(bytes);
    this.log(`firmware: ${info.model}, ${info.size} bytes, ${info.blocks} blocks`);
    if (enterBootloader) await this.enterBootloader();
    await this.probe(enterBootloader);
    await this.ok(0x0a, 0, enc.encode("BOOTLOADER_V3"), 5000, "bootloader version");
    await this.ok(0x02, 0, bytes.slice(MODEL_OFFSET, MODEL_OFFSET + MODEL_SIZE), 5000, "model check");
    await this.ok(0x04, 0, Uint8Array.from([(info.blocks - 1) >> 8, (info.blocks - 1) & 0xff]), 5000, "block count");
    for (let seq = 0; seq < info.blocks; seq++) {
      const block = new Uint8Array(BLOCK);
      block.set(bytes.slice(seq * BLOCK, (seq + 1) * BLOCK));
      await this.ok(0x03, seq, block, 10000, `block ${seq}`);
      progress(seq + 1, info.blocks);
    }
    const res = await this.command(0x45, 0, new Uint8Array(0), 10000);
    if (res !== 0x06) this.log(`end of update answered ${RESULTS[res] || res}`);
    this.log("update finished: the radio restarts with the new firmware");
    return info;
  }
}

/* Web Serial transport */
class WebSerialTransport {
  constructor(port) { this.port = port; this.queue = new ByteQueue(); this.reading = false; }
  async open() {
    await this.port.open({baudRate: BAUD, dataBits: 8, stopBits: 1, parity: "none", bufferSize: 4096});
    this.writer = this.port.writable.getWriter();
    this.reading = true;
    this.readLoop = (async () => {
      while (this.reading && this.port.readable) {
        this.reader = this.port.readable.getReader();
        try {
          for (;;) {
            const {value, done} = await this.reader.read();
            if (done) break;
            if (value) this.queue.push(value);
          }
        } catch (e) { /* port closed */ } finally { this.reader.releaseLock(); }
      }
    })();
  }
  async write(bytes) { await this.writer.write(bytes); }
  async close() {
    this.reading = false;
    try { await this.reader?.cancel(); } catch (e) {}
    try { this.writer.releaseLock(); } catch (e) {}
    try { await this.readLoop; } catch (e) {}
    try { await this.port.close(); } catch (e) {}
  }
}

const api = {crc16, packet, firmwareInfo, ByteQueue, Rt950Flasher, WebSerialTransport, BAUD};
if (typeof module !== "undefined") module.exports = api;
if (typeof window !== "undefined") window.RT950 = api;
