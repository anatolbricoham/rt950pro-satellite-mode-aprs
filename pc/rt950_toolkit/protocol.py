"""
protocol.py - Talking to the radio over the programming cable

Two firmwares, two protocols:

* Stock Radtel firmware ("OEM"): "PROGRAMBT9000U" -> 0x06, 'F' -> 16 byte
  identity, 'M' -> 12 byte model, "SEND"+selector+padding -> 0x06, then raw
  R/W commands {cmd, addrH, addrL, len} with a 4-byte XOR key chosen from a
  20-entry table during the handshake.  Same sequence as the maker's model
  definition (handCode) and the open-source cps_flash.py.

* Custom firmware (src/app/cps.c): A5-framed packets with CRC-16/CCITT and
  24-bit addresses (see rt950_toolkit.sat.RadioLink).

detect() tries the OEM handshake first: the custom firmware ignores it and
answers with an A5 model frame instead, which is recognised.
"""

from __future__ import annotations

import os
import time
from typing import Callable, Optional

from .codeplug import REGIONS, TRANSFER, Codeplug

HANDSHAKE = b"PROGRAMBT9000U"
# The "SEND" challenge sent by the maker's CPS (selects key index 6, "RVB ")
FACTORY_CHALLENGE = bytes.fromhex("53454E4411100F0603011302130E060C0D0C1204110D0B0E00")
ACK = 0x06
BLOCK = 128
XOR_KEY_TABLE = [
    b"BHT ", b"CO 7", b"A ES", b" EIY", b"M PQ", b"XN Y", b"RVB ", b" HQP", b"W RC", b"MS N",
    b" SAT", b"K DH", b"ZO R", b"C SL", b"6RB ", b" JCG", b"PN V", b"J PK", b"EK L", b"I LZ",
]

Progress = Optional[Callable[[int, int, str], None]]


def key_for_challenge(pkt: bytes) -> bytes:
    sel = pkt[4]
    idx = ((sel - 0x20) * 2 + 1 if sel & 0x20 else (sel - 0x10) * 2) + 1
    return XOR_KEY_TABLE[pkt[4 + idx]]


def xor_crypt(data: bytes, key: bytes) -> bytes:
    """Symmetric CPS scrambling (same function encrypts and decrypts)."""
    out = bytearray(len(data))
    for i, b in enumerate(data):
        k = key[i % 4]
        out[i] = b if (k == 0x20 or b in (0x00, 0xFF) or b == k or b == (k ^ 0xFF)) else b ^ k
    return bytes(out)


def open_serial(port: str, baud: int = 115200, timeout: float = 1.5):
    import serial  # pyserial
    return serial.Serial(port, baud, timeout=timeout, bytesize=8, parity="N", stopbits=1)


def list_ports():
    try:
        from serial.tools import list_ports as lp
        return [(p.device, p.description) for p in lp.comports()]
    except Exception:
        return []


class OemLink:
    kind = "oem"

    def __init__(self, ser, log=print):
        self.ser = ser
        self.log = log
        self.key: Optional[bytes] = None
        self.model = ""
        self.identity = b""

    def _read(self, n: int, what: str) -> bytes:
        d = self.ser.read(n)
        if len(d) < n:
            raise TimeoutError("radio timeout reading %s (%d/%d bytes)" % (what, len(d), n))
        return d

    def handshake(self, first_byte: Optional[bytes] = None) -> str:
        if first_byte is None:
            self.ser.reset_input_buffer()
            self.ser.write(HANDSHAKE)
            first_byte = self._read(1, "ACK")
        if first_byte[0] != ACK:
            raise IOError("no ACK to PROGRAMBT9000U (got 0x%02X)" % first_byte[0])
        self.ser.write(b"F")
        self.identity = self._read(16, "identity")
        self.ser.write(b"M")
        self.model = self._read(12, "model").rstrip(b"\x00 ").decode("ascii", "replace")
        if "RT-950" not in self.model:
            raise IOError("unexpected model %r (expected RT-950)" % self.model)
        pkt = FACTORY_CHALLENGE
        self.key = key_for_challenge(pkt)
        self.ser.write(pkt)
        if self._read(1, "SEND ACK")[0] != ACK:
            raise IOError("encryption handshake refused")
        return self.model

    def read_block(self, addr: int, n: int, cmd: int = 0x52) -> bytes:
        self.ser.write(bytes([cmd, (addr >> 8) & 0xFF, addr & 0xFF, n]))
        r = self._read(4 + n, "%s@0x%04X" % (chr(cmd), addr))
        if r[0] != cmd or ((r[1] << 8) | r[2]) != (addr & 0xFFFF):
            raise IOError("bad read reply at 0x%04X" % addr)
        return xor_crypt(r[4:], self.key) if self.key else r[4:]

    def write_block(self, addr: int, data: bytes, cmd: int = 0x57):
        enc = xor_crypt(data, self.key) if self.key else data
        self.ser.write(bytes([cmd, (addr >> 8) & 0xFF, addr & 0xFF, len(data)]) + enc)
        old = self.ser.timeout
        if cmd == 0x58:                     # the APRS page takes long to commit
            self.ser.timeout = 30
        try:
            if self._read(1, "%s ACK@0x%04X" % (chr(cmd), addr))[0] != ACK:
                raise IOError("write refused at 0x%04X" % addr)
        finally:
            self.ser.timeout = old

    def end(self, wrote: bool):
        try:
            self.ser.write(bytes([0x45 if wrote else ACK]))
            time.sleep(0.05)
        except Exception:
            pass


class CustomLink:
    """Custom firmware A5 protocol (24-bit addresses)."""
    kind = "custom"

    def __init__(self, ser, log=print):
        from .sat import RadioLink
        self.rl = RadioLink(None, log=log, ser=ser)
        self.model = "RT-950 (custom firmware)"
        self.sat_info = None

    def handshake(self, already_sent=True) -> str:
        if not already_sent:
            self.rl.ser.write(HANDSHAKE)
        rc, data = self.rl.read_frame(timeout=3.0)
        self.model = data.decode("ascii", "replace").strip() + " (custom firmware)"
        time.sleep(0.1)
        try:
            info = self.rl.cmd(ord("I"), expect=ord("I"), timeout=1.5)
            self.sat_info = info
        except Exception:
            self.sat_info = None
        return self.model

    def read_block(self, addr: int, n: int, cmd: int = 0x52) -> bytes:
        return self.rl.read_block(addr, n)

    def write_block(self, addr: int, data: bytes, cmd: int = 0x57):
        # the custom firmware writes into erased flash: erase the 4 KB sector
        # first (read-modify-write is done by write_region below)
        self.rl.cmd(ord("W"), bytes([addr >> 16 & 0xFF, addr >> 8 & 0xFF, addr & 0xFF]) + data, expect=ord("W"))

    def erase_sector(self, addr: int):
        self.rl.cmd(ord("E"), bytes([addr >> 16 & 0xFF, addr >> 8 & 0xFF, addr & 0xFF]), expect=ord("E"), timeout=3.0)

    def end(self, wrote: bool):
        self.rl.finish()


def detect(ser, log=print):
    """Return an initialised OemLink or CustomLink."""
    ser.reset_input_buffer()
    ser.write(HANDSHAKE)
    t0 = time.time()
    first = b""
    while not first and time.time() - t0 < 3.0:
        first = ser.read(1)
    if not first:
        raise TimeoutError("the radio did not answer (cable, port, radio on?)")
    if first[0] == ACK:
        link = OemLink(ser, log)
        link.handshake(first)
        return link
    if first[0] == 0xA5:
        # put the byte back for the frame parser
        class _Prepend:
            def __init__(self, s, b):
                self.s, self.b = s, b

            def read(self, n=1):
                if self.b:
                    out, self.b = self.b[:n], self.b[n:]
                    return out + (self.s.read(n - len(out)) if n > len(out) else b"")
                return self.s.read(n)

            def __getattr__(self, k):
                return getattr(self.s, k)
        link = CustomLink(_Prepend(ser, first), log)
        link.handshake(True)
        return link
    raise IOError("unknown answer 0x%02X to the handshake" % first[0])


# ---------------------------------------------------------------- codeplug

def read_codeplug(link, progress: Progress = None) -> Codeplug:
    cp = Codeplug()
    total = sum(s for _, _, s in REGIONS)
    done = 0
    for name, addr, size in REGIONS:
        rc, wc, taddr = TRANSFER[name]
        if link.kind == "custom":           # custom firmware: SPI addresses
            rc, taddr = 0x52, addr
        buf = bytearray()
        while len(buf) < size:
            n = min(BLOCK, size - len(buf))
            buf += link.read_block(taddr + len(buf), n, rc)
            done += n
            if progress:
                progress(done, total, name)
        cp.mem[name][:] = buf
    cp.meta = {"model": link.model, "source": "radio", "link": link.kind,
               "read_at": time.strftime("%Y-%m-%d %H:%M:%S")}
    cp.dirty_regions.clear()
    cp.mark_valid()
    cp.touched.clear()
    return cp


def region_data(cp: Codeplug, name: str, base: Optional[Codeplug]) -> Optional[bytes]:
    """Bytes to write for a region, or None to leave it alone.

    A region that is a complete image (read from the radio or a CPS file) is
    written as is. A region the program never saw complete (a codeplug made
    from scratch, a country preset) is written only if the user changed
    something in it, and then as the radio's current bytes with just those
    changes applied - never as blank 0xFF settings."""
    if name in cp.valid_regions:
        return bytes(cp.mem[name])
    a0, size = next((a, s) for n, a, s in REGIONS if n == name)
    if not any(a0 <= t < a0 + size for t in cp.touched):
        return None
    if base is None or name not in base.valid_regions:
        raise IOError("region %s is incomplete: read the radio first" % name)
    return cp.merged_region(name, bytes(base.mem[name]))


def write_codeplug(link, cp: Codeplug, regions=None, verify: bool = True,
                   progress: Progress = None, base: Optional[Codeplug] = None):
    """Write the given regions (default: all). `base` is a fresh read of the
    radio (the pre-write backup), used for regions `cp` does not hold
    completely. Custom firmware: sector read-modify-write so neighbouring
    data in the same 4 KB is preserved."""
    regions = regions or [n for n, _, _ in REGIONS]
    plan = {}
    for n in regions:
        d = region_data(cp, n, base)
        if d is not None:
            plan[n] = d
    todo = [(n, a, s) for n, a, s in REGIONS if n in plan]
    total = sum(s for _, _, s in todo) * (2 if verify else 1)
    done = 0
    for name, addr, size in todo:
        data = plan[name]
        rc, wc, taddr = TRANSFER[name]
        if link.kind == "custom":
            rc, taddr = 0x52, addr
            done = _custom_write(link, addr, data, progress, done, total, name)
        else:
            for off in range(0, size, BLOCK):
                link.write_block(taddr + off, data[off:off + BLOCK], wc)
                done += min(BLOCK, size - off)
                if progress:
                    progress(done, total, name)
        if verify:
            back = bytearray()
            while len(back) < size:
                n = min(BLOCK, size - len(back))
                back += link.read_block(taddr + len(back), n, rc)
                done += n
                if progress:
                    progress(done, total, "verify " + name)
            if bytes(back) != data:
                bad = next(i for i in range(size) if back[i] != data[i])
                raise IOError("verify failed in %s at 0x%05X" % (name, addr + bad))
    cp.dirty_regions.difference_update(plan)
    return sorted(plan)


def _custom_write(link, addr, data, progress, done, total, name):
    end = addr + len(data)
    sec = addr & ~0xFFF
    while sec < end:
        old = bytearray()
        for o in range(0, 4096, BLOCK):
            old += link.read_block(sec + o, BLOCK)
        lo, hi = max(addr, sec), min(end, sec + 4096)
        old[lo - sec:hi - sec] = data[lo - addr:hi - addr]
        link.erase_sector(sec)
        for o in range(0, 4096, BLOCK):
            chunk = bytes(old[o:o + BLOCK])
            if chunk != b"\xFF" * BLOCK:
                link.write_block(sec + o, chunk)
        done += hi - lo
        if progress:
            progress(done, total, name)
        sec += 4096
    return done


def backup_dir() -> str:
    base = os.environ.get("APPDATA") or os.path.join(os.path.expanduser("~"), ".config")
    from . import APP_ID
    d = os.path.join(base, APP_ID, "backups")
    os.makedirs(d, exist_ok=True)
    return d


def save_backup(cp: Codeplug, tag: str = "backup") -> str:
    path = os.path.join(backup_dir(), time.strftime("%Y%m%d-%H%M%S") + "-%s.rt950" % tag)
    cp.save(path)
    return path
