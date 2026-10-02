"""
oem_emulator.py - Stock RT-950 Pro programming-port emulator on a pseudo tty

Implements the radio side of the CPS protocol as observed in the maker's CPS
and in RT-950/950Pro Editor:

    PROGRAMBT9000U   -> 0x06
    'F'              -> 16 byte identity
    'M'              -> 12 byte model ("RT-950 Pro")
    "SEND"+21 bytes  -> 0x06            (selects the XOR key)
    'R' aH aL n      -> 'R' aH aL n + n scrambled bytes
    'W' aH aL n data -> 0x06
    'T' aH aL n      -> APRS page read  (same reply layout as 'R')
    'X' aH aL n data -> 0x06            (APRS page write)
    'E' / 0x06       -> end of session

Memory is kept in plain text; data on the wire is scrambled with the key the
handshake selected, exactly like the radio.  Used by test_toolkit.py; can
also be started by hand (python oem_emulator.py) to try the GUI without a
radio: it prints the pty path to select as "port".
"""

from __future__ import annotations

import os
import select
import sys
import threading
import tty

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from rt950_toolkit.protocol import HANDSHAKE, key_for_challenge, xor_crypt  # noqa: E402


class OemRadio:
    MODEL = b"RT-950 Pro\x00\x00"
    IDENT = bytes(range(0x30, 0x40))

    def __init__(self, model: bytes = None):
        self.mem = bytearray(b"\xFF" * 0x10000)
        self.aprs = bytearray(b"\xFF" * 0x100)
        self.model = model or self.MODEL
        self.key = None
        self.sessions = []          # list of (end byte, wrote?) per session
        self.log = []               # (cmd, addr, len)
        self.writes = 0
        self.master, self.slave = os.openpty()
        tty.setraw(self.slave)
        self.port = os.ttyname(self.slave)
        self._stop = False
        self._buf = bytearray()
        self.th = threading.Thread(target=self._run, daemon=True)
        self.th.start()

    # ------------------------------------------------------------------ io
    def _need(self, n: int) -> bytes:
        while len(self._buf) < n:
            if self._stop:
                raise EOFError
            r, _, _ = select.select([self.master], [], [], 0.1)
            if r:
                try:
                    d = os.read(self.master, 4096)
                except OSError:
                    raise EOFError
                if not d:
                    raise EOFError
                self._buf += d
        out = bytes(self._buf[:n])
        del self._buf[:n]
        return out

    def _send(self, b: bytes):
        os.write(self.master, b)

    # --------------------------------------------------------------- logic
    def _run(self):
        try:
            while not self._stop:
                self._session()
        except EOFError:
            pass

    def _session(self):
        # wait for the handshake magic (resynchronise on garbage)
        win = b""
        while win != HANDSHAKE:
            win = (win + self._need(1))[-len(HANDSHAKE):]
        self.key = None
        self._send(b"\x06")
        wrote = False
        while True:
            c = self._need(1)[0]
            if c == ord("F"):
                self._send(self.IDENT)
            elif c == ord("M"):
                self._send(self.model)
            elif c == ord("S"):
                pkt = b"S" + self._need(24)
                if pkt[:4] != b"SEND":
                    return
                self.key = key_for_challenge(pkt)
                self._send(b"\x06")
            elif c in (0x52, 0x54):                      # R / T
                h = self._need(3)
                addr, n = (h[0] << 8) | h[1], h[2]
                self.log.append((chr(c), addr, n))
                src = self.mem if c == 0x52 else self.aprs
                data = bytes(src[addr:addr + n])
                self._send(bytes([c]) + h + xor_crypt(data, self.key))
            elif c in (0x57, 0x58):                      # W / X
                h = self._need(3)
                addr, n = (h[0] << 8) | h[1], h[2]
                data = xor_crypt(self._need(n), self.key)
                self.log.append((chr(c), addr, n))
                dst = self.mem if c == 0x57 else self.aprs
                dst[addr:addr + n] = data
                self.writes += 1
                wrote = True
                self._send(b"\x06")
            elif c in (0x45, 0x06):                      # end
                self.sessions.append((c, wrote))
                return
            else:
                self.sessions.append((c, wrote))
                return

    def close(self):
        self._stop = True
        self.th.join(timeout=1)
        for fd in (self.master, self.slave):
            try:
                os.close(fd)
            except OSError:
                pass


if __name__ == "__main__":
    r = OemRadio()
    print("Emulated RT-950 Pro on", r.port, "(Ctrl+C to quit)")
    try:
        r.th.join()
    except KeyboardInterrupt:
        r.close()
