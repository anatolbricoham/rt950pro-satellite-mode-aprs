"""
bootloader_emulator.py - RT-950 bootloader emulator on a pseudo tty

Runs the radio side of the firmware update protocol (see
rt950_toolkit/flasher.py): optional PROGRAMBT9000U/UPDATE entry from the
"firmware", then 0xAA frames. Collects the received image so tests can
compare it with the file that was sent.
"""

from __future__ import annotations

import os
import select
import struct
import sys
import threading
import tty

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from rt950_toolkit.flasher import crc16, HEADER, FOOTER  # noqa: E402


class BootloaderRadio:
    def __init__(self, in_bootloader: bool = False, model: bytes = b"RT-950"):
        self.master, self.slave = os.openpty()
        tty.setraw(self.slave)
        self.port = os.ttyname(self.slave)
        self.in_bootloader = in_bootloader
        self.model = model
        self.image = bytearray()
        self.blocks_expected = None
        self.finished = False
        self.log = []
        self._buf = bytearray()
        self._stop = False
        self.th = threading.Thread(target=self._run, daemon=True)
        self.th.start()

    def _need(self, n):
        while len(self._buf) < n:
            if self._stop:
                raise EOFError
            r, _, _ = select.select([self.master], [], [], 0.05)
            if r:
                try:
                    d = os.read(self.master, 4096)
                except OSError:
                    raise EOFError
                self._buf += d
        out = bytes(self._buf[:n])
        del self._buf[:n]
        return out

    def _answer(self, cmd, result):
        body = bytes([cmd, 0, result, 0, 0])
        os.write(self.master, bytes([HEADER]) + body + struct.pack(">H", crc16(body)) + bytes([FOOTER]))

    def _run(self):
        try:
            if not self.in_bootloader:
                win = b""
                while win != b"PROGRAMBT9000U":
                    win = (win + self._need(1))[-14:]
                os.write(self.master, b"\x06")
                win = b""
                while win != b"UPDATE":
                    win = (win + self._need(1))[-6:]
                os.write(self.master, b"\x06")
            while not self._stop:
                if self._need(1)[0] != HEADER:
                    continue
                hdr = self._need(5)
                cmd, arg, ln = hdr[0], (hdr[1] << 8) | hdr[2], (hdr[3] << 8) | hdr[4]
                data = self._need(ln)
                crc = struct.unpack(">H", self._need(2))[0]
                self._need(1)
                self.log.append(cmd)
                if crc != crc16(hdr + data):
                    self._answer(cmd, 0xE2)
                elif cmd == 0x42:
                    self._answer(cmd, 0xE5)
                elif cmd == 0x0A:
                    self._answer(cmd, 0x06 if data == b"BOOTLOADER_V3" else 0xE5)
                elif cmd == 0x02:
                    self._answer(cmd, 0x06 if data[:len(self.model)] == self.model else 0xE6)
                elif cmd == 0x04:
                    self.blocks_expected = struct.unpack(">H", data)[0] + 1
                    self._answer(cmd, 0x06)
                elif cmd == 0x03:
                    if ln != 1024 or arg * 1024 != len(self.image):
                        self._answer(cmd, 0xE1)
                    else:
                        self.image += data
                        self._answer(cmd, 0x06)
                elif cmd == 0x45:
                    self.finished = True
                    self._answer(cmd, 0x06)
                else:
                    self._answer(cmd, 0xE5)
        except EOFError:
            pass

    def close(self):
        self._stop = True
        self.th.join(timeout=1)
        for fd in (self.master, self.slave):
            try:
                os.close(fd)
            except OSError:
                pass
