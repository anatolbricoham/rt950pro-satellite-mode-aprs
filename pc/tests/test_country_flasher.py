"""test_country_flasher.py - Country codeplugs and firmware flasher."""
import json
import os
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, HERE)

from rt950_toolkit import codeplug as C, country, flasher  # noqa: E402

try:
    import serial  # noqa: F401
    HAVE_SERIAL = True
except ImportError:
    HAVE_SERIAL = False


class Country(unittest.TestCase):
    def test_spain_layout(self):
        cp, rep = country.build("ES")
        self.assertEqual([cp.zone_name(z) for z in range(10)],
                         ["EA%d" % i for i in range(1, 10)] + ["PMR-CB"])
        z10 = [c for c in cp.channels()[9 * 99:10 * 99] if not c.empty]
        self.assertEqual(len(z10), 56)                       # 16 PMR446 + 40 CB
        self.assertTrue(all(c.tx_enable == "OFF" for c in z10))
        self.assertEqual(z10[0].rx_hz, 446_006_250)
        cb = [c for c in z10 if c.name.startswith("CB")]
        self.assertEqual((cb[0].rx_hz, cb[22].rx_hz, cb[-1].rx_hz), (26_965_000, 27_255_000, 27_405_000))
        self.assertTrue(all(c.rx_am == "AM" for c in cb))
        air = [c for c in cp.channels() if not c.empty and c.rx_am == "AM" and 108e6 <= c.rx_hz < 137e6]
        self.assertGreater(len(air), 100)
        self.assertTrue(all(c.tx_enable == "OFF" for c in air))
        madrid = [c for c in cp.channels()[3 * 99:4 * 99] if c.name.startswith("LEMD")]
        self.assertTrue(madrid, "Madrid-Barajas in EA4")
        albacete = [c for c in cp.channels()[4 * 99:5 * 99] if c.name.startswith("LEAB")]
        self.assertTrue(albacete, "Albacete airport in EA5")
        reps = [c for c in cp.channels() if not c.empty and c.name.startswith("ED")]
        self.assertTrue(all(c.tx_enable == "ON" for c in reps))
        self.assertTrue(any(c.name == "ED4YAD V" and c.tx_tone == "82.5" and c.tx_hz == c.rx_hz - 600_000 for c in reps))
        self.assertTrue(any(c.name.endswith(" U") and c.tx_hz == c.rx_hz - 7_600_000 for c in reps))

    def test_uk_layout_and_capacity(self):
        cp, rep = country.build("GB", "IO91WM")
        self.assertEqual(cp.zone_name(8), "UK SIMPLEX")
        z10 = [c for c in cp.channels()[9 * 99:10 * 99] if not c.empty]
        self.assertEqual(len(z10), 96)                       # PMR446 + UK 27/81 + CEPT
        self.assertTrue(any(c.rx_hz == 27_601_250 for c in z10))
        for z in rep.zones:
            self.assertLessEqual(z.added, 99)
        se = rep.zones[0]
        self.assertEqual(se.added, 99)
        self.assertTrue(se.dropped)                          # SE has more than 99 candidates
        kept = [c.name for c in cp.channels()[0:99] if not c.empty]
        self.assertTrue(any(n.startswith("EGLL") for n in kept), "Heathrow kept near IO91WM")

    def test_append_keeps_existing(self):
        cp = C.Codeplug()
        cp.set_channel(C.Channel(0, 145_500_000, 145_500_000, name="MINE"))
        cp.set_channel(C.Channel(3 * 99 + 5, 145_600_000, 145_000_000, "OFF", "82.5", name="DUP"))
        cp.set_zone_name(0, "MY ZONE")
        rep = country.apply(cp, "ES", "append")
        self.assertEqual(cp.channel(0).name, "MINE")
        self.assertEqual(cp.channel(3 * 99 + 5).name, "DUP")
        self.assertEqual(rep.zones[3].duplicates, 1)          # ED4YAD = same frequencies and tone
        self.assertEqual(cp.zone_name(0), "MY ZONE")          # append does not rename a named zone
        self.assertEqual(cp.zone_name(1), "EA2")

    def test_codeplug_writes_only_channels_and_zone_names(self):
        cp, _ = country.build("ES")
        self.assertEqual(cp.valid_regions, {"channels"})
        self.assertTrue(all(0xC000 <= t < 0xC100 for t in cp.touched if t >= 0x7C00))
        d = tempfile.mkdtemp()
        p = os.path.join(d, "es.rt950")
        cp.save(p)
        back = C.Codeplug.load(p)
        self.assertEqual(back.valid_regions, {"channels"})
        self.assertTrue(back.touched)


@unittest.skipUnless(HAVE_SERIAL, "pyserial not installed")
class Flasher(unittest.TestCase):
    DATA = None

    @classmethod
    def setUpClass(cls):
        root = os.path.join(HERE, "..", "..")
        for p in (os.path.join(root, "build", "rt950-custom.BTF"),
                  os.path.join(root, "binary", "RT_950Pro_V0.27_260203", "RT_950Pro_V0.27_260203.BTF")):
            if os.path.exists(p):
                cls.DATA = open(p, "rb").read()
                return

    def setUp(self):
        if not self.DATA:
            self.skipTest("no .BTF file available")

    def run_flash(self, in_bootloader, model=b"RT-950"):
        from bootloader_emulator import BootloaderRadio
        from rt950_toolkit import protocol
        r = BootloaderRadio(in_bootloader=in_bootloader, model=model)
        ser = protocol.open_serial(r.port, baud=flasher.BAUD)
        try:
            flasher.flash(ser, self.DATA, enter_bootloader=not in_bootloader, log=lambda m: None)
        finally:
            ser.close()
            time.sleep(0.05)
            r.close()
        return r

    def test_from_firmware(self):
        r = self.run_flash(False)
        self.assertTrue(r.finished)
        self.assertEqual(bytes(r.image[:len(self.DATA)]), self.DATA)

    def test_in_bootloader(self):
        r = self.run_flash(True)
        self.assertEqual(bytes(r.image[:len(self.DATA)]), self.DATA)

    def test_wrong_model_refused(self):
        with self.assertRaises(IOError):
            self.run_flash(True, b"UV-K5")

    def test_rejects_foreign_file(self):
        bad = bytearray(self.DATA)
        bad[0x3E0:0x3EC] = b"UV-K5\x00\x00\x00\x00\x00\x00\x00"
        with self.assertRaises(ValueError):
            flasher.FirmwareInfo(bytes(bad)).check()


if __name__ == "__main__":
    unittest.main(verbosity=2)
