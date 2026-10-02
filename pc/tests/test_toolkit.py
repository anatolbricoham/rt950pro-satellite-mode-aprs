"""
test_toolkit.py - RT-950 Toolkit tests (no radio needed)

Covers the codeplug encodings, CSV formats (native, CHIRP, RT-950 Editor
channels/zones/FM-AM-SSB), CPS .dat files, stock channel sets, the boot logo
converter and full read / write / verify sessions against an emulated stock
radio on a pseudo terminal (tests/oem_emulator.py).

    python -m unittest discover -s pc/tests -v
"""

import csv
import io
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))

from rt950_toolkit import codeplug as C, csvio, protocol, stock, bootlogo  # noqa: E402
from rt950_toolkit.__main__ import main as cli_main  # noqa: E402

try:
    import serial  # noqa: F401
    HAVE_SERIAL = True
except ImportError:
    HAVE_SERIAL = False

EDITOR_DIR = os.environ.get("RT950_EDITOR_DIR", "/mnt/user-data/uploads/RT-950 950Pro Editor")
DATA = os.path.join(HERE, "data")


def sample_codeplug() -> C.Codeplug:
    cp = C.Codeplug()
    cp.set_channel(C.Channel(0, 145_500_000, 145_500_000, name="S20"))
    cp.set_channel(C.Channel(1, 438_800_000, 431_200_000, "88.5", "88.5", power="Mid", name="RPT R70"))
    cp.set_channel(C.Channel(2, 446_006_250, 446_006_250, "D023N", "D023N", power="Low",
                             bandwidth="Narrow", name="PMR1"))
    cp.set_channel(C.Channel(98, 118_100_000, 118_100_000, rx_am="AM", tx_enable="OFF", name="TWR"))
    cp.set_channel(C.Channel(99, 145_800_000, 145_800_000, "OFF", "D754I", scramble="3",
                             busy_lock="ON", scan_add="OFF", encryption="DCP2", signal_code=16,
                             ptt_id="BOTH", name="ISS 2m"))
    cp.set_zone_name(0, "LOCAL")
    cp.set_zone_name(1, "SATELITES")
    cp.set_aprs_callsign("EA7ABC", 7)
    cp.set_vfo_freq(0, 145_525_000)
    cp.set_vfo_offset(0, 600_000)
    return cp


class Encodings(unittest.TestCase):
    def test_tones_roundtrip(self):
        tones = C.tone_list()
        self.assertEqual(len(tones), 261)
        for t in tones:
            b = C.encode_tone(t)
            self.assertEqual(C.decode_tone(b[0], b[1]), t)

    def test_tone_bytes(self):
        self.assertEqual(C.encode_tone("67.0"), bytes([0x9E, 0x02]))     # 670 LE
        self.assertEqual(C.encode_tone("D023N"), bytes([1, 0]))
        self.assertEqual(C.encode_tone("D023I"), bytes([106, 0]))
        self.assertEqual(C.encode_tone("OFF"), b"\x00\x00")

    def test_bcd(self):
        for hz in (18_000_000, 145_525_000, 446_006_250, 999_999_990):
            self.assertEqual(C.bcd_to_hz(C.hz_to_bcd(hz)), round(hz / 10) * 10)
        self.assertEqual(C.hz_to_bcd(145_525_000), bytes([0x00, 0x25, 0x55, 0x14]))

    def test_channel_roundtrip(self):
        cp = sample_codeplug()
        for ch in cp.channels():
            if ch.empty:
                continue
            again = C.decode_channel(ch.index, C.encode_channel(ch, cp.tones), cp.tones)
            self.assertEqual(again, ch)
        c = cp.channel(99)
        self.assertEqual((c.tx_tone, c.scramble, c.encryption, c.signal_code, c.ptt_id),
                         ("D754I", "3", "DCP2", 16, "BOTH"))

    def test_preserves_unknown_bits(self):
        cp = sample_codeplug()
        cp.poke(15, cp.peek(15) | 0x80)              # learn FHSS bit
        cp.write(16, b"\x12\x34\x56\x78")            # FHSS code
        ch = cp.channel(0)
        ch.name = "RENAMED"
        cp.set_channel(ch)
        self.assertTrue(cp.peek(15) & 0x80)
        self.assertEqual(cp.read(16, 4), b"\x12\x34\x56\x78")

    def test_gbk_names(self):
        self.assertEqual(C.ascii_get(C.ascii_put("中继台", 12)), "中继台")
        self.assertEqual(len(C.ascii_put("中继台中继台中", 12)), 12)   # never splits a character

    def test_fields(self):
        cp = sample_codeplug()
        self.assertEqual(cp.aprs_callsign(), ("EA7ABC", 7))
        self.assertEqual(cp.vfo_freq(0), 145_525_000)
        self.assertEqual(cp.vfo_offset(0), 600_000)
        sel = C.select_fields()
        self.assertGreater(len(sel), 100)
        for f in sel:                                 # every option index can be stored and read back
            f.set(cp, len(f.options) - 1)
            self.assertEqual(f.get(cp), len(f.options) - 1)
        d = C.dtmf_fields()[0]
        cp.set_dtmf_value(d, "1A*#")
        self.assertEqual(cp.dtmf_value(d), "1A*#")
        with self.assertRaises(ValueError):
            cp.set_dtmf_value(d, "12X")
        n = [f for f in C.numeric_fields() if f.title == "SSB Offest 13"][0]
        self.assertTrue(n.signed)
        cp.set_num_value(n, -250)
        self.assertEqual(cp.num_value(n), -250)

    def test_json_and_raw(self):
        cp = sample_codeplug()
        tmp = tempfile.mkdtemp()
        try:
            cp.save(os.path.join(tmp, "a.rt950"))
            self.assertEqual(C.Codeplug.load(os.path.join(tmp, "a.rt950")).mem, cp.mem)
            cp.export_raw(os.path.join(tmp, "a.bin"))
            self.assertEqual(C.Codeplug.import_raw(os.path.join(tmp, "a.bin")).mem, cp.mem)
        finally:
            shutil.rmtree(tmp)


class CsvFormats(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def p(self, n):
        return os.path.join(self.tmp, n)

    def test_native_roundtrip(self):
        cp = sample_codeplug()
        csvio.export_native(cp, self.p("n.csv"))
        cp2 = C.Codeplug()
        csvio.import_native(cp2, self.p("n.csv"))
        self.assertEqual([c for c in cp2.channels() if not c.empty], [c for c in cp.channels() if not c.empty])

    def test_editor_roundtrip(self):
        cp = sample_codeplug()
        csvio.export_editor(cp, self.p("e.csv"))
        cp2 = C.Codeplug()
        self.assertEqual(csvio.import_editor(cp2, self.p("e.csv")), 5)
        self.assertEqual([c for c in cp2.channels() if not c.empty], [c for c in cp.channels() if not c.empty])

    def test_chirp_roundtrip(self):
        cp = sample_codeplug()
        csvio.export_chirp(cp, self.p("c.csv"))
        with open(self.p("c.csv"), encoding="utf-8") as f:
            rows = list(csv.DictReader(f))
        r = {x["Name"]: x for x in rows}
        self.assertEqual((r["RPT R70"]["Duplex"], r["RPT R70"]["Offset"], r["RPT R70"]["Tone"]),
                         ("-", "7.600000", "TSQL"))
        cp2 = C.Codeplug()
        csvio.import_chirp(cp2, self.p("c.csv"))
        for i in (0, 1, 2):
            a, b = cp.channel(i), cp2.channel(i)
            self.assertEqual((a.rx_hz, a.tx_hz, a.rx_tone, a.tx_tone, a.name),
                             (b.rx_hz, b.tx_hz, b.rx_tone, b.tx_tone, b.name))

    def test_zones_and_modulation(self):
        cp = sample_codeplug()
        fm = [f for f in C.numeric_fields() if f.title == "FM 3"][0]
        cp.set_num_value(fm, 99.5)
        name = [t for t in C.text_fields() if t.title == "AM CH Name 2"][0]
        cp.set_text_value(name, "RNE")
        csvio.export_zones(cp, self.p("z.csv"))
        csvio.export_modulation(cp, self.p("m.csv"))
        cp2 = C.Codeplug()
        csvio.import_zones(cp2, self.p("z.csv"))
        csvio.import_modulation(cp2, self.p("m.csv"))
        self.assertEqual(cp2.mem["zones"], cp.mem["zones"])
        self.assertEqual(cp2.num_value(fm), 99.5)
        self.assertEqual(cp2.text_value(name), "RNE")

    @unittest.skipUnless(os.path.isdir(EDITOR_DIR), "RT-950 Editor sample files not available")
    def test_editor_sample_files(self):
        cp = C.Codeplug()
        n = csvio.import_editor(cp, os.path.join(EDITOR_DIR, "stock_configs", "EU LPD and PMR Channels.csv"))
        self.assertEqual(n, 16)
        n = csvio.import_editor(cp, os.path.join(EDITOR_DIR, "templates", "rt950_channels_template.csv"))
        self.assertEqual(n, 3)
        self.assertEqual(csvio.import_zones(cp, os.path.join(EDITOR_DIR, "templates", "rt950_zones_template.csv")), 10)
        self.assertEqual(cp.zone_name(1), "AIR")


class DatFiles(unittest.TestCase):
    @unittest.skipUnless(os.path.isfile(os.path.join(EDITOR_DIR, "startup_default.dat")), "no CPS .dat sample")
    def test_dat_roundtrip(self):
        from rt950_toolkit import datfile, nrbf
        src = os.path.join(EDITOR_DIR, "startup_default.dat")
        with open(src, "rb") as f:
            raw = f.read()
        self.assertEqual(nrbf.Stream(raw).dump(), raw)          # byte identical
        cp = datfile.open_dat(src)
        cp.set_channel(C.Channel(5, 145_800_000, 145_800_000, name="ISS"))
        cp.set_zone_name(2, "SATELITES")
        cp.set_aprs_callsign("EA7ABC", 9)
        tmp = tempfile.mkdtemp()
        try:
            out = os.path.join(tmp, "out.dat")
            datfile.save_dat(cp, src, out)
            cp2 = datfile.open_dat(out)
            self.assertEqual(cp2.channel(5).name, "ISS")
            self.assertEqual(cp2.channel(5).rx_hz, 145_800_000)
            self.assertEqual(cp2.zone_name(2), "SATELITES")
            self.assertEqual(cp2.aprs_callsign(), ("EA7ABC", 9))
        finally:
            shutil.rmtree(tmp)


class Misc(unittest.TestCase):
    def test_stock_sets_encode(self):
        cp = C.Codeplug()
        idx = 0
        for name, fn in stock.STOCK.items():
            for ch in fn():
                ch.index = idx
                cp.set_channel(ch)
                self.assertEqual(cp.channel(idx).rx_hz, ch.rx_hz, name)
                idx += 1
        self.assertGreater(idx, 30)

    def test_bootlogo(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("Pillow not installed")
        tmp = tempfile.mkdtemp()
        try:
            src = os.path.join(tmp, "in.png")
            Image.new("RGB", (400, 300), (255, 0, 0)).save(src)
            px = bootlogo.to_rgb(src)
            raw = bootlogo.rgb565(px)
            self.assertEqual(len(raw), 240 * 320 * 2)
            self.assertEqual(raw[:2], b"\xF8\x00")                  # red, big endian RGB565
            bootlogo.write_bmp(os.path.join(tmp, "o.bmp"), px)
            with Image.open(os.path.join(tmp, "o.bmp")) as im:
                self.assertEqual(im.size, (240, 320))
        finally:
            shutil.rmtree(tmp)

    def test_xor_scramble(self):
        key = protocol.key_for_challenge(protocol.FACTORY_CHALLENGE)
        self.assertEqual(key, b"RVB ")
        data = bytes(range(256))
        enc = protocol.xor_crypt(data, key)
        self.assertNotEqual(enc, data)
        self.assertEqual(protocol.xor_crypt(enc, key), data)
        self.assertEqual(enc[0x00], 0x00)
        self.assertEqual(enc[0xFF], 0xFF)


@unittest.skipUnless(HAVE_SERIAL, "pyserial not installed")
class EmulatedRadio(unittest.TestCase):
    def setUp(self):
        from oem_emulator import OemRadio
        self.radio = OemRadio()
        # factory content: one channel, a zone name, the call sign on the APRS page
        cp = sample_codeplug()
        for name, addr, size in C.REGIONS:
            if name == "aprs":
                self.radio.aprs[0:size] = cp.mem[name]
            else:
                self.radio.mem[addr:addr + size] = cp.mem[name]
        self.factory = cp

    def tearDown(self):
        self.radio.close()

    def link(self):
        ser = protocol.open_serial(self.radio.port, timeout=2)
        return ser, protocol.detect(ser, log=lambda *a: None)

    def test_read(self):
        ser, link = self.link()
        try:
            self.assertEqual(link.kind, "oem")
            self.assertEqual(link.model, "RT-950 Pro")
            self.assertEqual(link.key, b"RVB ")
            cp = protocol.read_codeplug(link)
            link.end(False)
        finally:
            ser.close()
        self.assertEqual(cp.mem, self.factory.mem)
        self.assertEqual(cp.channel(1).name, "RPT R70")
        self.assertEqual(cp.aprs_callsign(), ("EA7ABC", 7))
        # transfer plan: R for normal blocks, T at address 0 for the APRS page
        cmds = {(c, a) for c, a, n in self.radio.log}
        self.assertIn(("T", 0), cmds)
        self.assertNotIn(("R", 0xFFFF), cmds)
        self.assertEqual(sum(n for c, a, n in self.radio.log), sum(s for _, _, s in C.REGIONS))

    def test_write_verify(self):
        cp = sample_codeplug()
        cp.mark_valid()                                   # a complete image (as if read from a radio)
        cp.set_channel(C.Channel(10, 433_500_000, 433_500_000, name="NEW"))
        cp.set_aprs_callsign("EA7XYZ", 9)
        ser, link = self.link()
        try:
            protocol.write_codeplug(link, cp, verify=True)
            link.end(True)
        finally:
            ser.close()
        self.assertEqual(self.radio.sessions[-1], (0x45, True))
        self.assertEqual(bytes(self.radio.mem[10 * 32:11 * 32]), cp.read(10 * 32, 32))
        self.assertEqual(bytes(self.radio.aprs[0:0x80]), bytes(cp.mem["aprs"]))
        self.assertIn(("X", 0, 0x80), self.radio.log)

    def test_partial_codeplug_never_blanks_settings(self):
        """A codeplug made from scratch (e.g. a country preset) only changes
        what it touched: zone names are merged into the radio's zone block,
        settings it never saw stay as they are in the radio."""
        self.radio.mem[0x9000:0x9080] = bytes(range(0x80))          # factory settings
        self.radio.mem[0xC0A0:0xC0B0] = b"\x11" * 16                 # unknown zone-block bytes
        cp = C.Codeplug()
        cp.set_channel(C.Channel(5, 145_600_000, 145_000_000, name="RPT"))
        cp.set_zone_name(0, "EA4")
        ser, link = self.link()
        try:
            with self.assertRaises(IOError):                          # no fresh read: refused
                protocol.write_codeplug(link, cp)
            base = protocol.read_codeplug(link)
            written = protocol.write_codeplug(link, cp, base=base)
            link.end(True)
        finally:
            ser.close()
        self.assertEqual(written, ["channels", "zones"])
        self.assertEqual(bytes(self.radio.mem[0x9000:0x9080]), bytes(range(0x80)))
        self.assertEqual(bytes(self.radio.mem[0xC0A0:0xC0B0]), b"\x11" * 16)
        self.assertEqual(C.ascii_get(bytes(self.radio.mem[0xC000:0xC010])), "EA4")
        self.assertEqual(C.ascii_get(bytes(self.radio.mem[5 * 32 + 20:6 * 32])), "RPT")

    def test_channels_only(self):
        cp = sample_codeplug()
        cp.mark_valid()
        cp.set_zone_name(3, "NEW ZONE")
        cp.set_aprs_callsign("EA7XYZ", 1)
        ser, link = self.link()
        try:
            protocol.write_codeplug(link, cp, ["channels", "zones"])
            link.end(True)
        finally:
            ser.close()
        written = {c for c, a, n in self.radio.log if c in "WX"}
        self.assertEqual(written, {"W"})
        self.assertEqual(bytes(self.radio.aprs[0:0x80]), bytes(self.factory.mem["aprs"]))  # untouched

    def test_wrong_model_refused(self):
        self.radio.model = b"UV-K5\x00\x00\x00\x00\x00\x00\x00"
        ser = protocol.open_serial(self.radio.port, timeout=2)
        try:
            with self.assertRaises(IOError):
                protocol.detect(ser, log=lambda *a: None)
        finally:
            ser.close()

    def test_cli_read_write(self):
        tmp = tempfile.mkdtemp()
        os.environ["APPDATA"] = tmp                       # backups go to the temp folder
        try:
            out = os.path.join(tmp, "radio.rt950")
            self.assertEqual(cli_main(["read", "--port", self.radio.port, "-o", out]), 0)
            cp = C.Codeplug.load(out)
            self.assertEqual(cp.channel(99).name, "ISS 2m")
            ch = cp.channel(0)
            ch.name = "CLI"
            cp.set_channel(ch)
            cp.save(out)
            self.assertEqual(cli_main(["write", "--port", self.radio.port, out, "--channels-only"]), 0)
            self.assertEqual(C.ascii_get(bytes(self.radio.mem[20:32])), "CLI")
            backups = os.listdir(os.path.join(tmp, "BricoHamsRT950", "backups"))
            self.assertEqual(len(backups), 2)                  # read + before-write
        finally:
            os.environ.pop("APPDATA", None)
            shutil.rmtree(tmp)


if __name__ == "__main__":
    unittest.main(verbosity=2)
