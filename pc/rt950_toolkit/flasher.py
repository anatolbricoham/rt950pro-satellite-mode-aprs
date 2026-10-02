"""
flasher.py - Firmware update of the RT-950 / RT-950 Pro through its bootloader

Same protocol as tools/firmware_upload.py (and the maker's updater):

  Phase 1 (only from the running firmware):
      PROGRAMBT9000U -> 06, UPDATE -> 06, the MCU resets into the bootloader
  Phase 2 (0xAA frames):
      AA cmd argH argL lenH lenL data.. crcH crcL 55   (CRC-16/CCITT, init 0,
      over cmd..data); the answer is AA cmd 00 result 00 00 crc 55,
      result 06 = OK.
      42 probe  ->  0A "BOOTLOADER_V3"  ->  02 model (32 bytes from BTF@0x3E0)
      ->  04 number of blocks - 1  ->  03 data (1024-byte blocks, arg = seq)
      ->  45 end

The .BTF file is sent unchanged: the bootloader decrypts it with the key
stored at offset 0x400. Nothing is ever read back from the radio's flash:
the bootloader has no read command, so the installed firmware cannot be
backed up from the radio (keep the original .BTF file instead).
"""

from __future__ import annotations

import math
import struct
import time
from typing import Callable, Optional

BAUD = 115200
HANDSHAKE = b"PROGRAMBT9000U"
UPDATE = b"UPDATE"
HEADER, FOOTER, ACK = 0xAA, 0x55, 0x06
CMD_PROBE, CMD_VERSION, CMD_MODEL, CMD_COUNT, CMD_DATA, CMD_END = 0x42, 0x0A, 0x02, 0x04, 0x03, 0x45
VERSION = b"BOOTLOADER_V3"
MODEL_OFFSET, MODEL_SIZE, KEY_OFFSET = 0x3E0, 32, 0x400
BLOCK = 1024
RESULTS = {0x06: "OK", 0xE1: "wrong data length", 0xE2: "data verification error",
           0xE3: "flash write error", 0xE5: "command not applicable", 0xE6: "model mismatch"}


def crc16(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


def packet(cmd: int, arg: int = 0, data: bytes = b"") -> bytes:
    body = bytes([cmd, arg >> 8 & 0xFF, arg & 0xFF, len(data) >> 8 & 0xFF, len(data) & 0xFF]) + data
    return bytes([HEADER]) + body + struct.pack(">H", crc16(body)) + bytes([FOOTER])


class FirmwareInfo:
    def __init__(self, data: bytes):
        if len(data) < KEY_OFFSET + 16:
            raise ValueError("not a firmware file (too small)")
        self.size = len(data)
        self.blocks = math.ceil(len(data) / BLOCK)
        self.model = data[MODEL_OFFSET:MODEL_OFFSET + 12].rstrip(b"\x00").decode("ascii", "replace")
        self.encrypted = data[KEY_OFFSET:KEY_OFFSET + 16] != bytes(16)
        sp, reset = struct.unpack_from("<II", data, 0)
        self.vector_ok = 0x20000000 <= sp <= 0x2FFFFFFF and 0x08003000 <= reset <= 0x080FFFFF

    def check(self):
        if "RT-950" not in self.model and "RT950" not in self.model.replace("-", ""):
            raise ValueError("firmware is for %r, not for an RT-950" % self.model)

    def __str__(self):
        return "%s, %d bytes, %d blocks%s" % (self.model, self.size, self.blocks,
                                              ", encrypted" if self.encrypted else "")


class Bootloader:
    def __init__(self, ser, log: Callable[[str], None] = print):
        self.ser, self.log = ser, log

    def _frame(self, timeout: float) -> bytes:
        old, self.ser.timeout = self.ser.timeout, timeout
        buf = bytearray()
        t0 = time.time()
        try:
            while time.time() - t0 < timeout:
                b = self.ser.read(1)
                if not b:
                    break
                if not buf and b[0] != HEADER:
                    continue
                buf += b
                if b[0] == FOOTER and len(buf) >= 9:
                    break
        finally:
            self.ser.timeout = old
        return bytes(buf)

    def command(self, cmd: int, arg: int = 0, data: bytes = b"", timeout: float = 5.0) -> int:
        self.ser.write(packet(cmd, arg, data))
        self.ser.flush()
        r = self._frame(timeout)
        if len(r) < 9:
            raise TimeoutError("no answer from the bootloader to command 0x%02X" % cmd)
        return r[3]

    def expect_ok(self, cmd, arg=0, data=b"", timeout=5.0, what=""):
        res = self.command(cmd, arg, data, timeout)
        if res != ACK:
            raise IOError("%s refused: %s" % (what or "command 0x%02X" % cmd, RESULTS.get(res, "0x%02X" % res)))

    def enter_from_firmware(self):
        self.ser.reset_input_buffer()
        self.ser.write(HANDSHAKE)
        r = self.ser.read(1)
        if r != b"\x06":
            raise IOError("the radio did not answer PROGRAMBT9000U (is it on, cable connected?)")
        self.ser.write(UPDATE)
        r = self.ser.read(1)
        if r != b"\x06":
            raise IOError("the radio refused UPDATE")
        self.log("radio restarting into the bootloader")

    def probe(self, flood: bool, seconds: float = 15.0):
        pkt = packet(CMD_PROBE)
        old = self.ser.timeout
        t0 = time.time()
        try:
            time.sleep(0.03)
            self.ser.reset_input_buffer()
            self.ser.timeout = 0.015 if flood else 1.0
            while time.time() - t0 < (seconds if flood else 1.5):
                self.ser.write(pkt)
                self.ser.flush()
                r = self.ser.read(64)
                if r and len(r) >= 9 and r[0] == HEADER:
                    time.sleep(0.2)                  # bootloader erasing, then receive loop
                    self.ser.timeout = 0.3
                    while self.ser.read(64):
                        pass
                    self.ser.reset_input_buffer()
                    return
        finally:
            self.ser.timeout = old
        raise TimeoutError("no answer from the bootloader. Switch the radio off, hold the two lower "
                           "side keys while switching it on, and try again with 'radio already in bootloader'")


def flash(ser, data: bytes, enter_bootloader: bool = True,
          progress: Optional[Callable[[int, int, str], None]] = None, log=print) -> FirmwareInfo:
    info = FirmwareInfo(data)
    info.check()
    log("firmware: %s" % info)
    bl = Bootloader(ser, log)
    if enter_bootloader:
        bl.enter_from_firmware()
    bl.probe(flood=enter_bootloader)
    bl.expect_ok(CMD_VERSION, data=VERSION, what="bootloader version")
    bl.expect_ok(CMD_MODEL, data=data[MODEL_OFFSET:MODEL_OFFSET + MODEL_SIZE], what="model check")
    bl.expect_ok(CMD_COUNT, data=struct.pack(">H", info.blocks - 1), what="block count")
    for seq in range(info.blocks):
        block = data[seq * BLOCK:(seq + 1) * BLOCK].ljust(BLOCK, b"\x00")
        bl.expect_ok(CMD_DATA, seq, block, timeout=10.0, what="block %d" % seq)
        if progress:
            progress(seq + 1, info.blocks, "firmware")
    res = bl.command(CMD_END, timeout=10.0)
    if res != ACK:
        log("end of update answered %s" % RESULTS.get(res, "0x%02X" % res))
    log("update finished: the radio restarts with the new firmware")
    return info
