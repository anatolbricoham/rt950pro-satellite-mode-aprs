"""test_web_core.py - Cross-check the JavaScript core of the web / Android
programmer (mobile/www/js/core.js) against this Python implementation:
writes reference files with the toolkit and runs tests/web/core.test.cjs on
them with Node (skipped when Node is not installed)."""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, HERE)

from rt950_toolkit import country  # noqa: E402
from test_toolkit import sample_codeplug  # noqa: E402

NODE = shutil.which("node")


def write_reference(d):
    import json
    cp = sample_codeplug()
    cp.mark_valid()
    cp.save(os.path.join(d, "sample.rt950"))
    json.dump([[c.number, c.name, c.rx_hz, c.tx_hz, c.rx_tone, c.tx_tone, c.power, c.bandwidth, c.scramble,
                c.busy_lock, c.scan_add, c.encryption, c.tx_enable, c.rx_am, c.signal_code, c.ptt_id]
               for c in cp.channels() if not c.empty], open(os.path.join(d, "sample_channels.json"), "w"))
    for code, loc in (("ES", "IM98IB"), ("GB", "IO91WM"), ("GB", None)):
        p, _ = country.build(code, loc)
        p.save(os.path.join(d, "%s_%s.rt950" % (code, "null" if loc is None else loc)))
    cp2 = sample_codeplug()
    cp2.mark_valid()
    country.apply(cp2, "ES", "append", "IN80DK")
    cp2.save(os.path.join(d, "append_ES.rt950"))


@unittest.skipUnless(NODE, "node not installed")
class WebCore(unittest.TestCase):
    def test_js_matches_python(self):
        subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "export_web_meta.py")],
                              stdout=subprocess.DEVNULL)
        d = tempfile.mkdtemp()
        try:
            write_reference(d)
            r = subprocess.run([NODE, os.path.join(ROOT, "tests", "web", "core.test.cjs")],
                               env=dict(os.environ, XCHECK=d), capture_output=True, text=True, timeout=300)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn("RESULT: PASS", r.stdout)
        finally:
            shutil.rmtree(d)


if __name__ == "__main__":
    unittest.main(verbosity=2)
